#!/usr/bin/env python3
"""Own the relay and gateway processes for the C++ transport integration test."""
import asyncio
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile

from websockets.asyncio.server import serve

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from websocket_gateway import Gateway, MAX_MESSAGE


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
        if message == b"close-now":
            await ws.close()
            return
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


async def run(binary, relay):
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
            for _ in range(100):
                if gateway.active == 0:
                    break
                await asyncio.sleep(0.01)
            assert gateway.active == 0, "Disconnected WebSockets retained gateway resources"
        async with serve(echo, "127.0.0.1", 0, compression=None,
                         max_size=MAX_MESSAGE, close_timeout=1) as server:
            port = server.sockets[0].getsockname()[1]
            await client(binary, f"ws://127.0.0.1:{port}/", "--echo")
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
