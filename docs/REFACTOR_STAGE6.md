# Stage 6: Movement controller

## Goal

Stage 6 extracts the player movement / lock-timing half of `Place::play()` into an engine module while preserving the existing BOT, Replay, QP and rendering behavior.

The extracted responsibilities are:

- gravity stepping, including the legacy near-zero-speed instant-gravity path;
- hard drop and soft drop geometry / scoring;
- left/right movement;
- CW / CCW / 180 rotation through the existing kick callback;
- Extended Placement Lock Down reset counting and timer refresh;
- lock eligibility;
- DAS / ARR key-repeat state;
- per-piece key debouncing and DAS carry.

New files:

- `include/tetr/engine/movement_controller.h`
- `src/engine/movement_controller.cpp`
- `tests/movement_controller_test.cpp`

## Movement state

The formerly independent locals

```text
x / y / type / last_op / cnt_op / tim
```

are represented by `Engine::MovementState`:

```cpp
struct MovementState {
    int row;
    int col;
    int rotation;
    int lastRotationOp;
    int lockResetCount;
    double lockTimer;
};
```

`Place::play()` keeps short reference aliases (`x`, `y`, `type`, `last_op`, `tim`) so the BOT, Replay and QP compatibility code does not need a large mechanical rewrite in the same stage.

## Collision / rotation boundary

`MovementController` does not depend on `Block`, `map`, TUI or global configuration. It receives two callbacks:

- `CanPlace(row, col, rotation)`;
- `TryRotate(row, col, fromRotation, toRotation, kickIndex)`.

The production adapter forwards these to the existing `Block::check()` and `Block::checks()` implementation. This keeps SRS+/SRS/ARS/Pentomino/M123/Hexomino/20260401 kick behavior unchanged.

## EPLD behavior preserved

Horizontal movement and rotation use the original reset rule:

1. test whether the piece is grounded before the successful action;
2. perform the action;
3. test whether it is grounded after the action;
4. if either side is grounded, increment the lock reset count;
5. refresh the shared lock timer only while `lockResetCount <= EPLDLim`.

Failed horizontal moves and failed rotations do not consume a reset.

The same `lockTimer` remains shared between gravity, soft drop and EPLD resets, matching the legacy implementation.

## Input semantics preserved

`Engine::MovementInputState` replaces the raw `vis[256]` / `t[256]` arrays.

Three non-obvious legacy behaviors are intentionally retained:

### DAS carry

Left/right state is not reset when a new piece spawns. Holding a direction can therefore preserve DAS charge across pieces.

### One-shot release debounce

CW, CCW, 180, Hard Drop, Hold, Retry and Settings are marked as unavailable when already held at piece spawn. They must be released before they can trigger again.

### Soft Drop carry

Soft Drop is reset to the normal idle state at piece spawn rather than release-debounced. Holding soft drop across pieces therefore continues to act on the next piece.

The most recently pressed active left/right or rotation key still wins when opposite/conflicting inputs are held.

## Gravity / drop compatibility

- `fabs(Speed) < 1e-8` still performs the old instant-gravity fall-to-floor path.
- ordinary gravity still advances one row only when `now - lockTimer > Speed`;
- player hard drop still awards 2 points per descended row and does not refresh the EPLD timer;
- soft drop still awards 1 point per descended row;
- `SDF == 0` still means the held soft-drop path falls as far as possible in one poll;
- ARR remains the old implementation: `ARR == 0` repeats one column per game-loop poll rather than being converted to a separate instant-slide rule.

No TETR.IO behavior correction was mixed into this refactor.

## Replay / BOT boundary

Replay still records movement using the existing `move`, `end_move` and `end_move_data` operations. The file format is unchanged.

The low-PPS BOT animation path is deliberately left in `place.h` for now because it is a presentation of BOT-selected operations rather than player input timing.

All 8 files under `include/tetr/bot/` are byte-for-byte identical to Stage 5.

## Regression tests

`movement_controller_test` includes:

- 100000 deterministic randomized movement states comparing the extracted movement formulas with a Stage 5 reference implementation;
- gravity and instant gravity;
- hard / soft drop score increments;
- horizontal movement and grounded EPLD reset behavior;
- CW / CCW / 180 rotation with deterministic kick callbacks;
- lock condition checks;
- DAS carry across piece spawn;
- hard-drop / rotation release debounce;
- soft-drop cross-piece behavior;
- most-recent-input priority and DAS/ARR repeat transitions.

At the end of Stage 6:

```text
opener_phase             PASS
core_rules               PASS
post_lock_rules          PASS
garbage_system           PASS
placement_application    PASS
movement_controller      PASS
```

## `place.h`

Stage 5: 1024 lines.

Stage 6: 921 lines.

The input/movement portion is now mostly orchestration and rendering rather than movement formulas.

## Suggested next step

The next clean boundary is Replay / movement presentation:

1. extract the low-PPS BOT operation playback into an `engine/operation_player` adapter;
2. extract live/replay movement event recording / playback from `Place::play()`;
3. then split line-clear animation / TUI timing from the lock orchestration.

After those cuts, `place.h` can become a small coordinator around movement, lock resolution, placement application and presentation adapters.
