#ifndef TETR_CONFIG_MULTIPLAYER_CONFIG_H
#define TETR_CONFIG_MULTIPLAYER_CONFIG_H
#include "tetr/config/user_config.h"
#include "tetr/runtime/runtime_config.h"
#include <optional>
#include <string>
#include <string_view>
namespace UserConfig {
struct MultiplayerConfig {
    Settings settings;
    std::shared_ptr<const Runtime::MapDefinition> map;
    bool continuousCancellation = false;
};
std::string encode_multiplayer_config(const Settings& settings,const Runtime::RuntimeConfig& config,bool includePersonal=false);
std::optional<MultiplayerConfig> decode_multiplayer_config(std::string_view wire);
// Build match-local rules; local profile, controls and display preferences are preserved.
Runtime::RuntimeConfig apply_multiplayer_config(const Settings& local,const Runtime::RuntimeConfig& current,
    const MultiplayerConfig& host,bool forceRules);
bool copy_multiplayer_config(Settings& local,const MultiplayerConfig& host);
Settings multiplayer_config_view_settings(const Settings& local,const MultiplayerConfig& host);
}
#endif
