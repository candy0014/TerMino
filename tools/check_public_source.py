#!/usr/bin/env python3
"""Reject personal data and generated binaries from a public source tree."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_SUFFIXES = {'.challenges', '.ttr', '.ttrm', '.rep', '.replay', '.exe',
                      '.dll', '.pdb', '.obj', '.o', '.a', '.lib', '.pyc', '.log'}
SECRET = re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|-----BEGIN (?:OPENSSH|RSA|EC) PRIVATE KEY-----')


def check(root=ROOT):
    if (root / '.git').exists():
        output = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z'])
        files = [Path(p.decode('utf-8')) for p in output.split(b'\0') if p]
    else:
        files = [p.relative_to(root) for p in root.rglob('*') if p.is_file()]
    errors = []
    for name in files:
        parts = name.parts
        if any(p in {'.tetr', '.git', '.vs', '.venv', '__pycache__', 'node_modules'}
               for p in parts[:-1]) or parts[0] in {'dist', 'replay'} or parts[0].startswith('build'):
            errors.append(f'{name}: local/generated directory')
        if name.suffix in FORBIDDEN_SUFFIXES or (len(parts) == 1 and name.name.startswith('keybinding')) or name.name.startswith('HANDOFF_') or name.name.endswith('.user'):
            errors.append(f'{name}: personal/generated file')
        p = root / name
        if not p.is_file():
            errors.append(f'{name}: missing file or unresolved submodule')
        elif SECRET.search(p.read_bytes()):
            errors.append(f'{name}: credential-like content')
    if errors:
        raise RuntimeError('\n'.join(errors))
    print(f'Public source audit passed: {len(files)} files, no personal data or build products.')


if __name__ == '__main__':
    check(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT)
