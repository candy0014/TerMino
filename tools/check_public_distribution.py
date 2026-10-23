#!/usr/bin/env python3
"""Verify clean public packages and smoke-test native executables."""
from pathlib import Path
import hashlib
import json
import os
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile


def run(args, cwd, env=None):
    p = subprocess.run(args, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, timeout=90)
    if p.returncode:
        raise RuntimeError(p.stdout)
    return p.stdout


def smoke(folder, windows):
    base = folder.parent
    launcher = folder / ('Play.bat' if windows else 'Play.sh')
    def command(*args):
        return ['cmd', '/c', 'call', str(launcher), *args] if windows else [str(launcher), *args]
    assert 'TerMino' in run(command('--help'), base)
    config = folder / '.tetr' / ('player1_windows.ini' if windows else 'player1_linux.ini')
    config.parent.mkdir(exist_ok=True)
    config.write_text('[MODEL]\nModel = 3\n[GAME]\nSimulation PPS = 2.5\n[AUTOPLAY]\nAutoplay = 9\nPPS of Autoplay = 0\nWidth of Autoplay = 60\n')
    env = dict(os.environ, TETR_TEST_BATCH='1', TETR_EXIT_AFTER_GAME='1',
               TETR_TEST_MAX_PIECES='8', TETR_TEST_SEED='20261016',
               TETR_TEST_CLOCK_SCALE='100', TETR_TEST_REPLAY='public-smoke.txt')
    run(command('--play'), base, env)
    replay = folder / '.tetr/replay/public-smoke.txt'
    contents = replay.read_bytes()
    assert b'{end_all,' in contents and b'{end_place_data,' in contents
    consolidated = folder / '.tetr/config.json'
    if consolidated.exists():
        profile = json.loads(consolidated.read_text(encoding='utf-8'))
        platform = 'windows' if windows else 'linux'
        profile['players']['player1'][platform] = {'settings': {'MODEL': {
            'Model': 5, 'Replay': '.tetr/replay/public-smoke.txt', 'Time Multiplier': 100}}}
        consolidated.write_text(json.dumps(profile), encoding='utf-8')
    else:
        config.write_text('[MODEL]\nModel = 5\nReplay = .tetr/replay/public-smoke.txt\nTime Multiplier = 100\n')
    env.pop('TETR_TEST_MAX_PIECES'); env.pop('TETR_TEST_REPLAY')
    run(command('--play'), base, env)
    assert replay.read_bytes() == contents
    history = folder / '中文房间历史' / 'room-codes.txt'
    env['TETR_ROOM_CODE_HISTORY'] = str(history)
    server = folder / ('tetr_server.exe' if windows else 'tetr_server')
    proc = subprocess.Popen([str(server)], cwd=folder, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        ready_until = time.monotonic() + 10
        while time.monotonic() < ready_until:
            if proc.poll() is not None:
                raise RuntimeError(proc.stderr.read().decode(errors='replace'))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.settimeout(.1); probe.sendto(b'!probe', ('127.0.0.1', 3000))
                try:
                    if probe.recv(100) == b'!probe-ok':
                        break
                except (TimeoutError, ConnectionResetError):
                    pass
            time.sleep(.01)
        else:
            raise RuntimeError('Packaged relay did not answer')
        assert history.is_file()
    finally:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait()
    print('Native smoke passed: Chinese/space paths, game, replay and Unicode server history.')


def check(archive):
    windows = archive.suffix == '.zip'
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    assert digest == Path(str(archive) + '.sha256').read_text().split()[0]
    with tempfile.TemporaryDirectory(prefix='termino-public-中文 with spaces-') as temporary:
        base = Path(temporary)
        if windows:
            with zipfile.ZipFile(archive) as package:
                assert package.testzip() is None
                package.extractall(base)
        else:
            with tarfile.open(archive) as package:
                package.extractall(base, filter='data')
        folder, = base.iterdir()
        names = {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()}
        assert not any(n.startswith(('.tetr/', 'replay/', 'keybinding')) for n in names), names
        required = {'BUILD.txt', 'README.txt', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/websocket/curl.txt'}
        required |= {'tetr.exe', 'tetr_server.exe', 'Play.bat', 'Server.bat'} if windows else {'tetr', 'tetr_server', 'Play.sh', 'Server.sh'}
        assert required <= names
        assert 'Reset player settings: ON' in (folder / 'BUILD.txt').read_text()
        print(f'{archive.name}: clean archive and SHA256 passed.')
        if windows == (os.name == 'nt'):
            smoke(folder, windows)


if __name__ == '__main__':
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else 'dist')
    archives = sorted([*directory.glob('*.tar.gz'), *directory.glob('*.zip')])
    if not archives:
        raise SystemExit('No release archives found')
    for archive in archives:
        check(archive.resolve())
