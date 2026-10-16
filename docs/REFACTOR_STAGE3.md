# Stage 3: QP2 post-lock rules and field mutation

## Goal

Continue reducing `Place::play()` without changing BOT strategy or the existing QP2 gameplay. This stage targets the two responsibilities left explicitly for Stage 3:

1. QP2 lock-result side rules;
2. physical line-clear mutation, separated from the terminal animation.

## QP2 post-lock rules

New reusable module:

- `include/tetr/rules/qp_post_lock.h`
- `src/rules/qp_post_lock.cpp`

The module now owns the pure calculations for:

- legacy `QP_last_op` encoding/decoding;
- Double Hole Reverse blighted/attack transition;
- Snake Eyes attack suppression and reverse self-garbage amount;
- Volatile attack sent into garbage cancellation;
- Volatile/Backfire outgoing attack post-processing;
- Backfire self-garbage amount;
- All-Spin normal/reverse repeated-operation penalties;
- QP altitude/speed gain after a placement;
- All-Spin extension (`QP_mod_allspin == 3`) operation normalization and history penalty.

The field-changing part of normal All-Spin (countdown solid garbage decay) is deliberately not hidden in a rules function. The rules function returns `decayTimedSolidGarbage`; the engine then applies that effect.

`QP_mod_allspin == 3` remains config-file-only, as before.

## Field mutation

New plan module:

- `include/tetr/rules/line_clear.h`
- `src/rules/line_clear.cpp`

It builds the exact legacy destination/source row mapping for gravity-based line clears.

New engine module:

- `include/tetr/engine/field_mutation.h`
- `src/engine/field_mutation.cpp`

It applies:

- AntiGravity row clearing;
- normal gravity row collapse;
- timed solid-garbage countdown/release.

These functions operate through callbacks rather than depending on `Board`, terminal rendering, Replay, or the old global `Config` state. They return cell-change events for the UI to render afterward.

This creates a real separation:

```text
Rules: which rows disappear / how rows map
                  ↓
Engine: mutate the field
                  ↓
Place/TUI: render resulting changes
```

The line-clear delay animation is still coordinated by `place.h`, but it no longer performs the final physical row-collapse algorithm.

## Compatibility details intentionally preserved

- The legacy gravity collapse never uses the very top hidden row as a source once shifting begins. Stage 3 preserves that boundary exactly.
- AntiGravity only physically clears rows at or below the legacy `-mapHeightD` boundary.
- Timed All-Spin solid garbage is decremented before rows containing value `100` are released, matching the previous ordering.
- All-Spin reverse compares spin line counts using the same encoded previous-operation semantics, including the initial sentinel value.
- Replay still stores the same external events and formats.

## Regression tests

New test target: `post_lock_rules_test`.

Coverage includes:

- 100000 deterministic random QP post-lock states compared against copies of the Stage 2 formulas;
- 10000 operations for All-Spin extension history equivalence;
- 20000 random cleared-row sets comparing the new collapse plan and physical mutation with the Stage 2 loop;
- 10000 random timed-solid fields compared with the Stage 2 decay algorithm;
- AntiGravity field mutation and legacy bottom-to-top change ordering.

At the end of Stage 3:

```text
opener_phase     PASS
core_rules       PASS
post_lock_rules  PASS
```

## BOT boundary

No file under `include/tetr/bot/` is modified in Stage 3.

## Suggested next step

The remaining largest responsibilities in `place.h` are now orchestration rather than formulas. A sensible Stage 4 is:

1. extract garbage buffer cancellation/rise decisions from `garbage.h` into pure rules;
2. give `Place::play()` a small placement-application object that owns Board counter updates, Replay markers and QP side-effect dispatch;
3. only after that, split input/movement timing from lock resolution.
