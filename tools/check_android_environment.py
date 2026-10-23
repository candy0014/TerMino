#!/usr/bin/env python3
"""Inspect Android bootstrap tools without downloads, device access or game data writes."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
NDK_VERSION = "28.2.13676358"
BUILD_TOOLS_VERSION = "35.0.0"
GRADLE_VERSION = "8.12"


def first_directory(candidates):
    return next((Path(p).expanduser() for p in candidates if p and Path(p).expanduser().is_dir()), None)


def sdk_directory(explicit):
    configured = explicit or os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if configured:
        return Path(configured).expanduser()
    local = os.environ.get("LOCALAPPDATA")
    return first_directory([
        Path.home() / "Android/Sdk", Path.home() / "Android/sdk",
        Path.home() / "Library/Android/sdk", Path(local) / "Android/Sdk" if local else None,
        "/opt/android-sdk", "/usr/lib/android-sdk",
    ])


def executable(name, directories=()):
    suffixes = (".exe", ".bat", "") if os.name == "nt" else ("",)
    for directory in directories:
        for suffix in suffixes:
            candidate = directory / (name + suffix)
            if candidate.is_file() and (os.name == "nt" or os.access(candidate, os.X_OK)):
                return candidate
    found = shutil.which(name)
    return Path(found) if found else None


def version_output(tool, argument, env=None):
    if not tool:
        return ""
    try:
        result = subprocess.run([str(tool), argument], capture_output=True, text=True,
                                errors="replace", timeout=20, env=env)
        return result.stdout + result.stderr if result.returncode == 0 else ""
    except (OSError, subprocess.TimeoutExpired):
        return ""


def numeric_version(path):
    return tuple(int(part) for part in re.findall(r"\d+", path.parent.name))


def inspect(args):
    checks = []

    def check(name, ok, detail, required=True):
        checks.append({"name": name, "required": required, "ok": bool(ok), "detail": detail})

    sdk = sdk_directory(args.sdk)
    check("sdk", sdk and sdk.is_dir(), str(sdk) if sdk else "Android SDK not found; use --sdk or ANDROID_HOME")
    toolchains = Path.home() / ".local/share/termino/toolchains"
    java_home = args.java_home or os.environ.get("JAVA_HOME")
    java_dirs = [Path(java_home).expanduser() / "bin"] if java_home else [toolchains / "jdk-17/bin"]
    # An explicit JAVA_HOME must not be silently repaired by another JDK on PATH.
    if java_home:
        javac = next((java_dirs[0] / name for name in ("javac.exe", "javac")
                      if (java_dirs[0] / name).is_file()), None)
    else:
        javac = executable("javac", java_dirs)
    java = next((javac.parent / name for name in ("java.exe", "java")
                 if (javac.parent / name).is_file()), None) if javac else None
    javac_version = version_output(javac, "-version")
    check("jdk17", java and re.search(r"\bjavac 17(?:\.|\s|$)", javac_version),
          f"{javac}: {javac_version.strip()}" if javac else "JDK 17 not found; use --java-home or JAVA_HOME")

    cmake_dirs = sorted((sdk / "cmake").glob("*/bin"), key=numeric_version, reverse=True) if sdk else []
    cmake = executable("cmake", cmake_dirs)
    cmake_version = version_output(cmake, "--version")
    match = re.search(r"cmake version (\d+)\.(\d+)\.(\d+)", cmake_version)
    check("cmake", match and tuple(map(int, match.groups())) >= (3, 22, 1),
          f"{cmake}: {match.group(0)}" if match else "CMake >= 3.22.1 not found")
    ninja = executable("ninja", cmake_dirs)
    ninja_version = version_output(ninja, "--version").strip()
    check("ninja", ninja and ninja_version, f"{ninja}: {ninja_version}" if ninja else "Ninja not found")

    if sdk:
        platform = sdk / f"platforms/android-{args.api}/android.jar"
        build_tools = sdk / "build-tools" / BUILD_TOOLS_VERSION
        ndk = sdk / "ndk" / args.ndk_version
        check("sdk_platform", platform.is_file(), str(platform))
        required_tools = [executable(name, [build_tools]) for name in ("aapt2", "zipalign", "apksigner")]
        # These tools must belong to the selected SDK, not an unrelated PATH entry.
        check("build_tools", all(p and p.parent == build_tools for p in required_tools), str(build_tools))
        properties = ndk / "source.properties"
        try:
            ndk_properties = properties.read_text(encoding="utf-8")
        except OSError:
            ndk_properties = ""
        installed_revision = re.search(r"^Pkg.Revision\s*=\s*(\S+)", ndk_properties, re.M)
        compiler_dirs = list((ndk / "toolchains/llvm/prebuilt").glob("*/bin"))
        compiler = executable("clang++", compiler_dirs)
        check("ndk", installed_revision and installed_revision.group(1) == args.ndk_version
              and (ndk / "build/cmake/android.toolchain.cmake").is_file()
              and compiler and compiler.parent in compiler_dirs, str(ndk))
        sdk_bins = list((sdk / "cmdline-tools").glob("*/bin"))
        manager = executable("sdkmanager", sdk_bins)
        check("sdkmanager", manager, str(manager) if manager else "SDK command-line tools not found")
    else:
        for name in ("sdk_platform", "build_tools", "ndk", "sdkmanager"):
            check(name, False, "Android SDK not found")

    gradle = executable("gradle", [toolchains / f"gradle-{GRADLE_VERSION}/bin"])
    gradle_env = dict(os.environ)
    if java:
        gradle_env["JAVA_HOME"] = str(java.parent.parent)
    gradle_version = version_output(gradle, "--version", gradle_env)
    check("gradle", re.search(r"^Gradle 8\.12\s*$", gradle_version, re.M),
          str(gradle) if gradle else "Gradle 8.12 bootstrap installation not found")
    for name, subdir in (("adb", "platform-tools"), ("emulator", "emulator")):
        tool = executable(name, [sdk / subdir] if sdk else [])
        check(name, tool, str(tool) if tool else f"{name} not installed (optional for compilation)", required=False)
    return {
        "prerequisites_ready": all(c["ok"] for c in checks if c["required"]),
        "android_build_verified": False,
        "note": "Tool checks only; no project build, device validation, network access or game data access.",
        "baseline": {"sdk_api": args.api, "build_tools": BUILD_TOOLS_VERSION,
                     "ndk": args.ndk_version, "gradle": GRADLE_VERSION, "jdk_major": 17},
        "checks": checks,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="Android SDK root (otherwise use environment or common install locations)")
    parser.add_argument("--java-home", help="JDK 17 root (otherwise use JAVA_HOME, local bootstrap install or PATH)")
    parser.add_argument("--api", type=int, default=35, help="SDK platform to check (default: 35)")
    parser.add_argument("--ndk-version", default=NDK_VERSION, help="Side-by-side NDK version")
    parser.add_argument("--json", action="store_true", help="Print the report as JSON")
    parser.add_argument("--strict", action="store_true", help="Exit 1 if a required bootstrap tool is missing")
    args = parser.parse_args()
    if args.api < 21:
        parser.error("--api must be at least 21")
    report = inspect(args)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        for item in report["checks"]:
            status = "OK" if item["ok"] else "MISSING" if item["required"] else "OPTIONAL"
            print(f"[{status}] {item['name']}: {item['detail']}")
        print("Bootstrap prerequisites ready:", report["prerequisites_ready"])
        print(report["note"])
    return int(args.strict and not report["prerequisites_ready"])


if __name__ == "__main__":
    raise SystemExit(main())
