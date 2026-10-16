# Refactor Stage 8: Line-Clear Presentation

Stage 8 removes line-clear timing and replay clear-phase orchestration from
`place.h` while keeping the Stage 7 clear rules and field mutation unchanged.

## New engine module: `engine/line_clear_presentation`

The new presentation layer owns the legacy centre-out line-clear animation:

- exact column order for odd and even field widths;
- `min(0.2, LineClearDelay / mapWidth * 0.6)` per-column timing;
- the final wait until the full `LineClearDelay` expires;
- at most one presentation action released by each poll, preserving the old
  Frame/Replay interleaving;
- the complete `clear_line -> animation -> end_clear_line` coordinator.

`run_line_clear_presentation()` is independent of terminal, Board, map and
legacy globals. `place.h` supplies callbacks for drawing one cleared column,
Frame pumping, replay pumping, replay recording and flushing output.

The actual gravity/anti-gravity field collapse remains in the Stage 3
`field_mutation` layer. Stage 8 changes presentation only.

## New replay module: `replay/line_clear_event`

Legacy replay strings are mapped to named clear-phase events and markers:

- `clear_line`
- `end_clear_line`
- `end_all`

The marker pump consumes currently due replay records while preserving an
important old behavior: during the line-clear animation, `end_clear_line` is
left at the queue front even when its timestamp has already arrived. It is
consumed only by the final post-animation wait.

The serialized replay format is unchanged.

## Regression tests

`line_clear_presentation_test` covers:

- all field widths 1..100 against the legacy centre-out order;
- 100,000 randomized timing/state-machine comparisons;
- replay marker readiness and `end_clear_line` reservation;
- non-replay marker recording order;
- replay coordinator waiting behavior;
- early exit when a Frame result interrupts the animation.

All eight CTest targets pass in a clean Release build.

## Protected bot code

All files under `include/tetr/bot/` are byte-for-byte identical to Stage 7.

## `place.h`

Stage 7: 835 lines

Stage 8: 795 lines

The next useful boundary is the remaining per-frame/game-mode work in
`Place::Frame`: survival/QP garbage production, QP altitude/floor progression,
multiplayer receive handling, and status rendering should be separated into a
frame-update engine plus presentation/network adapters.
