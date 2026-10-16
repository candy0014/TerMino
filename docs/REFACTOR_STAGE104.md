# Refactor Stage 104 — Final non-BOT compatibility boundary

Stage 104 completes the non-BOT refactor by moving historical integer/seconds encodings out of the domain model and into one explicit compatibility boundary.

## Final boundary

`include/tetr/compat/legacy_encoding.h` now owns conversions for piece ids, rotations, kick indices, rotation records, board cells, gravity seconds, garbage-hole patterns and historical mode integers. Runtime/Rules/Engine/Render do not include this header.

Domain types expose only semantic/index APIs. Config, Replay, Network and the legacy BOT façade explicitly call `Compat::*` at their external-format boundaries. Tests that compare historical behavior also use Compat rather than reopening legacy methods on the domain objects.

## Earlier Stage 96–103 completion

- Config and Input are separate libraries/services.
- Replay known operations are typed variants.
- QP Rules no longer simulate packed bit layouts.
- Piece Catalog consumers use structured `CatalogEntry` data.
- GameContext component access is explicit.
- Render consumes `BoardCell` directly.
- Terminal lifecycle is RAII-owned and RelayServer waits for socket readability.
- Garbage/Gravity/Chain/Expert/Piece internal legacy semantics were removed.

## Regression protection

`architecture_boundary` now additionally rejects:

- Compat includes from Runtime/Rules/Engine/Render;
- `legacy()/from_legacy()/legacy_seconds()` returning to domain type headers;
- packed QP/sentinel state returning to normal gameplay code;
- Runtime reverse-depending on upper layers;
- Engine linking or including the protected BOT directly.

## Verification

- fresh Debug: 35/35 tests passed;
- fresh Release: 35/35 tests passed;
- non-BOT public headers: 107/107 self-contained first includes;
- terminal pseudo-TTY normal exit and SIGINT restoration passed;
- protected BOT headers: 8/8 SHA-256 identical to the Stage 95 Terminal Session baseline.


## Startup static-initialization fix

The first Stage 104 package exposed a full-executable-only static initialization ordering bug. `src/legacy/ppp_catalog.cpp` eagerly projected `Piece::catalog()` into global `PPP::allblock_*` vectors before `main()`. On the final link, this could run before the dynamic `std::vector` data in `piece_catalog_data.cpp` had been constructed, permanently caching an empty typed catalog. Board startup then computed a zero pieces-per-bag divisor and raised `SIGFPE`.

The PPP compatibility view is now lazy. `LazyCatalogView` objects are constant-initialized handles; each projected legacy container is a function-local static created only on first real access. Core board initialization additionally rejects an empty allowed-piece set with a clear invariant failure rather than performing arithmetic on zero.

Regression coverage now includes `startup_catalog`, linked with `tetr_bot_legacy`, plus UBSan and a real pseudo-TTY launch of the Release executable.
