# Refactor Stage 95

Stage 95 closes the Stage 89–94 refactor series and turns the new boundaries
into regression-tested architecture rules. The eight protected BOT strategy
headers remain byte-identical to the Stage 88 baseline.

## Stage 89: semantic Quick Play operations

Runtime QP placement state no longer stores packed integers or the historical
`8191` sentinel. `QuickPlayClearOperation` and `ExtendedAllSpinSignature`
represent the actual rule state. Compatibility tests preserve the historical
Reverse first-operation behavior and the Extended All-Spin piece/line bit
interaction without making packed integers Runtime state.

## Stage 90: BoardCell

`Runtime::Playfield` stores `BoardCell`. Empty, normal piece, garbage, solid
and timed-solid/release states are explicit. Old integer cell values exist only
at the UDP serialization and protected-BOT snapshot boundaries. Random garbage,
field-shift, rendering and board-state codec equivalence tests preserve legacy
behavior and wire output.

## Stage 91: Autoplay dependency inversion

`tetr_engine` no longer includes or links the legacy BOT target. Engine consumes
`Autoplay::Strategy`; `Legacy::BotStrategy` wraps the old `Bot::*` API and is
injected in `src/tetr.cpp`.

## Stage 92: stable Settings identity

Every settings item carries `SettingId`. Labels are presentation text only.
The settings editor also keeps text/number/key-capture rows highlighted while
input is active, remembers the previous menu position for the process lifetime,
and avoids re-consuming the Enter key that opened the editor.

## Stage 93: service ownership

`RuntimeServices` now owns only the monotonic clock and RNG. `Input::Services`
is an independent target containing typed bindings and platform keyboard state;
`UserConfig::Settings` is independently owned by the composition root. Both are
explicitly attached to `GameContext` for paths that need them.

## Stage 94: Garbage state/presentation separation

The old fixed `lastHole[105]` storage is replaced by `holeOrder`, sized to the
actual playfield width. Garbage buffer display cache moved from
`GarbageSessionState` to `PlayerPresentationState`.

## Stage 95: regression guards

`architecture_boundary` now rejects:

- Runtime includes of Input/App/Engine/Legacy/Network/Render/Replay/Rules;
- Engine includes or direct links to the legacy BOT;
- resurrection of the old Runtime input header paths;
- packed QP state type names and `lastHole` in normal state;
- Settings logic keyed by display labels;
- gameplay ownership of the garbage display cache.

External Replay/network/config/keybinding formats are unchanged.
