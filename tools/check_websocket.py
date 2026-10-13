#!/usr/bin/env python3
"""Probe the full WSS -> tunnel -> gateway -> UDP relay path without joining."""
import argparse
import asyncio
import sys
import time
from urllib.parse import urlsplit

from websockets.asyncio.client import connect


async def probe(url):
    started = time.monotonic()
    async with connect(url, open_timeout=5, close_timeout=1,
                       user_agent_header="TerMino", compression=None,
                       max_size=256) as websocket:
        connected = time.monotonic()
        await websocket.send(b"!probe")
        reply = await asyncio.wait_for(websocket.recv(), timeout=5)
        if reply not in (b"!probe-ok", "!probe-ok"):
            raise ValueError("Unexpected reply; check relay version and gateway routing")
        returned = time.monotonic()
        print(f"OK: WebSocket handshake {(connected-started)*1000:.1f} ms; "
              f"TerMino relay round trip {(returned-connected)*1000:.1f} ms")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="full ws:// or wss:// URL, usually ending in /ws")
    args = parser.parse_args()
    parsed = urlsplit(args.url)
    if parsed.scheme not in ("ws", "wss") or not parsed.hostname or parsed.username or parsed.password:
        parser.error("Use ws://host[:port]/ws or wss://host/ws without credentials")
    try:
        asyncio.run(probe(args.url))
    except Exception as error:
        print(f"WebSocket probe failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
