#ifndef TETR_APP_SETTINGS_INPUT_H
#define TETR_APP_SETTINGS_INPUT_H

#include <string>
#include <functional>
#include "tetr/input/action.h"

namespace Runtime { class GameContext; }

namespace App::Settings {

class TerminalSettingsView;
namespace Model { struct SettingItem; }

using CheckMenuControls = std::function<void()>;
using ScalarInputRedraw = std::function<void(const std::string&, std::size_t)>;
int capture_key(Runtime::GameContext& context, Input::Action action, const CheckMenuControls& checkControls);
void refresh_runtime_bindings(Runtime::GameContext& context);
std::string read_scalar_text(Runtime::GameContext& context, const CheckMenuControls& checkControls,
    const ScalarInputRedraw& redraw = {}, bool* savedByEscape = nullptr, const std::string& initialValue = {});
void choose_value(Runtime::GameContext& context, const TerminalSettingsView& view,
    Model::SettingItem& item, const std::string& breadcrumb, const CheckMenuControls& checkControls);

} // namespace App::Settings

#endif
