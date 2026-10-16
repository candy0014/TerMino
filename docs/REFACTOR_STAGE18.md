# Stage 18: isolate the BOT legacy environment and remove process-wide config globals

Stage 18 shrinks the final process-wide compatibility shell. The protected BOT
strategy headers remain unchanged, but their historical unqualified globals no
longer exist as public runtime APIs. Normal App/Engine/Replay/Render/Network
code operates only on explicit `GameContext`, `RuntimeConfig`, session state and
runtime services.

## Public `Config::*` compatibility layer removed

The old public files and implementations are gone:

```text
include/tetr/config.h
src/runtime/config.cpp
src/runtime/runtime_config.cpp
```

`RuntimeConfig` is now simply a normal value object. There is no longer a
`capture_runtime_config()` / `publish_runtime_config()` mirror between a
structured config and a second set of process-wide variables.

A source-tree audit outside the protected BOT area now reports zero references
to:

```text
tetr/config.h
Config::*
BoardStatic::*
board1 / board2
Function::*
```

## BOT compatibility is a private translation-unit boundary

The protected BOT headers still contain historical names such as:

```text
Speed
mapWidth / mapHeight
QP_mod_allspin
Random
board1
BoardStatic::QP_floor
timer
```

Those names are now declared only inside:

```text
src/legacy/bot.cpp
```

They have translation-unit-local storage where possible. Before each BOT
`init/getpos/getop` entry, the adapter copies the necessary values from the
explicit `GameContext` into that private compatibility environment.

The dedicated library is named:

```text
tetr_bot_legacy
```

and Engine/App use it as a private dependency. New normal modules therefore
cannot accidentally include or link against a general-purpose legacy Config
layer.

The eight strategy headers under `include/tetr/bot/` are restored byte-for-byte
to the sealed Stage 17 versions.

## `Function::*` removed

The old `Function` namespace mixed unrelated responsibilities. The remaining
useful pure helpers are now explicit utilities:

```text
include/tetr/util/numeric.h
src/util/numeric.cpp

include/tetr/util/deterministic_random.h
src/util/deterministic_random.cpp
```

They contain the historical loose number parser/formatter and the project's
legacy shuffle algorithm. `util_equivalence_test` compares them against copies
of the old implementation over randomized inputs and also verifies the RNG
state after shuffle so deterministic bag/garbage behavior is preserved.

Unused legacy replay/network wrappers in `Function` were deleted instead of
being moved to another compatibility namespace.

## Process-global timer alias removed

`tetr/timer.h` now declares only the `Timer` type. Normal code obtains time from:

```cpp
context.services.clock
```

The protected BOT translation unit binds its private historical `timer` name to
the default runtime service clock.

## User configuration is explicit state

The old collection of forty-plus mutable `UserConfig::*` namespace variables
has been replaced by:

```cpp
UserConfig::Settings
```

`RuntimeServices` owns the active persistent input/settings object:

```cpp
RuntimeServices::userSettings
```

Configuration loading is now explicit and side-effect free with respect to
process globals:

```cpp
UserConfig::load(settings, path);
```

`Init`, Settings UI, startup and the keybinding tool all operate on an explicit
settings object. Default key codes and control names are read-only accessors.

`user_config_test` locks down defaults, loose numeric parsing, unknown-key
handling, Replay path parsing and the historical duplicate `Invisible` key
registry priority.

## Debug-log global removed

The former global `fout` / `debug_log` facility had only one remaining use: a
startup diagnostic write of `Autoplay_Width`. It has been deleted. This also
removes another static-initialization object from the runtime.

## Utility and constant boundaries

The hidden-row capacity used by normal runtime/BOT compatibility code now lives
in:

```text
include/tetr/runtime/constants.h
```

The BOT adapter can expose its historical `mapHeightN` name privately without
reintroducing the public Config header.

## Build graph

The relevant direction is now:

```text
                 tetr_util
                    |
      +-------------+-------------+
      |                           |
tetr_terminal                 tetr_runtime      tetr_rules
      |                           |                 |
      +----------------------> tetr_render         |
                                  |                |
                                  +-------> tetr_engine <--- tetr_bot_legacy (private)
                                                 |
                                              tetr_app
```

`tetr_bot_legacy` is an isolation adapter, not a public state-ownership layer.

## Regression additions

Stage 18 adds:

```text
util_equivalence_test
user_config_test
legacy_bot_boundary_test
```

The legacy BOT boundary test deliberately changes `GameContext` values and
verifies that a protected BOT sees the synchronized values through the private
compatibility environment.

## Verification

Final clean verification:

```text
Release: 23 / 23 PASS
Debug:   23 / 23 PASS
```

`init_context_test` is executed separately from the ctest batch because its
large ANSI terminal output can occasionally make the container's ctest driver
stall; the binary itself completes normally in roughly one to two seconds in
Release and two seconds in Debug.

Additional audits:

```text
23 / 23 Release test targets contain -UNDEBUG
73 / 73 non-BOT public headers compile independently as the first include
-Wall -Wextra -Wpedantic: 0 non-BOT warnings
18 historical BOT warnings left untouched
8 / 8 protected BOT strategy headers byte-for-byte equal to Stage 17
0 public config.h/debug_log.h/function.h compatibility headers remain
0 non-BOT Config:: / Function:: / BoardStatic:: / board1/board2 references
```

## Remaining legacy surface

The remaining intentionally legacy-shaped code is now concentrated almost
entirely in two places:

```text
Board facade / Board::Garbage type compatibility used by BOT
src/legacy/bot.cpp private compatibility environment
```

The next architectural step can therefore focus on the `Board` facade itself
and BOT-facing data conversion, rather than maintaining process-wide global
configuration mirrors.
