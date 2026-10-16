# Refactor Stage 10: Coordinator Consolidation

Stage 10 deliberately combines several small follow-up boundaries into one larger
cleanup pass. The goal is no longer just to extract formulas from `place.h`; the
goal is to make `place.h` and `game.h` actual coordinators.

## Scope

This stage extracts:

- periodic movement-loop timing;
- player input dispatch;
- live piece movement orchestration;
- autoplay/BOT presentation orchestration;
- replay piece playback;
- post-lock placement orchestration;
- per-frame orchestration;
- spawn/top-out handling;
- Blitz level/gravity calculation;
- start countdown presentation;
- main game piece/hold/next loop;
- race/Blitz completion predicates;
- final score/time presentation;
- replay save interaction and replay hash generation.

The BOT search/score implementations remain a protected area and are not modified.

## `place.h` after Stage 10

Stage 9: 702 lines

Stage 10: 60 lines

`Place::play()` now performs only the following high-level sequence:

1. derive Blitz level/gravity when needed;
2. create the spawn movement state;
3. construct collision/rotation callbacks;
4. handle spawn collision/top-out;
5. dispatch to live movement or replay movement;
6. run the common locked-placement coordinator.

The implementation details now live behind named boundaries rather than open-coded
inside the function.

## `game.h` after Stage 10

Stage 9: 334 lines

Stage 10: 64 lines

The game shell now delegates:

- countdown presentation to `engine/countdown_presentation.h`;
- the Next/Hold/piece loop to `engine/game_loop_adapter.h`;
- finish conditions to `rules/game_completion`;
- final Sprint/Blitz display to `engine/game_end_presentation.h`;
- replay save UI/file writing to `replay/save_interaction.h`.

The legacy `flag_dead` concept is retained in `GameLoopOutcome::flagDead` and is
still surfaced as an explicit local marker in `Game::game()` for future game-over
state work.

## New pure/normal translation-unit modules

### `engine/periodic_events`

Owns small timing/formula helpers used by the movement loops:

- strict `now - last > interval` due checks;
- one-second QP gravity acceleration;
- autoplay PPS scheduling;
- Blitz timeout check.

The live and replay QP gravity paths intentionally keep their old difference:
live play guards `Speed > 1e9`, while replay historically did not.

### `engine/player_input`

Owns the complete per-poll player input dispatch:

- HD / Hold / Retry / Settings;
- CW / CCW / 180;
- Soft Drop;
- left/right movement;
- DAS / ARR;
- SDF repeat;
- EPLD reset integration;
- most-recent-key priority.

It consumes the `MovementInputState` and `MovementController` introduced in
Stage 6, so the live loop no longer contains a long action-string switch.

### `rules/blitz`

Owns the old Blitz line-to-level loop and gravity table without changing its
integer boundary behavior.

### `rules/game_completion`

Owns Sprint/Cheese-race and Blitz finish predicates.

### `replay/replay_hash`

Owns replay hash suffix generation. A compatibility detail is explicitly
preserved: the historical expression

```cpp
long long mod = 1e16 + 2137;
```

first rounds as `double`, producing the actual modulus
`10000000000002136`. Existing replay verification depends on that value, so the
new module intentionally preserves it and regression-tests it.

## New legacy-facing coordinator adapters

Some adapters remain inline headers because older project headers still define
many global objects/functions directly. Turning these adapters into ordinary
`.cpp` files before that ODR debt is removed would create multiple-definition
link failures.

The new adapters are:

- `engine/live_piece_controller.h`
- `engine/autoplay_adapter.h`
- `engine/frame_coordinator.h`
- `engine/piece_spawn.h`
- `engine/placement_coordinator.h`
- `engine/countdown_presentation.h`
- `engine/game_loop_adapter.h`
- `engine/game_end_presentation.h`
- `replay/piece_playback.h`
- `replay/save_interaction.h`

They do not reimplement the pure rule formulas. They preserve ordering between
existing Rules/Engine modules and legacy UI/Replay/network side effects.

## Important ordering preserved

### Live piece loop

The ordering remains:

1. garbage buffer UI update;
2. Blitz expiration;
3. 50 ms Frame pump;
4. one-second QP gravity acceleration;
5. 100 ms multiplayer state broadcast;
6. GarbageModel 4 timed garbage injection;
7. EPLD lock check;
8. automatic gravity;
9. autoplay/BOT placement;
10. player input;
11. movement redraw / hard-drop termination.

### Autoplay

The old hold carry behavior is now represented explicitly by
`AutoplayCarryState` instead of the globals `hold_x`, `hold_y`, `hold_type`,
`hold_last_op`, and `flag_hold`.

Low-PPS BOT animation still does not consume EPLD resets, and the BOT search files
are unchanged.

### Replay piece playback

`replay/piece_playback.h` owns the wait for `start_place`, movement event pumping,
RE/Settings input, QP one-second gravity update, and final `end_move_data`
validation.

The replay file format is unchanged.

## Game-loop extraction

`engine/game_loop_adapter.h` now owns:

- bag refill check;
- Next preview erasure/rebuild/redraw;
- multiplayer pre-piece state send;
- call into the piece runner;
- queue pop;
- Hold queue transition;
- held-piece continuation flag;
- Sprint/Cheese/Blitz goal checks;
- multiplayer round-result messages.

This is the first stage where both `place.h` and its direct caller `game.h` have
become small enough to read as top-level flow descriptions.

## Regression coverage

All previous tests remain enabled.

Stage 10 adds/extends tests for:

- QP gravity acceleration compatibility;
- periodic event strict boundary behavior;
- autoplay PPS due logic;
- Blitz level calculation over 100,000 line-count states;
- Sprint/Cheese/Blitz completion over 100,000 randomized states;
- replay hash modulus and 10,000 randomized replay strings;
- player input extraction using a randomized old-loop-vs-new-loop equivalence
  test across 4,000 sequences × 40 polls.

A clean Release build passes all 11 CTest targets.

The new normal `.cpp` modules compile cleanly under:

```text
-Wall -Wextra -Wpedantic
```

## Protected BOT audit

All eight files under `include/tetr/bot/` are byte-for-byte identical to Stage 9.

## ODR cleanup begun

The public objects/functions defined by `place.h` and `game.h` are now declared
`inline`, making those two headers C++17 ODR-safe if they are included from more
than one translation unit.

The old debug stream `fout` was also moved from a definition-before-includes trick
in `src/tetr.cpp` to a normal `extern` declaration in `debug_log.h` plus a single
`src/debug_log.cpp` definition. This fixes `init.h`'s hidden dependency on include
order without touching BOT code.

All 15 new Stage-10 public headers were also compiled as the first include in a
minimal translation unit to verify self-containment.

The broader legacy project still has additional header-defined globals/functions.
That should be addressed in a later dedicated pass rather than mixed into gameplay
refactoring.

## Next useful boundary

The next high-value work is no longer another `place.h` extraction. The two
largest structural debts are now:

1. move header-defined global state/functions (`Config`, `Board`, `Init`, legacy
   helpers) into declarations + `.cpp` definitions so compatibility adapters can
   become ordinary translation units;
2. split `garbage.h`/`board.h`/`init.h` legacy implementation bodies from their
   interfaces and introduce a proper runtime context instead of unrestricted
   global configuration access.

That pass can be larger and mostly mechanical, with compile/link tests guarding
against ODR regressions.
