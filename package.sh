#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
project_dir=$(pwd -P)
build_dir=build-portable-linux
output_dir=dist
data_dir=$project_dir
parallel=${CMAKE_BUILD_PARALLEL_LEVEL:-2}
full_static=ON
generator=

usage() {
    printf '%s\n' 'Usage: bash package.sh [--parallel N] [--build-dir DIR] [--output-dir DIR]' \
        '                       [--data-dir DIR] [--generator NAME] [--dynamic]' \
        'Defaults: Release, fully static runtimes, game + server only, output in dist/.' \
        'Each new package uses built-in player settings and key bindings.'
}
while (( $# )); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --dynamic) full_static=OFF; shift ;;
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
for tool in cmake tar sha256sum; do
    command -v "$tool" >/dev/null || { printf 'Required tool missing: %s\n' "$tool" >&2; exit 1; }
done
data_dir=$(cd -- "$data_dir" && pwd -P)
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
configure_args=(-S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=OFF -DTETR_PORTABLE_BUILD=ON -DTETR_PORTABLE_RESET_SETTINGS=ON
    "-DTETR_PORTABLE_FULL_STATIC=$full_static" "-DTETR_PORTABLE_DATA_DIR=$data_dir")
if [[ -n "$generator" ]]; then configure_args+=(-G "$generator"); fi
cmake "${configure_args[@]}"
cmake --build "$build_dir" --config Release --target tetr tetr_server --parallel "$parallel"

platform=$(<"$build_dir/portable-platform.txt")
package_name="TerMino-$platform-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd -P)
package_dir="$output_dir/$package_name"
# Each package starts fresh; preserve edited settings in existing distributions.
mkdir -- "$package_dir"
cmake --install "$build_dir" --config Release --component Portable --prefix "$package_dir"
tar -czf "$package_dir.tar.gz" -C "$output_dir" -- "$package_name"
(cd -- "$output_dir" && sha256sum -- "$package_name.tar.gz" > "$package_name.tar.gz.sha256")
printf 'Portable package: %s\nArchive:          %s.tar.gz\nStart with ./Play.sh\n' "$package_dir" "$package_dir"
