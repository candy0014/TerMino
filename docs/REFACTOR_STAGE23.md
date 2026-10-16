# Stage 23: Typed time semantics

Stage 23 removes the remaining ambiguity between absolute monotonic timestamps
and elapsed intervals in normal gameplay code. Both still use `double` seconds
internally, so thresholds, replay values and timing precision remain compatible;
the difference is now enforced by the C++ type system.

## Domain time types

`include/tetr/runtime/time.h` introduces two small value types:

```text
Runtime::TimePoint  absolute point on the process monotonic clock
Runtime::Duration   elapsed time / delay / interval in seconds
```

They intentionally have no implicit conversion from `double`.

```text
TimePoint - TimePoint -> Duration
TimePoint + Duration  -> TimePoint
TimePoint - Duration  -> TimePoint
Duration +/- Duration -> Duration
Duration * scalar     -> Duration
```

There is deliberately no `TimePoint + TimePoint`, no `Duration + double`, and
no implicit assignment from a raw numeric value. This makes mistakes such as
passing a DAS interval where a lock timestamp is expected fail at compile time.

`Timer::get()` remains the low-level system adapter and still returns seconds as
a `double`. `GameContext::now()` is the conversion boundary and returns
`Runtime::TimePoint`.

## Runtime ownership

The session-owned absolute timers are now `TimePoint` values:

- `SessionState::gameStartTime`
- `PlayerTimingState::{timedGarbage, frameUpdate, autoplay, qpGravity, multiplayerSync}`
- `MovementState::lockTimer`
- `MovementInputState` per-key press timestamps

DAS, ARR, gravity delay, EPLD, line-clear delay, playback step delay and periodic
frame/sync intervals enter Engine APIs as `Duration` values.

Configuration structs intentionally keep their historical numeric/file format.
The conversion happens at the Engine boundary with
`Duration::from_seconds(...)`, so `user_config.txt` is unchanged.

## Replay semantics

Replay movement timestamps were historically elapsed seconds since game start,
not process-clock timestamps. They are now represented as `Runtime::Duration`:

```text
text replay timestamp -> Duration -> event_due(...)
```

Recording performs the inverse conversion only at serialization:

```text
context.now() - gameStartTime -> Duration -> .seconds() -> replay text
```

No Replay file format changed.

## Compatibility boundaries

Some old formats intentionally stay numeric:

- `Timer::get()` at the system-clock adapter.
- Runtime configuration file values.
- Replay text serialization.
- Multiplayer / garbage packet fields whose wire or BOT-facing representation
  is historically `double`.

Normal code converts immediately when crossing those boundaries. In particular,
`Rules::GarbagePacket::tim_backfire` remains a legacy numeric field because the
protected BOT strategies consume that packet shape.

## Presentation and periodic timing

Line-clear animation, low-PPS BOT movement playback, countdown, Blitz timeout,
Quick Play gravity ticks, frame updates, timed garbage and multiplayer sync now
state whether each argument is a `TimePoint` or a `Duration`.

Frame delta remains numerically identical when entering legacy pure-rule
formulas: `now - frameUpdate` produces a `Duration`, and `.seconds()` is used
only at the existing rules boundary.

## Regression coverage

`time_semantics_test` adds compile-time and runtime checks for:

- no implicit `double -> TimePoint/Duration` conversion;
- arithmetic result types;
- periodic-event strict threshold behavior;
- Blitz timeout behavior;
- Replay timestamps as elapsed durations;
- movement-script absolute-start plus duration scheduling.

The randomized `player_input_equivalence_test` still passes after converting
DAS/ARR/gravity and input timestamps, providing old-vs-new coverage for the most
sensitive repeat timing path.

## Validation

```text
Debug:   27 / 27 PASS
Release: 27 / 27 PASS (tests built with -UNDEBUG)
78 / 78 non-BOT public headers self-contained under -Wall -Wextra -Wpedantic
0 raw double fields named lockTimer/gameStartTime/timedGarbage/frameUpdate/
  autoplay/qpGravity/multiplayerSync in production state
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 22
```

Stage 23 changes type boundaries only. Timing constants, comparison operators,
floating-point representation, Replay text and network packet formats are not
retuned.
