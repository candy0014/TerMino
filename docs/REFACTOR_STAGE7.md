# Refactor Stage 7: Movement Presentation and Replay Events

Stage 7 removes movement presentation and replay movement parsing from `place.h`
without changing bot search logic or the replay file format.

## New modules

### `engine/movement_presentation`

Owns the low-PPS autoplay command language emitted by `Bot::getop`:

- `H` hard drop
- `A` 180 rotation
- `X` clockwise rotation
- `Z` counter-clockwise rotation
- `S` one-cell soft drop
- `s` drop to floor
- `L` / `R` one-cell horizontal movement
- `l` / `r` move to wall

`MovementScriptPlayback` owns playback timing. Applying these commands does not
consume EPLD resets, matching the old presentation-only implementation.
Instant-gravity snapping before playback and after each non-hard-drop command is
also preserved.

### `replay/movement_event`

Parses legacy replay vectors into named movement events (`StartPlace`, `Move`,
`Hold`, `EndMove`, `EndMoveData`, etc.) and centralizes timestamp readiness.
The serialized replay format is unchanged.

### `replay/gameplay_application`

Moves the old `Place::DoOp` side-effect bridge out of `place.h`. It applies old
replay events for drawing, hold, garbage insertion, buffer insertion and offset.

This adapter is intentionally inline for now. Legacy `Board`, `Block`,
`Garbage`, `Config`, and PPP headers still contain definitions with external
linkage; compiling this bridge in an additional translation unit would trigger
ODR multiple-definition errors. The pure replay parser is a normal `.cpp`
module. Converting those legacy headers to declaration/definition pairs is a
later refactor target.

## Include-order fix

`ppp.h` now includes `<array>` and `<vector>` itself. It previously depended on
another header having included them first. No PPP table data was changed.

## Regression tests

`movement_presentation_test` covers:

- 100,000 random single-command comparisons against the Stage 6 interpreter;
- 20,000 random command sequences, including instant-gravity snapping;
- playback timing boundaries;
- legacy replay movement parsing and queue readiness.

All seven CTest targets pass in Release mode.

## Protected bot code

All files under `include/tetr/bot/` are byte-for-byte identical to Stage 6.

## `place.h`

Stage 6: 921 lines

Stage 7: 835 lines

The next useful boundary is line-clear presentation: move clear delay, row
highlight/erasure animation, and replay clear-line timing out of `place.h`,
leaving field mutation in the already separated engine layer.
