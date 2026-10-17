#!/usr/bin/env python3
"""Build/install clean client candidates locally. Never uploads or publishes."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import platform
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def runtime_dependencies(folder, windows):
    result = {}
    for name in ('tetr', 'tetr_ft7', 'tetr_server', 'tetr_gl'):
        file = folder / (name + ('.exe' if windows else ''))
        if not file.exists():
            continue
        tool = shutil.which('objdump' if windows else 'ldd')
        if not tool:
            result[file.name] = {'inspection': 'not available'}
            continue
        command = [tool, '-p', str(file)] if windows else [tool, str(file)]
        output = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace', timeout=30).stdout
        libraries = re.findall(r'DLL Name:\s*(\S+)', output) if windows else re.findall(r'^\s*(\S+\.so(?:\.\d+)*)\s*=>', output, re.M)
        result[file.name] = {'inspection': Path(tool).name, 'libraries': sorted(set(libraries))}
    return result


def audit_stage(folder):
    files = sorted(p for p in folder.rglob('*') if p.is_file())
    for p in folder.rglob('*'):
        if p.is_symlink():
            raise ValueError(f'Package symlink is not allowed: {p}')
    for p in files:
        name = p.relative_to(folder)
        if '.tetr' in name.parts or any(part in {'.git', '__pycache__', 'replay'} for part in name.parts[:-1]):
            raise ValueError(f'Personal/generated data in package: {name}')
        if name.name.startswith('keybinding') or p.suffix in {'.log', '.challenges', '.ttr', '.ttrm', '.rep', '.replay'}:
            raise ValueError(f'Personal/generated file in package: {name}')
    return files


def refresh_source_metadata(stage):
    """Reject stale configure provenance when packaging a local build tree."""
    file = stage / 'BUILD.json'
    info = json.loads(file.read_text(encoding='utf-8'))
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if info['source_commit'] != commit:
        raise ValueError('Build metadata belongs to another commit; reconfigure and rebuild before packaging')
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    # A dirty configure cannot be relabelled clean without a fresh configure.
    info['source_dirty'] = info['source_dirty'] or dirty
    file.write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def package_stage(stage, output, version, allow_dirty=False):
    stage, output = Path(stage).resolve(), Path(output).resolve()
    info = json.loads((stage / 'BUILD.json').read_text(encoding='utf-8'))
    if info['frontend'] not in {'terminal', 'opengl'} or info['platform'] not in {'Linux', 'Windows'}:
        raise ValueError('Unsupported client/platform metadata')
    if info['source_dirty'] and not allow_dirty:
        raise ValueError('Source snapshot is dirty; commit it or explicitly use --allow-dirty for a local candidate')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,80}', version):
        raise ValueError('Version must be 1..81 filename-safe ASCII characters')
    files = audit_stage(stage)
    windows = info['platform'] == 'Windows'
    suffix = '.exe' if windows else ''
    main = 'tetr_gl' if info['frontend'] == 'opengl' else 'tetr'
    required = {main + suffix, 'Play.bat' if windows else 'Play.sh', 'BUILD.json', 'README.txt', 'LICENSE',
                'THIRD_PARTY_NOTICES.md', 'licenses/websocket/curl.txt'}
    if info['frontend'] == 'terminal':
        required.add('tetr_ft7' + suffix)
    else:
        required |= {'resources/fonts/NotoSansCJKsc-Regular.otf', 'resources/fonts/LICENSE-Noto.txt',
                     'resources/licenses/SDL3.txt', 'resources/licenses/FreeType-FTL.txt', 'docs/OPENGL_PREVIEW.md'}
    names = {p.relative_to(stage).as_posix() for p in files}
    if not required <= names:
        raise ValueError(f'Missing package files: {sorted(required - names)}')
    if (stage / (main + suffix)).read_bytes()[:2 if windows else 4] != (b'MZ' if windows else b'\x7fELF'):
        raise ValueError('Executable format does not match the package platform')
    name = f'TerMino-{info["platform"]}-{info["architecture"]}-{info["frontend"]}-{version}'
    output.mkdir(parents=True, exist_ok=True)
    archive = output / (name + ('.zip' if windows else '.tar.gz'))
    if archive.exists() or Path(str(archive) + '.sha256').exists():
        raise FileExistsError(f'Refusing to overwrite an existing candidate: {archive}')
    manifest = dict(info, schema=1, version=version, status='local-candidate', native_acceptance='not implied by packaging',
                    runtime_dependencies=runtime_dependencies(stage, windows), host_libc=list(platform.libc_ver()) if not windows else None,
                    files={p.relative_to(stage).as_posix(): {'bytes': p.stat().st_size, 'sha256': sha256(p)} for p in files if p.name != 'PACKAGE.json'})
    (stage / 'PACKAGE.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='termino-archive-', dir=output) as temporary:
        pending = Path(temporary) / archive.name
        if windows:
            with zipfile.ZipFile(pending, 'w', compression=zipfile.ZIP_DEFLATED) as package:
                for p in sorted(stage.rglob('*')):
                    package.write(p, str(Path(name) / p.relative_to(stage)))
        else:
            with tarfile.open(pending, 'w:gz') as package:
                package.add(stage, arcname=name)
        pending.rename(archive)
    Path(str(archive) + '.sha256').write_text(f'{sha256(archive)}  {archive.name}\n', encoding='ascii')
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--preset', help='configure a Linux/Windows frontend preset for packaging')
    source.add_argument('--build-dir', type=Path, help='use an already configured build directory')
    source.add_argument('--stage-dir', type=Path, help='archive an already installed clean client component (also for Wine builds)')
    parser.add_argument('--frontend', choices=['terminal', 'opengl', 'both'])
    parser.add_argument('--configuration', default='Release')
    parser.add_argument('--parallel', type=int, default=2)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'dist')
    parser.add_argument('--version', help='filename-safe local candidate identifier')
    parser.add_argument('--allow-dirty', action='store_true')
    parser.add_argument('--no-build', action='store_true', help='install existing binaries without changing build flags')
    args = parser.parse_args()
    if args.frontend is None:
        args.frontend = args.preset.split('-')[0] if args.preset else 'both'
        if args.frontend not in {'terminal', 'opengl', 'both'}:
            parser.error('Choose --frontend explicitly for this preset')
    if not 1 <= args.parallel <= 256:
        parser.error('--parallel requires 1..256')
    if args.preset:
        portable = ['-DTETR_PORTABLE_BUILD=ON', '-DTETR_PORTABLE_RESET_SETTINGS=ON', '-DTETR_BUNDLED_CURL=ON']
        if args.frontend != 'terminal':
            portable += ['-DTETR_PORTABLE_FULL_STATIC=OFF', '-DTETR_GRAPHICS_BUNDLED_DEPS=ON']
        else:
            portable += ['-DTETR_PORTABLE_FULL_STATIC=ON']
        subprocess.run(['cmake', '--preset', args.preset, *portable], cwd=ROOT, check=True)
        args.build_dir = ROOT / ('build-' + args.preset)
    if args.stage_dir:
        info = json.loads((args.stage_dir / 'BUILD.json').read_text(encoding='utf-8'))
        version = args.version or 'local-' + info['source_commit'][:12]
        print(package_stage(args.stage_dir, args.output_dir, version, args.allow_dirty))
        return
    directory = args.build_dir.resolve()
    fronts = ['terminal', 'opengl'] if args.frontend == 'both' else [args.frontend]
    if not args.no_build:
        targets = (['tetr', 'tetr_ft7'] if 'terminal' in fronts else []) + (['tetr_gl'] if 'opengl' in fronts else [])
        cache = (directory / 'CMakeCache.txt').read_text(encoding='utf-8')
        if 'TETR_BUILD_SERVER:BOOL=ON' in cache:
            targets.append('tetr_server')
        subprocess.run(['cmake', '--build', str(directory), '--config', args.configuration, '--target', *targets,
                        '--parallel', str(args.parallel)], check=True)
    for front in fronts:
        with tempfile.TemporaryDirectory(prefix='termino-stage-') as temporary:
            component = 'ClientTerminal' if front == 'terminal' else 'ClientOpenGL'
            subprocess.run(['cmake', '--install', str(directory), '--config', args.configuration, '--component', component,
                            '--prefix', temporary], check=True)
            refresh_source_metadata(Path(temporary))
            info = json.loads((Path(temporary) / 'BUILD.json').read_text(encoding='utf-8'))
            version = args.version or 'local-' + info['source_commit'][:12]
            print(package_stage(temporary, args.output_dir, version, args.allow_dirty))


if __name__ == '__main__':
    main()
