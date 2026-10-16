2026-09-28 当前交付：默认为阶段5 F（`v0.3.2-rc1`），38个实验全OFF，现有宿主集成程序哈希为 `c91106e0cdc7904170071889c64c0372588cf46e8538a9c547d56b72d4341f7a`。
完整中文策略、候选差异、选择依据和保留清单见[默认版本与候选详解](../../../docs/BOT_CHATGPT_DEFAULT_AND_CANDIDATES_20260928.md)。
从仓库根运行 `cmake --preset chatgpt-stable`，再运行 `cmake --build --preset chatgpt-stable --parallel 4` 可覆盖旧实验缓存；代表候选有独立目录的预设。当前 `KEY_PIECE=ON` 是优化半权重空洞版，不是历史 `key_piece_v3`。下文阶段记录与旧比分按各自冻结版本理解。

局部形状表试验入口为 `TETR_CHATGPT_LOCAL_PATTERN`（默认OFF，与其他试验互斥）。冻结模型来自Moch回放，叠加原评分；对应历史固定10局协议见 `docs/BOT_CHATGPT_MOCH_20260926.md`，后续自然九局协议另见当前交接文档。

# bot_chatgpt v0.3.2-rc1

Current development (2026-09-26): replay-guided terrain candidates are being
tested; see [the current task log](../../../docs/BOT_CHATGPT_MOCH_20260926.md).
`TETR_CHATGPT_TERRAIN_V1` / `TETR_CHATGPT_TERRAIN_V2` /
`TETR_CHATGPT_LEARNED_TERRAIN` are mutually exclusive,
OFF-by-default experiments and cannot be combined with the older options.
No new strength claim or default promotion has been made yet.

The user now requires **exactly 10 games per versus matchup**. New benchmark
runs default to that count; `--versus-games 10` makes it explicit. Draws count
in the denominator. To reproduce a historical FT3 run, explicitly add
`--first-to 3`; old commands below predate the new default. Historical FT3 and
fixed-ten-game outcomes must remain separate in reports.

The working tree contains **model revision F**, retained as the next development
baseline after small-sample validation. At 1:1.3 turn quotas, its new-seed while1
score improved from **0:3 to 2:3**; botris remained **1:3**. Scores always put
bot_chatgpt first: both series were still lost. The full matrix is unverified,
so this retains its release-candidate label, not a claim of general superiority.
The former v0.3.1 baseline remains in `runlogs/chatgpt_stage5_baseline/`.

Stage 6 did **not** find a reliable improvement against botris at quota 1.3.
All three candidates remain experiments; the default source and binary were
restored exactly to v0.3.2-rc1. See the stage-6 results below before reusing them.
Stage 7's caching / Spin-opportunity candidates also failed independent score
validation. Their implementation and tests are archived, not enabled; see below.

Stage 8 retained **read-only root tracing and an offline same-root comparator**.
An extra 80-point covered-empty-cell penalty was rejected: development scores
were 0:3 for both control and candidate, with no evidence of an improvement.
Default scoring remains v0.3.2-rc1 (penalty zero); tracing changes the executable
hash, so older claims of byte-identical restoration apply only to earlier stages.
Stage-7 microprobe iteration-count claims were corrected; see experiments/README.md.
The retained trace build passed 55 CTests and 12 Python tests. Three 1000-piece
ZEN runs completed with 9.938859 aggregate actual PPS and zero fallbacks
(APP 0.9860; not a paired old/new quality comparison). Stage-8 results and
archives are indexed in the development log; the full matrix remains untested.

Stage 9 adds offline pre-break multi-Spin diagnostics in `analysis/`, without
changing the production executable or policy. One two-piece-earlier setup keeps
its finite-horizon score/height-risk advantage after wider-search checks; this
is conditional evidence, not a new match result. See
[`the analysis`](../../../docs/BOT_CHATGPT_SPIN_PATHS.md) and
[`tool methodology`](analysis/README.md) before interpreting the counts.

Development/requirements/results: [`docs/BOT_CHATGPT_LOG.md`](../../../docs/BOT_CHATGPT_LOG.md).

