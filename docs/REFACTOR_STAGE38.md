# Refactor Stage 38: Garbage Packet Semantic API

Kept the three historical `GarbagePacket` fields for protected BOT ABI/source compatibility, but added semantic accessors/mutators and migrated normal Rules/Network code to line count, arrival time and preserve-hole semantics. No gameplay or wire format change.

## Compatibility rule

Replay/network/config external formats and protected BOT strategy source are not rewritten in this stage. Legacy conversions are kept at explicit boundaries only.

## Validation focus

The affected focused regression tests are rebuilt and run before the final full Debug/Release suite. Protected BOT strategy header hashes are checked against the Stage 36 baseline.
