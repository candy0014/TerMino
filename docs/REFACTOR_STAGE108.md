# Refactor Stage 108: non-BOT legacy surface cleanup

Stage 108 deliberately leaves BOT search/evaluation algorithms and all external
file/wire formats unchanged.  It targets the remaining non-BOT areas that still
carried parallel registration tables or repeated historical text parsing.

## 1. One UserConfig / Settings schema

`include/tetr/config/settings_schema.h` and `src/config/settings_schema.cpp` are
now the single metadata source for editable settings.  Every setting has a
stable `SettingId`, section, legacy disk key, UI label, member binding, choice
kind, static numeric range, and UI flags where applicable.

The following former parallel tables now consume that schema:

- `src/config/user_config_file.cpp` resolves disk keys through the schema;
- `src/app/settings_model.cpp` builds all Settings pages through the schema;
- `src/config/normalization.cpp` reuses schema ranges for static clamps.

Dynamic constraints remain explicit in normalization where they depend on
other values, e.g. garbage hole count depends on field width and layer height
depends on field height.  The raw `UserConfig::Settings` representation remains
an IO boundary so `user_config.txt` stays byte-format compatible.

Quick Play Extended All-Spin remains manual-config only: the schema accepts
legacy value 3 for normalization but the Settings choice list still exposes
only Close/Open/Reverse.

## 2. Replay config codec is table-driven

`src/replay/config_codec.cpp` previously maintained the same ~45 historical
fields twice: an encode sequence and a long decode `if/else` ladder.  It now
uses one ordered `FieldCodec` registry containing both directions.

The order and spellings of every historical field remain unchanged, including
`QP_mod_*`, `mapWidth`, `G20`, `Autoplay_PPS`, etc.  `G20` intentionally remains
encode-only, matching the previous decoder behavior. Unknown historical replay
fields are still ignored and seeds remain strict unsigned integers.

`src/replay/operation_codec.cpp` similarly replaces the duplicated
name-to-enum / enum-to-name ladders with one operation-name table.

## 3. Shared strict text codec for Replay / Network

`Util::Text` now owns generic strict integer parsing, delimiter splitting and
nested-brace matching.  Network board-state, lobby and frame codecs no longer
each carry their own `from_chars` helper/splitter, and both Replay codecs share
the same brace scanner.

This is an implementation cleanup only.  Multiplayer wire strings and section
orders are unchanged. Existing exact-wire and randomized codec tests remain the
compatibility oracle.

## 4. Settings UI view/controller split

Terminal drawing moved from `settings_menu.cpp` into the private
`TerminalSettingsView` (`settings_view.h/.cpp`).  The menu now coordinates
navigation, editing and persistence while the view owns colors, cursor moves,
item rendering, choice rendering and square previews.

No UI flow, key capture semantics or config persistence behavior changed.

## 5. Deterministic RNG cleanup

Garbage generation had a second copy of the historical Fisher-Yates loop.
That duplicate was removed; Piece Queue and Garbage now both call
`Util::deterministic_shuffle`.  The implementation itself is intentionally retained
because RNG consumption order is part of deterministic gameplay/replay
behavior.  It was not replaced with `std::shuffle`.

## 6. Regression guards

A new `settings_schema` test verifies schema uniqueness, control coverage,
important clamps, BOT choices and the manual-only Extended All-Spin behavior.
The architecture test now rejects reintroduction of:

- disk/UI setting registration literals outside `settings_schema`;
- a Replay config `if (name == ...)` field ladder;
- local `std::from_chars` parsers in Network codecs;
- a second `deterministic_shuffle_holes` implementation.

## Validation

Fresh Stage 108 validation:

- Debug: **37/37 tests passed**;
- Release: **37/37 tests passed**;
- `core_geometry`: 30,000 randomized collision/kick comparisons still pass;
- `piece_queue_equivalence`: deterministic bag/RNG equivalence still passes;
- board-state encode/decode randomized tests still pass;
- lobby exact legacy wire and randomized round trips still pass;
- Replay config 10,000 randomized typed round trips still pass;
- architecture boundary regression passes.

The remaining `Compat::legacy_encoding`, Replay field spellings, Network wire
characters and `Util::deterministic_shuffle` are now intentional compatibility
boundaries rather than duplicated runtime architecture.
