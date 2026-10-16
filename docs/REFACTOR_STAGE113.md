# Stage 113: Narrow Compatibility Encodings

The former catch-all `compat/legacy_encoding.h` has been split into:

- `piece_encoding.h`
- `board_encoding.h`
- `gravity_encoding.h`
- `garbage_encoding.h`
- `config_encoding.h`

The umbrella header remains for legacy callers and regression tests, but production translation units include only the encoding domain they need. No historical numeric value or external format changed.