## P2-A search-allocation experiment

`spin_quota_engine.h` is a private adapter over the original public tree types.
It spends the first 80% of the existing 100 ms budget on normal search and up
to 20% probing waiting nodes across depths. A reachable Spin clear can receive
priority continuation work; verification also consumes that budget. Scores,
geometry, garbage forecasts and the opponent remain unchanged. Other children
retain their normal frontier entries, and final selection uses the old score,
not a compulsory B2B or Spin preference. Unknown risen garbage is excluded
from opportunity probing; existing model/horizon limitations still apply.

The build option is **OFF by default** and affects only bot_chatgpt. CMake
remembers options in an existing build directory, so explicitly turn it off
when restoring the stable policy:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=ON -DTETR_CHATGPT_P2_B=OFF
cmake --build build -j 4
# Restore stable policy after the experiment:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=OFF
cmake --build build -j 4
```

This is a compile-time experiment, never a runtime test-type switch. The
evaluator name still reads v0.3.2-rc1; identify P2-A by the option and archived
binary hash. The frozen candidate, source snapshot and registration are in
`runlogs/chatgpt_plan_p2_candidate_a/`. Consult the execution plan and log for
validation status before enabling it; extra search work is not a strength claim.

P2-A completed five independent paired FT3 groups against Botris at 1:1.3.
Both baseline and candidate finished 1 win / 15 losses, losing all five series;
only one group improved, one worsened, and three tied. It failed the registered
promotion gate and remains disabled. Three 1000-piece solo runs reached
9.939119 aggregate actual PPS with zero fallbacks. The stable executable was
restored byte-for-byte; representative/full-matrix and Windows acceptance were
not run for this rejected candidate. P2-B results follow; A is not being retuned.

## P2-B multi-branch continuation experiment

`spin_beam_quota_engine.h` retains the same 80%/20% split and unmodified native
scores, geometry and forecasts. Its local beam keeps at most four paths per
depth, allowing up to two pre-Spin setup moves. If the best four contain no
verified Spin path, one slot is reserved for the best available verified path.
Remaining nodes, including interrupted local work, return to their original
frontier. The global decision still uses the original score, never a forced
chain-preservation rule. P2-A's header remains unchanged for comparison.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=ON
cmake --build build -j 4
```

Both options default OFF and cannot be enabled together. To restore stable
policy, explicitly set **both OFF**, as in the restoration command above.
The evaluator name still reads v0.3.2-rc1; use the archived hash to identify B.

On the existing 30 P1 roots, normal/reversed execution orders produced 0/1
same-depth terminal-score improvements respectively, with no changed first
moves. No improvement repeated in both runs. This failed the pre-registered
mechanism gate: no development or independent matches are claimed for B.
The one reverse-run improvement is retained, not hidden or interpreted as a
win-rate gain. Frozen source/binaries and results are under
`runlogs/chatgpt_plan_p2_candidate_b/`. P2 has used its two-candidate limit;
next is P3's bounded pressure/transition consistency check in the execution plan.

All three 1000-piece solo runs finished with zero fallbacks: aggregate actual
9.944602 PPS and APP 0.973. This is speed acceptance, not paired quality or
versus validation. B remains disabled, and the default binary again matches
the stable baseline exactly. Full distributions are in
`runlogs/chatgpt_plan_p2b_profiles.json` and `chatgpt_plan_p2b_chains.json`.

## P3-A empty-HOLD preview-context experiment

