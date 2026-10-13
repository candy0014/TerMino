#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
unset TETR_USER_CONFIG TETR_USER_CONFIG_PLAYER2
export TETR_MAP_FILE=.tetr/maps.txt TETR_PRESET_FILE=.tetr/presets.txt
exec ./tetr "$@"
