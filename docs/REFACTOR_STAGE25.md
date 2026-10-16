# Stage 25: Board-state receive codec

Stage 25 removes the multiplayer `!board-state` wire parser from the renderer.
The wire format itself is unchanged.

The goal is a one-way boundary:

```text
network text
    |
    v
Network::decode_board_state()
    |
    v
Network::BoardStateSnapshot
    |
    v
Render::apply_remote_state()
```

Renderer code no longer knows semicolon section numbers, comma-delimited piece
ids, signed wire fields or the board-cell alphabet.

## Before Stage 25

`Render::apply_remote_state()` accepted the raw network string and performed all
of the following in one loop:

- skipped the leading `!` marker;
- counted `;` sections;
- decoded board cells from `-`, `0..9` and `A..Z`;
- decoded the incoming-garbage strip;
- parsed comma-delimited NEXT and HOLD piece ids;
- reconstructed active row / column / rotation;
- parsed wins, placed-block and attack counters;
- interpreted optional placement/spin/clear/B2B/combo/PC fields;
- mutated player state;
- rendered board differences and status effects.

The renderer therefore depended directly on historical field positions such as
"section 10 is last piece" and "section 16 is perfect clear".

## Decoded snapshot

`network/board_state_codec.h` now defines:

```cpp
Network::BoardStateSnapshot
Network::RemoteGarbageCell
Network::RemoteScoreSnapshot
Network::RemotePlacementSnapshot
```

A snapshot carries the decoded remote presentation state:

```text
visible Playfield cells
incoming-garbage display cells
NEXT queue
HOLD piece
active Piece::Pose
wins / placedBlocks / totalAttack
optional placement notification
```

The placement notification explicitly carries:

```text
piece
spin / mini
clearedLines
b2b / combo
perfectClear
```

The legacy lock-frame sentinel values are therefore not propagated into Render
as an overloaded rotation integer.

## Receive codec

The new API is:

```cpp
std::optional<Network::BoardStateSnapshot> Network::decode_board_state(
    std::string_view state,
    int width,
    int visibleHeight);
```

The decoder owns all wire syntax and validates the received record before the
renderer sees it. It rejects:

- records without the leading `!` marker;
- unexpected section counts;
- board payloads inconsistent with the negotiated field dimensions;
- invalid cell alphabet entries;
- invalid garbage-strip values;
- malformed NEXT/HOLD comma encoding;
- malformed numeric fields;
- negative/out-of-range active rotation on ordinary board frames;
- malformed optional placement records.

Malformed remote board-state frames are ignored by the Frame Coordinator rather
than being partially applied.

## Renderer after Stage 25

`Render::apply_remote_state()` now accepts:

```cpp
const Network::BoardStateSnapshot&
```

It is responsible only for presentation/state application:

1. update the three remote counters;
2. paint the decoded garbage strip;
3. display a placement notification when present; otherwise
4. diff the decoded field against the current remote field;
5. update/draw the active piece, NEXT and HOLD;
6. refresh score/message presentation.

There is no raw board-state string parsing in `src/render/board_renderer.cpp`.

The historical placement-frame behavior is preserved: placement notifications
update counters and the garbage strip, but do not replace the remote board,
NEXT, HOLD or active-piece cache until a subsequent ordinary board snapshot.

## Engine / network boundary

`Engine::run_frame()` still receives the outer multiplayer packet through the
existing `frame_packet` / `frame_pump` boundary. When a packet contains an
opponent board-state string, the Frame Coordinator calls
`Network::decode_board_state()` using the opponent field dimensions and passes
only a successful snapshot to Render.

This keeps the outer UDP/frame protocol separate from the board-state subcodec.

## Wire compatibility

`Network::encode_board_state()` is unchanged in format and remains a pure,
read-only serializer from Stage 24.

The historical board-state record remains:

```text
!<board>;<garbage>;<next>;<hold>;<row>;<col>;<rotation>;
 <wins>;<placed>;<attack>[;<piece>;<spin>;<mini>;<lines>;<b2b>;<combo>;<pc>]
```

No server or peer protocol migration is required.

## Regression coverage

Stage 25 adds `board_state_receive_codec_test`.

It performs 10,000 deterministic randomized encode/decode round trips covering:

- variable field width and height;
- empty, colored and timed-number cells;
- variable NEXT lengths and multi-digit piece ids;
- empty/non-empty HOLD;
- active poses and all four rotations;
- garbage packets on both sides of incoming-delay readiness;
- score counters;
- ordinary board frames and placement frames;
- spin, mini, clear, B2B, combo and perfect-clear metadata.

It also checks representative malformed records and requires `nullopt` rather
than partial decode.

The Stage-24 10,000-case encoder oracle remains unchanged and continues to
verify byte-equivalent wire output.

## Static boundary audit

After Stage 25:

```text
0 raw !board-state parsers in BoardRenderer
0 semicolon/section board-state knowledge in Render
0 string board-state arguments accepted by Render::apply_remote_state
1 board-state text decoder: Network::decode_board_state
1 board-state text encoder: Network::encode_board_state
```

## Validation

```text
Debug:   29 / 29 PASS
Release: 29 / 29 PASS (test targets explicitly compile with -UNDEBUG)
80 / 80 non-BOT public headers self-contained under -Wall -Wextra -Wpedantic
non-BOT build has no new warnings under -Wall -Wextra -Wpedantic
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 24
```

Stage 25 changes ownership of receive-side protocol knowledge, not multiplayer
wire semantics.
