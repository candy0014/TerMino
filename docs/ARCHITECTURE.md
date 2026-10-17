# Current architecture after Stage 113

The dual-frontend migration now adds a platform-independent `tetr_presentation`
library, a separate `tetr_presentation_context` timeline adapter and a semantic
`Frontend::TextInputSession` interface. The original terminal client remains the
complete product; no OpenGL executable exists at this migration checkpoint.
The implementation and remaining migration work are tracked in
[FRONTEND_MIGRATION.md](FRONTEND_MIGRATION.md).

`tetr_presentation` captures value-owned board views and owns the visibility
rules consumed by terminal rendering. It depends only on Runtime; the context
adapter obtains replay/live/remote/completed time without depending on terminal
rendering. Terminal garbage diff rendering already consumes the shared board
view. `ApplicationState` stores a frontend text-input interface, implemented by
`TerminalSession`, rather than a concrete terminal-session pointer.

Settings metadata is publicly declared in `include/tetr/app/settings_model.h`.
It still includes mutable settings references for the existing synchronous menu;
a future graphical thread must consume an immutable projection and submit typed
commands, never retain these references across threads. Terminal layout/paint
caches and blocking menu/game loops remain migration work.

Headless foundation builds disable `TETR_BUILD_TERMINAL` and
`TETR_BUILD_SERVER`, require neither terminal input nor graphics/network
dependencies, and exercise core geometry, rules, view snapshots and text-entry
lifecycle. A server-only configuration also avoids client/BOT targets.

The normal game is organized around explicit state ownership, one-way library
boundaries and typed domain values. Stage 113 finishes the current non-BOT
architecture pass: Engine is split into core/presentation/integration/gameplay
libraries, long-lived waits no longer hot-spin, terminal text-input mode is owned by
the active `TerminalSession`, and historical encoding codecs are split by domain.
The four surviving BOT algorithms remain separate `.cpp` translation units behind a
typed Runtime/Piece boundary.

## Dependency direction

The intended dependency direction is:

```text
                         executable / composition root
                                  |
                                  v
                           +--------------+
                           |   tetr_app   |
                           +--------------+
                                  |
                                  v
                           +--------------+
                           | tetr_gameplay|
                           +--------------+
                             |          |
                             v          v
                 +----------------+   +---------------------+
                 | engine         |   | engine integration  |
                 | presentation   |   +---------------------+
                 +----------------+      |    |      |    |
                         |               v    v      v    v
                         v            network replay render input
                 +----------------+
                 | engine core    |
                 +----------------+
                     |       |
                     v       v
                   rules   runtime
                              |
                              v
                             util
```

`tetr_engine` remains as an `INTERFACE` compatibility umbrella only; it owns no
sources. `tetr_engine_core` is guarded from including Render, Network, Replay or
Input, while `tetr_engine_presentation` may render but may not own Network/Replay
side effects. `architecture_boundary` checks these rules in CTest so later changes
cannot silently rebuild the old monolithic dependency graph.

## Composition root

`src/tetr.cpp` explicitly constructs the process/game objects:

```text
Runtime::SessionState
Runtime::RuntimeServices
Runtime::GameContext
Input::Services
UserConfig::Settings
Network::NetworkServices
Replay::ReplaySessionState
Legacy::BotStrategy
Render::TerminalSession
```

Network and Replay state are attached to the current `GameContext` by the
composition root. There is no `default_game_context()`,
`default_session_state()` or `default_runtime_services()` singleton/fallback.
Code that needs Network or Replay state fails explicitly when the corresponding
component was not injected instead of borrowing hidden global state.

## Runtime layer

Runtime is the bottom gameplay state layer. It owns no terminal, socket, Replay,
Rules, Engine, App or legacy implementation.

### RuntimeServices / injected services

`RuntimeServices` now contains only simulation-local process services:

```text
MonotonicClock
piece/general RNG
```

Platform input is a separate `Input::Services` component (`BindingSet` plus the
platform keyboard monitor), while `UserConfig::Settings` is owned by the
composition root. `GameContext` references those components explicitly when an
App/Engine path needs them. UDP transport is owned by `NetworkServices`; Replay
operations are owned by `ReplaySessionState`.

### RuntimeConfig

Configuration is normalized once from the historical text-oriented
`UserConfig::Settings` representation. The running configuration uses semantic
names and typed time values:

```text
ModeConfig
HandlingConfig
FieldConfig
GarbageConfig
QuickPlayConfig
AutoplayConfig
DisplayConfig
```