`hold_context_evaluation.h` corrects only the evaluator's NEXT view when an
empty HOLD consumes the first preview. The original native tree's later queue
advance is already correct; its immediate evaluator environment is not. A
branch-local empty-HOLD flag corrects that pointer/length before the existing
spawn check. Rewards, terrain, garbage model and search scheduling are unchanged.
No shared upstream/opponent code or Fusion implementation is imported.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=OFF -DTETR_CHATGPT_P3_A=ON
cmake --build build -j 4
# Restore the stable production binary after testing:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=OFF -DTETR_CHATGPT_P3_A=OFF
cmake --build build -j 4
```

P3-A defaults OFF and cannot be combined with P2 search experiments. Identify
it using the archived binary hash, not the evaluator's unchanged version name.
Registration and frozen artifacts are in `runlogs/chatgpt_plan_p3_candidate_a/`.
The native-tree cursor regression covers delayed empty HOLD and known preview
boundaries; it does not certify the existing known-HOLD/unknown-active tail or
unknown garbage-hole approximation. Match validation is recorded in the log.

P3-A failed development validation: fresh baseline scores were 1:3 and 2:3,
candidate scores 0:3 and 1:3 (bot_chatgpt:Botris, turn quota 1:1.3). Correcting
the context is not evidence of improved strength, and timed-search variation
prevents attributing every match difference to the fix. No new holdout series
or P4 regression was run. Three 1000-piece solo runs reached 9.940252 actual
PPS with zero fallbacks. The candidate remains OFF and the production binary
has been restored exactly to the stable hash; that default still retains the
known empty-HOLD evaluator limitation. Source/tests are kept for future work.

## P3-B post-rise entry-validation experiment

`rise_context_evaluation.h` filters virtual continuations that still cannot
reach their translated pose in a permissive post-rise geometry. It raises only
known occupied cells; newly risen garbage is left empty, not assigned invented
holes. Rotations keep possibly blocked kicks until a certainly free kick, and
Clutch starts follow the same uncertainty rule. The check certifies neither a
Spin nor a whole future plan. A single-parent content-checked cache bounds its
storage, and all validation stays inside the original 100 ms budget.

The original relative-board model remains approximate, including digging into
unknown garbage. This candidate addresses the future spawn/reachable-entry
mismatch only; it does not include P3-A or any search/weight change. An unusable
continuation uses the existing terminal penalty, without rewards for that
unreachable placement; this is not a claim that every route from its parent dies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=OFF -DTETR_CHATGPT_P3_A=OFF -DTETR_CHATGPT_P3_B=ON
cmake --build build -j 4
# Stable restoration requires all four cached experiment options OFF:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=OFF -DTETR_CHATGPT_P3_A=OFF -DTETR_CHATGPT_P3_B=OFF
cmake --build build -j 4
```

The option defaults OFF and cannot be combined with other experiments. Frozen
artifacts and registration belong to `runlogs/chatgpt_plan_p3_candidate_b/`;
consult the execution plan/log for results. No shared upstream or Fusion code
is changed or imported; no test/mode/opponent identifiers enter this guard.

P3-B did not pass the registered development gate. Fresh paired-series starts
0401/0701 (prefix 202610) scored baseline 0:3, 1:3 versus candidate 1:3, 0:3;
scores are bot_chatgpt:botris at quotas 1:1.3. Both versions won 1/7 games, with
one group worse for the candidate. No new holdout or P4 run was started. The
30-root mechanism comparison filtered branches in seven roots but changed no
chosen first move, terminal score or depth in either order. The regression
rejects 30 sealed-divider counterexamples and preserves 1576 materialized-board
landings; it does not certify every future route. Candidate CTest 61/61 and
Python 21/21 passed. Three 1000-piece solo runs reached 9.939522 actual PPS,
with zero fallbacks. All four experiment options are OFF; the default binary
is restored exactly to the stable hash, and restored CTest passed 61/61.
Full results and limitations are recorded in the handoff log and
`runlogs/chatgpt_plan_p3b_acceptance.json`.
P3 has used both candidates; do not silently enable A/B or add P3-C.

## P5-A full-entry coverage experiment

P5-A population experiment (completed, not promoted): `coverage_search.h` starts
movement BFS at the actual entry instead of descending the spawn column first.
It preserves the original geometry/strongest-Spin deduplication, evaluator,
search allocation and 100 ms budget; it may trade search work for move coverage.
Build with `TETR_CHATGPT_P5_A=ON` and all P2/P3 options OFF. The option defaults
OFF and cannot be combined with previous experiments. Stable restoration now
requires **all five** options OFF, including `TETR_CHATGPT_P5_A`.

