# Stage 26: Multiplayer frame boundary cleanup

Stage 26 removes raw multiplayer frame text from Engine code. The UDP/server
wire protocol is intentionally unchanged.

The runtime direction is now:

```text
Socket::UDP / raw datagram text
        |
        v
Network::transport
        |
        v
Network::multiplayer_channel
        |
        +--> decode_frame_packet()
        |       |
        |       +--> attack / round signal
        |       +--> decode_board_state() -> BoardStateSnapshot
        |
        v
FramePacket
        |
        v
frame_pump / Engine callbacks
```

Engine no longer interprets `"fail"`, `"end1"`, `"end2"`, the attack wire
field order, or raw opponent board-state strings.

## Typed transport availability

The historical socket layer uses the literal string `"fail"` to mean that a
non-blocking receive has no datagram available. That representation remains at
the low-level compatibility boundary, but `network/transport` now exposes:

```cpp
std::optional<std::string> Network::try_receive(...);
```

Typed runtime paths therefore represent "no packet" as `std::nullopt` rather
than a magic payload. Existing startup/configuration handshake code can still
use the legacy raw receive functions because that code itself is a protocol
boundary.

## Strict frame decoder

`parse_frame_packet()` has been replaced by:

```cpp
std::optional<Network::FramePacket> Network::decode_frame_packet(
    std::string_view text,
    int boardWidth,
    int visibleHeight);
```

The decoder owns the outer multiplayer wire grammar:

- `end1` -> `FramePacketType::Win`;
- `end2` -> `FramePacketType::OpponentLeft`;
- `!....` -> a fully decoded `BoardStateSnapshot`;
- `receiveId;sendId;attack` -> an attack packet.

Attack records now require exactly three non-negative decimal integer fields.
Malformed fields, extra separators and integer overflow return `nullopt`
instead of flowing through the historical character-arithmetic parser.

Board-state text is decoded before the packet reaches `frame_pump`; a
`FramePacket` carries `std::optional<BoardStateSnapshot>`, not a raw string.
`AppliedFramePacket` borrows the already-decoded snapshot by `const*`, avoiding
an extra full-Playfield copy on every remote state frame.

## Typed multiplayer channel

`network/multiplayer_channel.{h,cpp}` is the game-facing network adapter. It
provides typed operations for:

```text
send_attack_frame
send_round_signal
send_board_state_frame
try_receive_frame
acknowledge_round_complete
```

As a result, normal Engine sources no longer call `Network::send`,
`Network::receive` or `Network::drain` directly.

The legacy control tokens are kept inside Network:

```text
OutgoingRoundSignal::LocalLoss  -> "end1"
OutgoingRoundSignal::LeaveRound -> "end2"
round acknowledgement           -> "end"
```

The unusual `end1` direction is documented rather than renamed on the wire:
the local loser sends `end1`; the server forwards it to the opponent, who
interprets the received frame as a win.

## Frame pump

`FramePumpCallbacks` is now completely typed:

```cpp
std::function<std::optional<FramePacket>()> receive;
std::function<void(int)> addIncomingAttack;
std::function<void(const BoardStateSnapshot&)> applyOpponentState;
```

`frame_pump` contains no `std::string`, no `"fail"` sentinel handling and no
codec calls. It only applies already-decoded packet semantics.

## Engine boundary after Stage 26

The multiplayer portion of `Engine::run_frame()` now wires together typed
components:

```text
try_receive_frame()
      |
      v
FramePacket
      |
      v
pump_multiplayer_frame()
      |
      +--> GarbageService::add_buffer(int)
      +--> Render::apply_remote_state(BoardStateSnapshot)
      +--> Won / OpponentLeft signal
```

Outgoing Engine paths likewise call typed channel functions instead of
serializing or sending protocol strings themselves.

A static source audit after Stage 26 finds:

```text
0 direct Network::send/receive/drain calls in src/engine + include/tetr/engine
0 "end1" / "end2" / "fail" protocol tokens in Engine
0 std::string fields/callbacks in frame_pump
0 raw opponent board-state strings in FramePacket/AppliedFramePacket
```

`server.cpp` intentionally remains a raw protocol relay. It is the network
protocol boundary and does not depend on gameplay domain types.

## Compatibility

No peer/server migration is required. The following wire records remain byte
compatible:

```text
end1
end2
end
<sender-sequence>;<acknowledged-sequence>;<attack>
!<board-state sections...>
```

Stage 25's board-state encoder/decoder format is unchanged.

## Regression coverage

Stage 26 adds `multiplayer_frame_boundary_test`, covering:

- typed non-blocking transport return type;
- round-signal wire compatibility;
- attack encode/decode field-order compatibility;
- strict rejection of malformed attack packets and overflow;
- board-state decode into `BoardStateSnapshot` before frame-pump entry;
- typed frame-pump callback signatures;
- typed attack/state/terminal packet ordering.

The existing 50,000-case frame/attack equivalence test and 10,000-case
board-state encode/decode tests remain enabled.

## Validation

```text
Debug:   30 / 30 PASS
Release: 30 / 30 PASS (test targets compile with -UNDEBUG)
81 / 81 non-BOT public headers self-contained under -Wall -Wextra -Wpedantic
non-BOT Release build: 0 warnings under -Wall -Wextra -Wpedantic
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 25
```

Stage 26 changes protocol ownership and validation, not valid multiplayer wire
semantics.
