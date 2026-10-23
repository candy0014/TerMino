#!/usr/bin/env python3
"""Canonicalize only approved default-library line endings in verified CI bundles."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from check_frontend_distribution import extract, verify
from package_frontends import ROOT, package_stage, sha256


def normalize(source, output, commit):
    approved_defaults = {}
    for name in ('maps.txt', 'presets.txt'):
        approved = subprocess.check_output(
            ['git', 'show', f'{commit}:resources/defaults/{name}'], cwd=ROOT)
        if approved != (ROOT / 'resources/defaults' / name).read_bytes():
            raise ValueError(f'Checkout default library differs from validated source: {name}')
        approved_defaults[name] = approved
    output.mkdir(parents=True, exist_ok=False)
    archives = sorted([*source.glob('*.tar.gz'), *source.glob('*.zip')])
    if len(archives) != 2:
        raise ValueError('Expected the two validated desktop bundles')
    report = {'source_commit': commit, 'packages': []}
    processed = set()
    for archive in archives:
        checksum = Path(str(archive) + '.sha256')
        if checksum.read_text(encoding='ascii').split() != [sha256(archive), archive.name]:
            raise ValueError(f'Input archive checksum mismatch: {archive.name}')
        with tempfile.TemporaryDirectory(prefix='termino-canonical-defaults-') as temporary:
            folder = extract(archive, Path(temporary))
            info = json.loads((folder / 'PACKAGE.json').read_text(encoding='utf-8'))
            build = json.loads((folder / 'BUILD.json').read_text(encoding='utf-8'))
            for record in (info, build):
                if record['source_commit'] != commit or record['source_dirty'] or record['frontend'] != 'bundle':
                    raise ValueError('Bundle does not match the validated clean source')
            actual = {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file()} - {'PACKAGE.json'}
            if actual != set(info['files']):
                raise ValueError('Input manifest file set differs from archive')
            for name, expected in info['files'].items():
                file = folder / name
                if file.stat().st_size != expected['bytes'] or sha256(file) != expected['sha256']:
                    raise ValueError(f'Input file checksum mismatch: {name}')
            changed = []
            for name in ('maps.txt', 'presets.txt'):
                file = folder / '.tetr' / name
                approved = approved_defaults[name]
                data = file.read_bytes()
                if data.replace(b'\r\n', b'\n') != approved:
                    raise ValueError(f'Default library has changes other than CRLF: {name}')
                if data != approved:
                    file.write_bytes(approved)
                    changed.append(file.relative_to(folder).as_posix())
            # Prove that executables and every other original file are intact.
            for name, expected in info['files'].items():
                if name not in changed and sha256(folder / name) != expected['sha256']:
                    raise ValueError(f'Unexpected bundle modification: {name}')
            if changed:
                result = package_stage(folder, output, info['version'])
                verify(folder)
            else:
                verify(folder)
                result = output / archive.name
                shutil.copy2(archive, result)
                shutil.copy2(checksum, output / checksum.name)
            report['packages'].append({'archive': result.name, 'original_sha256': sha256(archive),
                'sha256': sha256(result), 'normalized_files': changed, 'other_file_bytes_unchanged': True})
        processed.update((archive.name, checksum.name))
    for file in source.iterdir():
        if file.is_file() and file.name not in processed:
            if not (file.suffix == '.apk' or file.name.endswith(('.apk.sha256', '.audit.json'))):
                raise ValueError(f'Unexpected release asset: {file.name}')
            shutil.copy2(file, output / file.name)
    (output / 'CANONICAL-DEFAULTS.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    args = parser.parse_args()
    normalize(args.input.resolve(), args.output.resolve(), args.source_commit)
