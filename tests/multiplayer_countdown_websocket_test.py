#!/usr/bin/env python3
"""Run the real two-client lifecycle through a delayed WebSocket gateway."""
import asyncio
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import websocket_gateway as gateway_module
from websockets.asyncio.server import serve


class DelayedPeer(gateway_module.RelayPeer):
    def datagram_received(self, data, address):
        # An asymmetric 80 ms path also tests that independent client clocks
        # do not assume the server's steady-clock epoch matches their own.
        # The old post-countdown barrier would add this RoundReady delay.
        delay = 0.75 if data.endswith(b";start7") else 0.08
        asyncio.get_running_loop().call_later(delay, super().datagram_received, data, address)


async def run(binary):
    gateway_module.RelayPeer = DelayedPeer
    gateway = gateway_module.Gateway(relay_port=3017)
    async with serve(gateway.handle, "127.0.0.1", 0,
                     process_request=gateway.process_request, compression=None,
                     subprotocols=[gateway_module.event_stream.PROTOCOL],
                     select_subprotocol=gateway_module.event_stream.select_subprotocol) as server:
        port = server.sockets[0].getsockname()[1]
        proc = await asyncio.create_subprocess_exec(binary, f"ws://127.0.0.1:{port}/ws")
        try:
            status = await asyncio.wait_for(proc.wait(), 45)
            if status:
                raise RuntimeError(f"Delayed WebSocket lifecycle exited {status}")
        finally:
            if proc.returncode is None:
                proc.kill()
                await proc.wait()


if __name__ == "__main__":
    asyncio.run(run(sys.argv[1]))
