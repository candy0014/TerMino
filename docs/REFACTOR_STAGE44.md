# Refactor Stage 44: Piece Catalog Storage Isolation

Renamed the actual piece geometry/metadata storage to private `PieceCatalogData` names and made normal `Piece` implementation use them directly. `PPP::allBlock/allblock_*` became reference aliases for BOT compatibility/regression tests rather than the primary runtime model.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
