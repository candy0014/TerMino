# Stage 21: Strong domain types

Stage 21 replaces the most error-prone gameplay integers with explicit domain
values while keeping all protected BOT strategy sources byte-for-byte intact.
The serialized Replay/network formats remain integer based at their boundaries.

## New strong types

`runtime/strong_types.h` defines:

```text
Piece::Type              piece identity / legacy sentinel
Runtime::Rotation        normalized 0..3 rotation
Runtime::CellPosition    row + column
Piece::Pose              position + rotation
Piece::KickIndex         successful kick index
Runtime::RotationRecord  whether the last action rotated + kick index
```

All integer constructors are explicit. In particular, an arbitrary `int` cannot
silently become a piece type, rotation, or kick index. `Rotation` normalizes
negative and positive legacy values modulo four.

`RotationRecord` replaces the historical overloaded `last_op` integer. Its
legacy encoding remains exactly the same for Replay/QP/BOT boundaries:

```text
0      no last rotation
k + 1  rotation succeeded with kick index k
```

## Typed gameplay state

The normal runtime now stores piece identity as `Piece::Type` in:

```text
HoldState::piece
PieceQueueState::bag
PieceQueueState::preview
PieceQueueState::allowedPieces
```

Movement state stores a `Piece::Pose` and `RotationRecord`. Spin/placement
resolution, render piece calls, active-piece network state, Replay movement
playback, spawn and autoplay use the same typed values.

Loop indices, board row scans, scoring values and encoded QP clear operations
remain ordinary integers where they are genuinely numeric rather than domain
identities.

## Explicit compatibility boundaries

The protected BOTs still consume their historical integer interface. Only
`src/legacy/bot.cpp` converts:

```text
Piece::Type <-> int
Piece::Pose <-> sx/sy/stype
RotationRecord <-> ans_rot
vector<Piece::Type> <-> vector<int>
```

The empty-hold sentinel `-1` and disabled-hold sentinel `-2` remain distinct.
This is important because both are invalid `Piece::Type` values but carry
different legacy BOT semantics.

Replay and multiplayer continue to write/read the historical integer text
format. Conversion occurs immediately at parsing/serialization boundaries;
internal state remains typed.

## Geometry and movement API

`Piece::Definition` now provides typed forms of `cells`, `can_place` and
`try_rotate`. Its old integer `check/checks/bot_check/bot_checks` methods remain
only as BOT compatibility APIs.

`MovementController` callbacks now operate on `Piece::Pose` and return
`Piece::KickIndex`, removing the possibility of swapping piece IDs, rotation
states and kick indices by accident.

## Regression coverage

Existing integer reference algorithms were intentionally retained in the tests.
For example, movement tests compare a legacy `{row,col,rotation,lastRotationOp}`
state against the new `Pose + RotationRecord` state over randomized operations.
Network and Replay tests continue to compare against the old integer wire/text
formats.

A new `strong_types_test` verifies:

- integer values are not implicitly convertible to the new domain types;
- rotation normalization for positive and negative legacy values;
- piece/sentinel round trips, including `-1` and `-2`;
- kick/rotation-record legacy round trips;
- pose translation semantics.

## Validation

```text
Debug:   25 / 25 PASS
Release: 25 / 25 PASS
Release tests with -UNDEBUG: 25 / 25
76 / 76 non-BOT public headers self-contained
0 warnings in all modified non-BOT translation units under
  -Wall -Wextra -Wpedantic
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 20
```

`board_state_codec_test` is executed directly in the final verification because
this environment can occasionally leave its otherwise-fast executable waiting
inside the ctest driver. The executable itself passes in both Debug and Release.
