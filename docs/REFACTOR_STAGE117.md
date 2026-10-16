# Stage 117 - BOT n-bag remainder metadata interface

This stage extends the BOT call boundary without changing any BOT strategy.

## New BOT input

Every `Autoplay::Strategy::choose_position()` call now receives one additional
parameter:

```cpp
const std::vector<Piece::Type>& unseenBagPieces
```

The legacy BOT implementations receive the equivalent legacy piece IDs:

```cpp
const std::vector<int>& unseenBagPieces
```

The three current strategies (`bot_while1`, `bot_candy0014`, and `bot_mech`)
intentionally ignore this parameter in Stage 117. Their search,
scoring, candidate generation, random tie-breaking, and move-selection logic are
otherwise unchanged.

## Semantics

For an n-bag randomizer, take the bag containing the last visible NEXT piece.
If `NEXT == 0`, use the current falling piece as that visible endpoint.  The
parameter contains the pieces later in the same bag that are still hidden beyond
the visible endpoint.

The returned vector is sorted and deduplicated.  Therefore it exposes only set
membership, not the already-generated hidden order inside the bag.

Examples:

```text
bag:     A B C D E F G
current: A
NEXT:    B C D
passed:  {E,F,G}
```

If the visible endpoint is the final piece of its bag, the set is empty even if
the following bag has already been generated internally.

The metadata is valid for deterministic n-bag generation over the active piece
catalog, including the Pentomino and Hexomino catalogs.  It is deliberately empty
for with-replacement `Random` generation and for the 2026 Fool modes whose bag
composition changes dynamically.

## Implementation boundary

`Engine::unseen_pieces_in_preview_endpoint_bag()` derives the set directly from
`PieceQueueState::bag` and its existing `QueuedPiece::startsNewBag` markers.  No
new RNG calls, queue mutations, bag IDs, replay fields, or network fields are
needed.

`Engine::autoplay_adapter` computes the metadata immediately before each BOT
`choose_position()` call.  `Legacy::BotStrategy` and `src/legacy/bot.cpp` forward
it to the selected BOT after typed-to-legacy piece-ID conversion.

## Regression coverage

A new `bot_bag_remainder_test` covers:

- standard n-bag suffix extraction;
- `NEXT == 0` current-piece endpoint semantics;
- preview endpoints at a bag boundary;
- previews crossing into a later 6-piece bag;
- Pentomino / Hexomino n-bag validity;
- with-replacement Random invalidation;
- 2026 Fool randomizer invalidation;
- proof that metadata extraction does not mutate the queue or consume RNG.

Release build and the complete regression suite pass **39/39** tests.