`Duration` is used for DAS, ARR, lock delay, line-clear delay, garbage generation
interval, incoming garbage delay and Blitz time. Natural gravity uses
`GravityInterval`, which explicitly distinguishes disabled, instant and periodic
gravity. Historical `double` seconds remain only at text/Replay/BOT boundaries.

The historical signed `HoleNum` convention is represented internally as
`GarbageHolePattern`: scattered-hole count and contiguous-hole run length are
distinct semantic states. The signed integer is recreated only while reading or
writing compatible config/Replay data.

### SessionState / PlayerSessionState

`SessionState` owns the two gameplay players and session-wide Quick Play/game
state. Replay state is intentionally not stored here.

Each `PlayerSessionState` owns:

```text
PlayerState
  PlayfieldState
  ScoringState
  HoldState
  QuickPlayPlacementState

PieceQueueState
GarbageSessionState
MultiplayerSessionState
PlayerTimingState
PlayerPresentationState
```

Bag entries use `QueuedPiece`, pending multiplayer attacks use `PendingAttack`,
and multiplayer sequence numbers use `MultiplayerSequenceId`; positional
`pair<int,int>`/bare sequence integers are not part of normal runtime state.
`PieceQueueState::nextPreview` contains only visible NEXT pieces. The current
piece is the front of the bag and is never hidden in preview slot zero.

Quick Play placement history is semantic state: `lastOperation` is an optional
`QuickPlayClearOperation`, while Extended All-Spin history stores normalized
`ExtendedAllSpinSignature` values. Historical packed integers and the `8191`
sentinel are not stored in Runtime state.

`GarbageSessionState::holeOrder` is sized to the actual field width. The garbage
buffer display cache lives in `PlayerPresentationState::garbageBufferDisplay`,
not in gameplay garbage state.

## Strong gameplay value types

Normal gameplay uses explicit domain values rather than interchangeable
primitives:

```text
Piece::Type
Runtime::Rotation
Runtime::CellPosition
Piece::Pose
Piece::KickIndex
Runtime::RotationRecord
Input::Action
Engine::KeyPhase
Runtime::TimePoint
Runtime::Duration
Runtime::GravityInterval
Input::BindingSet
Runtime::GarbageHolePattern
Runtime::BoardCell
Runtime::QuickPlayClearOperation
Runtime::ExtendedAllSpinSignature
Runtime::MultiplayerSequenceId
```

Legacy integers/strings/doubles are accepted or emitted only by explicit compatibility codecs/adapters. Production code includes narrow `compat/*_encoding.h` headers for the specific domain it serializes; `legacy_encoding.h` is only a compatibility umbrella for tests/external legacy callers. Runtime, Rules, Engine and Render are guarded from depending on compatibility codecs.

## Piece and Playfield boundaries

`Piece::Definition` is a normal Runtime value with encapsulated geometry. It does not
expose historical `ty/col/shape/kicks` storage or integer placement overloads.
Integer piece ids cross compatibility boundaries through
`Compat::piece_type_from_legacy`; `Piece::Type` itself keeps only domain/index
semantics.

`Runtime::Playfield` stores `Runtime::BoardCell` rather than historical
`-1 / 7 / 8 / 9 / 100+` integers. Empty, piece, garbage, solid and timed states are
explicit. Network/Replay codecs recreate the historical encodings only at their
format boundaries.

Stage 106 removed the old `map`, Board façade and PPP projection; Stage 107 removed
`Block::block`. BOT dispatch now passes the same `Runtime::Playfield`,
`PlayerSessionState` and `Piece::Definition` family used by normal gameplay. The BOT
strategies may still use private integer poses/bitboards internally, but there is no
second public geometry or board model.

## Compatibility encoding boundary

Domain types do not expose `legacy()` / `from_legacy()` / `legacy_seconds()` methods.
Historical conversions are explicit and grouped by domain:

```text
compat/piece_encoding.h
compat/board_encoding.h
compat/gravity_encoding.h
compat/garbage_encoding.h
compat/config_encoding.h
```

`compat/legacy_encoding.h` includes those headers only as an umbrella for regression
tests and external legacy callers. Production translation units include the narrow
codec they need. Runtime, Rules, Engine and Render are guarded from including any
Compat header.

## Rules layer

Rules contains deterministic gameplay transformations such as attack, spin,
post-lock resolution, QP2 and garbage generation. It consumes Runtime value/state
types but does not own presentation, Replay or transport behavior.

`Runtime::GarbagePacket` is now a semantic object with private storage:

