#!/usr/bin/env python3
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'deploy'))
import tunnel_watchdog as watchdog
import install_tunnel_watchdog as installer


def sample(**values):
    result = dict(load_state='loaded', active_state='active', pid=12, started_at=1,
                  ready=False, relay=True, gateway=True)
    result.update(values)
    return result


class RecoveryTest(unittest.TestCase):
    def decision(self, previous=None, now=100, **values):
        return watchdog.decide(previous or {}, sample(**values), now, 'boot')

    def fail_three(self, previous=None, now=100, **values):
        state = previous
        for i in range(3):
            state, action = self.decision(state, now+i*15, **values)
        return state, action

    def test_confirm_then_recover(self):
        state, action = self.fail_three()
        self.assertEqual(action, 'restart')
        self.assertEqual(state['restart_at'], 130)
        self.assertEqual(state['attempts'], 1)

    def test_transient_does_not_restart(self):
        state, _ = self.decision()
        state, action = self.decision(state, now=115, ready=True)
        self.assertIsNone(action)
        self.assertEqual(state['failures'], 0)
        state, action = self.decision(state, now=130)
        self.assertEqual(state['failures'], 1)
        self.assertIsNone(action)

    def test_startup_manual_stop_origin_failure(self):
        for values in (dict(started_at=90), dict(active_state='inactive'),
                       dict(active_state='activating'), dict(relay=False),
                       dict(gateway=False), dict(load_state='not-found')):
            state, action = self.fail_three(**values)
            self.assertIsNone(action, values)
            self.assertEqual(state['failures'], 0)

    def test_backoff_and_cap(self):
        state, _ = self.fail_three()
        state, action = self.fail_three(state, 145)
        self.assertIsNone(action)
        state, action = self.decision(state, now=250)
        self.assertEqual(action, 'restart')
        state, action = self.fail_three(state, 460)
        self.assertEqual(action, 'restart')
        state, action = self.fail_three(state, 700)
        self.assertIsNone(action)
        state, action = self.decision(state, now=790)
        self.assertEqual(action, 'restart')

    def test_pid_boot_and_stale_samples_reset_confirmation(self):
        state, _ = self.decision()
        state, _ = self.decision(state, now=115)
        for replacement, now in ((dict(pid=13), 130), ({}, 220)):
            result, action = self.decision(state, now=now, **replacement)
            self.assertEqual(result['failures'], 1)
            self.assertIsNone(action)
        result, action = watchdog.decide(state, sample(), 130, 'new-boot')
        self.assertEqual(result['failures'], 1)
        self.assertIsNone(action)

    def test_failed_unit_is_recoverable(self):
        state, action = self.fail_three(active_state='failed', pid=0)
        self.assertEqual(action, 'restart')

    def test_cooldown_survives_brief_recovery(self):
        state, _ = self.fail_three()
        state, _ = self.decision(state, now=145, ready=True)
        state, action = self.fail_three(state, now=160)
        self.assertIsNone(action)
        self.assertEqual(state['restart_at'], 130)

    def test_corrupted_state(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'state.json'
            for content in ('null', '[]', 'oops', '{"pid": -1}', '{"sample_at": NaN}',
                            '{"attempts": 1.5}'):
                path.write_text(content)
                self.assertEqual(watchdog.load_state(path), {})

    def test_cooldown_saved_before_failed_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            state, _ = self.decision(now=100)
            state, _ = self.decision(state, now=115)
            watchdog.save_state(path / 'state.json', state)
            def fail(*args, **kwargs):
                self.assertEqual(watchdog.load_state(path / 'state.json')['restart_at'], 130)
                raise subprocess.CalledProcessError(1, 'systemctl')
            read_text = Path.read_text
            with patch.object(watchdog, 'observe', return_value=sample()), \
                 patch.object(watchdog.time, 'monotonic', return_value=130), \
                 patch.object(watchdog.Path, 'read_text', autospec=True, side_effect=lambda p: 'boot' if str(p).startswith('/proc/') else read_text(p)), \
                 patch.object(watchdog, 'systemctl', side_effect=fail), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(subprocess.CalledProcessError):
                    watchdog.run_once(path, 'test.service')

    def test_readiness_and_origin_probes(self):
        properties = 'LoadState=loaded\nActiveState=active\nMainPID=12\nActiveEnterTimestampMonotonic=1000000\n'
        for reply, expected in (((200,b'{"readyConnections":2}'),True),
                                ((200,b'{"readyConnections":0}'),False),
                                ((503,b'bad'),False), ((200,b'[]'),False)):
            with patch.object(watchdog, 'systemctl', return_value=properties), \
                 patch.object(watchdog, 'relay_ready', return_value=True), \
                 patch.object(watchdog, 'http_get', side_effect=[reply,(200,b'TerMino WebSocket gateway ready\n')]):
                observed = watchdog.observe('test.service')
                self.assertEqual(observed['ready'], expected)
                self.assertTrue(observed['gateway'] and observed['relay'])

    def test_loopback_ignores_proxy(self):
        from http.server import BaseHTTPRequestHandler, HTTPServer
        import threading
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200); self.end_headers(); self.wfile.write(b'local')
            def log_message(self, *args): pass
        with HTTPServer(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with patch.dict(os.environ, {'http_proxy':'http://127.0.0.1:1', 'HTTP_PROXY':'http://127.0.0.1:1', 'no_proxy':'', 'NO_PROXY':''}):
                    self.assertEqual(watchdog.http_get(f'http://127.0.0.1:{server.server_port}/ready'), (200,b'local'))
            finally:
                server.shutdown(); thread.join()


class InstallerTest(unittest.TestCase):
    def test_install_and_rollback_preserve_servers(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            token = home / '.config/termino-cloudflare/token'
            token.parent.mkdir(parents=True);token.write_text('test-only-secret')
            old = home / '.config/systemd/user/termino-cloudflare.service.d/recovery.conf'
            old.parent.mkdir(parents=True);old.write_text('old policy');old.chmod(0o640)
            route = old.with_name('route.conf');route.write_text('keep route')
            calls = []
            def control(*args, check=True):
                calls.append(args)
                result = subprocess.CompletedProcess(args, 0, stdout='')
                if args[0] == 'show': result.stdout = 'loaded\n'
                elif args[0] in ('is-active','is-enabled'):result.returncode=1 if len(calls)<10 else 0
                return result
            with patch.object(installer, 'control', side_effect=control), contextlib.redirect_stdout(io.StringIO()):
                backup = installer.install(home, ROOT / 'deploy')
                self.assertIn('Restart=always',old.read_text())
                installer.rollback(backup)
            self.assertEqual(old.read_text(), 'old policy')
            self.assertEqual(old.stat().st_mode & 0o777, 0o640)
            self.assertEqual(route.read_text(), 'keep route')
            self.assertEqual(token.read_text(), 'test-only-secret')
            self.assertFalse((home / 'termino-cloudflare/tunnel_watchdog.py').exists())
            for args in calls:
                self.assertFalse(args[0] in ('restart','stop','start') and any(unit + '.service' in args for unit in installer.UNITS))

    def test_install_failure_restores_files(self):
        with tempfile.TemporaryDirectory() as directory:
            home=Path(directory);token=home/'.config/termino-cloudflare/token'
            token.parent.mkdir(parents=True);token.touch()
            def control(*args, check=True):
                if args[:2] == ('enable','--now'):raise subprocess.CalledProcessError(1,args)
                return subprocess.CompletedProcess(args, 0 if args[0]=='show' else 1, stdout='loaded\n')
            with patch.object(installer,'control',side_effect=control):
                with self.assertRaises(subprocess.CalledProcessError):installer.install(home,ROOT/'deploy')
            self.assertFalse((home/'termino-cloudflare/tunnel_watchdog.py').exists())
            self.assertTrue(token.exists())


if __name__ == '__main__':unittest.main()
