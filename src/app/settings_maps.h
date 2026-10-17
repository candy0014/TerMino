#ifndef TETR_APP_SETTINGS_MAPS_H
#define TETR_APP_SETTINGS_MAPS_H
#include "tetr/app/settings_model.h"
#include "tetr/config/map_store.h"
namespace Runtime { class GameContext; }
namespace App::Settings {
Model::SettingNode make_map_menu(UserConfig::Settings& user);
bool map_action(Model::NodeKind kind);
bool delete_map(Runtime::GameContext& context,int id,bool& settingsSaved,const std::vector<Runtime::GameContext*>& sharedOwners={},UserConfig::Maps::IdChanges* remapped=nullptr);
}
#endif
