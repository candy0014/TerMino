# Stage 17: PlayerState / PlayfieldState and Board legacy facade

Stage 17 moves the remaining simulation-owned data out of `Board` and into a
first-class per-player gameplay state. The goal is not to delete `Board`
immediately: protected BOT implementations still name its historical public
fields. Instead, `PlayerSessionState` becomes the real owner and `Board`
provides reference aliases only for legacy callers.

## First-class gameplay state

New public state lives in:

```text
include/tetr/runtime/player_state.h
```

It is split by responsibility:

```text
PlayerState
├── PlayfieldState
│   ├── width / visibleHeight / activeHeight
│   ├── nextCount / holdEnabled
│   ├── garbageHeight / solidGarbageHeight
│   └── field
├── ScoringState
│   ├── placedBlocks / totalAttack
│   ├── totalLines / totalGarbageLines
│   ├── score / wins
│   └── combo / b2b
├── HoldState
│   └── piece
└── QuickPlayPlacementState
    ├── blighted
    ├── lastOperation
    └── operations
```

`PlayerSessionState` now owns this `gameplay` object alongside the services
introduced in Stage 16:

```text
PlayerSessionState
├── gameplay : PlayerState
├── pieces
├── garbage
├── multiplayer
├── timing
└── presentation
```

A `SessionState` therefore owns the complete state of both players. The two
`Board` objects are only non-owning facades bound to those player slots.
Standalone Boards used by tests/tools still allocate a private
`PlayerSessionState`, so their historical construction semantics remain usable.

## Board facade compatibility

The protected BOT implementations directly use historical names such as:

```cpp
board1.board
board1.mapWidth
board1.score
board1.combo
board1.b2b
board1.now_hold
```

Those names are preserved as references. For example:

```text
board.mapWidth       -> board.state().playfield.width
board.board          -> board.state().playfield.field
board.score          -> board.state().scoring.score
board.now_hold       -> board.state().hold.piece
board.QP_last_op     -> board.state().quickPlay.lastOperation
```

The storage is no longer in `Board`. `player_state_facade_test` verifies the
actual addresses and bidirectional mutation behavior, not merely equal values.

`Board::Garbage` remains the one service-like compatibility member because BOT
code names that nested type and its `buf` member directly. Its mutable packet
storage remains owned by `PlayerSessionState::garbage` as in Stage 16.

## Engine uses PlayerState directly

The primary APIs of the following modules now accept `PlayerState` or
`PlayerSessionState` rather than depending on Board field layout:

```text
engine/placement_board_adapter
engine/frame_board_adapter
engine/board_lifecycle
engine/piece_queue
network/board_state_codec
render/board_renderer
render/piece_renderer
```

The main Engine implementation was migrated from legacy field names to
`board.state()` / `board.player_state()` and then audited. Outside the Board
facade implementation itself, `src/app`, `src/engine`, `src/replay`,
`src/render` and `src/network` contain zero accesses of historical fields such
as `board.score`, `board.cnt_line`, `board.mapHeight`, `board.board`, etc.

The remaining `Board&` parameters in some App/Replay coordinators are therefore
compatibility handles, not state ownership boundaries.

## Renderer no longer depends on Board

Stage 15 separated presentation from Board methods. Stage 17 takes the next
step: the render APIs now receive `PlayerSessionState` directly.

```cpp
Render::print_border(context, playerSession);
Render::draw_piece(context, piece, playerSession, ...);
Render::draw_hold(context, piece, playerSession, ...);
Render::draw_next(context, piece, playerSession, ...);
```

`src/render` and `include/tetr/render` no longer include or name `Board`.
Renderer layout comes from `PlayerPresentationState`; gameplay values come from
`PlayerState`; preview data comes from `PieceQueueState`.

Existing renderer byte-level expectations in `render_layer_test` continue to
pass.

## Multiplayer codec no longer depends on Board

`network/board_state_codec` now serializes `PlayerSessionState` directly. The
historical wire format is unchanged. The randomized codec regression still
compares 10,000 generated states against the pre-refactor serialization
algorithm.

This is useful structurally: multiplayer serialization no longer needs a Board
facade simply to reach gameplay/presentation/preview state.

## Replay and App paths

The remaining direct legacy field accesses in Init, Place, Replay playback,
Replay save interaction and countdown were replaced with explicit nested state
accesses. The main non-BOT source tree now has no `board.<legacy-field>` style
accesses.

The old `Function::replay_base()` compatibility helper also stopped reading
`BoardStatic::gameStartTime`; it now resolves the default `GameContext` and uses
`context.session.gameStartTime`. `BoardStatic::*` therefore remains only as a
default-context compatibility surface.

## New regression test

`player_state_facade_test` verifies:

```text
all legacy Board references alias the corresponding PlayerState members
mutating through Board is visible through PlayerState
mutating PlayerState is visible through Board
player/opponent gameplay state is isolated
standalone Boards own independent private state
placement adapter Board overload and PlayerState overload are equivalent
```

## Verification

- Debug: 21/21 tests pass;
- Release: 21/21 tests pass with assertions forced on via `-UNDEBUG`;
- 73/73 non-BOT public headers compile independently as the first include;
- clean `-Wall -Wextra -Wpedantic` audit has zero non-BOT warnings;
- the historical 18 BOT warnings are left untouched;
- 8/8 protected BOT strategy headers are byte-for-byte unchanged from Stage 16;
- render and board-state codec regression tests remain green;
- no historical Board gameplay field access remains in non-BOT App/Engine/
  Replay/Render/Network implementation code.

## Remaining compatibility shell

The main legacy surfaces are now deliberately concentrated:

```text
Board facade references used by protected BOT code
Board::Garbage nested adapter required by BOT type compatibility
board1 / board2 default-context aliases
BoardStatic::* default-context aliases
Config::* default-context aliases
Function::send/receive and Function::add_replay compatibility helpers
BOT implementation headers
```

The next useful step is no longer moving gameplay data out of Board: that is
complete. A follow-up can introduce a dedicated BOT compatibility adapter and
start shrinking the process-wide `Config::*`, `BoardStatic::*`, `board1/board2`
and `Function::*` legacy namespaces without changing strategy code.
