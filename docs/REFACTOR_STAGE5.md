# Stage 5: Placement application layer

## Goal

Stage 5 introduces an explicit application layer between `Rules::resolve_placement()` and the side-effect-heavy remainder of `Place::play()`.

Before this stage, `place.h` still directly orchestrated and calculated:

- Board counters and Blitz score accumulation;
- QP Double Hole reverse attack conversion / blighted state;
- Snake Eyes attack suppression and self-garbage;
- QP Volatile / Backfire offset attack conversion;
- post-offset outgoing attack;
- All-Spin repeat penalties and timed-solid decay decisions;
- QP altitude / speed advancement;
- Backfire self-garbage;
- extended All-Spin operation history and speed penalty.

Those calculations now live behind a reusable engine API.

## New engine module

New files:

- `include/tetr/engine/placement_application.h`
- `src/engine/placement_application.cpp`

The API intentionally has two phases:

```text
Rules::resolve_placement()
          |
          v
Engine::begin_placement_application()
          |
          |  pre-offset side effects
          |  + live/replay garbage offset
          v
Engine::finish_placement_application()
          |
          v
PlacementPostOffsetEffects
```

A single monolithic function would look simpler, but would obscure two ordering rules that matter to gameplay and Replay compatibility:

1. Snake Eyes reverse self-garbage is inserted **before** the current attack offsets the garbage buffer. It can therefore be cancelled by the same placement.
2. Backfire self-garbage is inserted **after** offset/outgoing calculation and cannot be cancelled by that placement.

Replay also records/replays `offset_buffer` operations between those phases. Keeping offset explicit allows the replay path to preserve the Stage 4 queue mutation order.

## Application state

`Engine::PlacementApplicationState` carries the gameplay state needed by the application calculation without depending on `Board`, terminal rendering, networking, Replay queues, or global configuration:

- placed block count;
- total attack / lines / cleared garbage lines;
- score;
- B2B / Combo;
- QP blighted state;
- QP previous clear operation;
- QP extended All-Spin operation history;
- QP altitude and speed.

`PlacementApplicationConfig` is a snapshot of the configuration values that influence this stage.

## Effects

The pre-offset result separates side effects by timing:

- `directGarbageLines`: GarbageModel 2 ordinary garbage insertion;
- `preOffsetBufferLines`: Snake Eyes reverse self-garbage;
- `attack`: attack after pre-offset QP modifiers;
- `offsetAttack`: amount passed to the garbage buffer;
- `offsetClearTrigger`: whether the buffer is offset or allowed to rise.

The post-offset result contains:

- `outgoingAttack`;
- `postOffsetBufferLines`: Backfire self-garbage;
- `solidGarbageLines`: All-Spin repeat punishment;
- `decayTimedSolidGarbage`: All-Spin timed-solid decay request.

Map mutation, TUI, Replay markers, and UDP remain in the adapter/orchestration layer.

## Board adapter

New header:

- `include/tetr/engine/placement_board_adapter.h`

This is a thin transitional adapter between the legacy `Board`/global `Config` representation and the new application structs. It contains no gameplay formula.

It is header-only because the current legacy project still defines many configuration globals directly in headers; compiling another `.cpp` that includes those definitions would reintroduce ODR/linkage problems. A later configuration-state refactor can turn this adapter into an ordinary source module.

## Compatibility details preserved

- GarbageModel 2 uses attack **before** Double Hole / Snake Eyes transformations when calculating direct garbage.
- QP Double Hole reverse runs before Snake Eyes.
- Snake Eyes reverse garbage remains pre-offset.
- Board `cnt_block` is incremented before the garbage offset call, so multiplayer Opener Phase still sees the same piece count as Stage 4.
- Replay still replays recorded `offset_buffer` mutations and reads `offset_data` instead of recalculating the recorded outgoing value.
- All-Spin repeat/timed-solid behavior remains post-offset.
- QP speed uses post-offset outgoing together with the pre-offset attack value exactly as before.
- Backfire self-garbage remains post-offset.
- Extended All-Spin history still applies after the ordinary QP speed update.
- Blitz score retains the legacy integer multiplication semantics before accumulation into the 64-bit score.

## Regression tests

New target:

- `placement_application_test`

Coverage:

- 100000 deterministic randomized states comparing the new two-phase application API with a Stage 4 reference implementation;
- all supported GarbageModel values used by the placement path;
- Blitz and normal scoring;
- QP Double Hole, Snake Eyes, Volatile, Backfire, All-Spin 0/1/2/3, Expert;
- QP previous-operation and extended-history state;
- explicit tests that Snake Eyes garbage is pre-offset and Backfire garbage is post-offset.

At the end of Stage 5:

```text
opener_phase             PASS
core_rules               PASS
post_lock_rules          PASS
garbage_system           PASS
placement_application    PASS
```

## `place.h`

Stage 4: 1045 lines.

Stage 5: 1024 lines.

The raw line reduction is modest because the lock orchestration, replay waits, TUI and line-clear animation still live in `place.h`. The important change is that the lock path no longer contains the post-lock attack/state formulas listed above.

## BOT boundary

No file under `include/tetr/bot/` is modified in Stage 5.

## Suggested next step

The next useful cut is the input/movement half of `Place::play()`:

1. represent player actions with an enum instead of repeated string comparisons such as `"L"`, `"R"`, `"HD"`, `"CW"`;
2. move DAS/ARR/soft-drop/rotation timing into an `engine/movement_controller` module;
3. leave `place.h` responsible for Replay/TUI orchestration until that controller has regression coverage.

After that, line-clear animation and Replay synchronization can be extracted as adapters, and `place.h` can become a small coordinator rather than the main engine.
