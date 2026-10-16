# Stage 24: Serialization boundary cleanup

Stage 24 moves historical Replay, multiplayer and garbage primitive formats to
explicit codec/boundary APIs. No disk or network format is changed.

The goal is not to invent a new protocol. It is to make normal gameplay stop
carrying serialized representation details after those details have crossed the
boundary.

## Replay: decode once

Before Stage 24, `ReplaySessionState::operations` stored:

```cpp
std::queue<std::vector<std::string>>
```

Every consumer therefore had to remember the historical layout:

```text
[0] operation name
[1] elapsed timestamp
[2...] operation arguments
```

Placement, line-clear playback and piece movement repeatedly parsed those
strings and used expressions such as `op[0]`, `op[2]` and `op[6]`.

Stage 24 adds:

```text
replay/operation.h
replay/operation_codec.h/.cpp
```

and stores decoded values instead:

```cpp
struct ReplayEvent::Operation {
    std::string name;
    Runtime::Duration timestamp;
    std::vector<std::string> arguments;
};
```

`Init` still owns replay configuration records, but the gameplay portion is now
passed once through `decode_operation_stream()`. Replay speed scaling is also
performed there. The session queue then contains `Operation`, not wire tokens.

Numeric argument compatibility parsing is centralized in:

```text
ReplayEvent::int_argument
ReplayEvent::double_argument
```

Movement parsing, gameplay application, line-clear markers and post-lock Replay
synchronization no longer depend on serialized column offsets.

Recording crosses the inverse boundary through `encode_operation_base()`, so
the `.seconds()` conversion for Replay timestamps lives in the codec rather
than in normal Replay orchestration.

The historical text remains:

```text
{operation,elapsed_seconds,arg0,arg1,...}
```

including the old `std::to_string(double)` formatting.

## Multiplayer attack frames

Outgoing garbage attacks were previously assembled in Engine code with:

```cpp
std::to_string(sendId) + ";" +
std::to_string(receiveId) + ";" +
std::to_string(attack)
```

That field order is subtle: the first field is the sender's sequence and becomes
the receiver's `receiveId`; the second acknowledges the peer's sent sequence.

Stage 24 adds `Network::OutgoingAttackFrame` and
`Network::encode_attack_frame()`. Engine code no longer knows the punctuation or
wire field order. `parse_frame_packet()` remains the receiving codec, and the
new serialization regression test locks the sender/receiver perspective swap.

The multiplayer wire format is unchanged.

## Garbage packet time boundary

`Rules::GarbagePacket` keeps the historical fields:

```text
cnt_backfire
 tim_backfire   (double seconds)
flag_hole
```

because protected BOT strategies access that exact shape. Stage 24 does not
change those files or the packet ABI-facing representation.

Normal Rules/Engine code, however, no longer passes raw `double now` and
`double delay` into garbage queue operations. The public APIs now accept:

```text
Runtime::TimePoint
Runtime::Duration
```

and `garbage_packet_time()` is the one compatibility conversion from the old
stored `tim_backfire` value.

This applies to packet creation, ready-garbage consumption and garbage-buffer
presentation timing. In non-BOT production code, direct `tim_backfire` access is
now confined to the compatibility field declaration/conversion itself.

## Pure board-state encoder

`Network::encode_board_state()` used to serialize a player and also update:

```text
player.presentation.blockRow
player.presentation.blockCol
player.presentation.blockRotation
```

Those writes do not participate in the serialized output. The actual
presentation cache is maintained while rendering received board states.

The encoder is now declared with:

```cpp
const Runtime::GameContext&
const Runtime::PlayerSessionState&
```

so serialization cannot mutate gameplay or presentation state. The existing
10,000-case board-state oracle test still checks byte-equivalent encoded output.

## Regression coverage

`serialization_boundary_test` adds focused checks for:

- `ReplaySessionState` owning `queue<ReplayEvent::Operation>`;
- one-shot Replay stream decoding and playback time scaling;
- typed Replay movement parsing from decoded operations;
- unchanged Replay timestamp text formatting;
- multiplayer attack-frame wire order and receive-side interpretation;
- legacy garbage packet primitive storage through typed time APIs.

Existing randomized tests continue to cover:

- 10,000 board-state codec cases;
- 50,000 multiplayer ACK/attack cancellation cases;
- garbage queue/reference equivalence;
- Replay movement and line-clear timing;
- player input timing equivalence.

## Static boundary audit

After Stage 24:

```text
0 queue<vector<string>> Replay operation queues in normal production code
0 Replay gameplay accesses using op[N] / serialized timestamp columns
0 direct tim_backfire accesses in non-BOT production code outside the legacy
  field declaration + garbage_packet_time() conversion
0 multiplayer attack strings assembled in Engine code
```

The Replay/network/garbage external formats are intentionally unchanged.

## Validation

```text
Debug:   28 / 28 PASS
Release: 28 / 28 PASS (test targets explicitly compile with -UNDEBUG)
80 / 80 non-BOT public headers self-contained under -Wall -Wextra -Wpedantic
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 23
```

Stage 24 changes ownership of serialization knowledge, not protocol semantics.