The user requested larger population comparisons. Frozen artifacts and the
46-job manifest are in `runlogs/chatgpt_plan_p5_candidate_a/`. Each version gets
20 FT3 series on disjoint unused seeds, alternating execution order, followed
by three runs each of ZEN/layer1.0/survival70. No per-paired-group veto or
score-based early stopping. `population_report.py` reports pooled fractions,
series Wilson intervals and whole-series bootstrap differences; auxiliary
metrics are exploratory, and neither an interval nor a point estimate alone
promotes a default without broader regression. See the updated plan/log.

The exploratory three-run digging comparison prompted a separately registered
15-trial-per-version confirmation on fresh seeds. Its exact fixed sequence is
`dig_confirmation_manifest.json` in the same archive. Exploration is excluded
from confirmation; `dig_confirmation_report.py` resamples whole trials and
checks completion before efficiency. Neither report is imported by the BOT.

All 76 jobs finished (195 games). Against Botris at turn quotas 1:1.3, baseline
won 19/76 games and 2/20 series; P5-A won 11/71 games and 0/20 series. The game
fraction difference was -9.51 percentage points, with a whole-series bootstrap
95% interval [-22.31, +2.95]; this is no improvement evidence, not a claim of
statistically established decline or equivalence. Fresh digging confirmation
completed 15/15 per version: pieces/garbage-line 2.397206 versus 2.309793
(3.65% lower), but its difference interval [-0.193843, +0.018307] crossed zero.
That signal remains unconfirmed. Three survival70 trials showed higher mean
survival (120.333 versus 254.333 pieces), but are exploratory, not confirmation.
ZEN APP was lower (0.975000 versus 0.955333); candidate actual PPS was 9.932580.

The 87,201-placement artifact audit passed with zero fallbacks; candidate and
restored CTest 62/62 and final Python tests 29/29 passed. All five options are OFF and the
production binary again matches the stable hash exactly. Frozen candidate
artifacts remain available; full-matrix/P4 and Windows acceptance are not done.
See `runlogs/chatgpt_plan_p5a_acceptance.json`, the population/confirmation
reports, and the handoff log. Root-only full coverage is the next bounded
hypothesis in plan section 14, not an implemented or proven improvement.

