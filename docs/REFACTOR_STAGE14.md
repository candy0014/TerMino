# Stage 14: context-aware initialization, garbage and transport

Stage 14 continues the Stage 13 migration from compatibility globals to an
explicit `Runtime::GameContext&`.  The focus is the remaining high-level entry
points that still acted as global-state gateways: `Init`, Garbage, multiplayer
transport, Replay gameplay application, Board bag initialization, and Settings.

No BOT strategy file or Replay disk format is intentionally changed.

## Init now builds the explicit context first

`Init::init(Runtime::GameContext&)` is now the primary initialization entry
point.  The old parameterless `Init::init()` remains as a thin wrapper around
the default context.

Previously Init mutated approximately forty `Config::*` globals while parsing
UserConfig or Replay data, then Stage 13 captured those globals into
`context.config` at the end.  Stage 14 reverses that ownership:

```text
UserConfig / Replay
        |
        v
local InitState
        |
        +-- clamps / mode transforms / QP mod transforms
        |
        v
context.config
context.session.replay
context.services bindings/RNG
        |
        v
legacy Config bridge (default context only)
```

The local InitState deliberately keeps the historical Replay configuration
field names and serialization order, so existing Replay config prefixes remain
compatible.

Replay hash validation now reuses the already isolated legacy hash helper in
`replay/replay_hash` instead of duplicating the hashing loop inside Init.

A full `Runtime::publish_runtime_config()` bridge was added.  It publishes all
structured RuntimeConfig fields back to legacy `Config::*`, but only when the
passed context is the process default.  Separately constructed contexts remain
isolated.

## Board init / bag generation

`Board` gained context-aware overloads:

```cpp
void Board::init(Runtime::GameContext&, int op = 1);
void Board::add_bag(Runtime::GameContext&);
```

They now read Random/Fool/default-floor state from `context.config` and
`context.session.quickPlay` instead of `Random`, `QP_mod_fool` and
`BoardStatic::QP_floor`.

The legacy overloads remain and delegate to `Runtime::default_game_context()`.
The protected BOT source therefore remains source-compatible.

## Garbage adapter

All Board::Garbage operations now have explicit context overloads:

```cpp
add_garbage(context,...)
add_solid_garbage(context,...)
update_buffer(context)
add_buffer(context,...)
offset_buffer(context,...)
```

The main game/replay path uses those overloads.  Garbage generation and queue
logic now obtain the following from the passed context rather than legacy
Config/BoardStatic/timer globals:

- Replay mode and Replay recorder;
- Garbage model / Cheese model / messiness;
- HoleNum and QP Double Hole;
- GarbageMultiple and incoming delay;
- TimeMultiplier and current time;
- QP floor;
- multiplayer outgoing transport.

The old Garbage signatures remain as compatibility wrappers around the default
context.

## Multiplayer transport

New files:

```text
include/tetr/network/transport.h
src/network/transport.cpp
```

The explicit transport API accepts `RuntimeServices&` or `GameContext&`:

```text
Network::send
Network::receive
Network::receive_blocking
Network::receive_until
Network::drain
```

The context-aware App/Engine path no longer calls `Function::send/receive*`.
Those Function functions remain only as legacy wrappers and delegate to the
new transport using the default RuntimeServices.

The UDP wire protocol is unchanged.

## Replay gameplay application

`ReplayEvent::apply_gameplay_event()` gained a GameContext overload.  Hold Fool
rules and replayed Garbage operations now use the selected context.  The old
signature remains as a default-context wrapper.

## Settings

`Setting::setting(Runtime::GameContext&)` is now the primary UI entry.  The
settings screen uses:

- `context.services.clock` for key-repeat timing;
- `context.services.keys/customBindings` for active bindings;
- `context.config.display.obscuredColors` for current display mode.

Changing ObscuredColors publishes the new display state only when operating on
the default context.  Other UserConfig edits retain the historical behavior of
taking effect on the next Init pass.

## Compatibility bridge scope

The main App/Engine/Replay/Network path now contains no direct `Config::*` or
`BoardStatic::*` reads.  `Function::send/receive*` also has no remaining
non-legacy call site.

The large legacy declarations in `config.h` are deliberately retained because
Board rendering and the protected BOT implementations still consume them.
They continue to mirror the default GameContext.

## New regression coverage

Stage 14 adds:

```text
garbage_context_test
config_bridge_test
init_context_test
```

The tests verify, among other things:

- a custom context's Garbage multiplier/timing/Replay queue does not leak into
  default Config or Replay state;
- full RuntimeConfig publication covers every legacy field and round-trips
  through `capture_runtime_config()`;
- `Board::init(context)` and bag generation update only the selected QP
  session state;
- `Init::init(customContext)` applies legacy clamps and QP transforms without
  publishing into the default context;
- Quick Play combinations such as Nohold Reverse + Gravity Reverse + Volatile
  Reverse + Fool preserve the historical integer/truncation behavior.

## Verification

- Debug build: 16/16 tests passed;
- Release build: 16/16 tests passed;
- Release tests still contain `-UNDEBUG`, so assert-based regression checks run;
- 65/65 non-BOT public headers compile independently as first includes;
- whole-project `-Wall -Wextra -Wpedantic`: zero non-BOT warnings;
- `tetr_runtime` has no unresolved dependency on `Engine::*` or
  `Board::Garbage::*`;
- all 8 protected BOT strategy files are byte-for-byte identical to Stage 13.

## Remaining legacy islands

The most important remaining global-state users are now primarily presentation
and compatibility code:

- Board drawing helpers in `function.cpp` (`print_*`, `go`, border rendering);
- Block rendering helpers;
- the legacy Function Replay/network wrappers;
- the protected BOT implementation headers;
- `using namespace Config` in the old compatibility header.

A useful next step is to separate Board rendering from Board game state and
make presentation functions accept an explicit display/context object.  Once
that is done, a large portion of Config's display/gameplay compatibility
surface can be retired without touching BOT algorithms.
