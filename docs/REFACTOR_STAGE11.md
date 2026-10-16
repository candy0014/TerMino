# Refactor Stage 11: Translation-Unit and Runtime Boundary Cleanup

Stage 11 is a structural cleanup pass rather than another gameplay extraction.
The goal is to stop relying on the original single-translation-unit include order
and turn the refactored project into a normal multi-TU C++17 program.

Gameplay rules and BOT strategy are intentionally kept unchanged.

## Main result

The legacy public headers are now primarily declarations. Large implementation
bodies and process-wide object definitions have been moved into normal `.cpp`
translation units under `src/runtime`, `src/engine`, and `src/app`.

Representative header sizes:

```text
function.h   594 -> 45 lines
init.h       525 ->  8 lines
setting.h    424 ->  8 lines
ppp.h        356 -> 24 lines
garbage.h    264 ->  9 lines
block.h      170 -> 31 lines
place.h       60 -> 16 lines
game.h        64 ->  8 lines
bot.h         95 -> 20 lines
```

Across non-BOT public headers:

```text
Stage 10: 6207 lines
Stage 11: 3337 lines
```

This is not code deletion: most of the removed header implementation now lives in
normal `.cpp` files where ownership and linkage are explicit.

## Runtime library

`tetr_runtime` now owns the remaining process-wide legacy runtime facilities:

- Config variable definitions and runtime-service initialization;
- UserConfig storage/loading;
- Timer implementation;
- UDP socket implementation;
- PPP piece/kick/static data definitions;
- Board objects and Board implementation;
- Block implementation;
- Interactive terminal helpers;
- Function helpers;
- BOT dispatch interface;
- Settings implementation;
- debug log definition.

Corresponding public headers contain declarations instead of object/function
bodies wherever practical.

## Explicit startup

The old UserConfig implementation depended on header/static-constructor startup
order. Stage 11 replaces that with:

```cpp
UserConfig::load();
Config::initialize_runtime_services();
```

at process startup.

`initialize_runtime_services()` owns Linux input device initialization and UDP
socket setup and is idempotent.

The old Config globals are still provided for compatibility, but their
pre-`Init::init()` values deliberately preserve the original static
zero-initialization contract. `runtime_config_test` checks this behavior.

## RuntimeConfig snapshot

A first explicit configuration boundary was added in:

```text
include/tetr/runtime/runtime_config.h
src/runtime/runtime_config.cpp
```

`Runtime::RuntimeConfig` groups commonly consumed settings into:

- `ModeConfig`
- `HandlingConfig`
- `FieldConfig`
- `GarbageConfig`
- `QuickPlayConfig`
- `AutoplayConfig`
- `DisplayConfig`

`capture_runtime_config()` snapshots the current compatibility globals. Existing
legacy code can continue using `Config::*`, while new modules have a migration
path toward explicit configuration dependency.

This stage does not yet remove all globals; it establishes the boundary without
forcing a risky all-at-once gameplay rewrite.

## Application translation units

The previously header-defined application entry points now live in:

```text
src/app/init.cpp
src/app/place.cpp
src/app/game.cpp
```

with compact declaration-only headers:

```text
include/tetr/init.h
include/tetr/place.h
include/tetr/game.h
```

Likewise, the legacy Garbage adapter and Board-facing frame/placement adapters
have been moved to normal engine `.cpp` files.

Replay gameplay application is also a normal translation unit now that the
relevant ODR dependencies have been removed.

## Static-library dependency direction

During this pass an intermediate `runtime <-> engine` static-library cycle was
exposed. It was removed rather than hidden through linker ordering.

The intended dependency direction is now:

```text
rules
  ^
  |
runtime
  ^
  |
engine
  ^
  |
 app
```

More precisely, `engine` may use both `rules` and `runtime`, while `runtime`
does not call back into `engine`. `app` coordinates the higher layers.

This makes the build less dependent on static-library ordering and creates a
usable base for further context/state injection.

## Header self-containment

All non-BOT public headers were compiled individually as the first include of a
minimal translation unit:

```text
61 / 61 passed
```

This exposed and fixed several historical include-order dependencies, including
PPP standard-library type dependencies.

The BOT strategy headers are excluded from this cleanup pass because they remain
a protected compatibility area.

## Warning audit

A clean Release build with:

```text
-Wall -Wextra -Wpedantic
```

has zero warnings outside `include/tetr/bot/`.

The remaining 18 warnings are all inside the protected BOT strategy headers and
are intentionally left untouched in this stage.

## Test infrastructure fix

An important test-infrastructure issue was found during final validation.

The regression tests use `assert()`, but normal Release compilation defines
`NDEBUG`. Earlier Release builds therefore compiled out many assertions even
though CTest itself reported green targets.

Stage 11 explicitly undefines `NDEBUG` for every test target:

```text
GCC/Clang: -UNDEBUG
MSVC:      /UNDEBUG
```

After assertions were genuinely enabled, two incorrect historical test
expectations were corrected. No production gameplay rule change was required.

Both configurations now execute the assertions and pass:

```text
Debug:   12 / 12 tests passed
Release: 12 / 12 tests passed
```

The new twelfth target is `runtime_config_test`.

## BOT protection audit

All eight files under `include/tetr/bot/` were SHA-256 compared against Stage 10:

```text
8 / 8 byte-for-byte identical
```

BOT scoring, search, heuristics, and internal state machines were not modified.

## Compatibility notes

- Replay file format remains unchanged.
- Replay historical hash behavior remains unchanged.
- PPP piece/kick table contents remain unchanged; only ownership moved.
- Existing Config global names remain available.
- `flag_dead` remains preserved for future game-state work.
- The Linux event-device configuration workflow remains unchanged.
- Existing BOT strategy source remains byte-for-byte unchanged.

## Validation summary

Final Stage 11 validation consists of:

- clean Debug build;
- clean Release build;
- 12/12 Debug CTest targets;
- 12/12 Release CTest targets with assertions enabled;
- 61/61 non-BOT public-header self-containment checks;
- zero non-BOT warnings under `-Wall -Wextra -Wpedantic`;
- 8/8 protected BOT files byte-for-byte identical to Stage 10.

## Next useful work

The major single-TU/ODR obstacle is now removed. The next structural step can be
more architectural rather than mechanical:

1. gradually pass `RuntimeConfig` (and later mutable game/session state) through
   explicit contexts instead of reading `Config::*` everywhere;
2. reduce `using namespace Config` and global Board/Replay ownership;
3. separate terminal/network services from gameplay session state;
4. add integration tests for complete deterministic games/replays, now that the
   codebase can be linked from multiple normal translation units.

That work can proceed incrementally without reopening the large header-definition
migration completed in Stage 11.
