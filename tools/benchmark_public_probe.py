#!/usr/bin/env python3
"""Measure a persistent public WSS -> relay round trip without joining a room."""
import argparse
import asyncio
import json
import math
import time
import event_stream
from websockets.asyncio.client import connect


async def run(args):
    start = time.monotonic()
    async with connect(args.url, subprotocols=[event_stream.PROTOCOL], compression=None,
                       open_timeout=15, close_timeout=2, proxy=None if args.direct else True) as ws:
        handshake = (time.monotonic() - start) * 1000
        stream = ws.subprotocol == event_stream.PROTOCOL
        samples = []
        for _ in range(args.samples):
            before = time.monotonic()
            await ws.send(event_stream.encode([b"!probe"]) if stream else b"!probe")
            reply = await asyncio.wait_for(ws.recv(), 5)
            events = event_stream.decode(reply) if stream else [reply]
            if events != [b"!probe-ok"]:
                raise RuntimeError("Unexpected relay reply")
            samples.append((time.monotonic() - before) * 1000)
            await asyncio.sleep(args.interval_ms / 1000)
    ordered = sorted(samples)
    def percentile(fraction):
        return ordered[math.ceil((len(ordered) - 1) * fraction)]
    print(json.dumps({"url": args.url, "metric": "persistent_public_relay_round_trip",
                      "proxy_mode": "direct" if args.direct else "environment",
                      "event_stream": stream, "handshake_ms": handshake, "samples": len(samples),
                      "p50_ms": percentile(.5), "p95_ms": percentile(.95),
                      "max_ms": max(samples), "rtt_ms": samples}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("--samples", type=int, default=20)
    parser.add_argument("--interval-ms", type=float, default=50)
    parser.add_argument("--direct", action="store_true", help="Ignore environment proxies for this probe only")
    args = parser.parse_args()
    if not 1 <= args.samples <= 1000 or not 0 <= args.interval_ms <= 1000:
        parser.error("invalid sample count or interval")
    asyncio.run(run(args))
