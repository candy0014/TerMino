# Refactor Stage 46: Legacy Header Containment

Moved PPP and Timer compatibility headers under `tetr/legacy/`. The only files left directly under `include/tetr/` are `map.h` and `block.h`, retained solely because protected BOT headers contain byte-preserved `../map.h` / `../block.h` includes. This marks the practical end of normal-mainline legacy-header cleanup without editing BOT sources.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

Final validation after Stages 37-46: Debug 33/33 and Release 33/33 tests pass; 87/87 non-BOT public headers compile standalone as the first include; all 8 protected BOT strategy header SHA-256 hashes exactly match the Stage 36 baseline.
