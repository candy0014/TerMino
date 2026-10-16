# Refactor Stage 41: Monotonic Clock

Replaced the normal-code `Timer` dependency with `Runtime::MonotonicClock`. `GameContext::now()` now returns the clock's typed `TimePoint` directly and normal sleeps use `Duration`. `get()/sleep(double)` survive only through the BOT compatibility alias.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