To restore the stable policy after any current experiment:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=OFF -DTETR_CHATGPT_P3_A=OFF -DTETR_CHATGPT_P3_B=OFF -DTETR_CHATGPT_P5_A=OFF
cmake --build build -j 4
```

## Implementation

- `../bot_chatgpt.cpp`: per-thread search workspace and host entry point. Only
  current/visible NEXT/HOLD, board, chain and incoming buffer are used. Test IDs,
  environment variables, hidden bags and garbage RNG are not used by the policy.
- `search.h`: reachable placements on zzztoj's precomputed SRS+ graph; remembers
  successful kick indices and uses the host `Rules::detect_spin` classifier.
- `evaluation.h`: zzztoj Botris terrain evaluation plus a separate policy using
  the host `Rules::update_chain` and `Rules::calculate_attack`. Supports full
  numeric B2B charging, combo multipliers, mini attacks and garbage clear bonus.
- `policy.h`: fixed baseline weights 560/480/240 for attack/B2B/garbage clearing.
  Extra Spin preference and pressure-discounted B2B failed versus validation and
  are archived, not enabled. The older firepower / fast-dig blend and bounded
  combo reserve are retained but disabled. No weights are inferred mochbot internals.
- `forecast.h`: advances only already visible garbage packets along the numeric
  game-clock pace, preserving FIFO cancellation and maturity. Opener cancellation
  uses the host's explicit rule capability and post-lock piece count.
- `../native_host.h`: shared geometry conversion, host reachability verification
  and legal fallback (implemented alongside the existing zzztoj adapter).
- `benchmark.py`: isolated runs of the **actual main executable**, raw placement
  records, scalar CSV and JSONL including distributions. It is not another game
  simulator. `smoke` covers four representative cases; it is not the full suite.
- `profile_report.py`: primary outcomes/efficiency and secondary full histograms
  plus explicitly defined small/middle/large bands. Better-looking tails cannot
  override worse completion, wins, digging efficiency or survival.
- `chain_report.py`: read-only chain extensions/breaks, completed-chain length
  histograms and separately censored final chains, with input hashes. Zero-line
  preservation does not count toward the active-clear retention rate.
- `search_report.py`: saved per-depth evaluation counts, selected-plan depth
  distributions and geometry-cache work, with input hashes. These are work
  measurements, not unique-state counts or a proxy score for game strength.
- `root_snapshot.h`, `fixed_root_search.h`, `root_probe.cpp`, `root_compare.py`:
  offline, fresh-tree comparison of weight 0 versus 80 using the same visible
  root and a fixed number of full zero-ms search passes. Records HOLD/NEXT,
  actual pose, chain state, visible packet timing and terminal search estimates;
  checks host reachability. It is not a full game simulator, the original warm
  timed decision, or an estimate of actual win rate. No hidden bag or garbage
  holes are supplied. `terrain_features.h` counts empty cells below occupied cells.
- `audit_stage4.py`: independent read-only audit of retained artifacts;
  `--runs-prefix` selects stage-5 cohorts without changing the stage-4 default.

The evaluator models known ready garbage rising by up to eight rows on a
non-clear. A clear stalls it, risen rows cannot be cancelled later, and a dead
intermediate position remains dead. A vertical offset models spawn pressure;
unknown hole positions and future opponent attacks are not invented. Existing
packets can mature within the visible preview, and multiplayer opener
double-cancellation is modeled without counting it as extra raw attack.
Actual root pose, HOLD respawn and clear-dependent Clutch are checked; dead
paths cannot earn posthumous rewards. Virtual risen garbage is still an
approximation, not a complete future-board simulation.
The policy also charges discounted risk along the whole plan, not just its
final board, so a later clear cannot erase an earlier exposed position.
Support is currently standard 10x20 SRS+ / seven-bag / positive normal gravity,
with visible NEXT and optional HOLD. Unsupported rule variants fail initialization.
Use Settings -> AUTOPLAY -> Bot -> bot_chatgpt. Config selection index is **9**;
historical replay serialization ID is **12** (other IDs are unchanged).

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTETR_CHATGPT_P2_A=OFF -DTETR_CHATGPT_P2_B=OFF
cmake --build build -j 4
ctest --test-dir build --output-on-failure
python3 tests/bot_chatgpt_benchmark_test.py
python3 src/bot/chatgpt/benchmark.py --suite zen --repetitions 3 --output runlogs/chatgpt_zen_new
```

On Windows use `--config Release`, `ctest -C Release`, and pass
`--binary build/Release/tetr.exe` to the Python runner if using Visual Studio.
Output directories must be new; previous runs are never overwritten. Search width
300 maps to 100 ms per decision (range 1–100 ms; an indivisible expansion can overrun
the budget). Width 150 is the 50 ms fast profile, and width 60 is 20 ms.
This spends more computation on quality while targeting at least 8 actual solo
PPS on the development computer; it is not a promise for every hardware setup.
Timed search can select different
plans with identical seeds on different hardware; retain source/binary hashes
and repeat measurements. Wall PPS includes process startup and, for versus,
the normal countdown. Run benchmarks without other heavy jobs.

`--suite solo` expands all 19 solo cases; `--suite full` additionally selects all
8 native Local Versus cases. Each versus point continues until one side has
3 wins (100-game safety cap). Following the corrected test specification,
**both players use Simulation PPS=1**, and native alternating play uses
**Turn Pieces=1 for the subject, 0.9/1/1.15/1.3 for the opponent**.
For example, select `--suite versus --case vs_4_turn1.0` for equal-quota Botris.
Historical `vs_*_pps*` results used concurrent play and are not valid results
for this corrected protocol. They remain readable via `--reparse` for audit.
The initial development phase has not validated the full matrix.

