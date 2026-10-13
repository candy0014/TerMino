#ifndef TETR_APP_SETTINGS_PRESET_EDITOR_H
#define TETR_APP_SETTINGS_PRESET_EDITOR_H
#include "tetr/config/presets.h"
#include "tetr/app/settings_menu.h"
#include "settings_view.h"

namespace App::Settings {
struct PresetEditorSession {
    PresetEditorSession* parent=nullptr;
    Runtime::GameContext* sourceContext=nullptr;
    UserConfig::Settings original;
    std::optional<UserConfig::Settings> originalTwo;
    UserConfig::Presets::SavedPreset preset;
    UserConfig::PresetFields baseline;
    std::optional<UserConfig::PresetFields> baselineTwo;
    std::string baselineName;
    bool playerTwo=false;
    bool hasPlayerTwo=false;
    bool dirty(const Runtime::GameContext& context) const;
    bool save(const Runtime::GameContext& context);
};
void run_new_preset(Runtime::GameContext& source);
bool run_saved_preset(Runtime::GameContext& source,int id,PresetEditorSession* parent=nullptr);
MenuResult run_preset_menu(Runtime::GameContext& context,PresetEditorSession& editor);
bool request_preset_exit(Runtime::GameContext& context,const TerminalSettingsView& view,PresetEditorSession& editor);
bool reset_preset_entry(Runtime::GameContext& context,const TerminalSettingsView& view,
    const Model::SettingNode& node,const PresetEditorSession& editor,bool both=false);
bool rename_preset(Runtime::GameContext& context,const TerminalSettingsView& view,PresetEditorSession& editor);
int preset_save_key();
int preset_undefined_key();
}
#endif
