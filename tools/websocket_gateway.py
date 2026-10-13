#!/usr/bin/env python3
"""Expose the existing two-player UDP relay through a WebSocket endpoint.

Bind to loopback behind Cloudflare Tunnel. Each WebSocket owns a connected UDP
socket, so the relay can distinguish players without changing its protocol.
Requires the pinned package in deploy/websocket-requirements.txt.
"""
import argparse
import asyncio
import contextlib
import logging
import signal
from http import HTTPStatus

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

MAX_MESSAGE = 65507
MAX_BUFFERED = 2 * 1024 * 1024
LOG = logging.getLogger("termino.gateway")


class RelayPeer(asyncio.DatagramProtocol):
    def __init__(self):
        self.replies = asyncio.Queue(maxsize=64)
        self.buffered = 0
        self.failed = asyncio.get_running_loop().create_future()

    def datagram_received(self, data, _address):
        if self.replies.full() or self.buffered + len(data) > MAX_BUFFERED:
            if not self.failed.done():
                self.failed.set_result("UDP reply queue overflow")
            return
        self.buffered += len(data)
        self.replies.put_nowait(data)

    def error_received(self, _error):
        if not self.failed.done():
            self.failed.set_result("UDP relay unavailable")

    async def forward_replies(self, websocket):
        while True:
            data = await self.replies.get()
            self.buffered -= len(data)
            await websocket.send(data)


class Gateway:
    def __init__(self, relay_host="127.0.0.1", relay_port=3000, max_connections=32):
        self.relay_address = (relay_host, relay_port)
        self.max_connections = max_connections
        self.active = 0

    def process_request(self, connection, request):
        if request.path == "/healthz":
            return connection.respond(HTTPStatus.OK, "TerMino WebSocket gateway ready\n")
        if request.path != "/ws":
            return connection.respond(HTTPStatus.NOT_FOUND, "Not found\n")
        if self.active >= self.max_connections:
            return connection.respond(HTTPStatus.SERVICE_UNAVAILABLE, "Gateway busy\n")
        return None

    async def handle(self, websocket):
        # Recheck after the handshake; simultaneous handshakes may have passed
        # process_request before any handler started.
        if self.active >= self.max_connections:
            await websocket.close(1013, "Gateway busy")
            return
        self.active += 1
        transport = None
        token = None
        tasks = []
        try:
            transport, peer = await asyncio.get_running_loop().create_datagram_endpoint(
                RelayPeer, remote_addr=self.relay_address)

            async def forward_requests():
                nonlocal token
                async for message in websocket:
                    data = message.encode("utf-8") if isinstance(message, str) else message
                    if len(data) > MAX_MESSAGE:
                        await websocket.close(1009, "Message too large")
                        return
                    if data.startswith((b"!join;", b"!join-v2;")):
                        candidate = data[9:] if data.startswith(b"!join-v2;") else data[6:]
                        if len(candidate) == 20 and all(97 <= ch <= 122 for ch in candidate):
                            token = candidate
                    transport.sendto(data)

            tasks = [asyncio.create_task(forward_requests()),
                     asyncio.create_task(peer.forward_replies(websocket))]
            done, _ = await asyncio.wait([*tasks, peer.failed], return_when=asyncio.FIRST_COMPLETED)
            if peer.failed in done:
                await websocket.close(1011, peer.failed.result())
            for task in done:
                if task is not peer.failed:
                    task.result()
        except ConnectionClosed:
            pass
        except OSError:
            LOG.warning("Cannot connect to local UDP relay")
            await websocket.close(1011, "UDP relay unavailable")
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            if transport is not None:
                # A TCP reset may prevent the client's queued leave message
                # from reaching us. Release only its last valid session token.
                if token is not None:
                    transport.sendto(b"!leave;" + token)
                transport.close()
            self.active -= 1


async def run(args):
    gateway = Gateway(args.relay_host, args.relay_port, args.max_connections)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        with contextlib.suppress(NotImplementedError):
            loop.add_signal_handler(signum, stop.set)
    async with serve(gateway.handle, args.host, args.port,
                     process_request=gateway.process_request, compression=None,
                     origins=[None], max_size=MAX_MESSAGE, max_queue=16,
                     write_limit=65536, open_timeout=5, close_timeout=2,
                     ping_interval=20, ping_timeout=20, server_header=None):
        LOG.info("Listening on %s:%s/ws; forwarding to UDP %s:%s",
                 args.host, args.port, args.relay_host, args.relay_port)
        await stop.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--relay-host", default="127.0.0.1")
    parser.add_argument("--relay-port", type=int, default=3000)
    parser.add_argument("--max-connections", type=int, default=32)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.relay_port <= 65535 or args.max_connections < 1:
        parser.error("ports must be 1..65535; max-connections must be positive")
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
