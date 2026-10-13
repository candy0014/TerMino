#ifndef TETR_APP_SETTINGS_TARGETS_H
#define TETR_APP_SETTINGS_TARGETS_H

#include "settings_model.h"

namespace App::Settings {
struct MenuLocation {
    std::vector<std::string> path;
    std::size_t selected = 0;
};
// Preserve the semantic row, even if the destination tree has a different
// visible row order; hidden rows resolve to a currently hiding control.
MenuLocation locate_target_selection(const Model::SettingNode& visible,
    const Model::SettingNode& unpruned, const UserConfig::Settings& user,
    const std::vector<std::string>& path, const std::string& selectedKey);
}
#endif
