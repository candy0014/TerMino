# Refactor Stage 109: compatibility boundary and startup-data closure

Stage 109 continues the Stage 108 non-BOT cleanup. BOT search/evaluation code,
gameplay formulas and all external file/network formats remain unchanged.

## 1. Permissive historical numeric parsing leaves generic Util

The old loose integer/real parser intentionally accepts strings such as
`abc-0012x` and `-0.25abc`. That behavior is still required for historical
`user_config.txt`, Settings text editing and Replay compatibility, but it is no
longer presented as a normal numeric utility.

- added `tetr/compat/legacy_text.h`;
- removed `parse_int_loose/parse_double_loose` from `tetr/util/numeric.h`;
- Config schema owns scalar text assignment through `assign_setting_text()`;
- Settings UI delegates editing to `tetr_config` and no longer includes a
  compatibility parser directly;
- Replay codecs use the same explicit compatibility boundary.

This keeps permissive parsing behavior byte/behavior compatible while making it
harder for normal Runtime/Rules/Engine code to accidentally adopt it.

## 2. Retired string key-action compatibility APIs removed

`Input::parse_action("CW"/"HD"/...)` had no production callers after typed
`BindingSet` migration. `UserConfig::control_names()` likewise existed only as a
parallel historical short-name table. Both APIs were deleted. Runtime bindings
now have one representation: configured key-code slots mapped to typed
`Input::Action` values.

## 3. Known Replay operations parse through string views

`decode_operation_stream()` previously split every `{...}` record into owning
`vector<string>` values even for known typed operations. Stage 109 parses known
records through `string_view` fields and constructs owned strings only for
`UnknownPayload`, where preservation of unknown historical data genuinely
requires ownership.

The public focused-test helper accepting `vector<string>` remains, but it now
projects views into the same decoder rather than defining a second path.

## 4. Fixed-width Network text packets use one exact splitter

`Util::Text::split_exact<N>()` now represents fixed-field text protocols.
Attack frames and lobby setup decoding use it directly; board-state section
count validation also uses it for the 10-field and 17-field historical layouts.
This removes another set of handwritten delimiter-position checks without
changing any wire character or field order.

## 5. Deterministic gameplay shuffle gets semantic naming

The historical Fisher-Yates implementation must remain bit-for-bit deterministic
with the same RNG consumption order. It is therefore retained, but the generic
`random.h` / `legacy_shuffle` naming has been replaced by
`deterministic_random.h` / `deterministic_shuffle`.

Piece Queue and Garbage generation still execute exactly the same swaps with
exactly the same `mt19937` draws. The equivalence test remains the oracle.

## 6. Piece Catalog raw data no longer has cross-TU dynamic initialization

The largest remaining non-BOT static-data file, `piece_catalog_data.cpp`, still
exported global vectors and arrays-of-vectors. That meant the raw geometry data
itself participated in cross-translation-unit dynamic initialization, the same
class of hazard that caused the historical Stage 104 startup SIGFPE.

All raw Piece Catalog tables are now exposed through function-local static
accessors. `Piece::catalog()` remains function-local lazy as before, but its
source geometry/metadata is now lazy as well. No public geometry, kick table,
metadata, piece ordering or rotation-system mapping changed.

`startup_catalog`, `piece_catalog` and the 30,000-case `core_geometry` oracle
cover this boundary.

## 7. Regression guards

Architecture guards now reject reintroduction of:

- loose numeric parsers in generic `Util`;
- direct legacy text parsing from Settings App code;
- `parse_action()` and `control_names()` parallel string models;
- `random.h` / `legacy_shuffle` naming;
- owning-string parsing in the known Replay stream path;
- handwritten delimiter search in fixed attack packets;
- global `extern const` Piece Catalog source tables.

## Compatibility contract

Unchanged by Stage 109:

- `user_config.txt` syntax and permissive historical numeric semantics;
- `keybinding.txt` integer layout;
- Replay field names, operation names, timestamps and unknown records;
- multiplayer lobby/frame/board-state wire strings;
- deterministic Piece/Garbage RNG output for the same seeds;
- all Piece geometry, kick order and metadata;
- BOT search/evaluation algorithms and parameters.
