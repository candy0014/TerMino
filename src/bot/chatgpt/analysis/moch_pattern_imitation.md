# Conditional local-pattern experiment

This offline experiment prepares a single bounded local-geometry correction to
the original evaluator. It does not reuse the old learned 13-feature correction,
alter production rewards/search, or learn HOLD. The coordinator's fixed protocol
is `runlogs/chatgpt_moch_20260926/pattern_preregistration.json`. Preparation and
audit are authorized separately from fitting; no fit runs implicitly.

`moch_pattern_imitation.py prepare` accepts all original and expanded placement
exports. It requires exactly the 106 verified Moch rounds / 13,784 placements,
rejects duplicate rounds, and reuses `moch_imitation.prepare_root` unchanged:
predecessor required, supported rules, no pending/intervening garbage, unchanged
pre-lock board/state, roof at most 15, and exact pure placement/clear reconstruction.
The lock snapshot follows HOLD and movement. Current plus five previews are kept
only for audit. Native root input has one selected family, no HOLD, and no future.

The existing archived native exporter verifies spawn reachability, four occupied
cells, spin/mini class, lines, and the complete after-board. Loading its output
reuses the old strict hash/schema/coverage checks. Only `production_present`
candidates are retained; a missing positive or absence of any distinct negative
rejects the root. Every occurrence of repeated conditional root states is removed
globally before training or evaluation. The old 13 exported geometric features
are then discarded. `fixed_reward + old_terrain` remains an immutable base.

The new features are 144 raw pattern counts, independently implemented in Python
and `local_pattern_model.h`. For each column x, visit empty centers from top[x]-3
through top[x]+1 inclusive, clamped to the board. Encode the eight neighbors of
the 3x3 square in this bit arrangement (y points upward):

```text
5 6 7
3 . 4
0 1 2
```

All out-of-map neighbors, including above map.height, are occupied. Each pattern
shares a class with its horizontal mirror; classes are assigned by increasing
`min(code, mirror(code))`, producing exactly 144 classes. Entirely empty code 0
is never counted and its coefficient is fixed to zero. Features use occupancy
alone, without piece, queue, opponent, teacher, line reward, or hidden packet input.
There are at most 50 counted windows on a width-10 board.

Before reading native choices or ranks, prepare fixes the split using counts of
roots passing the observation prefilter. `bennxt` and `torawo` are external fit
holdouts. `icly` and `icly2` form one group; `naraa0327` and `naraa0327_2` form one
group; every other file forms its own group. Sort development groups by descending
prefilter root count, then name. Assign each group to the currently lightest fold,
breaking ties by fold index. Native filtering and duplicate removal do not trigger
rebalancing. Adjacent positions and entire rounds/files remain together.

After native export, `audit` reports counts and feature coverage, never rankings,
and writes an immutable data-specific `preregistration.json`. It references the
coordinator registration and freezes all data/tool hashes. `fit` requires an
explicit matching plan SHA; the coordinator must confirm the audited plan before
that command is executed.

The fixed score and bounds are:

```text
score = fixed_reward + old_terrain + 560 * dot(theta, raw_pattern_counts)
theta[0] = 0
abs(theta[i]) <= 1/7       (abs(table[i]) <= 80 score points)
sum(abs(theta)) <= 2      (table L1 <= 1120 score points)
```

There is no normalization or learned intercept. Each retained replay round has
equal training mass; roots within a round have equal mass. Positive-negative
pairs within a root have equal mass, using at most 64 deterministic negatives
stratified by lines/spin/mini. Projected gradient minimizes pairwise logistic loss
plus lambda/2 times squared theta norm, starting at zero, for exactly 200 steps.
The box/L1 intersection projection fixes class zero before projection.

Only lambda 0.01, 0.1, and 1.0 are considered. Selection uses three-fold OOF
opponent-file-group macro MRR: average rank metrics over retained roots within
each group, then average groups equally; exact ties choose stronger regularization.
Per-round metrics are also reported. After lambda is frozen, fit all development
groups once and report the two external groups once. They are fit holdouts, not
an untouched scientific test or evidence of a win rate. A new fixed local match
protocol remains required for playing-strength acceptance.

Typical data-only sequence (use the currently assigned CPU, CPU5 here):

```sh
taskset -c 5 python3 src/bot/chatgpt/analysis/moch_pattern_imitation.py prepare \
  runlogs/chatgpt_moch_20260926/replay_analysis/*.placements.json \
  runlogs/chatgpt_moch_20260926/replay_expansion/*.placements.json \
  --output runlogs/chatgpt_moch_20260926/pattern_pilot
taskset -c 5 runlogs/chatgpt_moch_20260926/imitation_pilot/moch_candidates \
  runlogs/chatgpt_moch_20260926/pattern_pilot/roots.log \
  > runlogs/chatgpt_moch_20260926/pattern_pilot/candidates.jsonl
taskset -c 5 python3 src/bot/chatgpt/analysis/moch_pattern_imitation.py audit \
  --manifest runlogs/chatgpt_moch_20260926/pattern_pilot/manifest.json \
  --candidates runlogs/chatgpt_moch_20260926/pattern_pilot/candidates.jsonl \
  --coordinator-plan runlogs/chatgpt_moch_20260926/pattern_preregistration.json
```

The separate `moch_pattern_equivalence.{cpp,py}` checks every exported candidate
after-board, all 256 code mappings, all 144 classes/counts, mirrored boards, and
border/high-stack fixtures. A deterministic synthetic table exercises runtime
table summation, including the forced-zero class. This validation is not fitting.