Pass `--save-replays` to preserve native `.replay.txt` files before the isolated
working directory is removed. Copy one into the project's `replay/` directory
to load it in Replay mode. The raw logs and `.decisions.json` record the board,
known/ready incoming garbage, chosen pose, fallback reason and search time.
Diagnostics are output by the host adapter; the BOT does not read test flags.
Current `trace_version=1` logs include inputs for the offline probe. Numeric
timings use 17-digit output (early stage-8 control/candidate logs use six digits
for `forecast_step`, exactly 1 in those tests, and 17 for packet times).
Reconstructed boards preserve occupancy, not block colors; real garbage height
is supplied separately. Old logs without these root fields are rejected.
Build with `BUILD_TESTING=ON`, then compare raw logs using a new output file:

```sh
python3 src/bot/chatgpt/root_compare.py path/to/native.log --iterations 150 --stride 3 --repeat-check --output runlogs/roots_new.json
```

Use `--binary build/Release/bot_chatgpt_root_probe.exe` on Visual Studio builds.
Completed search trees may stop doing work before the pass count; equal passes
do not mean equal evaluated nodes or equal milliseconds. The initial stage-8
`chatgpt_stage8_roots_control.json` used an ineffective parameterless continuation
on fresh roots and is **invalid**; the corrected result is `..._control_v2.json`.

When comparing an archived executable, supply `--binary-source-metadata` with
its original metadata JSON so its binary hash is verified and its original
source manifest retained; the ordinary source hashes describe the current tree.

## Historical v0.3 validation snapshot

On this Linux machine, the default profile measured about 9.92 solo PPS. In
three fresh paired seeds per pressure, mean survival increased from 127 to 309
pieces at APM60 and 279 to 380 at APM70, but none reached 1000 pieces. Equal-seed
ZEN APP improved from 0.928 to 0.982. The trade-off is slower 100-line digging
(221 to 258 pieces on average); the 1:1.3 while1 match remains **1:3**.
These are small-sample results, not a universal improvement or equal-compute
comparison: v0.2 used 50 ms and v0.3 uses 100 ms.

Use `runlogs/chatgpt_stage3_final_*_v03/` for that historical profile. Earlier
`chatgpt_stage3_*_v03/` directories without `final` contain a 50 ms candidate.
All 75 stage-3 games were retained; 55 CTests and 6 Python tests passed.
The full 27-case matrix and Windows execution remain unverified.

## Stage 4: maintenance release, experiments not promoted

Seven automatic-blend / dual-search candidates were tested. The fast-dig blend
reduced pieces per actual garbage row by 9.3% at messiness 1.0 and 3.2% at 0.4
in three paired seeds, but APM60 survival regressed. A conservative blend
improved APM70 in another three seeds but worsened APM60/digging; while1 at
1:1.3 remained **bot_chatgpt 1:3 while1**. None replaces the stable default.
These are small samples, not claims of statistical significance.

The production v0.3.1 keeps the v0.3 scoring weights and 100 ms budget, and
does not call `experimental_policy_for`. It fixes an upstream precondition:
`update()` must not run before the first valid `run()` initializes preview
length. Experimental policies and raw results remain available for future work.

Stage-4 directories named `final_*_v04` contain rejected candidate B;
`acceptance_*_v04` contain rejected candidate F. **Neither is a release.**
Use `chatgpt_stage4_stable_*_v031` for the maintenance release's runtime checks.
The final build passed 55 CTests and 8 Python tests. Three 1000-piece ZEN runs
and three 100-row digging runs completed, with 9.923 aggregate solo PPS and
no fallbacks. ZEN APP was 0.9677; digging used 2.45 pieces per garbage row.
There is no equal-seed old/new quality comparison for this maintenance build.
Independent audit (from the repository root):

```sh
python3 src/bot/chatgpt/audit_stage4.py
python3 src/bot/chatgpt/audit_stage4.py --verify-current --candidate-prefix chatgpt_stage4_stable_ --candidate-suffix _v031
```

Hash verification requires the matching build and source tree; a different
computer/platform may produce a different executable. Missing archived
binaries are explicitly reported rather than claimed to have been rehashed.

## Stage 5: model corrections, small-sample versus gain

