# Refactor Stage 45: Typed Quick Play Player State

Moved QP encoded-operation value types into Runtime state ownership. `PlayerState::quickPlay` now stores `QPLastOperation` and `vector<ExtendedAllSpinOperation>` directly; the sentinel 8191 is private to the value type. Removed unused `QP_blighted` and `QP_ops` fields from the Board BOT façade; `QP_last_op` remains a read-only legacy snapshot because protected bots read it.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
