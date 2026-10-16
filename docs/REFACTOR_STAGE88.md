# Stage 88: Pre-QP final cleanup (Stages 84-88)

This pass completes the remaining non-BOT work scheduled before Stage 89. The
protected BOT strategy headers remain byte-for-byte unchanged. Stage 89 itself,
which would replace the remaining QP operation bit packing, is intentionally
not part of this package.

## Stage 84: RelayServer and dead compatibility files

The relay server used to have two different headers with the same logical name:
`src/network/relay_server.h` and `include/tetr/network/relay_server.h`. Depending
on include search order, targets could silently compile against different APIs.
The private duplicate was deleted and all users now include the one public
header.

The architecture CTest now rejects any `src/.../*.h` that duplicates an
`include/tetr/.../*.h` at the same relative path. The unused legacy timer header
and obsolete PPP-generator ignore entries were also removed.

## Stage 85: Typed input bindings and full Linux keycode state

Runtime no longer stores both a `map<int,string>` binding table and a derived
key list. `RuntimeServices` owns a single `Input::BindingSet`; legacy action
names are parsed once when bindings are refreshed. Gameplay, Settings,
countdown and post-game input paths operate only on `Input::Action`.

`MovementInputState` no longer allocates 256 fixed slots. Key phase/timestamp
state is created dynamically for actual non-negative keycodes. This removes the
old mismatch where Linux key capture accepted values through `KEY_MAX` but
movement processing rejected values >= 256.

The `keybinding.txt` format and OS keycodes are unchanged.

## Stage 86: Explicit start and gravity states

Three historical sentinel conventions were removed from normal gameplay state:

- `gameStartTime = 1e18` became `optional<TimePoint>`; elapsed game time is
  computed through the session helper only after start.
- gravity no longer uses `1e9+5` for "disabled" or tiny floating-point checks
  for "instant". `Runtime::GravityInterval` represents Disabled, Instant and
  Periodic explicitly.
- Engine no longer manufactures a `Piece::Type(-2)` to tell BOTs that HOLD is
  disabled. `Bot::HoldInput` is typed, and `-2` is recreated only inside the
  legacy adapter immediately before calling protected strategy source.

Historical config/Replay/BOT numeric values are still decoded/encoded at their
compatibility boundaries, preserving external behavior.

## Stage 87: Typed board-state placement sending

The previous board-state send API accepted seven loosely related placement
arguments and used `-1` defaults to mean "no placement":

```text
lastPiece, spin, mini, clearedLines, b2b, combo, perfectClear
```

It now accepts `optional<Network::RemotePlacementSnapshot>`. Placement code
constructs the snapshot once, while periodic/movement state sends pass no
placement. The historical 10-section/17-section UDP board-state syntax remains
unchanged and randomized codec tests compare against the legacy wire oracle.

## Stage 88: NEXT means NEXT

`PieceQueueState` previously stored one vector with two meanings:

```text
preview[0]     = current piece
preview[1..N]  = displayed NEXT pieces
```

Before the first piece it even used piece id 7 as a placeholder in slot zero.
This convention has been deleted. Runtime now stores only:

```text
nextPreview[0..N-1] = displayed NEXT pieces
```

The current piece remains the front of the typed bag. Consequences:

- the renderer reads NEXT with ordinary zero-based indices;
- multiplayer lobby setup sends `nextPreview` directly;
- board-state decoding exposes `currentPiece` and `nextPreview` separately;
- the wire codec alone reconstructs/parses the compatible historical
  `[current,next...]` list;
- remote active piece type is presentation state instead of being smuggled in
  the NEXT vector;
- the BOT autoplay adapter synthesizes the old `[current,next...]` vector only
  for the duration of a protected BOT call.

Malformed remote snapshots with the wrong NEXT count are not installed into the
remote presentation state, avoiding out-of-range NEXT rendering.

## Compatibility and validation

No gameplay rule, Replay format, multiplayer wire format, user config key or
keybinding file format is intentionally changed by Stages 84-88.

Validation for the final Stage 88 tree:

- Debug CTest: 34/34 passed;
- clean Release CTest: 34/34 passed;
- non-BOT public headers: 101/101 independently first-include clean;
- architecture boundary checks: passed, including duplicate-header detection;
- protected BOT strategy headers: 8/8 SHA-256 identical to the Stage 83
  baseline (which was already identical to Stage 46).

Stage 89 remains deliberately pending.