```text
line_count()
arrival_time()
preserve_hole()
consume_lines()
mark_preserve_hole()
```

The old `cnt_backfire / tim_backfire / flag_hole` layout is absent from Runtime
state. BOT compatibility receives only the explicit scalar/environment snapshot it
needs at dispatch time.

Garbage row generation uses `GarbageHolePattern` instead of branching on the
sign of an integer.

## Engine layer

Stage 110 replaces the former monolithic `tetr_engine` static library with four
explicit layers:

```text
tetr_engine_core
  field mutation / garbage field
  piece queue generation
  movement / placement algorithms
  board/frame adapters
  locked-piece resolution

tetr_engine_presentation
  movement / line-clear presentation state machines
  garbage and NEXT rendering helpers
  countdown / game-end / placement presentation

tetr_engine_integration
  Replay adapters
  multiplayer frame/state adapters
  garbage/network/replay side effects
  autoplay playback adapter

tetr_gameplay
  live-piece orchestration
  player input coordination
  piece spawn / placement coordinator
  game loop / line-clear phase
```

The old `tetr_engine` target is an `INTERFACE` umbrella so existing callers can
migrate gradually without reintroducing a source-owning monolith. Core source files
cannot include Render, Network, Replay or Input. Presentation may use Render/Input
but cannot own Network or Replay effects.

`PieceQueueState` generation is core-only. NEXT preview drawing moved to a dedicated
presentation helper, removing the former Render dependency from `piece_queue.cpp`.

Engine still depends only on the abstract `Autoplay::Strategy`; `Legacy::BotStrategy`
is injected at `src/tetr.cpp`, and no Engine library links `tetr_bot_legacy`.

## Replay boundary

Replay is split into a standalone core and higher-level adapters.

```text
historical text {name,time,args...}
              |
              v
       operation_codec
              |
              v
       Replay::Operation
       + OperationKind
              |
              +--> movement_event
              +--> gameplay_payload
              +--> line_clear_event
```

Normal Engine/App code does not interpret positional Replay arguments.
`gameplay_payload` exposes named payloads such as `AddGarbagePayload` and
`QuickPlayPlacementPayload`; positional `int_argument/double_argument` helpers
are private to the Replay implementation.

The namespace is `Replay`, not the historical `ReplayEvent`. Historical
single-player event names, configuration keys and on-disk syntax remain
unchanged. A Local Versus replay uses a versioned, length-delimited envelope
around two complete legacy-compatible streams. Each `GameContext` decodes its
own queue and both are played concurrently through the existing two-board
Local Versus composition.

`ReplaySessionState` is an explicit component attached to the active
`GameContext`, not a field of Runtime `SessionState`.

## Network boundary

`NetworkServices` owns `UdpSocket` and server configuration. Runtime does not own
or include socket code.

UDP receive has three explicit modes: `try_receive()` for polling/draining,
`receive_for()` for bounded waits, and `receive_blocking()` for protocol phases that
must wait for the next datagram. The historical `"fail"` data sentinel is gone and
`UdpException` participates in the standard exception hierarchy. Lobby seed/setup
waits use blocking receive, while the cancellable lobby UI uses a short timed wait so
Settings input remains responsive without burning a CPU core.

Game-facing traffic uses typed protocol boundaries:

```text
Lobby protocol/channel
FramePacket
OutgoingAttackFrame
BoardStateSnapshot { currentPiece, nextPreview, optional placement, ... }
multiplayer_channel
```

Board-state sending accepts an optional `RemotePlacementSnapshot`; the old
seven positional placement values and their `-1` sentinels do not appear in
the public API. The codec alone recreates the historical `[current,next...]`
piece list on the wire.

The relay server is a `RelayServer` object instead of a collection of file-level
mutable globals. Lobby/round acknowledgement counts distinct players rather than
raw packet count. Historical wire strings remain inside protocol codecs only.

## Render boundary

`TerminalRenderer` owns ANSI/console primitives. Board rendering has been split
into layout, stats and event presentation implementations. Render has no Network
include and does not mutate remote gameplay state.

`TerminalSession` owns full-screen lifecycle and canonical text-input mode as
instance methods. `ApplicationState` carries a non-owning handle to the active
session for Settings/replay-save text entry. Linux emergency signal recovery keeps
only a small POD termios snapshot required by the signal handler; there is no mutable
process-global `TerminalSessionState*`.

## App boundary

App owns user-facing orchestration:

