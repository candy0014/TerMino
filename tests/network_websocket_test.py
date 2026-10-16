#!/usr/bin/env python3
"""Own the relay and gateway processes for the C++ transport integration test."""
import asyncio
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile
import time

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed
from websockets.asyncio.client import connect

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from websocket_gateway import Gateway, RelayPeer, MAX_MESSAGE
import event_stream


async def client(binary, url, *args):
    proc = await asyncio.create_subprocess_exec(binary, url, *args)
    try:
        status = await asyncio.wait_for(proc.wait(), 40)
        if status:
            raise RuntimeError(f"C++ WebSocket test exited {status}")
    finally:
        if proc.returncode is None:
            proc.kill()
            await proc.wait()


async def echo(ws):
    async for message in ws:
        events = event_stream.decode(message) if ws.subprotocol == event_stream.PROTOCOL else [message]
        if events == [b"close-now"]:
            await ws.close()
            return
        if events == [b"burst-160"]:
            for index in range(160):
                wire = f"checkpoint-{index}".encode()
                await ws.send(event_stream.encode([wire]) if ws.subprotocol == event_stream.PROTOCOL else wire)
            continue
        if isinstance(message, bytes) and len(message) > 10000:
            async def fragments():
                yield message[:10000]
                pong = await ws.ping(b"fragment-ping")
                # Finish the frame after giving the peer time to receive the
                # control frame; awaiting pong here would deadlock its recv.
                await asyncio.sleep(0.03)
                yield message[10000:30000]
                yield message[30000:]
            await ws.send(fragments())
        else:
            await ws.send(message)


async def echo_until_disconnect(ws):
    try:
        await echo(ws)
    except ConnectionClosed:
        # The socket owner may stop its worker without a close handshake.
        # Client assertions still verify every response and explicit close.
        pass


async def check_event_queue():
    # A valid resolved-motion fixture exercises the gateway's owner/revision
    # boundaries, including UDP reordering before batching.
    def motion(sequence, revision=1, slot=0):
        data = b"!motion-v1;" + (77).to_bytes(4, "big")
        data += bytes([sequence, sequence, revision]) + (123).to_bytes(8, "big")
        data += bytes([0, 0, 0, 2, 0, 4, 8, 0])
        return b"!player;77;" + str(slot).encode() + b";" + data

    queue = event_stream.EventQueue()
    now = asyncio.get_running_loop().time()
    assert queue.push(motion(3), now, True)
    assert queue.push(motion(2), now, True)
    assert queue.push(motion(1, slot=1), now, True)
    assert queue.push(b"attack", now, True)
    assert queue.push(motion(4), now, True)
    assert queue.push(b"checkpoint", now, True)
    assert queue.push(motion(5, revision=2), now, True)
    expected = [motion(3), motion(1, slot=1), b"attack", motion(4), b"checkpoint", motion(5, revision=2)]
    assert await queue.take(True) == expected
    assert queue.bytes == 0
    # A complete thumbnail can remove its obsolete images while reliable
    # events keep their order and the new image stays after those events.
    def mini(sequence):
        return f"!player;77;1;!mini-v1;{sequence};10;20;1;1;0;".encode() + b"\x01"
    assert queue.push(mini(1), now, True)
    assert queue.push(b"attack", now, True)
    assert queue.push(b"checkpoint", now, True)
    assert queue.push(mini(2), now, True)
    assert queue.push(mini(1), now, True)
    assert await queue.take(True) == [b"attack", b"checkpoint", mini(2)]
    assert queue.bytes == 0
    # Even a future queue timestamp cannot impose a visual batching delay.
    assert queue.push(motion(6), now + 60, True)
    waiting = asyncio.create_task(queue.take(True))
    await asyncio.sleep(0)
    assert waiting.done() and await waiting == [motion(6)]
    # Idle waits still wake immediately when reliable control data arrives.
    waiting = asyncio.create_task(queue.take(True))
    await asyncio.sleep(0)
    assert not waiting.done()
    assert queue.push(b"urgent-attack", now, True)
    assert await asyncio.wait_for(waiting, 1) == [b"urgent-attack"]
    binary = event_stream.encode([b"\0;TES1", b"", b"!probe"])
    assert event_stream.decode(binary) == [b"\0;TES1", b"", b"!probe"]
    for malformed in [binary + b"x", b"TES1\0\0", b"TES1\0A", b"TES1\0\1\xff\xff\xff\xff"]:
        try:
            event_stream.decode(malformed)
            raise AssertionError("Invalid event envelope accepted")
        except ValueError:
            pass
    for length in range(len(binary)):
        try:
            event_stream.decode(binary[:length])
            raise AssertionError("Truncated event envelope accepted")
        except ValueError:
            pass
    # A relay batch becomes one ready WebSocket batch, retaining binary
    # payloads and event order without an event-loop or timer delay.
    peer = RelayPeer()
    peer.stream = True
    peer.datagram_received(binary, None)
    assert await peer.replies.take(True) == [b"\0;TES1", b"", b"!probe"]
    assert (peer.relay_datagrams, peer.relay_events, peer.relay_batches) == (1, 3, 1)
    peer.datagram_received(binary + b"trailing", None)
    assert peer.failed.done() and peer.replies.bytes == 0
    assert peer.failed.result() == "Invalid UDP event batch"
    legacy = RelayPeer()
    legacy.datagram_received(binary, None)
    assert await legacy.replies.take(False) == [binary]
    def room(revision, suffix=b"32;0;state-A", request=0):
        return f"!room-reply;token;{request};1;524F4F4D;1;2;0;{revision};".encode() + suffix
    peer = RelayPeer()
    peer.stream = True
    states = [room(1), room(2), room(3, b"32;0;state-B"), room(4), room(5, request=42), room(6)]
    for state in states:
        peer.datagram_received(state, None)
    assert await peer.replies.take(True) == [states[i] for i in (0, 2, 3, 4)]
    assert peer.duplicate_room_bytes == len(states[1]) + len(states[5])
    # Unnegotiated clients retain all original heartbeat state messages.
    for state in states[:2]:
        legacy.datagram_received(state, None)
    assert await legacy.replies.take(False) == states[:1]
    assert await legacy.replies.take(False) == states[1:2]


