# Stage 28: In-game input configuration

## Goal

Linux keyboard device selection and keybinding editing should be normal game configuration, not separate executables or shell scripts. The change must preserve the existing `user_config.txt` and `keybinding.txt` formats and must not modify protected BOT strategy sources.

## Linux keyboard device selection

`discover_linux_keyboards()` parses `/proc/bus/input/devices` and keeps devices with both a `kbd` handler and an `eventN` handler. Each result records its event number, human-readable device name, `/dev/input/eventN` path and whether the process can read it.

At startup, `InputSetup::ensure_startup_keyboard()` checks the configured `Linux Keyevent` before Runtime initialization. A valid device adds no prompt. If it is unavailable, the game itself lists readable keyboards on stdin, accepts a choice and updates the existing `Linux Keyevent = ...` line in `user_config.txt`.

The same discovered-device list appears as `Linux Keyboard` on the in-game `CONTROLS` page. Selecting another device calls the restart-safe `KEYHELPER::init()` immediately. A failed open restores the previous device and value.

## Keybinding configuration

The existing `CONTROLS` page remains the key capture UI, so `tools/keybinding.cpp` is no longer needed. The historical format remains one integer key code per line in `keybinding.txt`.

Stage 28 centralizes the disk boundary:

- `UserConfig::load_keybindings()` applies defaults for missing or `-1` values and rebuilds the action map.
- `UserConfig::save_keybindings()` rebuilds the action map and writes all configured controls.
- `Settings::controlCount` is exactly 11, matching the 11 actions currently consumed by the game.
- The Replay binding (index 10) is now saved correctly; the old Settings code accidentally wrote only the first 10 entries.

## Removed redundancy

The following standalone input-configuration artifacts were removed:

- CMake target `tetr_keybinding`
- `tools/keybinding.cpp`
- `tools/configure_linux_input.sh`
- unreferenced `keyb.txt`

`compile.sh` now reports only the game and server binaries.

## KEYHELPER lifecycle

`KEYHELPER::init()` is now safe to call more than once. It stops and joins any previous monitor thread, closes the previous fd, clears key state, then opens and starts the requested event device. It returns `false` instead of starting a broken monitor when `open()` fails.

## Compatibility

- `Linux Keyevent = N` in `user_config.txt` is unchanged.
- `keybinding.txt` integer line format is unchanged.
- The optional alternate keybinding-file command-line argument remains supported.
- Windows input behavior is unchanged.
- Protected BOT strategy files are not edited.

## Validation

Linux adds `linux_input_device_test`, which verifies keyboard-vs-mouse handler parsing from a synthetic `/proc/bus/input/devices` sample. `user_config_test` now verifies in-place config updates, centralized keybinding load/save, default fallback and persistence of the Replay binding.
