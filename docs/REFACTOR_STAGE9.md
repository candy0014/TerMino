# Refactor Stage 9: Per-Frame Update / Multiplayer Pump / Status Presentation

Stage 9 removes the remaining game-mode rule calculations, multiplayer receive
parser, and status rendering details from `Place::Frame()`.

The ordering of the old Frame implementation is treated as compatibility data.
In particular, QP2 still executes in this order:

1. generate incoming garbage using the APM value from the start of the frame;
2. advance QP altitude and decay QP speed;
3. derive the new SurvivalAPM from altitude;
4. process multiplayer receive packets;
5. render message/blighted/altitude status;
6. derive the new QP floor and GarbageInTime;
7. on a floor transition, update bag breather and Gravity-mod Speed/EPLD;
8. apply Expert Reverse altitude drain;
9. update the frame timer.

This order matters because the status display historically observes the
pre-floor-drain altitude while later gameplay observes the post-floor state.

## New pure rules module: `rules/frame_update`

`FrameRuleState` contains only the per-frame values needed by the formulas:

- QP speed / altitude / floor;
- Survival APM;
- GarbageInTime;
- gameplay gravity Speed;
- EPLD;
- QP bag-breather state.

The old Frame rule chain is intentionally split into two explicit phases:

- `advance_frame_before_presentation()`
- `advance_frame_after_presentation()`

The first phase owns Survival/QP garbage scheduling and QP altitude/speed/APM
progression. The second owns floor transitions, garbage delay, Gravity-mod
Speed/EPLD updates, and Expert Reverse altitude drain.

Generated garbage is returned as packet sizes. The rules module never touches
a Board, map, TUI, Replay, or socket.

The legacy RNG algorithm and call order are preserved exactly. The QP burst
size also preserves the old integer-conversion behavior rather than silently
"fixing" its `+0.5` expression.

## New network modules

`network/frame_packet` maps the legacy UDP strings to named packet types:

- win (`end1`)
- opponent left (`end2`)
- attack acknowledgement/garbage packet (`receive_id;send_id;attack`)
- opponent board-state packet (`!...`)

It also owns the legacy outgoing-attack acknowledgement/cancellation logic.

`network/frame_pump` owns the receive-until-`fail` loop. The engine-facing code
provides callbacks for adding incoming garbage and applying an opponent state.
A terminal `end1/end2` signal stops the pump immediately, matching Stage 8.

The wire format is unchanged.

## Legacy adapters / presentation

`engine/frame_board_adapter.h` converts the existing global/Board state to and
from `FrameRuleState`. It remains an inline compatibility adapter because the
legacy project still defines many globals directly in headers.

`engine/frame_presentation.h` owns the small status-render sequence:

- `print_message()`
- optional QP Double-Hole-Reverse `print_blighted()`
- `print_altitude()` or `print_score()`
- stdout flush

This keeps terminal details out of the pure frame rules.

## Regression tests

`frame_update_test` adds:

- 100,000 randomized Stage-8-vs-Stage-9 comparisons of Survival/QP frame
  updates;
- exact comparison of generated garbage packet sequences;
- exact comparison of the post-update `std::mt19937` state;
- comparison of altitude, speed, APM, floor, GarbageInTime, Speed, EPLD and
  bag-breather state;
- 50,000 randomized multiplayer acknowledgement/attack comparisons;
- receive-pump ordering and terminal-signal tests.

All nine CTest targets pass in a clean Release build.

The new normal translation units also compile cleanly with
`-Wall -Wextra -Wpedantic`.

## Protected bot code

All eight files under `include/tetr/bot/` are byte-for-byte identical to
Stage 8.

## `place.h`

Stage 8: 795 lines

Stage 9: 702 lines

The old `Place::Frame()` occupied roughly 136 lines. It is now a small adapter
that captures state, invokes the two rule phases, applies generated garbage,
pumps multiplayer packets, renders status, and stores the result.

## Next boundary

The largest remaining orchestration inside `Place::play()` is now the
non-Frame periodic work:

- one-second QP gravity acceleration;
- 100 ms multiplayer state broadcast;
- interval-based GarbageModel 4 injection;
- BOT/replay/player loop scheduling around those timers.

A useful Stage 10 boundary is a generic periodic-event scheduler/controller so
these timers stop being open-coded inside multiple movement/replay loops.
