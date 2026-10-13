#include "tetr/config/user_config.h"
#include "tetr/util/text_codec.h"
#include "tetr/input/action.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include <linux/input.h>
#endif

namespace UserConfig {

namespace {

void load_keybindings_from(
    Settings& settings,
    const std::string& path,
    const std::array<int, Settings::kControlCount>& defaults,
    bool fallBackToPlayerOneFile) {
    std::ifstream input(std::filesystem::u8path(path));
    settings.keybindingPath = path;
    if (!input && fallBackToPlayerOneFile && path != kDefaultKeybindingPath) {
        input.clear();
        input.open(kDefaultKeybindingPath);
        settings.keybindingPath = kDefaultKeybindingPath;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::istringstream values(contents);
    // Original files allowed arbitrary whitespace between single key codes.
    // Lists use commas, and an empty binding is written explicitly as "none".
    const bool lists = contents.find(',') != std::string::npos || contents.find("none") != std::string::npos;
    for (std::size_t index = 0; index < Settings::kControlCount; ++index) {
        std::string value;
        const bool provided = lists ? static_cast<bool>(std::getline(values, value))
                                    : static_cast<bool>(values >> value);
        const auto keys = provided ? parse_control_keys(value) : std::nullopt;
        if (!keys || (keys->empty() && value.find("none") == std::string::npos))
            set_control_keys(settings, index, {defaults[index]});
        else set_control_keys(settings, index, *keys);
        // Old files used U for Save Replay and had no Undo slot. Give Undo
        // its default U without shadowing the existing save action.
        if (index==11 && !provided && settings.keyCodes[10]==defaults[11])
            set_control_keys(settings, 10, {defaults[10]});
        // Adding Pause to an old file must not steal a customized F2 binding.
        if (index==12 && !provided) {
            for (std::size_t other=0; other<index; ++other) {
                const auto existing=control_keys(settings,other);
                if (std::find(existing.begin(),existing.end(),defaults[index])!=existing.end()) {
                    set_control_keys(settings,index,{});
                    break;
                }
            }
        }
    }
}

} // namespace

const std::array<int, Settings::kControlCount>& default_key_codes() {
#ifdef _WIN32
    static const std::array<int, Settings::kControlCount> values = {
        VK_LEFT, VK_RIGHT, VK_DOWN, VK_SPACE, VK_UP, 'Z', 'A', 'C', 'R', 'P', 'S', 'U', VK_F2};
#else
    static const std::array<int, Settings::kControlCount> values = {
        KEY_LEFT, KEY_RIGHT, KEY_DOWN, KEY_SPACE, KEY_UP, KEY_Z, KEY_A, KEY_C, KEY_R, KEY_P, KEY_S, KEY_U, KEY_F2};
#endif
    return values;
}

const std::array<int, Settings::kControlCount>& default_player_two_key_codes() {
    return default_key_codes();
}

void load_keybindings(Settings& settings, const std::string& path) {
    load_keybindings_from(settings, path, default_key_codes(), true);
}

void load_player_two_keybindings(Settings& settings, const std::string& path) {
    load_keybindings_from(settings, path, default_player_two_key_codes(), false);
}

bool save_keybindings(Settings& settings) {
    std::ofstream output(std::filesystem::u8path(settings.keybindingPath), std::ios::trunc);
    if (!output) return false;
    for (std::size_t index = 0; index < Settings::kControlCount; ++index)
        output << control_keys_text(settings, index) << '\n';
    return static_cast<bool>(output);
}

std::vector<int> control_keys(const Settings& settings, std::size_t control) {
    std::vector<int> keys;
    const auto action=Input::kBindableActions.at(control);
    if (settings.keyCodes.at(control) >= 0 && !Input::reserved_binding_key(settings.keyCodes[control],action))
        keys.push_back(settings.keyCodes[control]);
    for (const int key : settings.additionalKeyCodes.at(control))
        if (key >= 0 && !Input::reserved_binding_key(key,action) && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    return keys;
}

void set_control_keys(Settings& settings, std::size_t control, const std::vector<int>& keys) {
    auto& first = settings.keyCodes.at(control);
    auto& extra = settings.additionalKeyCodes.at(control);
    const auto action=Input::kBindableActions.at(control);
    first = -1;
    extra.clear();
    for (const int key : keys) {
        if (key < 0 || Input::reserved_binding_key(key,action) || key == first ||
            std::find(extra.begin(), extra.end(), key) != extra.end()) continue;
        if (first < 0) first = key;
        else extra.push_back(key);
    }
}

void toggle_control_key(Settings& settings, std::size_t control, int key) {
    if (key < 0 || control >= Settings::kControlCount || Input::reserved_binding_key(key,Input::kBindableActions[control])) return;
    auto selected = control_keys(settings, control);
    const bool present = std::find(selected.begin(), selected.end(), key) != selected.end();
    for (std::size_t index = 0; index < Settings::kControlCount; ++index) {
        auto keys = control_keys(settings, index);
        keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        if (index == control && !present) keys.push_back(key);
        set_control_keys(settings, index, keys);
    }
}

std::optional<std::vector<int>> parse_control_keys(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return std::nullopt;
    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    if (text == "none" || text == "-1") return std::vector<int>{};
    std::string value(text);
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream input(value);
    std::vector<int> keys;
    std::string token;
    while (input >> token) {
        const auto key = Util::Text::parse_integer<int>(token);
        if (!key || *key < 0) return std::nullopt;
        // Repair legacy phantom bindings while retaining valid alternate keys.
        if (Input::reserved_binding_key(*key, Input::Action::Unknown)) continue;
        if (std::find(keys.begin(), keys.end(), *key) == keys.end()) keys.push_back(*key);
    }
    if (keys.empty()) return std::nullopt;
    return keys;
}

std::string control_keys_text(const Settings& settings, std::size_t control) {
    std::string result;
    for (const int key : control_keys(settings, control)) {
        if (!result.empty()) result += ", ";
        result += std::to_string(key);
    }
    return result.empty() ? "none" : result;
}

} // namespace UserConfig
