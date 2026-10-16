# Stage 20: Playfield and Piece type cleanup

Stage 20 removes the last two historical core type names from normal gameplay:
`map` and `Block::block`. The protected BOT strategy headers remain unchanged;
small forwarding headers preserve their source-level environment only at the
legacy boundary.

## Runtime::Playfield

Normal state now stores:

```cpp
Runtime::Playfield field;
```

instead of the historical global `struct map`.

`Playfield` owns visible and negative/hidden rows and exposes explicit APIs:

```text
width() / height()
contains(row, col)
cell(row, col)
set_cell(row, col, value)
copy_row(destination, source)
clear()
initialize(width, height)
```

Production Engine/Render/Network code no longer uses `field[row][col]`; indexed
access remains only as a transitional BOT compatibility syntax.

The storage and hidden-row semantics remain unchanged: 105 physical positive
rows, 105 physical negative rows, `-1` as empty, and the same 100-row configured
hidden capacity used by the old implementation.

## Piece::Definition

Normal code now uses:

```cpp
Piece::Definition
Piece::get_definition(type)
Piece::fool_display_type(type)
```

instead of:

```cpp
Block::block
Block::get_block(type)
Block::fool_getblock(type)
```

The normal geometry API is now named by intent:

```text
can_place(...)
try_rotate(...)
type_id()
color_id()
cells(rotation)
```

The existing shape and kick tables are still sourced from PPP without any data
conversion or normalization.

## BOT forwarding headers

The files:

```text
include/tetr/map.h
include/tetr/block.h
```

are now tiny legacy forwarding headers. They exist because the eight protected
BOT implementation headers explicitly include `../map.h` and `../block.h`.
They provide:

```cpp
using map = Runtime::Playfield;
using Block::block = Piece::Definition;
```

plus the historical `Block::get_block/fool_getblock` wrappers. Normal source
code has zero references to these historical names.

`Piece::Definition` retains `check/checks/bot_check/bot_checks` only for those
protected BOT sources. Normal code exclusively calls `can_place/try_rotate`.

## Placement naming cleanup

The post-lock coordinator no longer aliases movement state as `x/y/type`.
Those variables are now explicitly:

```text
row
col
rotation
lastRotationOp
immobile
```

This removes one of the most misleading pieces of historical naming where
`type` actually meant rotation state.

## Core geometry regression test

`core_geometry_test` adds deterministic coverage for the type migration:

- Playfield visible/hidden row access, clearing, copying and row movement.
- Every PPP piece definition, color, shape and kick table.
- Fool display mapping.
- 30,000 randomized collision and rotation-kick comparisons against a direct
  transcription of the Stage 19 `Block::check/checks` algorithms.

## Validation

```text
Debug:   24 / 24 PASS
Release: 24 / 24 PASS
Release tests with -UNDEBUG: 24 / 24
75 / 75 non-BOT public headers self-contained
0 normal historical map/Block geometry references
0 direct indexed Playfield accesses in normal production code
0 warnings in all modified non-BOT translation units under
  -Wall -Wextra -Wpedantic
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 19
```

The BOT search/score strategy source is not modified in Stage 20.
