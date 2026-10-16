# Refactor Stage 43: Public Header Layout

Moved Settings UI, user config, input action and BOT adapter headers into their owning `app/`, `runtime/` and `legacy/` directories; moved Settings implementation into `src/app`. After this stage only compatibility headers remained at `include/tetr/` root.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