The retained changes model actual root pose, clear-dependent Clutch and HOLD,
FIFO maturity of visible packets, opener double cancellation, and terminal
death. Fixed attack/B2B/garbage weights and the 100 ms budget are unchanged.
Extra Spin rewards and pressure-discounted B2B failed independent validation;
their source/binaries/results remain archived, not enabled by the BOT.

Frozen F was tested from seeds 2026100301 onward, with both Simulation PPS=1
and opponent turn quota 1.3. Each series stopped at three wins by either side,
so old/new series can have different lengths and are not fully paired by seed.
Against while1, special clears rose from 56.23% to 67.91% and Spin density from
20.38 to 23.75 per 100 pieces, but APP fell from 1.2357 to 1.1219. The improved
2:3 score takes precedence. These are descriptive small-sample observations,
not a causal reconstruction of mochbot or proof of universal improvement.

The final build passed 55 CTests and 8 Python tests. Three ZEN games completed
1000 pieces each with APP 0.9670 and **9.939 aggregate solo PPS**; this is runtime
acceptance, not an equal-seed old/new ZEN comparison. All 3722 F decisions had
zero fallbacks. Digging/survival, other versus quotas and Windows were not
retested for F. The entire stage retained and audited 76 games, 149 player
records and 32290 placements, including controls and failed candidates.

Results: `runlogs/chatgpt_stage5_model_versus_report.json` and
`runlogs/chatgpt_stage5_all_profiles.json`. Frozen F source/binary provenance:
`runlogs/chatgpt_stage5_candidate_f/`. Archives are scoped BOT snapshots, not
self-contained repository backups; also sync the uncommitted working tree and
latest development log. Verify the current matching source/build with:

```sh
python3 src/bot/chatgpt/audit_stage4.py --runs-prefix chatgpt_stage5_model_ --verify-current --candidate-prefix chatgpt_stage5_model_ --candidate-suffix _f
```

## Stage 6: B2B experiments rejected by the score gate

A confirmed an incomplete spawn-drop pruning boundary: some legal landings on
tall side platforms are omitted. The destination-low fix plus constructive
coverage regressions are preserved in
[`experiments/complete_spawn_drop.patch`](experiments/complete_spawn_drop.patch),
**not enabled in the default search**. This is incomplete candidate coverage,
not evidence of illegal emitted moves. See [experiment notes](experiments/README.md).

Scores below are bot_chatgpt:botris, with both Simulation PPS=1 and turn quotas
1:1.3. Each series stops when one side wins three games; differing series lengths
are not fully paired samples. Timed search is not bitwise seed-deterministic.

| Cohort seed start | Original v0.3.2-rc1 | A (coverage fix) |
|---|---:|---:|
| 2026100401, development | 0:3 | 2:3 |
| 2026100501, independent | 1:3 | 1:3 |
| 2026100601, independent | 1:3 | 0:3 |

B valued actual banked charge plus a bounded continuation premium; C reduced
only that premium under visible pressure. Development scores were 1:3 and 0:3.
B raised active-clear retention from A's 71.83% to 80.93%, but did not improve
the score. Neither was promoted. No test/mode/opponent-ID policy branch was added.

All 33 games / 63 player records / 19719 placements were audited with no
fallbacks. A's three 1000-piece ZEN runs measured APP 0.981667 and 9.937 solo PPS;
these are rejected-candidate runtime checks, not new default-version results
or an equal-seed ZEN quality comparison. Restored baseline binary SHA256 remains
`c53f70a094158a9b8d843230df984f91a14ce285cad14fa0b014a448e5669f48`.
The restored build passed all 55 CTests and 10 Python reporting tests.

Results are `runlogs/chatgpt_stage6_all_profiles.json` and
`runlogs/chatgpt_stage6_all_chains.json`. Archives in
`runlogs/chatgpt_stage6_baseline/` and `chatgpt_stage6_candidate_a/b/c/` retain
source snapshots, binaries and provenance; they are not complete repo backups.
Current reporting code and the final development log supersede freeze-time notes.
Recompute independent-cohort chains into a new file with:

