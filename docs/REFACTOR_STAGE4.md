# Stage 4: Garbage rules, generation, and field rise

## Goal

Stage 4 removes the rule-heavy parts of `garbage.h` without changing BOT strategy or the external `Board::Garbage` interface used by existing code.

The old file mixed five concerns:

1. incoming garbage queue timing;
2. attack cancellation and multiplayer Opener Phase;
3. decision of when/how many garbage lines rise;
4. random hole / Cheese / QP Double Hole generation;
5. physical field shifting plus terminal rendering / Replay / networking.

The first four are now reusable modules. Terminal, Replay, and network side effects remain in the compatibility adapter in `garbage.h`.

## Garbage queue rules

New module:

- `include/tetr/rules/garbage_queue.h`
- `src/rules/garbage_queue.cpp`

It owns:

- pending garbage line counting;
- creation of timed garbage packets;
- incoming cancellation;
- multiplayer first-14-piece Opener Phase double cancellation;
- ready-garbage consumption with the legacy 8-line rise cap;
- partial-packet continuation-hole semantics;
- two-stage / three-stage garbage-buffer display state calculation.

`Board::Garbage::Buffer` remains available as an alias to `Rules::GarbagePacket`. The legacy member names inside the packet are intentionally retained because existing BOT code accesses them directly.

The queue rules do not know about `Board`, `map`, TUI, Replay, UDP, or global `Config` state.

## Garbage row generation

New module:

- `include/tetr/rules/garbage_generation.h`
- `src/rules/garbage_generation.cpp`

It owns the deterministic row-generation algorithms for:

- ordinary garbage holes;
- negative `HoleNum` contiguous-hole mode;
- Cheese layer / timer messiness;
- checkerboard Cheese modes;
- QP Double Hole normal and reverse variants;
- timed solid-garbage holes.

The legacy shuffle algorithm is reproduced exactly rather than replaced with `std::shuffle`. This is important because the random-number consumption order is part of seeded gameplay behavior. Given the same `std::mt19937` state, Stage 4 produces the same rows and leaves the RNG in the same state as Stage 3.

## Field rise engine

New module:

- `include/tetr/engine/garbage_field.h`
- `src/engine/garbage_field.cpp`

It performs the physical upward field shift through callbacks and reports changed cells to the rendering adapter. This is shared by normal and solid garbage insertion.

The dependency direction is now:

```text
Rules::garbage_queue       Rules::garbage_generation
          \                       /
           \                     /
             Board::Garbage adapter
                    |
          Engine::garbage_field
                    |
        Replay / UDP / terminal rendering
```

## Compatibility details intentionally preserved

- `Board::Garbage::Buffer` and `garbage.buf` remain available to BOTs.
- The default maximum garbage rise remains 8 lines per no-clear lock.
- A partially consumed packet marks its remainder with `flag_hole = 1`, so later rise preserves the previous hole.
- Replay mode still mutates the queue without physically adding garbage, matching the old replay execution path.
- `GarbageMultiple` keeps the legacy truncating `int` conversion.
- Buffer display timing still applies `TimeMultiplier`; actual rise readiness still uses the unscaled timer difference, matching Stage 3.
- `HoleNum` mutation in QP Double Hole Reverse is preserved.
- Solid garbage type 8 still increments `height_solid_garbage` once per call, not once per inserted line. This odd legacy behavior is intentionally unchanged.
- Generated garbage rows remain visible under the same conditions as before; only shifted existing cells obey the legacy Invisible rendering check.

## Regression tests

New test target: `garbage_system_test`.

Coverage:

- 100000 deterministic random garbage queues comparing cancellation, Opener Phase, ready-rise packet splitting, and buffer display states against copies of the Stage 3 loops;
- 50000 random garbage-generation configurations comparing hole order, generated cell values, mutable `HoleNum`, checkerboard state, and post-operation RNG state;
- 20000 random fields comparing the new upward field-shift engine with the Stage 3 row-copy loop;
- solid-garbage generation and RNG equivalence.

At the end of Stage 4:

```text
opener_phase     PASS
core_rules       PASS
post_lock_rules  PASS
garbage_system   PASS
```

## BOT boundary

No file under `include/tetr/bot/` is modified in Stage 4. A SHA-256 audit against Stage 3 is identical for all BOT files.

## Suggested next step

The garbage subsystem now has a stable reusable core. The next useful cut is a placement-application layer between `Rules::resolve_placement()` and `Place::play()` that owns:

- Board counter updates;
- QP post-lock dispatch;
- garbage queue calls;
- Replay markers / multiplayer attack dispatch decisions.

After that, `place.h` can be split into input/movement timing and lock-resolution orchestration without moving BOT logic.
