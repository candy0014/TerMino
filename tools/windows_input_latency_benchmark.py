#!/usr/bin/env python3
"""Attach the Windows benchmark to Wine's Unix console through a real PTY."""
import argparse
import asyncio
import errno
import fcntl
import os
import pathlib
import pty
import select
import struct
import subprocess
import termios
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(prefix, url="udp", samples=200, case="all", observation="stream", binary=None):
    prefix = pathlib.Path(prefix).resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 45, 140, 0, 0))
    native_prefix = "Z:" + str(prefix).replace("/", "\\")
    legacy_binary = ROOT / "build-portable-windows-wine/windows_multiplayer_latency_benchmark.exe"
    suite_binary = legacy_binary.parent / "tests" / legacy_binary.name
    binary = pathlib.Path(binary).resolve() if binary else suite_binary if suite_binary.is_file() else legacy_binary
    args = [str(ROOT / "build-wine-tools/run-wine.sh"),
            str(binary),
            native_prefix, url, str(samples), case, "inherit", observation]
    started = time.monotonic()
    proc = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
    os.close(slave)
    try:
        with open(str(prefix) + "_terminal.log", "wb") as output:
            while True:
                cases = 9 if case == "all" else 1
                budget = max(180, cases * (samples * 0.11 + 30))
                if time.monotonic() - started > budget:
                    raise TimeoutError(f"Windows console benchmark exceeded {budget:.0f} seconds")
                ready, _, _ = select.select([master], [], [], 0.2)
                if ready:
                    try:
                        data = os.read(master, 65536)
                    except OSError as error:
                        if error.errno == errno.EIO:
                            break
                        raise
                    if not data:
                        break
                    output.write(data)
                    output.flush()
                elif proc.poll() is not None:
                    break
        status = proc.wait(timeout=10)
        print(f"Wine console benchmark exit={status}; terminal trace: {prefix}_terminal.log", flush=True)
        if status:
            raise RuntimeError(f"Windows benchmark exited {status}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)
        os.close(master)


async def run_local_websocket(prefix, samples, case, observation, binary, delay_ms):
    # Loaded only for WebSocket testing; the plain UDP/console harness has no
    # external Python dependency. Use deploy/websocket-requirements.txt.
    import websocket_gateway as gateway_module
    from websockets.asyncio.server import serve

    class DelayedPeer(gateway_module.RelayPeer):
        def datagram_received(self, data, address):
            asyncio.get_running_loop().call_later(
                delay_ms / 1000, super().datagram_received, data, address)

    gateway_module.RelayPeer = DelayedPeer
    gateway = gateway_module.Gateway(relay_port=3025)
    async with serve(gateway.handle, "127.0.0.1", 0,
                     process_request=gateway.process_request, compression=None,
                     subprotocols=[gateway_module.event_stream.PROTOCOL],
                     select_subprotocol=gateway_module.event_stream.select_subprotocol) as server:
        port = server.sockets[0].getsockname()[1]
        await asyncio.to_thread(run, prefix, f"ws://127.0.0.1:{port}/ws",
                                samples, case, observation, binary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix")
    parser.add_argument("--url", default="udp")
    parser.add_argument("--samples", type=int, default=200)
    parser.add_argument("--case", default="all", choices=("all", "offline", "idle", "motion120", "stress480", "multi8", "multi8legacy", "multi16", "bot-slow", "bot-handling"))
    parser.add_argument("--observation", default="stream", choices=("stream", "console"),
                        help="Wine uses stream verification; console requires a host that parses VT into its Win32 buffer")
    parser.add_argument("--binary", type=pathlib.Path, help="Alternate Windows benchmark executable for comparisons")
    parser.add_argument("--local-websocket", action="store_true", help="Start a local WebSocket gateway instead of UDP")
    parser.add_argument("--downstream-delay-ms", type=float, default=0,
                        help="Add relay-to-client delay with --local-websocket")
    args = parser.parse_args()
    if not 8 <= args.samples <= 2000:
        parser.error("--samples must be between 8 and 2000")
    if not 0 <= args.downstream_delay_ms <= 1000:
        parser.error("--downstream-delay-ms must be between 0 and 1000")
    if args.local_websocket:
        if args.url != "udp":
            parser.error("--local-websocket cannot be combined with --url")
        asyncio.run(run_local_websocket(args.prefix, args.samples, args.case,
                                       args.observation, args.binary, args.downstream_delay_ms))
    else:
        if args.downstream_delay_ms:
            parser.error("--downstream-delay-ms requires --local-websocket")
        run(args.prefix, args.url, args.samples, args.case, args.observation, args.binary)
