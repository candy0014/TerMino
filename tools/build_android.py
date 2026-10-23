#!/usr/bin/env python3
"""Build and audit the landscape Android preview APK; never opens desktop .tetr data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

import check_android_environment as environment

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / "build/android-deps"
SOURCES = (
    ("SDL3-3.4.18", "SDL3-3.4.18.tar.gz", "https://www.libsdl.org/release/SDL3-3.4.18.tar.gz",
     "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3"),
    ("freetype-2.14.1", "freetype-2.14.1.tar.xz", "https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.xz",
     "32427e8c471ac095853212a37aef816c60b42052d4d9e48230bab3bdf2936ccc"),
)


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def prepare():
    DEPS.mkdir(parents=True, exist_ok=True)
    cache = DEPS / "archives"
    cache.mkdir(exist_ok=True)
    for name, filename, url, sha in SOURCES:
        archive = cache / filename
        previous = Path.home() / ".cache/termino/android-setup" / filename
        if not archive.exists() and previous.is_file() and digest(previous) == sha:
            shutil.copyfile(previous, archive)
        if not archive.exists():
            print(f"Downloading {filename}", flush=True)
            partial = archive.with_suffix(".download")
            try:
                with urllib.request.urlopen(url, timeout=90) as src, partial.open("wb") as dst:
                    shutil.copyfileobj(src, dst)
                if digest(partial) != sha:
                    raise RuntimeError(f"Checksum mismatch: {filename}")
                partial.replace(archive)
            finally:
                partial.unlink(missing_ok=True)
        if digest(archive) != sha:
            raise RuntimeError(f"Checksum mismatch: {archive}; remove this cached archive and retry")
        target = DEPS / name
        marker = DEPS / (name + ".sha256")
        if target.is_dir() and marker.is_file() and marker.read_text().strip() == sha:
            continue
        # Extract a verified upstream archive to a fresh staging directory.
        with tempfile.TemporaryDirectory(dir=DEPS) as staging:
            with tarfile.open(archive) as source:
                source.extractall(staging, filter="data")
            unpacked = Path(staging) / name
            if not (unpacked / "CMakeLists.txt").is_file():
                raise RuntimeError(f"Unexpected source archive: {filename}")
            if target.exists():
                shutil.rmtree(target)
            shutil.move(str(unpacked), target)
        marker.write_text(sha + "\n")


def run(command, env):
    print("Running:", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=ROOT, env=env, check=True)


def elf_alignment(data):
    if data[:6] != b"\x7fELF\x02\x01":
        raise RuntimeError("Expected a little-endian 64-bit ELF library")
    offset = struct.unpack_from("<Q", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 54)
    loads, relro = [], []
    for i in range(count):
        kind, flags, off, va, pa, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, offset + i * size)
        if kind == 1:
            if align < 16384 or (va - off) % 16384:
                raise RuntimeError("ELF LOAD segment is not 16 KB aligned")
            loads.append((va,memsz,align))
        if kind == 0x6474e552:
            relro.append((va,memsz))
    if not loads:
        raise RuntimeError("ELF has no LOAD segments")
    for va, memsz in relro:
        # NDK r28 also permits a RELRO that occupies its own LOAD segment;
        # its following writable LOAD starts on a separate page.
        if (va + memsz) % 16384 and not any(start==va and length==memsz for start,length,_ in loads):
            raise RuntimeError("ELF GNU_RELRO end is not 16 KB aligned or isolated")
    return min(align for _,_,align in loads)


def audit(apk, sdk, env):
    native = {}
    libraries = {}
    with zipfile.ZipFile(apk) as package:
        names = package.namelist()
        for abi in ("arm64-v8a", "x86_64"):
            for library in ("libmain.so", "libSDL3.so", "libc++_shared.so"):
                path = f"lib/{abi}/{library}"
                native[path] = elf_alignment(package.read(path))
        for path in names:
            if ".tetr" in Path(path).parts or path.endswith((".jks", ".keystore", ".ttr", ".rep")):
                raise RuntimeError(f"Unexpected personal data or signing material in APK: {path}")
        for path in ("assets/fonts/NotoSansCJKsc-Regular.otf", "assets/certificates/cacert.pem",
                     "assets/licenses/fast_float.txt", "assets/licenses/MbedTLS.txt"):
            if path not in names:
                raise RuntimeError(f"Missing Android asset: {path}")
        for name in ("maps.txt", "presets.txt"):
            path = "assets/defaults/" + name
            data = package.read(path)
            if data != (ROOT / "resources/defaults" / name).read_bytes():
                raise RuntimeError(f"Android default library differs from the approved source: {name}")
            count = sum(line.startswith(b"[map ") for line in data.splitlines()) if name == "maps.txt" else int(data.splitlines()[0].split()[3])
            libraries[name] = {"sha256": hashlib.sha256(data).hexdigest(), "records": count}
    buildtools = sdk / "build-tools/35.0.0"
    suffix = ".exe" if os.name == "nt" else ""
    run([buildtools / ("zipalign" + suffix), "-c", "-P", "16", "4", apk], env)
    run([buildtools / ("apksigner.bat" if os.name == "nt" else "apksigner"), "verify", "--verbose", apk], env)
    return {"sha256": digest(apk), "bytes": apk.stat().st_size, "native_alignment": native,
            "default_libraries": libraries,
            "min_sdk": 26, "target_sdk": 35, "signature": "local Android debug key (preview)",
            "device_validation": "Separate from this static package audit"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare-only", action="store_true", help="Fetch/check pinned graphics sources only")
    parser.add_argument("--audit-only", type=Path, help="Audit an already built universal APK")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/android/TerMino-android-preview.apk")
    parser.add_argument("--sdk", help="Android SDK directory")
    parser.add_argument("--java-home", help="JDK 17 directory")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.prepare_only:
        prepare()
        return
    sdk = environment.sdk_directory(args.sdk)
    if not sdk:
        parser.error("Android SDK not found; set ANDROID_HOME or --sdk")
    env = dict(os.environ, ANDROID_HOME=str(sdk))
    java = args.java_home or env.get("JAVA_HOME")
    if not java:
        fallback = Path.home() / ".local/share/termino/toolchains/jdk-17"
        if fallback.is_dir():
            java = str(fallback)
    if java:
        env["JAVA_HOME"] = java
        env["PATH"] = str(Path(java)/"bin") + os.pathsep + env.get("PATH", "")
    if args.audit_only:
        report = audit(args.audit_only.resolve(), sdk, env)
        print(json.dumps(report, indent=2))
        return
    prepare()
    # AGP's incremental ZIP writer retains holes from replaced native entries.
    # Recreate only its generated packaging output, keeping native build caches.
    source = ROOT / "android/app/build/outputs/apk/debug/app-debug.apk"
    source.unlink(missing_ok=True)
    packaging = ROOT / "android/app/build/intermediates/incremental/packageDebug"
    if packaging.is_dir():
        shutil.rmtree(packaging)
    # Gradle's wrapper pins the version and distribution checksum. A matching
    # local installation avoids downloading the same distribution twice.
    gradle = environment.executable("gradle", [Path.home() / ".local/share/termino/toolchains/gradle-8.12/bin"])
    if not gradle or "Gradle 8.12" not in environment.version_output(gradle, "--version", env):
        gradle = ROOT / "android" / ("gradlew.bat" if os.name == "nt" else "gradlew")
    run([gradle, "--no-daemon", "--max-workers", args.jobs, "-p", ROOT / "android", "assembleDebug"], env)
    report = audit(source, sdk, env)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output != source:
        shutil.copyfile(source, output)
    output.with_suffix(".apk.sha256").write_text(f"{report['sha256']}  {output.name}\n")
    output.with_suffix(".audit.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Installable preview APK: {output}")


if __name__ == "__main__":
    main()
