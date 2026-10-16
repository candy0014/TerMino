# Stage 30: Multiplayer Lobby Protocol Boundary

Stage 26 moved the per-frame multiplayer protocol behind typed `FramePacket` / `multiplayer_channel`, but the connection/startup path was still a separate legacy island. `Init` and `Game` directly sent and compared `start1` through `start6`, `end_`, manually parsed the shared RNG seed, and constructed the opponent setup record by counting semicolon sections.

Stage 30 moves that startup protocol behind the same kind of explicit network boundary.

## 1. Typed lobby protocol

`network/lobby_protocol` now owns the historical startup wire tokens:

- server signals: `PairReady`, `ConfigExchangeReady`, `CountdownReady`;
- client signals: `ReadyForConfig`, `SetupReceived`, `CountdownComplete`, `Cancel`.

Only the codec knows that these map to `start1 ... start6` and `end_`. App code deals exclusively in the enum values.

The shared multiplayer seed also has a strict decimal codec. Invalid, negative, trailing-character and overflowing values are rejected instead of being interpreted by an ad-hoc digit loop.

## 2. Structured player setup exchange

The historical setup record

```text
height;width;nextCount;holdEnabled;piece,piece,...;wins;blockMaxHeight;blockMaxWidth;
```

is represented internally by `Network::LobbyPlayerSetup`. `encode_lobby_player_setup()` preserves the existing wire bytes, while `decode_lobby_player_setup()` validates:

- the exact section count;
- non-negative dimensions/counters;
- boolean HOLD values;
- piece ids;
- trailing preview separator;
- preview count matching `nextCount`.

`Init` now assigns named snapshot fields to the opponent session instead of carrying `_cnt`, `val`, and eight positional parsing branches.

## 3. Typed lobby channel

`network/lobby_channel` is the socket-facing wrapper used by App. It exposes:

- `send_lobby_signal()`;
- `try_receive_lobby_signal()`;
- `wait_for_lobby_signal()`;
- `receive_lobby_seed()`;
- `exchange_lobby_player_setup()`.

The existing ability to open Settings while waiting for another player is preserved, but it now checks `Input::Action::OpenSettings` instead of comparing the historical `"SET"` binding string directly.

`src/app/init.cpp` and `src/app/game.cpp` contain no raw `Network::send/receive/drain` calls and no startup wire-token literals.

## 4. Server uses the same codec

`tetr_server` now shares `lobby_protocol` with the client. It no longer has a second hard-coded table of `start1 ... start6` meanings, and it validates player setup records before forwarding them. The per-frame relay intentionally remains a wire-level server boundary.

The server source also no longer depends on `bits/stdc++.h` or `using namespace std`.

## 5. Transport API cleanup

Once App stopped using raw transport, several public compatibility helpers became redundant:

- the `GameContext` overload set was removed;
- public `receive()` was made an implementation-private raw helper;
- unused `receive_until()` was deleted;
- `receive_blocking()` was deleted from the transport API and the typed lobby channel now performs its own typed wait loop.

The remaining raw transport surface accepts `RuntimeServices&` only and is intended for typed Network channels.

## 6. Regression coverage

`multiplayer_lobby_protocol_test` locks down the exact historical setup wire, signal mapping, malformed packet rejection, seed parsing, and 10,000 deterministic randomized setup encode/decode round trips.

Final Stage 30 verification:

- Debug: 31/31 tests passed;
- Release: 31/31 tests passed;
- 83/83 non-BOT public headers compile standalone;
- enhanced warning build has no non-BOT warnings;
- all 8 protected BOT headers are byte-for-byte identical to Stage 29.

No Replay, board-state, attack-frame, keybinding or BOT format was changed.