```text
Game / Place
Init
  -> init_replay
  -> init_multiplayer
  -> init_player_setup
Settings
  -> settings_model
  -> settings_navigation
  -> settings_input
Replay playback / post-game interaction
```

Settings consistently uses namespace `App::Settings`. Initialization uses an
explicit reconfiguration loop rather than recursively calling itself after a
Settings transition.

## BOT / legacy compatibility boundary

The project currently has three standalone legacy BOTs plus five native
`tetris_ai_runner` choices. Their public headers are declaration-only and
the algorithms are compiled as separate translation units:

```text
include/tetr/bot/bot_while1.h       -> src/bot/bot_while1.cpp
include/tetr/bot/bot_candy0014.h    -> src/bot/bot_candy0014.cpp
include/tetr/bot/bot_mech.h         -> src/bot/bot_mech.cpp
include/tetr/bot/bot_zzztoj.h       -> src/bot/bot_zzztoj.cpp + zzztoj_standalone/{src,upstream/src}
```

`bot_while1` contains the retained old `bot_while2` logic; `bot_mech` contains the
retained old `bot_mech_all` logic. The old `bot_while1`, `bot_while_1`,
`bot_while2-`, and `bot_mech` implementations were retired. Stage 107 removed the shared historical BOT environment. `src/legacy/bot.cpp`
constructs one explicit `Bot::Environment` snapshot per call and passes it through
the dispatch table; strategy translation units no longer read process-global
`Config`, `BoardStatic`, or timer state.

Runtime BOT IDs are contiguous and semantic:

```text
0 Disabled
1 bot_while1
2 bot_candy0014
3 bot_mech
4 zzztoj_botris
5 zzztoj_toj
6 zzztoj_dig
7 zzztoj_qq
8 zzztoj_c2
```

Settings presents those names as a choice list. Replay remains an external
compatibility boundary and deliberately maps the surviving strategies back to their
historical Replay IDs; IDs 5 and 6 are disabled. The five zzztoj choices use Replay IDs 7..11. The unmodified upstream engines, shared
adapter, and tetr SRS+ reachability layer build with the project's C++17
baseline. Search-quality value 300 maps to strength 1 / the upstream 50 ms
reference budget rather than imposing a literal depth or node count of 300.

Compatibility with the old algorithms is now intentionally narrow:

```text
legacy/bot                   # dispatch + explicit Environment/legacy scalar conversion
src/bot/bot_piece_access.h   # private integer-pose helpers over Piece::Definition
```

Stage 106 removed `map`, `legacy/board_facade` and `legacy/ppp_catalog`; Stage 107
removed `Block::block`, `block_compat`, the legacy field-dimension singleton and the
BOT config/timer globals. BOTs receive `Runtime::Playfield`, `PlayerSessionState`, and
an explicit `Bot::Environment` snapshot. Stage 117 also passes a sorted
`unseenBagPieces` set derived from the n-bag containing the preview endpoint; it
is empty for non-n-bag randomizers.  This metadata is computed from existing
`startsNewBag` queue markers without consuming RNG, and the four strategies do
not use it yet. Only the strategy-internal integer pose/bitboard representation
remains, isolated behind private free functions.

`Util::deterministic_shuffle` and the corresponding hole shuffle deliberately preserve
the historical RNG-consumption order instead of switching to `std::shuffle`.

## Configuration and external compatibility

The refactor intentionally preserves:

- `user_config.txt` keys and historical primitive representation;
- `keybinding.txt` format and platform key codes;
- Replay text format, event names and legacy configuration names;
- multiplayer UDP wire format;
- retained BOT strategy behavior while BOT data/geometry compatibility is progressively reduced;
- QP2 All-Spin extended mode remaining manually enabled by configuration.

Internal names/types are free to be semantic because conversion is centralized
at these boundaries.

## Regression guards

The test suite covers rules, state, geometry, randomized equivalence, input,
time, Replay serialization, Network protocol, rendering, initialization, BOT
compatibility and architecture boundaries. The architecture guard also rejects
public/private headers with the same relative path, preventing include-order
ambiguity such as the pre-Stage-84 duplicate RelayServer header.

The regression suite includes architecture guards for Engine layering, removed BOT
compatibility artifacts, typed state boundaries, narrow Compat usage and terminal
ownership. Stage 113 also adds a timed UDP wait regression so `receive_for()` cannot
silently collapse back into a hot `try_receive()` loop. BOT-specific tests continue
to pin the active ID/name table, Settings choice model, historical Replay mapping and
absence of retired implementation files.
