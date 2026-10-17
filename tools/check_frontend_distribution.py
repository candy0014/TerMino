#!/usr/bin/env python3
"""Audit local frontend candidates; all graphical execution is hidden."""
from pathlib import Path, PurePosixPath
import argparse
import json
import os
import queue
import socket
import subprocess
import tarfile
import tempfile
import threading
import time
import zipfile
from package_frontends import audit_stage, sha256


def extract(archive, destination):
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as package:
            names = package.namelist()
            for name in names:
                path = PurePosixPath(name.replace('\\', '/'))
                if path.is_absolute() or '..' in path.parts or ':' in path.parts[0]:
                    raise ValueError(f'Unsafe archive path: {name}')
            package.extractall(destination)
    else:
        with tarfile.open(archive) as package:
            package.extractall(destination, filter='data')
    roots = list(destination.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        raise ValueError('Expected one package root')
    return roots[0]


def verify(folder):
    info = json.loads((folder / 'PACKAGE.json').read_text(encoding='utf-8'))
    files = audit_stage(folder)
    actual = {p.relative_to(folder).as_posix() for p in files} - {'PACKAGE.json'}
    if actual != set(info['files']):
        raise ValueError('Manifest file set differs from archive')
    for name, expected in info['files'].items():
        file = folder / name
        if file.stat().st_size != expected['bytes'] or sha256(file) != expected['sha256']:
            raise ValueError(f'File checksum mismatch: {name}')
    build = json.loads((folder / 'BUILD.json').read_text(encoding='utf-8'))
    for key in ('source_commit', 'source_dirty', 'frontend', 'platform', 'architecture'):
        if info[key] != build[key]:
            raise ValueError(f'Inconsistent build metadata: {key}')
    return info


class Runner:
    def __init__(self, folder, windows, wine=False):
        self.folder, self.windows, self.wine = folder, windows, wine

    def path(self, file):
        file = Path(file).resolve()
        return 'Z:' + file.as_posix() if self.wine else str(file)

    def command(self, name, *args):
        exe = self.folder / (name + ('.exe' if self.windows else ''))
        return (['wine', str(exe)] if self.wine else [str(exe)]) + [self.path(a) if isinstance(a, Path) else str(a) for a in args]

    def run(self, name, *args, env=None, timeout=90):
        result = subprocess.run(self.command(name, *args), cwd=self.folder, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, encoding='utf-8', errors='replace', timeout=timeout)
        if result.returncode:
            raise RuntimeError(result.stdout)
        return result.stdout

    def launcher_help(self):
        launcher = self.folder / ('Play.bat' if self.windows else 'Play.sh')
        if self.windows:
            command = (['wine', 'cmd'] if self.wine else ['cmd']) + ['/d', '/c', self.path(launcher).replace('/', '\\'), '--help']
        else:
            command = [str(launcher), '--help']
        result = subprocess.run(command, cwd=self.folder.parent, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                encoding='utf-8', errors='replace', timeout=60)
        if result.returncode or 'TerMino' not in result.stdout:
            raise RuntimeError(f'Packaged launcher failed from another directory: {result.stdout}')


def smoke(folder, info, wine=False, skip_graphics=False):
    windows = info['platform'] == 'Windows'
    runner = Runner(folder, windows, wine)
    main = 'tetr_gl' if info['frontend'] == 'opengl' else 'tetr'
    assert 'TerMino' in runner.run(main, '--help')
    runner.launcher_help()
    if main == 'tetr_gl':
        if skip_graphics:
            return {'help': 'passed', 'launcher': 'passed', 'hidden_opengl': 'not run; graphics environment unavailable'}
        runtime = folder.parent / '隔离运行 with spaces'
        result = runner.run(main, '--data-dir', runtime, '--smoke-test', '--hidden', '--window-size', '1024x768',
                            '--frame-limit', '120', '--screenshot', folder.parent / 'hidden.bmp')
        assert 'hidden=1' in result and 'OPENGL_SMOKE_OK' in result
        saved = (runtime / '.tetr/graphics.ini').read_text(encoding='utf-8')
        assert 'width=1024' in saved and 'height=768' in saved and 'frame_limit=120' in saved
        assert (folder.parent / 'hidden.bmp').stat().st_size > 1000
        # A bounded second run reads the saved configuration, with no mouse
        # or terminal input and no visible test window.
        runner.run(main, '--data-dir', runtime, '--hidden', '--frames', '4')
        return {'help': 'passed', 'launcher': 'passed', 'hidden_opengl': 'passed', 'window_settings': 'passed'}
    config = folder / '.tetr' / ('player1_windows.ini' if windows else 'player1_linux.ini')
    config.parent.mkdir(exist_ok=True)
    config.write_text('[MODEL]\nModel = 3\n[GAME]\nSimulation PPS = 2.5\n[AUTOPLAY]\nAutoplay = 1\nPPS of Autoplay = 0\n', encoding='utf-8')
    env = dict(os.environ, TETR_TEST_BATCH='1', TETR_EXIT_AFTER_GAME='1', TETR_TEST_MAX_PIECES='8',
               TETR_TEST_SEED='20261017', TETR_TEST_CLOCK_SCALE='100', TETR_TEST_REPLAY='candidate-smoke.txt')
    # Package launchers also clear these imports. Direct executable checks
    # must not inherit developer overrides from the invoking environment.
    for key in ('TETR_USER_CONFIG', 'TETR_USER_CONFIG_PLAYER2', 'TETR_MAP_FILE', 'TETR_PRESET_FILE'):
        env.pop(key, None)
    runner.run(main, '--play', env=env)
    replay = folder / '.tetr/replay/candidate-smoke.txt'
    contents = replay.read_bytes()
    assert b'{end_all,' in contents and b'{end_place_data,' in contents
    config.write_text('[MODEL]\nModel = 5\nReplay = .tetr/replay/candidate-smoke.txt\nTime Multiplier = 100\n', encoding='utf-8')
    env.pop('TETR_TEST_MAX_PIECES'); env.pop('TETR_TEST_REPLAY')
    runner.run(main, '--play', env=env);assert replay.read_bytes() == contents
    runner.run('tetr_ft7', '--help', env=env)
    server = folder / ('tetr_server.exe' if windows else 'tetr_server')
    if server.exists():
        history = folder.parent / '中文房间历史' / 'room-codes.txt'
        env['TETR_ROOM_CODE_HISTORY'] = runner.path(history)
        process = subprocess.Popen(runner.command('tetr_server'), cwd=folder, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            until = time.monotonic() + 10
            while time.monotonic() < until:
                if process.poll() is not None:
                    raise RuntimeError(process.stderr.read().decode(errors='replace'))
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                    probe.settimeout(.1);probe.sendto(b'!probe', ('127.0.0.1', 3000))
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
            process.terminate()
            try:process.wait(5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    return {'help': 'passed', 'launcher': 'passed', 'game_and_replay': 'passed', 'ft7': 'passed', 'server': 'passed' if server.exists() else 'not included'}


def cross_frontends(archives, wine=False):
    results = []
    with tempfile.TemporaryDirectory(prefix='termino-cross-中文 with spaces-') as temporary:
        base = Path(temporary);folders = {}
        for index, archive in enumerate(archives):
            destination = base / str(index);destination.mkdir()
            folder = extract(archive, destination)
            info = verify(folder);folders[(info['platform'], info['frontend'])] = folder
        for operating_system in ('Linux', 'Windows'):
            windows = operating_system == 'Windows'
            native = windows == (os.name == 'nt')
            if (not native and not (wine and windows)) or any((operating_system, f) not in folders for f in ('terminal', 'opengl')):
                continue
            terminal = Runner(folders[(operating_system, 'terminal')], windows, wine and not native)
            graphical = Runner(folders[(operating_system, 'opengl')], windows, wine and not native)
            smoke(terminal.folder, {'platform': operating_system, 'frontend': 'terminal'}, terminal.wine)
            replay = terminal.folder / '.tetr/replay/candidate-smoke.txt'
            profile = base / 'replay-profile.ini';profile.write_text('[MODEL]\nTime Multiplier = 100\n', encoding='utf-8')
            runtime = base / (operating_system + '-跨端运行')
            graphical.run('tetr_gl', '--data-dir', runtime, '--profile', profile, '--replay', replay, '--hidden', '--frames', '180', '--no-vsync')
            # Prove actual clients share an OS lock, and that terminating the
            # owner releases it without deleting either the lock or data.
            owner = subprocess.Popen(graphical.command('tetr_gl', '--data-dir', runtime, '--hidden', '--frames', '1800', '--frame-limit', '60'),
                                     cwd=graphical.folder, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            try:
                ready = queue.Queue()
                def read_ready():
                    lines = b''
                    while True:
                        line = owner.stderr.readline();lines += line
                        if not line or b'hidden=1' in line:
                            ready.put(lines);return
                threading.Thread(target=read_ready, daemon=True).start()
                diagnostics = ready.get(timeout=20)
                if b'hidden=1' not in diagnostics:raise RuntimeError(diagnostics.decode(errors='replace'))
                blocked = subprocess.run(graphical.command('tetr_gl', '--data-dir', runtime, '--hidden', '--frames', '1'),
                                         cwd=graphical.folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
                assert blocked.returncode and b'in use by another' in blocked.stdout
                conflict = subprocess.run(terminal.command('tetr', '--play'), cwd=runtime,
                                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
                assert conflict.returncode == 3 and b'in use by another' in conflict.stdout
            finally:
                owner.terminate()
                try:owner.wait(5)
                except subprocess.TimeoutExpired:owner.kill();owner.wait()
            assert (runtime / '.tetr/client.lock').is_file()
            graphical.run('tetr_gl', '--data-dir', runtime, '--hidden', '--frames', '2')
            results.append({'platform': operating_system, 'environment': 'native' if native else 'Wine',
                            'terminal_to_opengl_replay': 'passed', 'cross_client_lock_and_crash_release': 'passed'})
    return results


def check(archive, wine=False, structure_only=False, skip_graphics=False):
    if sha256(archive) != Path(str(archive) + '.sha256').read_text(encoding='ascii').split()[0]:
        raise ValueError(f'Archive checksum mismatch: {archive.name}')
    with tempfile.TemporaryDirectory(prefix='termino-check-中文 with spaces-') as temporary:
        folder = extract(archive, Path(temporary));info = verify(folder)
        result = {key: info[key] for key in ('platform', 'architecture', 'frontend', 'source_commit', 'source_dirty', 'version')}
        result.update(archive=archive.name, sha256=sha256(archive), files='passed', native_acceptance='pending physical IME/DPI/device/GPU checks')
        native = (info['platform'] == 'Windows') == (os.name == 'nt')
        if not structure_only and (native or (wine and info['platform'] == 'Windows')):
            result['execution'] = smoke(folder, info, wine and not native, skip_graphics)
            result['environment'] = 'Wine' if wine and not native else 'native'
        else:
            result['execution'] = 'not run on this host'
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--wine', action='store_true', help='execute Windows candidates through Wine, never claim native Windows acceptance')
    parser.add_argument('--structure-only', action='store_true')
    parser.add_argument('--skip-graphics', action='store_true', help='record graphics execution as not run, without inventing a pass')
    parser.add_argument('--matrix', action='store_true', help='require exactly the four OS/frontend combinations from one clean commit')
    parser.add_argument('--cross-frontends', action='store_true', help='check cross-client replay, directory contention and crash release')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    archives = sorted([*args.directory.glob('*.tar.gz'), *args.directory.glob('*.zip')])
    if not archives:
        parser.error('No candidate archives found')
    results = []
    for archive in archives:
        result = check(archive.resolve(), args.wine, args.structure_only, args.skip_graphics)
        results.append(result);print(json.dumps(result, ensure_ascii=False))
    if args.matrix:
        combos = [(r['platform'], r['frontend']) for r in results]
        if set(combos) != {(p, f) for p in ('Linux', 'Windows') for f in ('terminal', 'opengl')} or len(combos) != 4:
            raise ValueError('Expected exactly Linux/Windows × terminal/opengl')
        if len({r['source_commit'] for r in results}) != 1 or any(r['source_dirty'] for r in results):
            raise ValueError('Matrix packages must use one clean source commit')
    crossed = cross_frontends(archives, args.wine) if args.cross_frontends and not args.structure_only and not args.skip_graphics else []
    for result in crossed:print(json.dumps(result, ensure_ascii=False))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({'packages': results, 'cross_frontends': crossed, 'github_published': False}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
