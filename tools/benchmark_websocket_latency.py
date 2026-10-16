#!/usr/bin/env python3
"""Measure actual client -> WS gateway -> UDP echo -> gateway -> client RTT.

No Internet, input-to-display, or gameplay delay is measured. Use identical
client builds and --gateway-dir snapshots for a controlled before/after run.
"""
import argparse
import asyncio
import importlib.util
import json
from pathlib import Path
import sys

from websockets.asyncio.server import serve


class Echo(asyncio.DatagramProtocol):
    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, address):
        self.transport.sendto(data, address)


async def run(args):
    sys.path.insert(0, str(args.gateway_dir))
    spec = importlib.util.spec_from_file_location("latency_gateway", args.gateway_dir / "websocket_gateway.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(Echo, local_addr=("127.0.0.1", 0))
    try:
        gateway = module.Gateway(relay_port=transport.get_extra_info("sockname")[1])
        async with serve(gateway.handle, "127.0.0.1", 0, compression=None,
                         process_request=gateway.process_request,
                         subprotocols=[module.event_stream.PROTOCOL],
                         select_subprotocol=module.event_stream.select_subprotocol,
                         max_size=module.event_stream.MAX_FRAME, close_timeout=1) as server:
            port = server.sockets[0].getsockname()[1]
            proc = await asyncio.create_subprocess_exec(str(args.binary), f"ws://127.0.0.1:{port}/ws",
                                                       "--latency-samples", stdout=asyncio.subprocess.PIPE,
                                                       stderr=asyncio.subprocess.PIPE)
            try:
                stdout, stderr = await asyncio.wait_for(proc.communicate(), 30)
            finally:
                if proc.returncode is None:
                    proc.kill()
                    await proc.wait()
            if proc.returncode:
                raise RuntimeError(stderr.decode(errors="replace"))
            result = json.loads(stdout)
            result.update(metric="loopback_motion_round_trip", binary=str(args.binary), gateway_dir=str(args.gateway_dir))
            print(json.dumps(result))
    finally:
        transport.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--gateway-dir", type=Path, default=Path(__file__).resolve().parent)
    asyncio.run(run(parser.parse_args()))
