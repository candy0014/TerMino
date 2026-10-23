#!/usr/bin/env python3
"""Build/install clean client candidates locally. Never uploads or publishes."""
from pathlib import Path
import argparse
from datetime import datetime
import hashlib
import json
import os
import re
import platform
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
import uuid

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
        if name.as_posix() in {'.tetr/maps.txt', '.tetr/presets.txt'}:
            approved = ROOT / 'resources/defaults' / name.name
            if not approved.is_file() or p.read_bytes() != approved.read_bytes():
                raise ValueError(f'Library differs from the approved distribution default: {name}')
            continue
        if '.tetr' in name.parts or any(part in {'.git', '__pycache__', 'replay'} for part in name.parts[:-1]):
            raise ValueError(f'Personal/generated data in package: {name}')
        if name.name.startswith('keybinding') or p.suffix in {'.log', '.challenges', '.ttr', '.ttrm', '.rep', '.replay'}:
            raise ValueError(f'Personal/generated file in package: {name}')
    return files


def source_metadata():
    try:
        commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True, stderr=subprocess.DEVNULL).strip()
        dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
    except (OSError, subprocess.CalledProcessError):
        # A Windows cross-build may not have Git in its execution environment.
        # An explicit revision can identify it, but cannot establish cleanliness.
        commit, dirty = os.environ.get('TETR_DISTRIBUTION_COMMIT', ''), True
    if not re.fullmatch(r'[0-9a-f]{40,64}', commit):
        raise ValueError('Git is required, or supply TETR_DISTRIBUTION_COMMIT for a local cross-build')
    return commit, dirty


def refresh_source_metadata(stage):
    """Reject stale configure provenance when packaging a local build tree."""
    file = stage / 'BUILD.json'
    info = json.loads(file.read_text(encoding='utf-8'))
    commit, dirty = source_metadata()
    if info['source_commit'] != commit:
        raise ValueError('Build metadata belongs to another commit; reconfigure and rebuild before packaging')
    # A dirty configure cannot be relabelled clean without a fresh configure.
    info['source_dirty'] = info['source_dirty'] or dirty
    file.write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def required_files(info):
    windows = info['platform'] == 'Windows'
    suffix = '.exe' if windows else ''
    main = 'tetr_gl' if info['frontend'] == 'opengl' else 'tetr'
    required = {main + suffix, 'Play.bat' if windows else 'Play.sh', 'BUILD.json', 'README.txt', 'LICENSE',
                'THIRD_PARTY_NOTICES.md', 'licenses/websocket/curl.txt'}
    if info.get('default_libraries'):
        required |= {'.tetr/maps.txt', '.tetr/presets.txt'}
    if info['frontend'] in {'terminal', 'bundle'}:
        required.add('tetr_ft7' + suffix)
    if info['frontend'] in {'opengl', 'bundle'}:
        required |= {'resources/fonts/NotoSansCJKsc-Regular.otf', 'resources/fonts/LICENSE-Noto.txt',
                     'resources/licenses/SDL3.txt', 'resources/licenses/FreeType-FTL.txt', 'docs/OPENGL_PREVIEW.md'}
    if info['frontend'] == 'bundle':
        required |= {'tetr_gl' + suffix, 'tetr_server' + suffix,
                     'PlayGL.bat' if windows else 'PlayGL.sh', 'Server.bat' if windows else 'Server.sh'}
    return required


