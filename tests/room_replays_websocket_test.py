#!/usr/bin/env python3
"""Exercise room replay uploads, range downloads and spectator save via WS."""
import asyncio
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import websocket_gateway as gateway_module
from websockets.asyncio.server import serve


async def run(binary):
    gateway = gateway_module.Gateway(relay_port=3045)
    async with serve(
        gateway.handle, "127.0.0.1", 0,
        process_request=gateway.process_request, compression=None,
        subprotocols=[gateway_module.event_stream.PROTOCOL],
        select_subprotocol=gateway_module.event_stream.select_subprotocol,
    ) as server:
        port = server.sockets[0].getsockname()[1]
        process = await asyncio.create_subprocess_exec(
            binary, "room_replays_test", f"ws://127.0.0.1:{port}/ws"
        )
        try:
            status = await asyncio.wait_for(process.wait(), 100)
            if status:
                raise RuntimeError(f"Room replay WebSocket test exited {status}")
        finally:
            if process.returncode is None:
                process.kill()
                await process.wait()


if __name__ == "__main__":
    asyncio.run(run(sys.argv[1]))