async def check_room_heartbeat_states(url):
    token = b"q" * 20
    async with connect(url, subprotocols=[event_stream.PROTOCOL], compression=None) as ws:
        async def exchange(events):
            await ws.send(event_stream.encode(events + [b"!probe"]))
            replies = []
            while True:
                received = event_stream.decode(await asyncio.wait_for(ws.recv(), 5))
                for event in received:
                    if event == b"!probe-ok":
                        return replies
                    replies.append(event)
        def states(replies):
            return [data for data in replies if data.startswith(b"!room-reply;" + token + b";0;")]
        await exchange([b"!join-v4;" + token])
        name = ("Gateway-state-" + str(time.monotonic_ns())).encode().hex().upper().encode()
        created = await exchange([b"!room;" + token + b";1;0;" + name + b";1"])
        assert len(states(created)) == 1
        # Reliable transport already holds the state; ordinary heartbeat
        # pongs remain, without repeating an unchanged complete roster.
        replies = await exchange([b"!ping;" + token] * 3)
        assert replies.count(b"!pong;" + token) == 3 and not states(replies)
        for request, role in [(2, 1), (3, 0)]:
            replies = await exchange([b"!room;" + token + f";{request};6;;0;round-v1;{role};0;0".encode()])
            assert len(states(replies)) == 1
            assert any(data.startswith(b"!room-reply;" + token + f";{request};".encode()) for data in replies)
            assert not states(await exchange([b"!ping;" + token]))
        await ws.send(event_stream.encode([b"!leave;" + token]))


