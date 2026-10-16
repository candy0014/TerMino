# Stage 19: remove `Board` from the normal runtime model

Stage 19 finishes the state-ownership transition started in Stages 16-18. The
historical `Board` object is no longer a normal runtime/gameplay type. Normal
App/Engine/Replay/Render/Network code works directly with
`Runtime::PlayerSessionState` / `Runtime::PlayerState`; the old Board-shaped API
exists only inside the BOT legacy boundary.

## SessionState directly owns players

The old transitional arrangement embedded Board facades around per-player
state. It is replaced by the direct model:

```text
SessionState
├── PlayerSessionState player
├── PlayerSessionState opponent
├── QuickPlaySessionState
├── ReplaySessionState
└── gameStartTime
```

A `PlayerSessionState` owns gameplay, queue/RNG, garbage, multiplayer, timing
and presentation state. There is no extra state-owning Board layer between the
session and these components.

## Board is a private legacy facade

The old public files are gone:

```text
include/tetr/board.h
src/runtime/board.cpp
include/tetr/garbage.h
```

The BOT-only replacement is:

```text
include/tetr/legacy/board_facade.h
src/legacy/board_facade.cpp
```

It is compiled only into `tetr_bot_legacy`. `libtetr_runtime.a` contains zero
`Board::` symbols.

The facade owns no gameplay data when bound to a session. Historical fields
such as `mapWidth`, `score`, `combo`, `b2b`, `now_hold` and `QP_ops` are
references into `PlayerSessionState::gameplay`.

A standalone facade is still supported for protected BOT implementation code
and the dedicated compatibility test; it owns a private `PlayerSessionState`
sidecar.

## Board::Garbage reduced to the actual BOT requirement

Protected BOT source only names:

```cpp
Board::Garbage
Board::Garbage::Buffer
garbage.buf
```

It does not call the historical garbage mutation methods. The facade is
therefore reduced to a buffer view whose `buf` is a reference to:

```text
PlayerSessionState::garbage.buffer
```

All garbage mutation now belongs to the normal Engine API:

```text
Engine::GarbageService::initialize
Engine::GarbageService::add_garbage
Engine::GarbageService::add_solid_garbage
Engine::GarbageService::add_buffer
Engine::GarbageService::offset_buffer
Engine::GarbageService::update_buffer
```

These functions accept `GameContext + PlayerSessionState` explicitly.

## BOT conversion happens at the boundary

The public BOT dispatcher accepts normal session state:

```cpp
Bot::getpos(context, playerSession, ...)
```

Inside `src/legacy/bot.cpp` only, a local `Board` facade is bound to that
`PlayerSessionState` before entering a protected strategy implementation. The
eight strategy headers remain byte-for-byte unchanged.

This means the compatibility direction is one-way:

```text
normal PlayerSessionState
        |
        v
legacy Board facade
        |
        v
protected BOT strategy
```

Normal code never converts back into a Board object.

## Board-free normal modules

A source audit reports zero `Board` type references in:

```text
src/app
src/engine
src/replay
src/render
src/network
src/runtime
include/tetr/{engine,replay,render,network,runtime}
```

The only production users of `legacy/board_facade.h` are:

```text
src/legacy/board_facade.cpp
src/legacy/bot.cpp
```

The only non-production user is `legacy_board_facade_test`.

## Test migration

Tests that are intended to validate normal gameplay now use
`PlayerSessionState` directly. `legacy_board_facade_test` is deliberately the
single test that instantiates `Board` and checks that every historical alias,
including `garbage.buf`, points at the correct session-owned state.

`board_state_codec_test`, `render_layer_test`, `garbage_context_test`,
`piece_queue_equivalence_test` and `game_context_test` now exercise the same
APIs as production code instead of pulling the facade back into the test link
graph.

## Validation

Final verification is performed in clean Debug and Release builds. The Release
suite explicitly uses `-UNDEBUG`, so assert-based randomized equivalence checks
remain active.

Additional structural checks require:

```text
0 Board:: symbols in libtetr_runtime.a
Board:: symbols confined to libtetr_bot_legacy.a
0 Board type references in normal app/engine/replay/render/network/runtime
73 / 73 non-BOT public headers self-contained
0 non-BOT -Wall -Wextra -Wpedantic warnings
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 18
```

## Remaining legacy surface

After Stage 19, `Board` is no longer a runtime architecture concern. The
remaining intentionally legacy-shaped area is the protected BOT strategy
implementation itself and the small adapter that presents modern session state
to it.

A future BOT-specific refactor could define a first-class `BotView` and port
strategies one by one, but that should be a separate project because it would
modify the protected strategy source rather than merely adapting its boundary.
