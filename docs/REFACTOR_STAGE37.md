# Refactor Stage 37: QP Encoded Operation Types

Separated the two Quick Play bit encodings into `Runtime::QPLastOperation`/`ExtendedAllSpinOperation`-style value boundaries (initially rule-owned in this stage), made clear-operation piece ids `Piece::Type`, and removed raw encoded-int mixing from placement application code. Wire/BOT integer representation is unchanged.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
