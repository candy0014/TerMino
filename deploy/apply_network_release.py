#!/usr/bin/env python3
"""Activate checked relay/gateway releases together, with local health rollback.

Run on the deployment host after uploading files under each releases/NAME.
The gateway release must contain a matching lib/ dependency directory.
"""
import argparse
import asyncio
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release")
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9-]{1,100}", args.release):
        raise ValueError("Invalid release name")
    roots = [Path.home() / "termino-server", Path.home() / "termino-websocket"]
    with (roots[0] / ".network-deploy.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        activate_release(args, roots)


def activate_release(args, roots):
    destinations = [root / "releases" / args.release for root in roots]
    previous = [(root / "current").resolve(strict=True) for root in roots]
    expected = json.loads(args.manifest.read_text())
    files = {"tetr_server": destinations[0] / "tetr_server",
             "websocket_gateway.py": destinations[1] / "websocket_gateway.py",
             "event_stream.py": destinations[1] / "event_stream.py"}
    if set(expected) != set(files):
        raise ValueError("Manifest must name exactly the relay and two gateway modules")
    for name, path in files.items():
        if not re.fullmatch(r"[a-f0-9]{64}", expected[name]) or hashlib.sha256(path.read_bytes()).hexdigest() != expected[name]:
            raise ValueError("Uploaded checksum mismatch: " + name)
    files["tetr_server"].chmod(0o755)
    sys.path[:0] = [str(destinations[1]), str(destinations[1] / "lib")]
    from websockets.asyncio.client import connect
    import event_stream

    def select(paths):
        for root, path in zip(roots, paths):
            temporary = root / (".current-next-" + str(os.getpid()))
            temporary.symlink_to(path)
            temporary.replace(root / "current")

    def restart():
        subprocess.run(["systemctl", "--user", "restart", "termino-relay.service", "termino-websocket.service"], check=True, timeout=20)

    async def websocket_probe():
        async with connect("ws://127.0.0.1:8080/ws", subprotocols=[event_stream.PROTOCOL],
                           open_timeout=2, close_timeout=1, compression=None) as ws:
            if ws.subprotocol != event_stream.PROTOCOL:
                raise RuntimeError("Gateway did not negotiate the event stream")
            await ws.send(event_stream.encode([b"!probe"]))
            if event_stream.decode(await asyncio.wait_for(ws.recv(), 2)) != [b"!probe-ok"]:
                raise RuntimeError("Gateway did not reach the relay")

    def health():
        end = time.monotonic() + 10
        while True:
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    udp.settimeout(0.3)
                    udp.sendto(b"!probe", ("127.0.0.1", 3000))
                    if udp.recv(256) != b"!probe-ok":
                        raise RuntimeError("Unexpected relay reply")
                with urllib.request.urlopen("http://127.0.0.1:8080/healthz", timeout=1) as response:
                    if response.status != 200:
                        raise RuntimeError("Gateway health failed")
                asyncio.run(websocket_probe())
                return
            except (OSError, RuntimeError, TimeoutError):
                if time.monotonic() >= end:
                    raise
                time.sleep(0.1)

    try:
        select(destinations)
        restart()
        health()
    except Exception:
        select(previous)
        restart()
        health()
        print(json.dumps({"rolled_back": list(map(str, previous))}), flush=True)
        raise
    record = {"release": args.release, "current": list(map(str, destinations)),
              "previous": list(map(str, previous)), "sha256": expected,
              "health": "UDP, HTTP and negotiated WebSocket relay probe passed"}
    (destinations[0] / "deployment.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record), flush=True)


if __name__ == "__main__":
    main()
