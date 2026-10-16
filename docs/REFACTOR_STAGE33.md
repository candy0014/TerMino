# Stage 33: Renderer Domain Types

Renderer callers now use `ColorRole`, `SquareFill`, `PieceDrawMode` and `TextAlignment`. UI colors no longer cross module boundaries as `-7..-1`, and piece/ghost/erase behavior no longer shares an `op=-1/0/1/2` parameter.

`set_board_cell_color()` is the single boundary for encoded field cells. Optional values replace the `-1` default sentinels in B2B, combo and blighted presentation APIs, while spin/all-clear flags are bools.
