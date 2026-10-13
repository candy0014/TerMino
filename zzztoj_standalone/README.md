# Standalone native tetris_ai_runner bot visualizer

This directory is an independent control program for all five complete bot
frontends exported by the official `TetrisAI/tetris_ai_runner` `ai.cpp`:
Botris, TOJ, Dig, QQ/AX Attack, and C2. The visualizer executable does not
include or link gameplay code from the parent `tetr` project.

## Upstream

- Repository: <https://github.com/TetrisAI/tetris_ai_runner>
- Branch: `botris`
- Tag: `pre0.8.3-botris`
- Commit: `ce12b474af8438abec77ae8ec69c8f24a918c7b3`
- Checkout: `upstream/` (the vendored source snapshot)

The build compiles the upstream evaluators and search implementations without
modifying them, plus upstream `rule_srs.cpp` as the shape base for a local
reachability wrapper. Each menu item keeps its native evaluator, search,
preview policy, and HOLD behavior. There is no fallback placement, PC override,
or dependency on the parent program at runtime.

Only the pointer graph used by search/path reachability is changed from
`rule_botris` to `rule_tetr_srs_plus`. It uses the parent tetr project's exact
standard-piece geometry, spawn pose, ordered 90-degree kicks, ordered
180-degree kicks, and four invariant O rotation states. The snapshot includes the existing bounds-only fixes documented in the parent project.

QQ keeps its native target search mode. Its selected target is executed using
the upstream graph path builder, which handles the wall kicks in tetr SRS+;
the simpler upstream path builder can fail on those targets.

## Available bots and strength

The interactive settings page starts before a visual run and provides Bot,
PPS, and Strength fields. The default strength is `1`. For bots whose native
API exposes a scalable search level, larger values provide proportionally more
search time or preview/level strength. Fixed-strength bots display `N/A` and
ignore `--strength`.

| Menu id | Upstream bot | Native evaluator/search | HOLD | Strength mapping |
|---|---|---|---:|---|
| `botris` | ZZZ Botris v0.8 | `Botris` + `search_aspin` | yes | `strength * --search-ms`, max 20 |
| `toj` | ZZZ TOJ v0.8 | `TOJ_v08` + `search_tspin` | yes | `strength * --search-ms`, max 20 |
| `dig` | ZZZ Dig v0.2 | `Dig` + `search_path` | no | fixed native 49 ms; ignored |
| `qq` | AX Attack v0.1 | `qq::Attack` + native QQ composite search | no | native level/preview 1-10 |
| `c2` | C2 v0.1 | `C2` + `search_cautious` | no | `strength * --search-ms`, max 20 |

`--search-ms` is the time represented by one strength unit and defaults to 50
ms. The final time budget is capped at 10 seconds. QQ retains its native fixed
60 ms deadline because its exposed strength is a level/preview parameter, not
a search-time parameter. Dig has no upstream strength control.

The other names found under `upstream/src` are helper PC modules or dormant
evaluator source components; they are not separately configured public bot
frontends in `ai.cpp`, so they are not presented as runnable menu entries.

## Display and controls

The terminal layout follows `tetr`: a 10 x 20 center board, HOLD on the left,
five NEXT pieces on the right, and a left data panel with P/A/S/PPS/APM plus
line, APP, height, holes, B2B, combo, and search timing. It uses the same seven
piece RGB colors, dark background, two-character cells, and gray ghost color.

The default speed is 2.5 PPS, matching `tetr`. Below 5 PPS, every upstream path
operation and each downward cell is rendered with the active piece and ghost;
at 5 PPS or above, placements snap to the selected result.

Controls:

- `Space`: pause/resume
- `+` / `-`: change target PPS by 0.5
- `f`: fastest display speed (100 PPS)
- `q`: exit

## Attack calculation

Each bot receives the chain/status fields accepted by its native interface, so
display work does not alter its decisions. Displayed attack, APP, and APM are
calculated separately from the current rules documented at
<http://tetriswiki.cn/p/TETR.IO> (wiki revision 38236, checked 2026-09-19).
The visualizer models the deterministic multiplayer/Tetra League defaults,
not Quick Play's stochastic rounding:

- SRS+ with current All-Mini+: every non-T immobility spin is Mini; a T
  placement that is immobile but fails the normal T-corner full-spin test is
  also Mini;
- actual last action and ordered SRS+ kick index from the reproduced path;
- current base table for 1/2/3/4 lines: normal or Mini `0/1/2/4`, full Spin
  `2/4/6/10`; the Handheld-only `1/2/3/5` row is not used by All-Mini+;
- `floor((Basic + B2B) * (1 + 0.25 * Combo)) + AC`, with the documented
  `ln(1.25 * Combo + 1)` fallback for combo Singles;
