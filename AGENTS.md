# TerMino development requirements

Read `CONTRIBUTING.md` and `docs/FRONTEND_MIGRATION.md` before frontend or shared
gameplay work. The baseline and recovery instructions are in
`docs/FRONTEND_BASELINE.md`.

- Preserve **all** terminal features, modes, settings, challenges, bots, tools,
  keyboard operations, data formats and network/replay behavior.
- Keep one shared implementation of gameplay. Terminal and OpenGL are separate
  frontends of the same product and must be maintained together.
- Every graphical flow must work without a mouse. Mouse controls are additional
  shortcuts. Keep the visual design stylish and readable; avoid excessive
  in-game effects.
- Keep terminal builds independent of SDL/OpenGL/font dependencies. Preserve the
  existing default targets and scripts. Support Linux and Windows explicitly.
- Use `./build` for normal local builds of both frontends. On Linux, keep the
  current clients at `./build/tetr` and `./build/tetr_gl`; use
  `./compile.sh --frontend both` when updating both. PowerShell also defaults to
  `build` (Visual Studio adds its configuration subdirectory). Isolated tests,
  CI and package presets may retain separate build directories.
- Do not modify, delete, reset or publish local `.tetr` data as part of migration,
  testing or packaging. Use isolated temporary test data and clean default packages.
- Validate affected behavior and architecture boundaries. Record real platform
  coverage; Wine does not establish native Windows graphics/input correctness.
- Graphical tests must use hidden windows or an offscreen display. Do not show
  test windows on the user's desktop; smoke/frame-limited runs are hidden.
- Keep only essential cropped screenshot evidence. Delete temporary screenshots
  immediately after verification instead of retaining full capture galleries.
- Keep migration changes reviewable and independently reversible. Do not claim a
  graphical build or feature exists while it is still planned.
- Package clients with `tools/package_frontends.py` and audit unpacked archives
  with `tools/check_frontend_distribution.py`; preserve terminal FT7/server tools.
  Native evidence in `docs/acceptance/native.json` must remain pending until
  actually checked. No GitHub publication was authorized for phase four.

## Android frontend

Read `docs/ANDROID_ADAPTATION.md` before Android or touch-input work. Android is
a landscape frontend of the same game. Its virtual-button editor replaces the
Android settings menu's keyboard-binding entry; desktop bindings remain intact.
Only gameplay controls are configurable: movement, all three rotations, soft/
hard drop, Hold, pause, retry and undo. Each virtual button's position, size, shape and
opacity must be independently editable. Other commands use fixed page controls,
preferably a consistent left-side column, and are excluded from layout files.
Touch must preserve shared input timing and multi-touch releases. Preparation
documents and layout examples do not establish an Android client or APK.
The Android deliverable is an installable `.apk` file; build and audit with
`python3 tools/build_android.py`. Record emulator and real-device coverage separately. The source/build
folder is retained as project material, not substituted for that deliverable.