def package_stage(stage, output, version, allow_dirty=False):
    stage, output = Path(stage).resolve(), Path(output).resolve()
    info = json.loads((stage / 'BUILD.json').read_text(encoding='utf-8'))
    if info['frontend'] not in {'terminal', 'opengl', 'bundle'} or info['platform'] not in {'Linux', 'Windows'}:
        raise ValueError('Unsupported client/platform metadata')
    if info['source_dirty'] and not allow_dirty:
        raise ValueError('Source snapshot is dirty; commit it or explicitly use --allow-dirty for a local candidate')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,80}', version):
        raise ValueError('Version must be 1..81 filename-safe ASCII characters')
    files = audit_stage(stage)
    windows = info['platform'] == 'Windows'
    suffix = '.exe' if windows else ''
    required = required_files(info)
    names = {p.relative_to(stage).as_posix() for p in files}
    if not required <= names:
        raise ValueError(f'Missing package files: {sorted(required - names)}')
    for executable in (name for name in required if name in {n + suffix for n in ('tetr', 'tetr_gl', 'tetr_ft7', 'tetr_server')}):
        if (stage / executable).read_bytes()[:2 if windows else 4] != (b'MZ' if windows else b'\x7fELF'):
            raise ValueError(f'Executable format does not match the package platform: {executable}')
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
    parser.add_argument('--frontend', choices=['terminal', 'opengl', 'both', 'bundle'])
    parser.add_argument('--configure', action='store_true', help='configure the build directory with clean portable defaults')
    parser.add_argument('--generator', help='CMake generator when configuring')
    parser.add_argument('--cmake-arg', action='append', default=[], help='additional CMake configure argument (use --cmake-arg=-DNAME=VALUE)')
    parser.add_argument('--configuration', default='Release')
    parser.add_argument('--parallel', type=int, default=2)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'dist')
    parser.add_argument('--version', help='filename-safe local candidate identifier')
    parser.add_argument('--allow-dirty', action='store_true')
    parser.add_argument('--no-build', action='store_true', help='install existing binaries without changing build flags')
    args = parser.parse_args()
    if args.frontend is None:
        args.frontend = args.preset.split('-')[0] if args.preset else 'both'
        if args.frontend not in {'terminal', 'opengl', 'both', 'bundle'}:
            parser.error('Choose --frontend explicitly for this preset')
    if not 1 <= args.parallel <= 256:
        parser.error('--parallel requires 1..256')
    if args.configure and not args.build_dir:
        parser.error('--configure requires --build-dir')
    if args.preset or args.configure:
        commit, dirty = source_metadata()
        portable = ['-DTETR_PORTABLE_BUILD=ON', '-DTETR_PORTABLE_RESET_SETTINGS=ON', '-DTETR_BUNDLED_CURL=ON']
        portable += [f'-DTETR_DISTRIBUTION_COMMIT={commit}', f'-DTETR_DISTRIBUTION_DIRTY={str(dirty).lower()}']
        if args.frontend != 'terminal':
            portable += ['-DTETR_PORTABLE_FULL_STATIC=OFF', '-DTETR_GRAPHICS_BUNDLED_DEPS=ON']
        else:
            portable += ['-DTETR_PORTABLE_FULL_STATIC=ON']
        if args.frontend == 'bundle':
            portable += ['-DTETR_BUILD_TERMINAL=ON', '-DTETR_BUILD_OPENGL=ON', '-DTETR_BUILD_SERVER=ON']
        if args.preset:
            command = ['cmake', '--preset', args.preset]
            args.build_dir = ROOT / ('build-' + args.preset)
        else:
            command = ['cmake', '-S', str(ROOT), '-B', str(args.build_dir.resolve()),
                       '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DCMAKE_SUPPRESS_REGENERATION=ON']
            if args.generator:
                command += ['-G', args.generator]
        if os.environ.get('WINEPREFIX'):
            portable += ['-DCMAKE_DISABLE_FIND_PACKAGE_Perl=ON']
        subprocess.run([*command, *portable, *args.cmake_arg], cwd=ROOT, check=True)
    if args.stage_dir:
        info = json.loads((args.stage_dir / 'BUILD.json').read_text(encoding='utf-8'))
        version = args.version or 'local-' + info['source_commit'][:12]
        print(package_stage(args.stage_dir, args.output_dir, version, args.allow_dirty))
        return
    directory = args.build_dir.resolve()
    fronts = ['terminal', 'opengl'] if args.frontend == 'both' else [args.frontend]
    if not args.no_build:
        targets = (['tetr', 'tetr_ft7'] if 'terminal' in fronts or 'bundle' in fronts else []) + (['tetr_gl'] if 'opengl' in fronts or 'bundle' in fronts else [])
        cache = (directory / 'CMakeCache.txt').read_text(encoding='utf-8')
        if 'TETR_BUILD_SERVER:BOOL=ON' in cache:
            targets.append('tetr_server')
        subprocess.run(['cmake', '--build', str(directory), '--config', args.configuration, '--target', *targets,
                        '--parallel', str(args.parallel)], check=True)
    for front in fronts:
        with tempfile.TemporaryDirectory(prefix='termino-stage-') as temporary:
            component = {'terminal': 'ClientTerminal', 'opengl': 'ClientOpenGL', 'bundle': 'ClientBundle'}[front]
            subprocess.run(['cmake', '--install', str(directory), '--config', args.configuration, '--component', component,
                            '--prefix', temporary], check=True)
            refresh_source_metadata(Path(temporary))
            info = json.loads((Path(temporary) / 'BUILD.json').read_text(encoding='utf-8'))
            version = args.version or ('local-' + datetime.now().strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:8]
                                       if front == 'bundle' else 'local-' + info['source_commit'][:12])
            print(package_stage(temporary, args.output_dir, version, args.allow_dirty))


if __name__ == '__main__':
    main()