```sh
python3 src/bot/chatgpt/chain_report.py runlogs/chatgpt_stage6_holdout1_botris_v032 runlogs/chatgpt_stage6_holdout1_botris_a runlogs/chatgpt_stage6_holdout2_botris_v032 runlogs/chatgpt_stage6_holdout2_botris_a --output runlogs/chatgpt_stage6_chains_repeat.json
```

Next investigate branch cost/search depth with complete placement coverage and
preview-supported Spin opportunities, not just larger B2B reserve rewards.
Seeds through the 2026100601 cohort are observed; use a fresh independent cohort.

## Stage 7: caching / Spin opportunities, not promoted

A corrected placement coverage and cached geometry with full board/start checks.
B also guided the first two search levels using a legal Spin clear by visible
NEXT or HOLD (bounded at 560), without changing actual attack or cumulative
reward. The estimate vanished deeper in search and was disabled for unknown
risen garbage. C changed the depth taper from 1.5 to 0.5, increasing deep work
but losing development 0:3. None read a mode, test ID or opponent identity.

With turn quotas 1:1.3, development scores were A 0:3, B 3:2, C 0:3; the old
baseline's historical score for those seeds was 0:3. In two fresh cohorts,
baseline scored 0:3 and 1:3, while frozen B scored 0:3 and 0:3. Thus the default
was restored unchanged. No reliable versus improvement is claimed.

All 27 new games / 51 records / 15919 placements were audited without fallbacks.
B completed three 1000-piece ZEN runs at 9.937302 solo PPS, APP 0.969333; these
are rejected-candidate runtime checks, not new default-version quality results.
The current baseline binary hash remains the stage-5 value shown above.
The restored default passed all 55 CTests and 11 Python statistics tests.
Reports: `runlogs/chatgpt_stage7_all_{profiles,chains,search}.json`.

Reusable implementation and dedicated C++ regressions are in
[`experiments/cached_spin_opportunity.patch`](experiments/cached_spin_opportunity.patch).
It also contains the coverage fix; do not stack it with the stage-6 patch.
See [experiment instructions](experiments/README.md) to reconstruct B and run
its deterministic fixed-expansion probe. The default build has neither those
algorithm changes nor the probe target; reporting scripts remain available.
Frozen A/B/C sources/binaries/results are retained under `runlogs/`.

Next use identical real root positions to test candidate explanations before
further tuning; a deeper tree or a certified next Spin does not alone establish
a better long-term position. The 0701/0801 cohorts are now observed.

## Upstream dependency

Reuses `TetrisAI/tetris_ai_runner` commit
`ce12b474af8438abec77ae8ec69c8f24a918c7b3` under
`zzztoj_standalone/upstream`, its MIT license, and this project's SRS+ adapter.
Keep `zzztoj_standalone/upstream/LICENSE.md` with distributions. The original
terrain evaluator and tree infrastructure are credited to ZouZhiZhang; the new
BOT does not claim them as original work.

At task start the local upstream `ai_zzz.cpp` already contained bounds fixes.
`upstream_bounds.patch` records those existing fixes for another computer;
do not reapply it if `git apply --check` reports it already applied. The new BOT
also explicitly guards the empty-board terrain evaluation. No upstream files
were modified for this BOT implementation.


## 2026-09-27：关键块预放置实验

`TETR_CHATGPT_KEY_PIECE`（默认 OFF）识别具体 Spin/Mini/四消等关键消行，核对可见 NEXT/HOLD 的供给，在棋盘副本中完成关键消行后评价剩余地形。单块可补成的弱结构获得折扣信用，实际补形动作有额外终端奖励。结构、可达路径和计划由现有线程级全局 workspace 跨相邻决策缓存；真实攻击、链和垃圾状态不写入虚拟收益。

实现范围、缓存失效条件、测试与运行方法见 [关键块实验说明](../../../docs/BOT_CHATGPT_KEY_PIECE_20260927.md)。旧 `KEY_QUAD` 留作历史实验，新功能没有替换默认策略。
