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
- Do not modify, delete, reset or publish local `.tetr` data as part of migration,
  testing or packaging. Use isolated temporary test data and clean default packages.
- Validate affected behavior and architecture boundaries. Record real platform
  coverage; Wine does not establish native Windows graphics/input correctness.
- Graphical tests must use hidden windows or an offscreen display. Do not show
  test windows on the user's desktop; smoke/frame-limited runs are hidden.
- Keep migration changes reviewable and independently reversible. Do not claim a
  graphical build or feature exists while it is still planned.
- Package clients with `tools/package_frontends.py` and audit unpacked archives
  with `tools/check_frontend_distribution.py`; preserve terminal FT7/server tools.
  Native evidence in `docs/acceptance/native.json` must remain pending until
  actually checked. No GitHub publication was authorized for phase four.
