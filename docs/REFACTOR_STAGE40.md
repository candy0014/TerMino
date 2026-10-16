# Refactor Stage 40: App Namespace and Layout

Moved Game/Init/Place headers into `include/tetr/app/`, unified their API under namespace `App`, updated callers, and removed the old root header locations. Behavior is unchanged.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