- immediate B2B `+1`, Tetra League B2B Charging/Surge payout on break, and
  `+5` all clear;
- no garbage-clear bonus because this standalone run starts with no garbage.

`tetrio_attack_spec` exhaustively checks 25,704 combinations of B2B 0-8,
combo 0-50, lines 0-6, spin, mini, and all clear against the documented
formula. `tetrio_all_mini_plus` checks 10,752 classifications across all seven
pieces, four rotations, rotation-action state, kick index, immobility, and T
corner masks. `tetr_srs_plus_rule_parity` checks all 28 piece states and 84
rotation transitions directly against the parent SRS+ kick catalog.

## Build and run

```sh
cmake -S zzztoj_standalone -B zzztoj_standalone/build \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build zzztoj_standalone/build -j
./zzztoj_standalone/build/zzztoj_visualizer
```

Windows (Visual Studio with the C++ desktop workload and Windows SDK):

```powershell
cmake -S zzztoj_standalone -B build-windows-zzztoj -DBUILD_TESTING=ON
cmake --build build-windows-zzztoj --config Release --parallel 4
ctest --test-dir build-windows-zzztoj -C Release --output-on-failure
.\build-windows-zzztoj\Release\zzztoj_visualizer.exe
```

MinGW-w64 is also supported; use `-G "MinGW Makefiles"` with a separate
build directory and `-DCMAKE_BUILD_TYPE=Release`. Run in Windows Terminal for
UTF-8 graphics and true-color output. The standalone program restores the
console modes and output code page on exit.

On an interactive terminal, use `W/S` or Up/Down to select a settings row,
`A/D` or Left/Right to change it, Enter to start, and Q to quit. Use
`--no-settings` to start immediately from command-line options.

Useful deterministic runs:

```sh
# Visible run with movement animation
./zzztoj_standalone/build/zzztoj_visualizer \
  --bot toj --strength 1 --pieces 100 --seed 20260919 \
  --search-ms 50 --pps 2.5

# Visible snap-placement run
./zzztoj_standalone/build/zzztoj_visualizer \
  --bot qq --strength 5 --pieces 1000 --seed 20260919 --pps 100

# Unpaced non-visual soak test
./zzztoj_standalone/build/zzztoj_visualizer \
  --headless --bot c2 --strength 2 --pieces 1000 \
  --seed 20260919 --search-ms 50
```

Use `--help` for all options. A nonzero exit status means the native engine
topped out or the upstream path generator failed to reproduce its target.

## Verification

Release tests:

```text
ctest --test-dir zzztoj_standalone/build --output-on-failure
9/9 passed

./zzztoj_standalone/build/tetrio_attack_spec_test
matched 25704 current TETR.IO attack/chain cases

./zzztoj_standalone/build/tetrio_spin_test
matched 10752 current TETR.IO All-Mini+ classifications

./zzztoj_standalone/build/srs_plus_rule_test
matched all 28 tetr SRS+ states and 84 kick transitions
```

TTY checks confirmed colored HOLD/NEXT/board rendering, alternate-screen
cleanup, settings selection, animated movement at 2.5 PPS, and snap placement
at 5 PPS. Dig correctly displays fixed strength as `N/A`.

Five final-binary 100-piece checks at default strength 1 and seed 20260919 all
completed without top-out. They are smoke/compatibility checks, not a claim
that every upstream bot has equal stacking quality:

```text
botris: lines=38 attack=65 height=4  holes=5 dead=0
toj:    lines=39 attack=77 height=3  holes=0 dead=0
dig:    lines=39 attack=12 height=2  holes=3 dead=0
qq:     lines=29 attack=19 height=14 holes=8 dead=0
c2:     lines=31 attack=18 height=12 holes=9 dead=0
```

Two earlier independent Botris 1000-piece runs using a 50 ms search budget:

```text
seed=20260919 blocks=1000 lines=398 attack=827 APP=0.827
              spins=324 mini=308 mismatches=0 height=4 holes=3 dead=0
seed=20260910 blocks=1000 lines=399 attack=712 APP=0.712
              spins=332 mini=316 mismatches=0 height=2 holes=0 dead=0
```

The previous 1.45-1.49 APP totals were too high because non-T immobility spins
were treated as full Spins. Under current All-Mini+, 624 of the 656 spins in
these two runs correctly use the lower Mini row. Spin reachability still
matches the bot target on every placement. Raw summaries are stored under
`logs/`; exact placement statistics may vary slightly because search uses a
wall-clock time budget.

An additional ASan/UBSan build exposes two existing out-of-bounds expressions
in the untouched upstream `ai_zzz.cpp` (`Botris::eval` on an empty map and
`Botris::map_in_danger_` for a very large safe height). They are documented
rather than patched so this remains a faithful native-bot baseline.
