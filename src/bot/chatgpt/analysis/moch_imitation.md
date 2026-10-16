# Conditional Moch placement imitation

This is a small offline experiment, not an enabled policy. It does not change
HOLD, move generation, search depth/budget, or attack/B2B/garbage rewards. Native
enumeration and explicit fitting should run only on the CPU reserved by the
active experiment coordinator; the example below uses CPU 5.

`moch_imitation.py prepare` reads the existing reviewed official-engine exports.
It keeps only mochbot, uses the demonstrated piece family as a condition, skips
the first placement of each round, and requires no pending garbage at the
preceding spawn or current lock. It rejects intervening board/garbage changes,
high-stack spawn ambiguity, and any mismatch between the recorded after-board
and a reconstructed pure placement plus line clear. The latter reconstruction
retains colors and garbage masks. Official coordinates use `ceil(y)` and the
piece matrix offsets, including O's distinct horizontal origin.

The source `before` snapshot is after HOLD. This experiment deliberately does
not learn whether to HOLD. The legacy source `next` contains current plus six
raw bag entries: only current plus five are retained as manifest audit data.
Native input receives exactly one selected family and HOLD disabled. Neither
queue, source identifiers, nor the demonstrated target enter learned features.

`moch_candidates.cpp` uses the native full movement graph, independent host
reachability, and pure attachment/clear. It matches the positive's cells,
spin/mini class, cleared lines, and complete post-clear board. Missing native
or production-search positives are counted and rejected. It exports the same
13 fixed terrain features for all candidates and includes the complete input
line so the Python reader can verify its SHA-256 against the manifest.

The primary-well selector uses explicit, fixed original V1 weights. **Changing
learned coefficients must not reselect the well.** A future production trial
must preserve this exact feature definition and training normalization.

From the repository root, with a freshly built separate exporter supplied as
`/tmp/moch_candidates` (not the live match binary):

```sh
taskset -c 5 python3 src/bot/chatgpt/analysis/moch_imitation.py prepare \
  runlogs/chatgpt_moch_20260926/replay_analysis/mochengine2vszzz.placements.json \
  runlogs/chatgpt_moch_20260926/replay_analysis/talothebot.placements.json \
  runlogs/chatgpt_moch_20260926/replay_analysis/5han.placements.json \
  --output runlogs/chatgpt_moch_20260926/imitation_new

taskset -c 5 /tmp/moch_candidates \
  runlogs/chatgpt_moch_20260926/imitation_new/roots.log \
  > runlogs/chatgpt_moch_20260926/imitation_new/candidates.jsonl

taskset -c 5 python3 src/bot/chatgpt/analysis/moch_imitation.py audit \
  --manifest runlogs/chatgpt_moch_20260926/imitation_new/manifest.json \
  --candidates runlogs/chatgpt_moch_20260926/imitation_new/candidates.jsonl
```

Preparation and audit do not import NumPy or fit coefficients. The exporter
does native enumeration but performs no beam search or match. Fit is a separate,
explicit command; it pins numerical libraries to one worker:

```sh
taskset -c 5 python3 src/bot/chatgpt/analysis/moch_imitation.py fit \
  --manifest runlogs/chatgpt_moch_20260926/imitation_new/manifest.json \
  --candidates runlogs/chatgpt_moch_20260926/imitation_new/candidates.jsonl \
  --output runlogs/chatgpt_moch_20260926/imitation_fit_new
```

Only NumPy is needed for fitting. New output directories are required; existing
exports, source replays and model artifacts are not overwritten.

The fitted score is a bounded correction in the existing terminal-score units:

```
fixed_reward + old_terrain + 560 * dot(theta, (fixed_features - mean) / std)
```

`fixed_reward` comes from the existing evaluator with old terrain subtracted;
it retains the existing immediate rule rewards and height-risk terms. Fitting
cannot scale those terms or the old terrain. Each standardized-feature
coefficient has `abs(theta) <= 0.5`, and `sum(abs(theta)) <= 1`. Thus arbitrary
post-fit rescaling would violate the artifact contract. Reports include actual
correction magnitudes in score units, not merely imitation accuracy.

The pairwise logistic objective gives every root equal mass and uses at most
64 deterministic, clear-class-stratified unchosen alternatives per root. These
are preferences, not proven mistakes. Scaling is fitted on training roots only.
Repeated identical root states are removed entirely. Three fixed regularizers
(0.01, 0.1, 1) are compared in three whole-round folds: vs zzz rounds 1–3, 4–6,
and 7–9. The model then fits all eligible vs zzz roots. The four exported talos
and 5han rounds receive one external report after the model choice is frozen.
Each fit is capped at 200 deterministic projected-gradient steps.

The files were already inspected in manual terrain analysis. The external
report is a fit holdout, not a previously untouched scientific test. Closely
related positions remain correlated within each round. Domain transfer also
discards official placements whose kick/spin/clear behavior differs from native
rules. This experiment teaches neither garbage timing nor full HOLD/queue
strategy, and one-step preferences may miss the teacher's later plans.

`model.json` contains feature definitions, fixed-selector metadata, normalization,
bounded coefficients and input/tool hashes; `enabled_in_production` is false.
An improvement in round-macro imitation rank does not imply a higher win rate.
Any production integration and fixed match protocol are separate decisions.
