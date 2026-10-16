# Stage 32: Structured Settings and User Config

The historical pointer-array Settings implementation was replaced by typed `SettingPage` and `SettingItem` data using `std::variant` references. Navigation repeat, choices, scalar editing and the 11 key bindings now have separate, named paths.

All C++ `UserConfig::Settings` members use domain names such as `gameMode`, `fieldWidth` and `holdEnabled`; the existing `user_config.txt` keys are unchanged. Parsing is section-aware, so `[QUICK PLAY] Invisible` and `[GAME] Invisible` no longer collide. Flat legacy files still use the historical first-match fallback.

Removed: `include/tetr/setting.h`, `src/runtime/setting.cpp`, duplicate `Model` in the default config and the old three-raw-pointer setting node.
