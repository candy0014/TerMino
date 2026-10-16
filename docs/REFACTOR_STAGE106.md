# Stage 106: typed Map / Board / Piece Catalog boundary for BOTs

Stage 106 deliberately refactors the data/geometry layer underneath the three retained BOTs before touching their search or evaluation logic.

## Scope

The following strategy behavior is intentionally unchanged:

- search topology and BFS/beam expansion;
- evaluation/scoring formulas and constants;
- search width and autoplay PPS handling;
- hold decisions and opening scripts;
- random tie breaking;
- Quick Play strategy branches.

Only the data structures used to feed those algorithms were replaced.

## Map removal

The historical `include/tetr/map.h` stored a second 105x105 integer snapshot of the playfield and converted every `Runtime::BoardCell` back to the legacy `-1 / piece / 7 / 8 / 9 / 100+` encoding.

It is removed. BOT declarations and implementations now receive `Runtime::Playfield` directly. The small number of direct cell accesses were translated mechanically:

- `cell != -1` -> `cell(...).occupied()`;
- `cell == 7` -> `cell(...).is_normal_garbage()`;
- legacy row assignment -> `Playfield::copy_row()`;
- temporary BOT placement -> `BoardCell::piece()`.

`Block::block::check/checks` now accept `const Runtime::Playfield&` and delegate to `Piece::Definition::can_place/try_rotate`. This keeps the BOT's historical integer row/column/rotation call sites while sharing the engine's real geometry implementation.

## Board facade removal

`legacy/board_facade.*` previously created a temporary object containing aliases/snapshots of `PlayerSessionState` fields solely to satisfy old BOT signatures.

It is removed. The BOT `getpos()` declarations now accept:

```cpp
const Runtime::PlayerSessionState& player,
Runtime::Playfield field
```

Only `bot_while1` consumed Board fields and required internal state-access edits: B2B, combo, garbage height, garbage packets and QP last operation.

Candy and Mech only changed their function signatures.

The old packed QP operation remains local to `bot_while1` because its evaluation logic still consumes that historical representation. It is now projected directly from the typed `QuickPlayClearOperation` instead of being stored in a Board facade.

## PPP removal

`legacy/ppp_catalog.*` and the separate `tetr_legacy_catalog` target are removed.

Only one production strategy expression still directly used `PPP::*`: the shape write in `bot_while1::board::drop`. It now reads:

```cpp
Piece::catalog_entry(Piece::Type::from_index(x)).shapes[d]
```

All other geometry already flowed through `Piece::Definition`.

## What intentionally remains

`include/tetr/block.h` remains as a thin BOT compatibility wrapper. Its ordinary-field collision/rotation path is typed, but `bot_while1` still uses the historical internal bitboard helpers `bot_check/bot_checks`. Removing those would require modifying the strategy's internal search representation and is deferred to a later BOT stage.

`src/bot/bot_compat_environment.h` also remains. It still provides the old global configuration/timer names expected throughout the strategy bodies. Stage 106 does not change those algorithms.

## Regression guards

- `core_geometry_test`: 30000 randomized collision/kick equivalence cases.
- `bot_typed_boundary_test`: typed `Playfield + PlayerSessionState` -> real Candy `getpos()` -> typed answer -> movement script.
- `architecture_boundary_test`: fails if `map.h`, `board_facade.*` or `ppp_catalog.*` reappear.
- Debug: 36/36 tests passed.
- Release: 36/36 tests passed with assertions enabled for tests.