async def run(binary, relay, room_binary=None, view_binary=None):
    await check_event_queue()
    proc = subprocess.Popen([relay], stdout=subprocess.DEVNULL)
    try:
        # Probe the actual relay rather than guessing that startup is complete.
        for _ in range(100):
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.settimeout(0.02)
                probe.sendto(b"!probe", ("127.0.0.1", 3000))
                try:
                    if probe.recv(100) == b"!probe-ok":
                        break
                except TimeoutError:
                    pass
            if proc.poll() is not None:
                raise RuntimeError("UDP relay failed to start (port 3000 already occupied?)")
            await asyncio.sleep(0.01)
        else:
            raise RuntimeError("UDP relay did not become ready")
        gateway = Gateway()
        async with serve(gateway.handle, "127.0.0.1", 0,
                         process_request=gateway.process_request, compression=None,
                         max_size=MAX_MESSAGE, close_timeout=1) as server:
            port = server.sockets[0].getsockname()[1]
            await client(binary, f"ws://127.0.0.1:{port}/ws")
            if room_binary:
                await client(room_binary, f"ws://127.0.0.1:{port}/ws")
            if view_binary:
                await client(view_binary, f"ws://127.0.0.1:{port}/ws")
            for _ in range(100):
                if gateway.active == 0:
                    break
                await asyncio.sleep(0.01)
            assert gateway.active == 0, "Disconnected WebSockets retained gateway resources"
        async with serve(echo_until_disconnect, "127.0.0.1", 0, compression=None,
                         max_size=MAX_MESSAGE, close_timeout=1) as server:
            port = server.sockets[0].getsockname()[1]
            await client(binary, f"ws://127.0.0.1:{port}/", "--echo")
            await client(binary, f"ws://127.0.0.1:{port}/", "--paced-visuals")
        # New clients with an old gateway above retain one-packet transport.
        # With negotiation, exercise the same real relay and room behavior.
        async with serve(gateway.handle, "127.0.0.1", 0,
                         process_request=gateway.process_request, compression=None,
                         subprotocols=[event_stream.PROTOCOL], select_subprotocol=event_stream.select_subprotocol,
                         max_size=event_stream.MAX_FRAME, close_timeout=1) as server:
            port = server.sockets[0].getsockname()[1]
            url = f"ws://127.0.0.1:{port}/ws"
            await check_room_heartbeat_states(url)
            await client(binary, url)
            if room_binary:
                await client(room_binary, url)
            if view_binary:
                await client(view_binary, url)
            # Older clients need not offer a protocol to use the new gateway.
            async with connect(url) as ws:
                assert ws.subprotocol is None
                await ws.send(b"!probe")
                assert await ws.recv() == b"!probe-ok"
            async with connect(url, subprotocols=[event_stream.PROTOCOL]) as ws:
                await ws.send(event_stream.encode([b"!probe"] * 64))
                replies = []
                while len(replies) < 64:
                    replies.extend(event_stream.decode(await asyncio.wait_for(ws.recv(), 2)))
                assert replies == [b"!probe-ok"] * 64
            # Reject the whole malformed batch before forwarding its valid prefix.
            async with connect(url, subprotocols=[event_stream.PROTOCOL]) as ws:
                await ws.send(event_stream.encode([b"!probe"]) + b"trailing")
                try:
                    await ws.recv()
                    raise AssertionError("Malformed batch yielded a game response")
                except ConnectionClosed as error:
                    assert error.rcvd.code == 1009
        async with serve(echo_until_disconnect, "127.0.0.1", 0, compression=None,
                         subprotocols=[event_stream.PROTOCOL], select_subprotocol=event_stream.select_subprotocol,
                         max_size=event_stream.MAX_FRAME, close_timeout=1) as server:
            port = server.sockets[0].getsockname()[1]
            await client(binary, f"ws://127.0.0.1:{port}/", "--echo")
            await client(binary, f"ws://127.0.0.1:{port}/", "--event-stream")
            await client(binary, f"ws://127.0.0.1:{port}/", "--stream-visuals")
        # Verification must reject an untrusted certificate, not silently use
        # an insecure TLS mode. OpenSSL only generates the local test fixture.
        with tempfile.TemporaryDirectory(prefix="termino-ws-tls-") as directory:
            cert, key = pathlib.Path(directory)/"cert.pem", pathlib.Path(directory)/"key.pem"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                            "-keyout", str(key), "-out", str(cert), "-days", "1",
                            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            tls.load_cert_chain(cert, key)
            async with serve(echo, "127.0.0.1", 0, ssl=tls, close_timeout=1) as server:
                port = server.sockets[0].getsockname()[1]
                await client(binary, f"wss://localhost:{port}/", "--bad-tls")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


if __name__ == "__main__":
    asyncio.run(run(*sys.argv[1:]))
