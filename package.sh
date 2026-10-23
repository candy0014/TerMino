#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
project_dir=$(pwd -P)
build_dir=build-portable-linux
output_dir=dist
data_dir=$project_dir
parallel=${CMAKE_BUILD_PARALLEL_LEVEL:-2}
generator=

usage() {
    printf '%s\n' 'Usage: bash package.sh [--parallel N] [--build-dir DIR] [--output-dir DIR]' \
        '                       [--data-dir DIR] [--generator NAME] [--dynamic]' \
        'Defaults: Release, terminal + OpenGL + FT7 + server in one clean package.' \
        'No personal data is imported. --data-dir / --dynamic remain compatibility options.'
}
while (( $# )); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --dynamic) shift ;;
        --parallel|--build-dir|--output-dir|--data-dir|--generator)
            if (( $# < 2 )) || [[ -z "$2" ]]; then usage >&2; exit 2; fi
            case "$1" in
                --parallel) parallel=$2 ;;
                --build-dir) build_dir=$2 ;;
                --output-dir) output_dir=$2 ;;
                --data-dir) data_dir=$2 ;;
                --generator) generator=$2 ;;
            esac
            shift 2 ;;
        *) usage >&2; exit 2 ;;
    esac
done
if [[ $(uname -s) != Linux ]]; then
    printf 'Run this script on Linux; use package.bat on Windows.\n' >&2
    exit 1
fi
if [[ ! "$parallel" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Parallel jobs must be a positive integer.\n' >&2
    exit 2
fi
for tool in cmake python3; do
    command -v "$tool" >/dev/null || { printf 'Required tool missing: %s\n' "$tool" >&2; exit 1; }
done
if [[ "$data_dir" != "$project_dir" ]]; then
    printf '%s\n' '--data-dir is ignored: packages always use built-in defaults and empty data.'
fi
# CMake and bundled dependencies retain absolute paths after a directory move.
# Preserve their generated state before configuring at the new location.
if [[ -f "$build_dir/CMakeCache.txt" ]]; then
    cached_source_dir=
    cached_build_dir=
    while IFS='=' read -r key value; do
        case "$key" in
            CMAKE_HOME_DIRECTORY:INTERNAL) cached_source_dir=$value ;;
            CMAKE_CACHEFILE_DIR:INTERNAL) cached_build_dir=$value ;;
        esac
    done < "$build_dir/CMakeCache.txt"
    current_build_dir=$(cd -- "$build_dir" && pwd -P)
    if [[ -n "$cached_source_dir" && "$cached_source_dir" != "$project_dir" ]] ||
       [[ -n "$cached_build_dir" && "$cached_build_dir" != "$current_build_dir" ]]; then
        cache_backup_dir=$(mktemp -d "$build_dir/cmake-cache-backup.XXXXXX")
        for generated_state in CMakeCache.txt CMakeFiles _deps; do
            if [[ -e "$build_dir/$generated_state" ]]; then
                mv -- "$build_dir/$generated_state" "$cache_backup_dir/"
            fi
        done
        printf 'Project paths changed; old CMake state backed up to %s\n' "$cache_backup_dir"
    fi
fi
package_args=(--build-dir "$build_dir" --configure --frontend bundle
    --parallel "$parallel" --output-dir "$output_dir" --allow-dirty)
if [[ -n "$generator" ]]; then package_args+=(--generator "$generator"); fi
python3 tools/package_frontends.py "${package_args[@]}"
