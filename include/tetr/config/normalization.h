#ifndef TETR_CONFIG_NORMALIZATION_H
#define TETR_CONFIG_NORMALIZATION_H

#include "tetr/runtime/runtime_config.h"

namespace UserConfig { struct Settings; }

namespace Runtime {

GameMode selected_game_mode(const UserConfig::Settings& settings);

// Converts raw, user-editable values into a complete and deterministic runtime
// configuration. No value from a previous game session is retained.
RuntimeConfig normalize_user_config(
    const UserConfig::Settings& settings,
    GameMode selectedMode);

} // namespace Runtime

#endif
