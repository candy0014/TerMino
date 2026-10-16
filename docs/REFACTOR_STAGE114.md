# Stage 114 - Final correctness and cleanup audit

Stage 114 is the final non-architectural audit after the Engine/Network/Terminal/Compat refactors. It intentionally avoids strategy or wire-format redesign and concentrates on defensive correctness, portability, and small redundancy cleanup.

## Correctness fixes

- UDP receive capacity now covers the legal IPv4 UDP payload range (65507 bytes) instead of truncating at 1024 bytes. Send/bind/address/non-blocking failures are checked, Windows keeps the native `SOCKET` width, and rebind preserves non-blocking mode.
- Lobby and board-state decoders reject impossible field dimensions, oversized previews, and unknown piece ids before data enters Runtime/Render code. Relay pairing ignores duplicate handshakes from the same endpoint.
- `Runtime::Playfield` validates storage dimensions and row/column access in Release builds, safely assigns from an uninitialized field, and clears the active storage when reinitialized. Piece collision/ARS checks respect the hidden-row lower bound.
- Empty Piece Queue operations fail with `logic_error` instead of indexing an empty deque or taking modulo zero.
- Garbage row generation repairs malformed hole-order state, handles non-positive widths, and cannot loop forever in width-one double-hole cases. Valid configurations preserve the deterministic RNG path.
- Historical permissive integer parsing saturates at `LLONG_MIN/LLONG_MAX` instead of overflowing. `digit_count(LLONG_MIN)` and large fixed-number formatting no longer invoke undefined conversions.
- Linux keyboard monitoring backs off after persistent `poll()` errors rather than spinning.
- `bot_candy0014` now returns the rotation/kick record that its BFS already computed; this fixes a missing output assignment without changing its search or evaluation. Several dead writes and non-standard/misleading C++ expressions in BOT sources were cleaned up without changing strategy calculations.

## Cleanup

- Removed a duplicated renderer include and redundant Playfield clear after initialization.
- Removed unused `GameContext` plumbing from garbage-generation state conversion.
- Strict `-Wall -Wextra -Wpedantic -Wformat=2 -Wnull-dereference` build completes with zero project warnings.

## Verification

Final source was validated with:

- Debug regression suite: 38/38 passed.
- Release regression suite: 38/38 passed.
- AddressSanitizer + UndefinedBehaviorSanitizer: 38/38 passed with leak detection and halt-on-error enabled.
- 32 KiB UDP loopback regression, malformed network/config boundary cases, hidden-row/storage limits, empty Piece Queue checks, and deterministic equivalence tests are included in the suite.

External Replay/config/keybinding formats and the historical multiplayer wire format are unchanged.
