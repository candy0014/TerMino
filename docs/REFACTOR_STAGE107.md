# Stage 107: remove Block wrapper and BOT global environment

Stage 107 continues the BOT-first boundary cleanup started in Stage 106. The goal is structural only: keep the three retained strategy algorithms behaviorally unchanged while removing the last public geometry wrapper and process-global compatibility environment.

## Removed compatibility artifacts

The following files are deleted:

- `include/tetr/block.h`;
- `src/legacy/block_compat.cpp`;
- `include/tetr/legacy/bot_environment.h`;
- `src/bot/bot_compat_environment.h`.

`CMakeLists.txt` no longer builds `block_compat.cpp`.

## Piece geometry

Candy, Mech and `Bot::getop()` now operate on `Piece::Definition` directly. Their historical integer `(row, col, rotation, kick)` call sites use private free functions in `src/bot/bot_piece_access.h` that forward ordinary collision/rotation to `Definition::can_place()` and `Definition::try_rotate()`.

`bot_while1` keeps its internal bitboard search representation, but its old `Block::block::bot_check/bot_checks` methods are gone. Equivalent private helpers consume `Piece::Definition::cells()` and `kick_tests()` directly. This changes ownership and API shape, not the search graph or evaluation logic.

## Explicit BOT environment

Stage 106 still synchronized a set of process globals before each BOT call:

- `Config::*`;
- `BoardStatic::QP_floor`;
- `LegacyTimerProxy timer`.

Stage 107 replaces those globals with `Bot::Environment`. `src/legacy/bot.cpp` constructs one snapshot from the current `GameContext` and passes it explicitly through the BOT dispatch table to `init()` and `getpos()`.

The snapshot deliberately retains the historical scalar encoding for the few modifier/mode values used by the old strategy formulas. This keeps the algorithm changes mechanical while eliminating hidden ownership and cross-call state.

`bot_while1` also no longer stores a global pointer to the active player. Its root search node receives QP last-operation directly from the `PlayerSessionState` passed to that `getpos()` call.

Because the timer proxy has no remaining users, the legacy scalar `MonotonicClock::get()` and `sleep(double)` methods are removed; the runtime clock surface is again only `now()` / `sleep_for(Duration)`.

## Deliberately unchanged

No strategy tuning was performed. In particular Stage 107 does not alter:

- scoring/evaluation formulas or constants;
- BFS/beam expansion order;
- search widths;
- opening scripts;
- HOLD policy;
- random tie breaking;
- Quick Play strategy branches.

## Regression guards

`architecture_boundary_test` now fails if any removed Block/environment artifact returns, or if BOT sources reintroduce `Config::*`, `BoardStatic::*`, `LegacyTimerProxy`, `Block::block`, `tetr/block.h`, or the `bot_while1` global player pointer.

The final Stage 107 tree passes **36/36 tests in both Debug and Release**, including the 30000-case `core_geometry` randomized collision/kick test and the `bot_typed_boundary` real BOT selection/movement path.

A final dead-state pass also removes `bot_while1`'s `bs[500010]` scratch array: it was only assigned from the active beam and never read, so removing it changes no search decisions while eliminating a large unused static workspace.
