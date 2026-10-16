# Archived experiments (not default policy)

Stage-8 notice: these patches target the **archived, pre-trace v0.3.2-rc1**
tree, not necessarily the current files with root diagnostics. Preserve current
changes and port in a separate worktree; do not force an overlapping application.
The stage-7 probe's parameterless `run_hold()` can return without expansion on a
fresh root. Its claim of 102 effective passes on every board is withdrawn; saved
work counters still describe the work actually performed, not that budget.
Use stage-8 `fixed_root_passes` (repeated full zero-ms overloads) when porting it.
The normal timed playing interface and saved match scores are unaffected.

## Stage 7: caching and certified Spin opportunity

`cached_spin_opportunity.patch` reconstructs candidate B (v0.3.4-rc2) from the
v0.3.2-rc1 baseline, including its search diagnostics, deterministic probe and
coverage/opportunity regressions. It already includes the stage-6 boundary fix:
**choose this patch OR `complete_spawn_drop.patch`, not both.** The patches are
source-level experiments, not runtime mode switches.

B memoizes only legal geometry using the exact board and actual resolved start.
Shallow beam scoring gets a bounded estimate for a verified Spin clear by visible
NEXT/HOLD; deep scoring, actual attack and accumulated reward remain unchanged.
The estimate is disabled for unknown risen garbage or exhausted visible preview.
C additionally changed the depth taper 1.5 to 0.5, but was rejected (development
0:3 despite more deep evaluations); that change is not in this patch.

B won development 3:2, but independent cohorts were 0:3 and 0:3 against botris
at 1.3 turn quota. Original results were 0:3 and 1:3. B therefore failed the
primary-score gate. Higher work counts or local Spin opportunities are not proof
of better decisions. Source snapshots and binaries remain under
`runlogs/chatgpt_stage7_candidate_a/`, `..._b/`, and `..._c/`.

On the restored baseline, inspect the patch; apply only for deliberate new work:

```sh
git apply --check src/bot/chatgpt/experiments/cached_spin_opportunity.patch
git apply src/bot/chatgpt/experiments/cached_spin_opportunity.patch
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/bot_chatgpt_search_probe 0
./build/bot_chatgpt_search_probe 1
```

The archived probe uses twelve deterministic synthetic boards; see the stage-8
correction above before using its iteration count. Cache on/off must give matching target/spin/
hold/value lines. Run timing checks without concurrent benchmarks or builds.
`search_report.py` remains usable without applying the patch; it reads the saved
stage-7 diagnostics and reports absent historical counters as null.

## Stage 6: complete spawn drop only

`complete_spawn_drop.patch` reconstructs stage-6 candidate A from the current
v0.3.2-rc1 baseline. It contains the search boundary correction, version label
and deterministic coverage regressions as one unit. It does not alter game rules
or detect test types. Current C++ tests intentionally remain those of the retained
baseline; the added coverage regression fails on that baseline and passes on A.

The defect is confirmed: dropping the search start one row too far omits legal
O landings on a five-row side tower. Checking the destination's `low` keeps
horizontal movement above the obstacles. A also verifies all constructive
high-altitude rotate/translate/hard-drop placements on twelve synthetic boards.
This verifies those coverage cases, not exhaustive reachability for every board.

Despite development score 2:3 (baseline 0:3), A scored 1:3 and 0:3 on two fresh
botris/1.3 cohorts, versus baseline 1:3 and 1:3. It therefore failed the user's
primary-score gate. The default search still has the documented coverage limit;
restoring the baseline is not a claim that the defect disappeared.

From the repository root, inspect without applying:

```sh
git apply --check src/bot/chatgpt/experiments/complete_spawn_drop.patch
```

For deliberate future experiment work, first preserve any overlapping changes,
then apply and rebuild:

```sh
git apply src/bot/chatgpt/experiments/complete_spawn_drop.patch
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

There is no runtime enable flag. Do not use old default-binary provenance after
applying it. Repeat independent versus validation before promoting the change.
The patch was checked against the restored baseline, and its reverse was checked
against frozen A; it is not automatically applied by CMake or the benchmark runner.

B (banked charge plus bounded continuation) and C (pressure-decayed continuation)
were rejected too. Their complete scoped source snapshots, tests, binaries and
metadata are in `runlogs/chatgpt_stage6_candidate_b/` and
`runlogs/chatgpt_stage6_candidate_c/`; A and the original are in
`chatgpt_stage6_candidate_a/` and `chatgpt_stage6_baseline/`. These archives require
the rest of the project and dependencies. Consult the final
[`development log`](../../../../docs/BOT_CHATGPT_LOG.md) for provenance, measurements
and limitations, rather than older notes inside the archives.
