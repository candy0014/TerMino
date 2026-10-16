# Stage 29: Automatic multi-keyboard Linux input

## Goal

Remove Linux keyboard-device selection entirely. A wrong saved `eventN` must never make the in-game Settings unreachable. Linux should behave like a normal input source: every readable keyboard contributes to one merged key state, and plugging in another keyboard should require no restart or configuration.

## Automatic device monitoring

`KEYHELPER` no longer owns one configured file descriptor. Its monitor thread now:

1. parses `/proc/bus/input/devices` for input nodes carrying both `kbd` and `eventN` handlers;
2. opens every currently readable `/dev/input/eventN` with non-blocking I/O;
3. polls all opened keyboard fds together;
4. rescans once per second so newly attached keyboards are opened automatically and removed keyboards are dropped;
5. releases any keys that were still recorded as down when a device disappears.

There is no preferred/selected keyboard. Built-in, USB and other readable keyboard event devices can be used at the same time.

## Merged key state

A single boolean per key is insufficient when more than one keyboard is active: releasing `KEY_A` on one device must not clear `KEY_A` while another keyboard still holds it.

Stage 29 therefore keeps a per-device set of pressed key codes and an aggregate press count per key. `Interactive::keydown(code)` is true while that count is non-zero. Kernel autorepeat (`EV_KEY` value 2) keeps a key down but does not increment the count repeatedly.

## Removed selection/configuration state

The following Stage 28 machinery became unnecessary and was deleted:

- `InputSetup::ensure_startup_keyboard()` and `include/tetr/input_setup.h` / `src/app/input_setup.cpp`;
- the `Linux Keyboard` row in the in-game Controls page;
- `Settings::LinuxKeyevent`;
- `Settings::config_file`, which existed only so the selected event number could be written back;
- `UserConfig::update_value()`, whose only production use was updating `Linux Keyevent`;
- the distributed `Linux Keyevent = ...` entry in `user_config.txt`;
- the public Linux keyboard-discovery API and its selection-oriented test.

Existing user configuration files that still contain `Linux Keyevent = N` remain loadable. The line is simply an unknown legacy setting and is ignored.

## Keybinding configuration

Stage 28's useful part remains: bindings are edited from the in-game `CONTROLS` page, and `UserConfig::load_keybindings()/save_keybindings()` remain the single disk-format boundary. Because `Interactive::keydown()` now merges every keyboard, key capture also works from any monitored keyboard.

## Compatibility

- `keybinding.txt` remains one integer Linux input key code per action.
- Windows input code is unchanged.
- Replay and network formats are unchanged.
- Protected BOT headers are unchanged.

## Validation

- Debug: 30/30 tests pass.
- Release: 30/30 tests pass.
- 81/81 non-BOT public headers compile independently as the first include.
- Strong warning build has no non-BOT warning.
- All 8 protected BOT headers are byte-for-byte identical to Stage 28.
