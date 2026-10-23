#!/usr/bin/env python3
"""Install reversible user-service recovery without restarting healthy servers."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

UNITS = ('termino-cloudflare', 'termino-websocket', 'termino-relay')
TIMER = 'termino-tunnel-watchdog.timer'


def control(*args, check=True):
    return subprocess.run(['systemctl', '--user', *args], check=check,
                          capture_output=True, text=True, timeout=30)


def copy_atomic(source, target, mode):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + '.install-' + str(os.getpid()))
    try:
        shutil.copyfile(source, temporary)
        temporary.chmod(mode)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def rollback(backup):
    manifest = json.loads((backup / 'manifest.json').read_text())
    home = Path(manifest['home'])
    control('stop', TIMER, check=False)
    for entry in reversed(manifest['files']):
        target = home / entry['path']
        if entry['existed']:
            copy_atomic(backup / 'files' / entry['path'], target, entry['mode'])
        else:
            target.unlink(missing_ok=True)
    control('daemon-reload')
    control('enable' if manifest['enabled'] else 'disable', TIMER, check=False)
    if manifest['active']:
        control('start', TIMER)


def install(home, source):
    units = home / '.config/systemd/user'
    if not (home / '.config/termino-cloudflare/token').is_file():
        raise RuntimeError('existing tunnel token is missing (its contents are never read)')
    for unit in UNITS:
        if control('show', unit + '.service', '--property=LoadState', '--value').stdout.strip() != 'loaded':
            raise RuntimeError('existing service is missing: ' + unit)
    files = [(source / 'tunnel_watchdog.py', home / 'termino-cloudflare/tunnel_watchdog.py', 0o700)]
    for name in ('termino-tunnel-watchdog.service', TIMER):
        files.append((source / name, units / name, 0o644))
    for unit in UNITS:
        files.append((source / 'termino-network-recovery.conf', units / (unit + '.service.d') / 'recovery.conf', 0o644))
    for src, _, _ in files:
        if not src.is_file():
            raise RuntimeError('missing installer file: ' + str(src))
    backup = home / 'termino-cloudflare/watchdog-backups' / str(time.time_ns())
    backup.mkdir(parents=True, mode=0o700)
    manifest = {'home': str(home), 'enabled': control('is-enabled', TIMER, check=False).returncode == 0,
                'active': control('is-active', TIMER, check=False).returncode == 0, 'files': []}
    for _, target, _ in files:
        relative = str(target.relative_to(home))
        entry = {'path': relative, 'existed': target.exists()}
        if target.exists():
            entry['mode'] = target.stat().st_mode & 0o777
            saved = backup / 'files' / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved)
        manifest['files'].append(entry)
    (backup / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    try:
        control('stop', TIMER, check=False)
        for src, target, mode in files:
            copy_atomic(src, target, mode)
        control('daemon-reload')
        control('enable', '--now', TIMER)
        control('start', 'termino-tunnel-watchdog.service')
        control('is-active', TIMER)
    except BaseException:
        rollback(backup)
        raise
    print(json.dumps({'installed': True, 'backup': str(backup), 'server_restart': False}))
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rollback', type=Path)
    args = parser.parse_args()
    os.umask(0o077)
    if args.rollback:
        rollback(args.rollback.resolve())
    else:
        install(Path.home(), Path(__file__).resolve().parent)


if __name__ == '__main__':
    main()
