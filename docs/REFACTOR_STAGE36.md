# Stage 36: Explicit Input and Legacy Name Cleanup

The process-global `Interactive::keydown()` adapter was deleted. All App, Engine, Replay and Settings input reads now use `context.services.key_down()`, so an independent `GameContext` cannot accidentally read the default runtime's keyboard.

Linux `KEYHELPER` and its files became `Runtime::KeyboardMonitor` in `runtime/keyboard_monitor.{h,cpp}`. It retains automatic multi-keyboard discovery, per-device pressed sets and hot-plug rescanning.

Validation: Debug 33/33, Release with assertions 33/33, 85/85 non-BOT public headers, three final executables linked, non-BOT strict warnings zero, and all eight protected BOT headers byte-identical to Stage 31.
