# Stage 22: Typed input semantics

Stage 22 removes two remaining sets of implicit input protocol values from normal
runtime code: action-name strings such as `"CW"` / `"HD"`, and the historical
movement-input phase integers `-1 / 0 / 1 / 2`.

## Input actions

`include/tetr/input_action.h` defines `Input::Action` and the single compatibility
parser for the existing keybinding text format. The on-disk/user-facing binding
format remains unchanged. Normal gameplay, countdown and Replay interaction code
now branches on `Input::Action` values instead of repeating string comparisons.

The conversion boundary is deliberately narrow:

```text
customBindings: map<int,string>  ->  Input::action_for(...)  ->  Input::Action
```

This preserves current configuration files while making rotation, horizontal
movement, retry/settings and Replay-save decisions explicit in code.

## Key phases

`Engine::KeyPhase` replaces the overloaded integer states used by
`MovementInputState`:

```text
AwaitRelease = -1
Released     =  0
Pressed      =  1
Repeating    =  2
```

The numeric values are retained only to document historical behavior. Engine and
Replay code use the enum names directly, so phase values can no longer be mixed
accidentally with key codes, lock-reset counts or other integers.

## Compatibility

- `keybinding.txt` and `UserConfig::custom` keep their historical string values.
- OS key codes remain integers because they are external input identifiers.
- Protected BOT strategy files are untouched.
- Movement semantics are still checked by the randomized
  `player_input_equivalence_test`.

## Regression coverage

`input_semantics_test` checks every supported action token, direction/rotation
mapping, unknown actions, binding lookup and the release-debounce phase lifecycle.
The existing movement/input tests continue to exercise DAS/ARR/SDF and randomized
old-vs-new behavior.

## Validation

```text
Debug:   26 / 26 PASS
Release: 26 / 26 PASS (tests built with -UNDEBUG)
77 / 77 non-BOT public headers self-contained
0 direct customBindings magic-action comparisons in normal source
8 / 8 protected BOT strategy files byte-for-byte equal to Stage 21
```
