# Refactor Stage 39: Frame Timing Types

Changed Frame rule delta, elapsed game time, incoming-garbage delay and EPLD values from interchangeable doubles to `Runtime::Duration`. Numeric conversion remains only where formulas/config compatibility require raw seconds.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
