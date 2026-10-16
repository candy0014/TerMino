# Stage 15: renderer boundary and Board/Block presentation cleanup

Stage 15 separates terminal presentation from game state.  The previous stages
made the game rules and runtime state explicit, but `Board` and `Block` still
contained a large historical drawing API (`go`, `put`, `print_*`, border/UI
rendering) and multiplayer state rendering was still attached to `Board`.

This stage removes those responsibilities from the state/geometry objects.
BOT strategy code is unchanged.

## New render layers

New public interfaces:

```text
include/tetr/render/terminal_renderer.h
include/tetr/render/board_renderer.h
include/tetr/render/piece_renderer.h
```

Implementations:

```text
src/render/terminal_renderer.cpp
src/render/board_renderer.cpp
src/render/piece_renderer.cpp
```

The low-level terminal renderer owns ANSI/console operations:

```text
cursor visibility
terminal initialization / clear
cursor positioning
RGB / semantic colors
square glyph rendering
circled-number rendering
flush
```

It is built as `tetr_terminal` and has no dependency on Board/GameContext.

The high-level `tetr_render` library depends on runtime state and provides:

```text
Board coordinates / HOLD / NEXT coordinates
score / QP altitude / status lines
Spin / Clear / B2B / Combo / All Clear / Blighted text
board border
piece / ghost / HOLD / NEXT rendering
remote multiplayer state redraw
```

All high-level rendering accepts an explicit `Runtime::GameContext&`, so
SquareVersion, ObscuredColors, Ghost, field height and mode-specific layout no
longer come from hidden Config globals in the main App/Engine/Replay path.

## Board is no longer a renderer

The following presentation/network methods were removed from `Board`:

```text
go / go_hold / go_next
put / put_hold / put_next
print_score / print_altitude / print_message / print_border
print_spin / print_clear / print_b2b / print_combo
print_allclear / print_blighted / print_string
get_sta / copy_sta
```

They were not used by any protected BOT strategy file, so no BOT compatibility
wrapper was necessary.

`Board` now keeps state, bag/random state, garbage ownership and game-related
initialization, but not terminal positioning or drawing.

## Block is no longer a renderer

The following methods were removed from `Block::block`:

```text
put_block
put
put_hold
put_next
```

Collision, rotation checks, kick handling and piece factory data remain on
`Block::block` / `Block` because those are game geometry rather than
presentation.

The renderer equivalents are:

```cpp
Render::draw_block(...)
Render::draw_piece(...)
Render::draw_hold(...)
Render::draw_next(...)
Render::draw_board_piece(...)
Render::draw_board_hold(...)
Render::draw_board_next(...)
```

## Interactive and Function cleanup

`Interactive` now contains only keyboard state input:

```cpp
Interactive::keydown(...)
```

Terminal output functions (`setcol`, `gotoxy`, `rgb_set`) were removed.

Likewise, these historical `Function` presentation helpers were removed:

```text
cur_hide / cur_show
rgb_init / clear
put_square / put_number
```

Settings, Replay save interaction, the main entry point and the keybinding tool
now use `Render::terminal_renderer` directly.

`Function` remains for generic parsing/formatting/shuffle helpers and the
legacy network compatibility wrappers.

## Multiplayer board-state codec

Board-state wire serialization was separated from Board presentation:

```text
include/tetr/network/board_state_codec.h
src/network/board_state_codec.cpp
```

The App/Engine path now calls:

```cpp
Network::encode_board_state(context, board, ...)
```

Remote state application/redraw is handled by:

```cpp
Render::apply_remote_state(context, opponent, state)
```

The wire format is unchanged.

`board_state_codec_test` executes 10,000 randomized state encodes against an
oracle copied from the Stage 14 serialization algorithm, including garbage
buffer stages and the historical block-position side effect.

## Main path uses the explicit renderer

After this stage, `src/app`, `src/engine` and `src/replay` contain zero uses of:

```text
Board::go / Board::print_* / Board::put_*
Block::put*
Interactive::setcol / gotoxy / rgb_set
Function::put_square / put_number
```

Rendering dependencies are visible at the call site as `Render::*` calls with
the active GameContext.

## Output compatibility checks

In addition to the permanent unit tests, Stage 14 and Stage 15 were linked into
small renderer probes and their raw terminal byte streams were compared.

Two substantially different configurations were checked:

```text
normal colors + SquareVersion 1 + ordinary field
obscured colors + SquareVersion 0 + QP-style shifted left panel
```

The probes covered border, score, clear labels, B2B, Combo, All Clear,
Blighted, Spin, active-piece, HOLD and NEXT rendering.

Both comparisons were byte-for-byte identical.

## Source-size effect

Representative changes:

```text
include/tetr/board.h          134 -> 111 lines
include/tetr/block.h           31 ->  27 lines
include/tetr/function.h        45 ->  39 lines
include/tetr/interactive.h     11 ->   8 lines

src/runtime/function.cpp      534 ->  61 lines
src/runtime/block.cpp          98 ->  72 lines
src/runtime/board.cpp         131 -> 117 lines
```

The extracted renderer/codec implementation is now in dedicated modules rather
than hidden inside unrelated runtime files.

## Library dependency

Stage 15 adds two explicit presentation layers:

```text
                  tetr_terminal
                        |
                        v
                   tetr_runtime
                    /       \
                   /         \
            tetr_rules     tetr_render
                   \         /
                    \       /
                    tetr_engine
                        |
                     tetr_app
```

`tetr_terminal` is deliberately independent of runtime/game state.  This keeps
terminal mechanics reusable by Settings/tools without creating a
runtime <-> renderer dependency cycle.

## Verification

- Debug: 18/18 tests passed;
- Release: 18/18 tests passed;
- Release tests compile with `-UNDEBUG`, so assert-based random regression tests
  execute;
- 69/69 non-BOT public headers compile independently as the first include;
- all non-BOT source files compile with `-Wall -Wextra -Wpedantic` and zero
  warnings;
- 8/8 protected BOT strategy headers are byte-for-byte unchanged from Stage 14;
- renderer Stage 14 -> Stage 15 byte probes are identical in both tested display
  configurations.

## Remaining work

The game state / renderer boundary is now explicit.  The largest remaining
legacy areas are no longer presentation code, but the compatibility/global
configuration surface and a few Board-owned services:

```text
Config::* default-context aliases
Board::Garbage as a nested service
Board bag/random initialization methods
legacy Function::send/receive wrappers
protected BOT implementation headers
```

A useful next step is to split `Board` into a smaller pure playfield/player
state and explicit queue/random/garbage session services, then reduce the
legacy Config bridge further without touching BOT algorithms.
