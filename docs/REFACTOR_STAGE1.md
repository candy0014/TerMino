# Stage 1 refactor notes

## Naming changes

| Old | New | Meaning |
| --- | --- | --- |
| `FSBorYPA` | `ObscuredColors` | Black/gray display for reduced readability at distance |
| `BoardStatic::begin_tim` | `BoardStatic::gameStartTime` | Start timestamp of current game/replay |
| `last_tim` | `timedGarbageTimer` | Timed cheese/garbage tick |
| `last_tim2` | `frameUpdateTimer` | 50 ms frame/rules update tick |
| `last_tim3` | `autoplayTimer` | BOT PPS throttle |
| `last_tim4` (QP) | `qpGravityTimer` | Per-second QP gravity acceleration |
| `last_tim4` (MP) | `multiplayerSyncTimer` | Multiplayer state broadcast timer |

`last_tim4` previously represented two unrelated clocks depending on mode. It is now two independent fields.

## Control-flow cleanup

`Place::Frame()` previously returned `114514` for “continue”. `Rules::PlayResult` now gives each outcome an explicit name, including `ContinueFrame`.

The unrelated multiplayer RNG salt still has the same numeric value for compatibility, but is named `kMultiplayerBlockSeedSalt`.

## Rules extracted so far

`include/tetr/rules/opener_phase.h` contains the pure decision logic for TETR.IO S2 opener-phase double cancellation. This is covered by `tests/opener_phase_test.cpp`.

This is deliberately a small extraction. Spin and attack code are left in place until regression coverage exists for them.
