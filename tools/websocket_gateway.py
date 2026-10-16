#!/usr/bin/env python3
"""Expose the room-based UDP relay through a WebSocket endpoint.

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
import event_stream

MAX_MESSAGE = 65507
MAX_BUFFERED = 2 * 1024 * 1024
LOG = logging.getLogger("termino.gateway")


class RelayPeer(asyncio.DatagramProtocol):
    def __init__(self):
        self.replies = event_stream.EventQueue()
        self.stream = False
        self.failed = asyncio.get_running_loop().create_future()
        self.relay_datagrams = self.relay_events = self.relay_batches = self.websocket_frames = 0
        self.duplicate_room_bytes = 0
        self.last_room_state = None

    def datagram_received(self, data, _address):
        try:
            events = event_stream.decode(data) if self.stream and data.startswith(b"TES1") else [data]
        except ValueError:
            if not self.failed.done():
                self.failed.set_result("Invalid UDP event batch")
            return
        now = asyncio.get_running_loop().time()
        self.relay_datagrams += 1
        self.relay_events += len(events)
        self.relay_batches += len(events) > 1
        for event in events:
            room_state = None
            if self.stream and event.startswith(b"!room-reply;"):
                fields = event.split(b";", 9)
                if len(fields) == 10 and fields[2:4] == [b"0", b"1"] and fields[8].isdigit():
                    # Heartbeats generate fresh revision numbers even when
                    # every room field is unchanged. Once queued on reliable
                    # WebSocket transport, that same state needs no repair.
                    # Keep requests and every actual transition in order.
                    room_state = (tuple(fields[:8]), fields[9])
                    if room_state == self.last_room_state:
                        self.duplicate_room_bytes += len(event)
                        continue
            if not self.replies.push(event, now, self.stream):
                if not self.failed.done():
                    self.failed.set_result("UDP reply queue overflow")
                return
            if room_state is not None:
                self.last_room_state = room_state

    @property
    def buffered(self):
        return self.replies.bytes

    def error_received(self, _error):
        if not self.failed.done():
            self.failed.set_result("UDP relay unavailable")

    async def forward_replies(self, websocket):
        while True:
            events = await self.replies.take(self.stream)
            await websocket.send(event_stream.encode(events) if self.stream else events[0])
            self.websocket_frames += 1


class Gateway:
    def __init__(self, relay_host="127.0.0.1", relay_port=3000, max_connections=256):
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
        explicit_leave = False
        peer = None
        endpoint = websocket.remote_address
        started = asyncio.get_running_loop().time()
        try:
            transport, peer = await asyncio.get_running_loop().create_datagram_endpoint(
                RelayPeer, remote_addr=self.relay_address)
            peer.stream = websocket.subprotocol == event_stream.PROTOCOL

            async def forward_requests():
                nonlocal token, explicit_leave
                async for message in websocket:
                    data = message.encode("utf-8") if isinstance(message, str) else message
                    try:
                        events = event_stream.decode(data) if peer.stream else [data]
                        if any(len(event) > MAX_MESSAGE for event in events):
                            raise ValueError("Message too large")
                    except ValueError:
                        await websocket.close(1009, "Invalid event batch")
                        return
                    for data in events:
                        if data.startswith((b"!join;", b"!join-v2;", b"!join-v3;", b"!join-v4;")):
                            candidate = data[9:] if data.startswith((b"!join-v2;", b"!join-v3;", b"!join-v4;")) else data[6:]
                            if len(candidate) == 20 and all(97 <= ch <= 122 for ch in candidate):
                                token = candidate
                        if token is not None and data == b"!leave;" + token:
                            explicit_leave = True
                        transport.sendto(data)
                        if peer.stream and token is not None and data.startswith((b"!join;", b"!join-v2;", b"!join-v3;", b"!join-v4;", b"!ping;")):
                            # Opt in only on this gateway-owned UDP endpoint.
                            # Old relays ignore it; ordinary UDP and legacy
                            # WebSocket peers keep receiving single packets.
                            transport.sendto(b"!relay-batch-v1;" + token)

            tasks = [asyncio.create_task(forward_requests()),
                     asyncio.create_task(peer.forward_replies(websocket))]
            done, _ = await asyncio.wait([*tasks, peer.failed], return_when=asyncio.FIRST_COMPLETED)
            if peer.failed in done:
                LOG.warning("Relay connection failed: endpoint=%s reason=%s buffered=%s",
                            endpoint, peer.failed.result(), peer.buffered)
                await websocket.close(1011, peer.failed.result())
            for task in done:
                if task is not peer.failed:
                    task.result()
        except ConnectionClosed as error:
            LOG.info("WebSocket transport ended: endpoint=%s received=%s sent=%s",
                     endpoint, error.rcvd, error.sent)
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
            LOG.info("Session closed: endpoint=%s duration=%.1fs close_code=%s reason=%s explicit_leave=%s pending_bytes=%s relay_datagrams=%s relay_events=%s relay_batches=%s websocket_frames=%s duplicate_room_bytes=%s",
                     endpoint, asyncio.get_running_loop().time()-started, websocket.close_code,
                     websocket.close_reason, explicit_leave, peer.buffered if peer else 0,
                     peer.relay_datagrams if peer else 0, peer.relay_events if peer else 0,
                     peer.relay_batches if peer else 0, peer.websocket_frames if peer else 0,
                     peer.duplicate_room_bytes if peer else 0)


async def run(args):
    gateway = Gateway(args.relay_host, args.relay_port, args.max_connections)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        with contextlib.suppress(NotImplementedError):
            loop.add_signal_handler(signum, stop.set)
    async with serve(gateway.handle, args.host, args.port,
                     process_request=gateway.process_request, compression=None,
                     subprotocols=[event_stream.PROTOCOL], select_subprotocol=event_stream.select_subprotocol,
                     origins=[None], max_size=event_stream.MAX_FRAME, max_queue=16,
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
    parser.add_argument("--max-connections", type=int, default=256)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.relay_port <= 65535 or args.max_connections < 1:
        parser.error("ports must be 1..65535; max-connections must be positive")
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
