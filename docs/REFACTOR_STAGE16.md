# Stage 16: per-player session services and Board state reduction

Stage 16 continues the ownership cleanup started by `GameContext` and the
renderer split. The remaining historical `Board` aggregate still owned several
objects that are not board/gameplay state at all: piece queues and their RNG,
garbage generation state/RNG, multiplayer acknowledgement bookkeeping,
periodic timers and terminal layout state.

This stage moves those objects into an explicit per-player session sidecar while
keeping the protected BOT strategy headers unchanged.

## New per-player session ownership

New public state definitions live in:

```text
include/tetr/runtime/player_session_state.h
```

A `PlayerSessionState` contains five explicit components:

```text
PieceQueueState
  bag queue
  NEXT preview
  allowed piece set
  bag count / pieces-per-bag
  Fool recent-type pool / bag-breather
  block RNG

GarbageSessionState
  garbage RNG
  hole order
  checkerboard state
  incoming packet queue
  rendered buffer-stage cache

MultiplayerSessionState
  pending sent attacks / ACK queue
  send id
  receive id

PlayerTimingState
  timed-garbage timer
  frame-update timer
  autoplay timer
  QP gravity timer
  multiplayer-sync timer

PlayerPresentationState
  terminal margin
  remote/current piece display position
  preview piece bounding size
  HOLD/NEXT panel layout heights
```

`SessionState` now owns one `PlayerSessionState` for the player and one for the
opponent, followed by the corresponding Boards:

```text
SessionState
  playerRuntime ------> player Board
  opponentRuntime ----> opponent Board
  QuickPlay state
  Replay state
  game start time
```

The Boards are constructed with non-owning bindings to their matching runtime
state. A standalone `Board` created by tests/tools receives a private sidecar,
so it remains usable without constructing a full `SessionState`.

## Board is smaller and more focused

The following members were removed from `Board`:

```text
bl / next_block / block_list
cnt_bag / block_per_bag / QP_fool_lasttype / QP_bagbreather
rd_block / rd_garbage
MT_send / MT_send_id / MT_receive_id
five periodic timer doubles
Margin / block_x / block_y / block_type
block_maxheight / block_maxwidth
next_height / hold_height
unused replay string
```

The remaining Board data is primarily:

```text
playfield dimensions + map
score / lines / attack / combo / B2B / win counters
HOLD piece
field garbage heights
per-board QP clear/history state
```

`Board` exposes explicit accessors to the sidecar components (`piece_queue()`,
`garbage_state()`, `multiplayer_state()`, `timing_state()`,
`presentation_state()`) instead of owning those values itself.

## BOT compatibility

The protected BOT implementations use one historical API directly:

```cpp
Board::Garbage* gbp = &board1.garbage;
std::deque<Board::Garbage::Buffer> bf = gbp->buf;
```

That surface is preserved. `Board::Garbage::buf` is now a reference view onto
`PlayerSessionState::garbage.buffer`; the queue itself is session-owned.

All eight strategy headers are byte-for-byte unchanged from Stage 15.

The BOT translation unit now includes `timer.h` explicitly because it had
previously obtained the timer declaration accidentally through `board.h`.
This fixes an include-order dependency without touching BOT code.

## Piece queue service

Stage 16 introduces:

```text
include/tetr/engine/piece_queue.h
src/engine/piece_queue.cpp
```

It owns bag generation and preview coordination:

```cpp
Engine::add_bag(context, board);
Engine::prepare_next_preview(context, board);
Engine::current_piece(board);
Engine::consume_current_piece(board);
Engine::prepend_piece(board, type);
Engine::preview(board);
```

The historical Random/Fool bag algorithm was transferred without replacing its
shuffle routine or changing RNG call order.

`piece_queue_equivalence_test` executes 30,000 randomized states, performs up
to eight bag additions per state and compares:

```text
complete bag contents
bag count
Fool recent-type pool
QP bag-breather
next RNG output
```

against an oracle copied from the Stage 15 algorithm.

## Board lifecycle service

Board lifecycle operations no longer live as Board member functions. New entry
points are:

```text
include/tetr/engine/board_lifecycle.h
src/engine/board_lifecycle.cpp
src/engine/board_qp_init.cpp
```

```cpp
Engine::initialize_board(context, board, initializeQueue);
Engine::initialize_board_timers(context, board);
Engine::initialize_quick_play(context, board);
```

This removes configuration/time/renderer responsibilities from the state
object and makes the lifecycle dependency on `GameContext` explicit.

## Garbage and multiplayer state

The existing `Board::Garbage` methods remain as a legacy adapter because BOT
code names that type directly. Their mutable storage is no longer in Board:

```text
buf                 -> GarbageSessionState::buffer
hole order          -> GarbageSessionState::lastHole
checkerboard flag   -> GarbageSessionState::chessboardFlag
buffer render cache -> GarbageSessionState::display
garbage RNG         -> GarbageSessionState::random
```

Outgoing multiplayer ACK bookkeeping similarly moved to
`MultiplayerSessionState`. Frame pumping and garbage attack sending now read the
explicit component instead of `Board::MT_*` fields.

## Presentation/timing state

The renderer still receives `Board&`, but panel layout and remote-piece display
cache now come from `board.presentation_state()`. This keeps terminal-only
layout values out of gameplay state without changing renderer output.

Movement/Replay/Frame controllers likewise read `board.timing_state()` for
periodic scheduling.

## New ownership test

`player_session_state_test` verifies:

```text
SessionState player/opponent Boards bind to the correct sidecars
Board::Garbage::buf aliases the session garbage packet deque
piece/multiplayer/timer/presentation mutations are isolated per player
standalone Boards own independent private sidecars
```

## Verification

- Debug: 20/20 tests passed;
- Release: 20/20 tests passed;
- Release tests still compile with `-UNDEBUG` after `-DNDEBUG`;
- `piece_queue_equivalence_test`: 30,000 randomized equivalence cases;
- 72/72 non-BOT public headers compile independently as the first include;
- full `-Wall -Wextra -Wpedantic` build: zero non-BOT warnings;
- the only 18 warnings remain inside protected BOT implementation headers;
- 8/8 BOT strategy headers are byte-for-byte unchanged from Stage 15.

## Remaining work

Board is now much closer to a pure player/field state. The largest remaining
legacy surfaces are:

```text
Board::Garbage method namespace (kept for BOT type compatibility)
BoardStatic::* default-context references used by a small amount of BOT code
Config::* default-context compatibility aliases
Function::send/receive legacy wrappers
BOT implementation headers themselves
```

A useful next step is to introduce a first-class `PlayerState`/`PlayfieldState`
for the remaining Board data and make the non-BOT engine operate on that type,
leaving `Board` as a narrowly scoped compatibility facade for protected BOT
code. That can be done incrementally without changing the BOT strategies.
