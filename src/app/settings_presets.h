#ifndef TETR_APP_SETTINGS_PRESETS_H
#define TETR_APP_SETTINGS_PRESETS_H
#include "settings_model.h"
namespace App::Settings {
Model::SettingNode make_preset_menu(UserConfig::Settings& first,bool localVersus,UserConfig::Settings* second=nullptr);
bool preset_action(Model::NodeKind kind);
bool apply_preset_action(UserConfig::Settings& user,const Model::SettingNode& node,bool keepMatchMode,bool playerTwo=false);
}
#endif
