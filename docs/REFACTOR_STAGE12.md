# Stage 12: implementation-header cleanup

Stage 12 corrects the remaining public headers that still contained whole
runtime/orchestration implementations after the Stage 11 translation-unit
migration. The goal is conventional C++ ownership:

```text
include/tetr/.../*.h   declarations, public types, templates/constexpr helpers
src/.../*.cpp          executable implementation
```

No gameplay formulas, Replay format, BOT search logic, or network protocol were
intentionally changed.

## Moved from public headers to `.cpp`

The following modules now have declaration-only public headers and normal
implementation translation units:

| Public header | Implementation |
| --- | --- |
| `engine/placement_coordinator.h` | `src/engine/placement_coordinator.cpp` |
| `engine/live_piece_controller.h` | `src/engine/live_piece_controller.cpp` |
| `engine/autoplay_adapter.h` | `src/engine/autoplay_adapter.cpp` |
| `engine/game_loop_adapter.h` | `src/engine/game_loop_adapter.cpp` |
| `engine/countdown_presentation.h` | `src/engine/countdown_presentation.cpp` |
| `engine/frame_coordinator.h` | `src/engine/frame_coordinator.cpp` |
| `engine/piece_spawn.h` | `src/engine/piece_spawn.cpp` |
| `engine/game_end_presentation.h` | `src/engine/game_end_presentation.cpp` |
| `replay/piece_playback.h` | `src/replay/piece_playback.cpp` |
| `replay/save_interaction.h` | `src/replay/save_interaction.cpp` |
| `keyhelper.h` | `src/runtime/keyhelper.cpp` on Linux |

`engine/frame_presentation.h` had only one consumer and no useful independent
public API. It was removed; its small status-render helper is now private to
`frame_coordinator.cpp`.

Several helper functions that are only used inside one implementation file are
now file-local (`static`) instead of accidental public symbols, including the
countdown drawing/wait helpers, live HOLD helper, next-preview/multiplayer-message
helpers, and Replay path/write helpers.

## Include hygiene

Moving these implementations exposed one transitive-include dependency in
`src/app/game.cpp`: it used `Function::*` without including `tetr/function.h`.
The source now includes its own direct dependency.

The Linux keyboard event reader no longer pulls `fcntl`, `unistd`, threading
implementation, and event-loop code into every translation unit that sees
`keyhelper.h`. The header only declares the class; the Linux implementation is
compiled conditionally through CMake.

## Headers intentionally left with implementation

Not every function body belongs in a `.cpp`:

- `Function::add_replay` is a variadic template and must remain visible in the
  header;
- Opener Phase helpers are tiny `constexpr`/inline pure functions;
- `map` is a small, heavily used value type whose trivial indexing/clear
  operations remain inline;
- class/struct member initializers and POD definitions remain in headers;
- BOT strategy files remain protected legacy implementation headers. Their
  algorithms and bytes are unchanged in this stage.

The BOT files are not considered good public-header design, but converting them
to independent `.cpp` files would be a BOT-specific refactor because several of
them contain strategy-local globals and overlapping legacy namespaces. That is
kept outside this structural cleanup by design.

## Size change

Non-BOT public headers:

```text
Stage 11: 3337 lines
Stage 12: 2297 lines
```

Examples:

```text
placement_coordinator.h   350 -> 24
live_piece_controller.h   174 -> 29
autoplay_adapter.h        125 -> 44
game_loop_adapter.h       115 -> 34
save_interaction.h        113 -> 12
piece_playback.h           101 -> 24
countdown_presentation.h    78 -> 14
frame_coordinator.h         62 -> 13
keyhelper.h                 76 -> 31
```

## Verification

- Release build: success;
- Release regression suite: 12/12 passed;
- Debug build: success;
- Debug regression suite: 12/12 passed;
- Release tests retain assertions via `-UNDEBUG`;
- 60/60 non-BOT public headers compile independently as the first include;
- `-Wall -Wextra -Wpedantic`: no non-BOT warnings; the remaining 18 warnings
  originate exclusively from the protected BOT strategy headers;
- all 8 protected BOT files are SHA-256 identical to Stage 11.
