# zzztoj upstream snapshot

- Project: `TetrisAI/tetris_ai_runner`
- Repository: <https://github.com/TetrisAI/tetris_ai_runner>
- Branch: `botris`
- Tag: `pre0.8.3-botris`
- Commit: `ce12b474af8438abec77ae8ec69c8f24a918c7b3`
- Commit date: `2024-08-12T11:38:14+08:00`
- Commit subject: `Botris 10`
- License: MIT; see [the active checkout's license](../../../zzztoj_standalone/upstream/LICENSE.md).

The main project now compiles the checkout under
`zzztoj_standalone/upstream/src` and the shared adapters in
`zzztoj_standalone/src`. This keeps the main game and the independent visualizer
on exactly the same upstream evaluators, searches, and SRS+ reachability graph.
Bounds-only local fixes are recorded in
[`upstream_bounds.patch`](../chatgpt/upstream_bounds.patch); they guard empty
boards and danger masks whose four rows would extend below the board.
The older partial copy beside this document was removed during the 2026-10-08
cleanup after verifying it had no build or include references. The active
checkout and its license remain in `zzztoj_standalone/upstream`.

Five native engines are exposed by the main Settings menu: Botris v0.8, TOJ
v0.8, Dig v0.2, AX Attack v0.1, and C2 v0.1. Host-specific board conversion,
hold validation, target-to-pose conversion, and safe fallback handling remain
in `src/bot/bot_zzztoj.cpp`.

The host's default search-width value 300 maps to the standalone runner's
strength 1 / 50 ms baseline. Botris, TOJ, and C2 scale their time budget
proportionally; AX maps each additional 300 host units to one native strength
level; Dig has fixed native strength and ignores this setting.
