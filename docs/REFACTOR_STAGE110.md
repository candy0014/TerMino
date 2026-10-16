# Stage 110: Engine Layering

The monolithic `tetr_engine` static library is replaced by four explicit layers:

- `tetr_engine_core`: field/queue/movement/placement algorithms with no Render, Network, Replay or Input dependencies.
- `tetr_engine_presentation`: presentation state machines and terminal-facing helpers; no Network/Replay ownership.
- `tetr_engine_integration`: Replay/Network adapters and side effects.
- `tetr_gameplay`: high-level per-piece/per-game orchestration.

`tetr_engine` remains only as an INTERFACE compatibility umbrella. NEXT preview rendering was extracted from `piece_queue.cpp`, so queue generation is now render-independent. Architecture tests enforce the new lower-layer boundaries.
