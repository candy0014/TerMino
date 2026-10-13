#!/usr/bin/env bash
set -euo pipefail

# Resolve all relative CMake paths from the script's directory.
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

# CMake stores absolute paths in its cache even when -S/-B are relative.
# Preserve stale generated state after the project or build directory moves.
if [[ -f build/CMakeCache.txt ]]; then
    cached_source_dir=
    cached_build_dir=
    while IFS='=' read -r key value; do
        case "$key" in
            CMAKE_HOME_DIRECTORY:INTERNAL) cached_source_dir=$value ;;
            CMAKE_CACHEFILE_DIR:INTERNAL) cached_build_dir=$value ;;
        esac
    done < build/CMakeCache.txt
    project_dir=$(pwd -P)
    build_dir=$(cd build && pwd -P)
    if [[ -n "$cached_source_dir" && "$cached_source_dir" != "$project_dir" ]] ||
       [[ -n "$cached_build_dir" && "$cached_build_dir" != "$build_dir" ]]; then
        cache_backup_dir=$(mktemp -d build/cmake-cache-backup.XXXXXX)
        mv -- build/CMakeCache.txt "$cache_backup_dir/"
        if [[ -d build/CMakeFiles ]]; then
            mv -- build/CMakeFiles "$cache_backup_dir/"
        fi
        printf 'Project paths changed; old CMake cache backed up to %s\n' "$cache_backup_dir"
    fi
fi

build_tests=OFF
if [[ "${1:-}" == "--test" ]]; then
    build_tests=ON
    shift
fi
if (( $# )); then
    printf 'Usage: %s [--test]\n' "$0" >&2
    exit 2
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$build_tests"
cmake --build build --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
if [[ "$build_tests" == ON ]]; then
    ctest --test-dir build --output-on-failure
fi

echo
echo "Build complete."
echo "Game:       ./build/tetr"
echo "Server:     ./build/tetr_server"
