# Stage 2: place.h core rules extraction

## Goal

Keep current gameplay behavior while moving lock-resolution rules out of `Place::play()` and into a reusable, testable rules library.

## New rule modules

- `rules/lock_analysis`: determines cleared rows, garbage/layer clears and perfect clear.
- `rules/spin`: reproduces the three existing spin-checker paths without any rendering.
- `rules/chain`: B2B transition, B2B charging and combo update.
- `rules/attack`: base attack calculation, including the existing QP All-Spin reverse branch.
- `rules/scoring`: placement score calculation before Blitz multiplier.
- `rules/placement_resolution`: aggregates the modules above into one lock-resolution call.

Implementations live under `src/rules/` and are built as the `tetr_rules` static library.

## place.h responsibility after this stage

`Place::play()` still owns:

- input and movement timing;
- piece locking and rendering;
- Replay sequencing;
- multiplayer send/receive;
- QP2 modifier side effects that mutate the field or garbage queue;
- line-clear animation and physical row compaction;
- application of the result to `Board` counters/UI.

It no longer owns the formulas for the basic lock result.

## Compatibility notes

The extracted functions intentionally preserve several non-obvious legacy behaviors:

- zero-line spins preserve B2B rather than incrementing it in the normal branch;
- QP All-Spin reverse uses its existing B2B behavior rather than standard Tetris qualification;
- mini status may be cleared by the existing All-Spin reverse attack branch for non-standard spin pieces;
- perfect clear detection uses the exact previous row-state test;
- integer truncation in attack and score formulas is preserved.

## Regression coverage

`tests/core_rules_test.cpp` includes:

- exhaustive comparison of the new B2B/Combo transition against the Stage 1 integer `b2b_flag` behavior;
- 100000 deterministic random Attack/Score states compared with a copy of the Stage 1 formulas;
- direct Spin detector checks;
- direct clear/perfect-clear/garbage-layer analysis checks.

At the end of Stage 2 all CMake targets build and both CTest tests pass.

## BOT boundary

No files under `include/tetr/bot/` are modified in Stage 2.
