# Stage 27: Dead Code & State Cleanup

Stage 27 is a consolidation pass after the boundary work in Stages 20-26. The goal is not to introduce another abstraction, but to remove code and state that no longer has a functional owner. Protected BOT strategy headers remain a strict compatibility zone.

## Cleanup policy

An item is removed only when at least one of these is true:

1. the API has no caller anywhere in production/tests and is not required by protected BOT code;
2. the stored value is fully derivable from authoritative state and has no independent lifecycle;
3. a returned field exists only so a regression test can observe an intermediate, while the same behavior can be verified from final state;
4. the code is mechanically duplicated or a branch has no registered input path.

Low-call-count functions that still perform a real job are intentionally retained. The goal is smaller truthful code, not maximum line deletion.

## Removed zero-call compatibility APIs

The following old convenience surfaces had no callers and were removed:

- no-argument `Game::game()`, `Init::init()`, and `Setting::setting()` wrappers;
- `Runtime::is_default_game_context()`;
- default-context overloads of `make_frame_rule_config()` and `capture_placement_application_config()`;
- `Runtime::Duration::zero()`, `Runtime::TimePoint::epoch()`, and `Piece::KickIndex::valid()`;
- legacy `Block::fool_getblock()` wrapper;
- `MovementScriptPlayback` default constructor and the unused `index()/size()/stepInterval()` observation getters;
- `Socket::to_sockaddr()` / `Socket::from_sockaddr()`, which had no callers and allocated raw heap objects. UDP send/receive already uses stack-owned `sockaddr_in` directly.

The default game context itself remains because the executable entry and protected BOT compatibility methods still require it.

## Removed pseudo-state

Several values were stored as long-lived state even though they had no independent meaning:

- `PieceQueueState::piecesPerBag` is now a local initialization value;
- `HandlingConfig::twentyG` was redundant with the existing config bridge behavior that converts `G20` into zero gravity interval; the external config/replay field is preserved;
- `PlayerPresentationState::nextHeight` and `holdHeight` were always derivable from `blockMaxHeight` and `nextCount`. They are now computed by `next_panel_height()` / `hold_panel_height()`, so presentation layout cannot become stale after its source values change.

## Removed test-only observation fields

Production results no longer carry values that no gameplay caller uses:

- `FramePostPresentationResult::qpFloorChanged`; the post-presentation function now returns `void`;
- `GarbageCancelResult::cancelledLines` and `openerDoubleCancel`;
- `GarbageRisePlan::totalLines`.

Regression tests now compare the resulting rule state, garbage queue and rise chunk sequence directly. This keeps the tests strong without expanding the production API for introspection.

## Init and mechanical cleanup

- `InitState::Node` no longer contains a `std::string*` branch because no string node is registered;
- a duplicated `parse_double_loose()` assignment in config parsing was removed;
- an uninstantiated `refGrounded` test template was removed;
- an unnecessary `static_cast<int>(256)` in key validation was removed;
- Linux non-blocking socket handling now avoids the duplicate `EAGAIN || EWOULDBLOCK` expression when the platform defines the constants identically, while still supporting platforms where they differ.

## Size effect

Before adding Stage 27 documentation, the source/header/test diff against Stage 26 is:

- 55 added lines;
- 132 deleted lines;
- net **77 lines removed**.

The additions are mostly derived-state accessors and the safer cross-platform `EAGAIN/EWOULDBLOCK` branch.

## Validation

Final code validation:

- Debug: **30 / 30 PASS**;
- Release: **30 / 30 PASS**;
- **81 / 81** non-BOT public headers compile as the first include;
- non-BOT warnings: **0** under `-Wall -Wextra -Wpedantic`;
- enhanced redundancy audit also reports **0** non-BOT warnings under `-Wredundant-decls -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast`;
- protected BOT strategy files: **8 / 8** SHA-256 identical to Stage 26.

Replay/config/network wire formats are unchanged.

## Next target

After this cleanup pass, the next useful refactor is to type the remaining Session/QP mode and phase values that still use bare integers, while continuing to delete obsolete compatibility surfaces as their last callers disappear. Startup multiplayer handshake parsing is another independent candidate and should stay separate from BOT compatibility work.
