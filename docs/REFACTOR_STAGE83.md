# Stage 83: Non-BOT refactor completion pass

Stages 47 through 83 continue the refactor from the Stage 46 baseline while
keeping the eight protected BOT strategy headers byte-for-byte unchanged.

## 47-55: hidden context, strong state and library ownership

- Removed BOT-only methods and hidden default-context reads from normal Piece
  definitions; legacy Block became an actual compatibility wrapper.
- Replaced positional queue/pending-attack pairs and bare multiplayer sequence
  ids with named domain types.
- Moved UDP ownership out of Runtime into explicit `NetworkServices`; nonblocking
  receive now returns `optional<Datagram>` rather than the data sentinel
  `"fail"`.
- Converted running timing configuration to `Runtime::Duration` while retaining
  compatible text/Replay seconds at serialization boundaries.
- Split Replay core from Engine/App adapters and introduced `OperationKind`.
- Removed Network state application from Render.
- Converted remaining boolean-like Runtime fields to `bool`.
- Removed the obsolete PPP generator target/file.

## 56-67: coordinator decomposition

- Split placement lock resolution, Replay synchronization and line-clear phases
  out of the old placement coordinator.
- Split garbage generation/field mutation from buffer/network processing and
  then isolated garbage rendering.
- Split Settings into model, navigation, input and menu components.
- Split Init into Replay loading, multiplayer lobby/setup and player setup.
- Replaced file-global relay server state with `Network::RelayServer`.
- Extracted live-piece periodic events and cleaned the autoplay boundary.
- Split user config parsing from keybinding file I/O.
- Split board rendering into layout, stats and event implementations.

## 68-73: default/global and reverse-dependency removal

- Deleted `default_game_context()`, `default_session_state()` and
  `default_runtime_services()` completely.
- Moved PPP legacy references out of Runtime.
- Moved garbage packet storage into Runtime and removed Runtime -> Rules
  dependency.
- Moved `ReplaySessionState` out of Runtime `SessionState`; Replay is now an
  explicitly injected GameContext component.
- Encapsulated `GarbagePacket` so historical field names no longer determine
  normal Runtime layout. The legacy Board facade builds a compatibility snapshot
  for BOTs instead.

## 74-83: public API and serialization cleanup

- Removed integer Piece overloads from normal public APIs.
- Unified Settings under `App::Settings`.
- Added the `architecture_boundary` CTest to prevent Runtime reverse includes,
  Render -> Network dependencies and reintroduction of default globals/old
  Garbage fields.
- Renamed `ReplayEvent` namespace to `Replay` without changing serialized event
  names.
- Added typed Replay gameplay payloads; positional argument readers became
  private implementation details.
- Converted `Piece::Definition` to encapsulated geometry and changed legacy
  `Block::block` from inheritance to composition.
- Removed `Runtime::Playfield::operator[]`; legacy `map` alone preserves `[][]`
  for protected BOT source.
- Renamed running RuntimeConfig fields to semantic names while retaining legacy
  config keys in codecs.
- Added `Runtime::GarbageHolePattern`, eliminating the signed `HoleNum` sentinel
  from normal Runtime/Rules logic. Positive/negative historical values now
  convert only at config/Replay boundaries.

## Compatibility constraints

No intended gameplay rule or external file/wire format changed in this pass.
Historical deterministic shuffle behavior is retained. The BOT strategy headers
are not reformatted, renamed or edited; all adaptation remains outside their
source files.

## Validation

The final package was validated with:

1. Debug CTest: **34/34 passed**;
2. clean Release build and CTest: **34/34 passed**;
3. isolated first-include compilation: **101/101 non-BOT public headers**, zero diagnostics;
4. architecture-boundary checks: **passed**;
5. protected BOT strategy SHA-256 comparison against Stage 46: **8/8 identical**.
