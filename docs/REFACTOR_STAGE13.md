# Stage 13: explicit game/session context

Stage 13 begins the architectural migration away from process-wide gameplay
state.  The compatibility globals are intentionally still available for the
legacy Board/UI/BOT code, but ownership is now explicit and the main gameplay
path receives a `Runtime::GameContext&`.

No BOT search/scoring code or Replay on-disk format is intentionally changed.

## New ownership model

Three runtime types now separate process services, per-game state, and
configuration:

```text
RuntimeServices
  clock
  process RNG
  UDP socket / server IP
  Linux keyboard event reader
  key/action bindings

SessionState
  player Board
  opponent Board
  gameStartTime
  QuickPlay session state
  Replay data/config-prefix/event queue

GameContext
  RuntimeConfig
  SessionState&
  RuntimeServices&
```

The process default is available through:

```cpp
Runtime::default_game_context();
```

`src/tetr.cpp` now obtains that context once and passes it to `Game::game()`.

## Legacy globals are compatibility references

The old names remain source-compatible, but no longer own independent state:

```text
board1 / board2                -> default SessionState Boards
BoardStatic::gameStartTime     -> SessionState::gameStartTime
BoardStatic::QP_*              -> SessionState::quickPlay
Config::Replay / conf_replay   -> SessionState::replay
Config::ReplayOp               -> SessionState::replay.operations
Config::rd                     -> RuntimeServices::random
Config::sock / server_ip       -> RuntimeServices network service
Config::KEY / custom           -> RuntimeServices input bindings
timer                          -> RuntimeServices::clock
```

This keeps the protected BOT implementations compiling without editing them,
while new code has a real owner to depend on.

## Context-aware gameplay chain

The following modules now have explicit `GameContext&` overloads and the main
path uses those overloads:

```text
Game::game
Place::play / Place::Frame
Engine::run_frame
Engine::run_live_piece_movement
Engine::run_autoplay_step
Engine::finalize_locked_piece
Engine::run_piece_loop
Engine::run_start_countdown
Engine::render_final_mode_result
Engine::handle_spawn_collision
ReplayEvent::playback_piece_movement
ReplayEvent::run_post_game_interaction
```

The old signatures remain as thin wrappers around the default context for
compatibility.

For the Stage 12 main gameplay chain, a lexical audit found 231 direct reads of
legacy state names such as `Speed`, `GarbageModel`, `ReplayOp`, `timer`,
`BoardStatic::*`, `KEY`, and `custom`.  The same audited Stage 13 files contain
zero direct reads of those names; they use `context.config`, `context.session`,
or `context.services` instead.

## RuntimeConfig is now live state, not only a snapshot

`Runtime::refresh_runtime_config()` still captures the legacy configuration
after `Init::init()`, but the active main game loop uses the resulting
`context.config` directly.

Fields intentionally modified during play, such as QP Gravity speed, EPLD,
Survival APM, incoming-garbage delay, GarbageMultiple and SingleMessiness, are
updated in the context first.  `publish_mutable_runtime_config()` mirrors those
changes back to the old globals only for the process default context, so legacy
Board/UI/BOT code stays coherent without making separately constructed
contexts leak into global state.

## QP session state

QP speed/altitude/floor/garbage multipliers are now owned by
`SessionState::quickPlay`.  `BoardStatic::QP_*` are aliases into the default
session.

`Board::init_time()` and `Board::init_qp()` gained context-aware overloads.  The
QP initialization implementation was moved to `src/engine/board_qp_init.cpp`.
This also fixed a hidden layer violation from Stage 12: the implementation had
lived in `tetr_runtime` while calling Engine-owned garbage methods.  After the
move, `tetr_runtime` has no unresolved dependency on `Engine::*` or
`Board::Garbage::*`.

## Replay recorder

New files:

```text
include/tetr/replay/recorder.h
src/replay/recorder.cpp
```

The context-aware game path records into `context.session.replay.data` and uses
`context.session.gameStartTime`/`context.now()` for timestamps.  The old
`Function::add_replay()` remains as a compatibility template for legacy code.
Replay file format and hash behavior remain unchanged.

## New regression test

`game_context_test` verifies:

- every legacy Board/Replay/RNG/Timer/Socket/Input alias points at the default
  owner;
- writes through either the legacy name or the new owner are visible from the
  other side;
- Frame state capture/store works against a separately constructed context;
- a non-default context does not publish dynamic configuration into legacy
  globals;
- the context-aware Replay recorder writes only to the selected session;
- context-aware `Board::init_time()` updates only that session.

## Verification

- Debug build: 13/13 tests passed;
- Release build: 13/13 tests passed;
- Release tests still compile with `-UNDEBUG`, so assert-based random
  comparisons execute;
- 64/64 non-BOT public headers compile independently as first includes;
- all Stage 13 modified non-BOT sources pass `-Wall -Wextra -Wpedantic` with no
  warnings;
- `tetr_runtime` has no Engine/Board::Garbage unresolved dependency;
- all 8 protected BOT strategy files are byte-for-byte identical to Stage 12.

## Remaining compatibility globals

Stage 13 deliberately does not rewrite `Init::init()`, legacy Board drawing,
Garbage adapter internals, Settings, or BOT algorithms around a context in one
jump.  Those pieces still use the compatibility names.  The important change
is that they now point into explicit owners, so later migration can proceed
module by module without changing storage identity again.
