"""Exercise release activation and rollback without restarting real services."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("network_release", ROOT / "deploy/apply_network_release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
sys.path.insert(0, str(ROOT / "tools"))
import event_stream


class FakeWebSocket:
    subprotocol = event_stream.PROTOCOL

    async def __aenter__(self):
        return self

    async def __aexit__(self, *args):
        pass

    async def send(self, data):
        assert event_stream.decode(data) == [b"!probe"]

    async def recv(self):
        return event_stream.encode([b"!probe-ok"])


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.home = Path(self.directory.name)
        self.roots = [self.home / "termino-server", self.home / "termino-websocket"]
        for root in self.roots:
            (root / "releases/old").mkdir(parents=True)
            (root / "releases/new").mkdir()
            (root / "current").symlink_to(root / "releases/old")
        files = {"tetr_server": self.roots[0] / "releases/new/tetr_server",
                 "websocket_gateway.py": self.roots[1] / "releases/new/websocket_gateway.py",
                 "event_stream.py": self.roots[1] / "releases/new/event_stream.py"}
        for name, path in files.items():
            path.write_bytes(name.encode())
        self.manifest = self.home / "manifest.json"
        self.manifest.write_text(json.dumps({name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}))
        self.saved_path = sys.path[:]
        self.addCleanup(lambda: sys.path.__setitem__(slice(None), self.saved_path))

    def activate(self, restart):
        udp = MagicMock()
        udp.__enter__.return_value.recv.return_value = b"!probe-ok"
        http = MagicMock()
        http.__enter__.return_value.status = 200
        real_socket = release.socket.socket
        def socket_factory(family=-1, kind=-1, *args, **kwargs):
            if family == release.socket.AF_INET and kind == release.socket.SOCK_DGRAM:
                return udp
            return real_socket(family, kind, *args, **kwargs)
        with patch.object(Path, "home", return_value=self.home), \
             patch.object(sys, "argv", ["apply", "new", str(self.manifest)]), \
             patch.object(release.subprocess, "run", side_effect=restart) as calls, \
             patch.object(release.socket, "socket", side_effect=socket_factory), \
             patch.object(release.urllib.request, "urlopen", return_value=http), \
             patch("websockets.asyncio.client.connect", return_value=FakeWebSocket()), \
             contextlib.redirect_stdout(io.StringIO()):
            release.main()
        return calls

    def test_success_checks_both_links_before_restart(self):
        def restart(*args, **kwargs):
            for root in self.roots:
                self.assertEqual((root / "current").resolve().name, "new")
        self.assertEqual(self.activate(restart).call_count, 1)
        record = json.loads((self.roots[0] / "releases/new/deployment.json").read_text())
        self.assertEqual(record["previous"], [str(root / "releases/old") for root in self.roots])

    def test_restart_failure_restores_both_previous_releases(self):
        calls = 0
        def restart(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise subprocess.CalledProcessError(1, "simulated restart failure")
        with self.assertRaises(subprocess.CalledProcessError):
            self.activate(restart)
        self.assertEqual(calls, 2)
        for root in self.roots:
            self.assertEqual((root / "current").resolve().name, "old")

    def test_checksum_failure_never_switches_or_restarts(self):
        (self.roots[0] / "releases/new/tetr_server").write_bytes(b"wrong upload")
        def restart(*args, **kwargs):
            self.fail("must not restart after a checksum failure")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            self.activate(restart)
        for root in self.roots:
            self.assertEqual((root / "current").resolve().name, "old")


if __name__ == "__main__":
    unittest.main()
