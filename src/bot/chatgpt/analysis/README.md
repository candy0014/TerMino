# Offline BOT diagnostics

These tools are separate from the playing policy. Historical raw datasets and
one-off HTML reports were removed during the 2026-10-08 cleanup. Compact final
results and rejected-candidate decisions are in
[retained reports](../../../../docs/retained_bot_reports/README.md).

Current rule, reachability, quota, evaluation and reporting regressions remain in
`tests/`. They are built with `-DBUILD_TESTING=ON`. Standalone root, Spin and native
probes are opt-in targets and are not compiled by a normal game build:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bot_chatgpt_root_probe bot_chatgpt_spin_probe bot_chatgpt_native_probe -j 4
```

Supply fresh root traces or replay data when running a probe. Old paths shown in
dated research documents refer to archived experiments and are not input fixtures
for the current test suite. The redundant key-piece, coverage, rise, hold and
quota standalone probes were removed; their meaningful rule checks remain in
regression tests.

The retained parameterized training, pressure, geometry and match-report helpers
accept supplied data. Some historical scripts still use old experiment paths as
their defaults; provide current inputs explicitly when reusing them. Frozen
weights already embedded in the evaluator do not depend on the deleted logs.

Search reports describe bounded exploration under each tool's own horizon and
beam limits. Failure to find a path does not prove impossibility, and evaluator
scores do not establish match strength. Use the native reachability checks and
fresh held-out games when comparing candidates.
