#include "tetr/config/user_config.h"

#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"
#include "tetr/challenges/challenges.h"
#include "tetr/util/text_codec.h"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <string_view>
#include <sstream>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace UserConfig {
namespace {

std::string trim(std::string_view input) {
    const auto isSpace = [](char value) {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    };
    std::size_t first = 0;
    std::size_t last = input.size();
    while (first < last && isSpace(input[first])) ++first;
    while (last > first && isSpace(input[last - 1])) --last;
    return std::string(input.substr(first, last - first));
}


} // namespace

void copy_settings_except_bindings(Settings& target,const Settings& source) {
    auto copied=Challenges::persistent_settings(source);
    copied.keyCodes=target.keyCodes;
    copied.additionalKeyCodes=target.additionalKeyCodes;
    copied.keybindingPath=target.keybindingPath;
    copied.persistencePath=target.persistencePath;
    if(copied.presetDefined && target.presetDefined) {
        for(const auto& descriptor:setting_schema()) if(descriptor.keyBinding) {
            if(target.presetDefined->count(descriptor.id)) copied.presetDefined->insert(descriptor.id);
            else copied.presetDefined->erase(descriptor.id);
        }
    }
    // The copied preset recovery must not later restore the other player's
    // bindings when an imported layer is removed.
    for(const auto& descriptor:setting_schema()) if(descriptor.keyBinding) {
        copied.presets.base.erase(descriptor.id);copied.presets.undefinedBase.erase(descriptor.id);
        for(auto& layer:copied.presets.layers) {layer.reference.erase(descriptor.id);layer.values.erase(descriptor.id);}
    }
    auto& layers=copied.presets.layers;
    layers.erase(std::remove_if(layers.begin(),layers.end(),[](const auto& layer){return layer.values.empty();}),layers.end());
    target=std::move(copied);
}

static void load_settings(Settings& settings, const std::string& path, bool loadKeybindings) {
    Challenges::restore_replay(settings);
    Challenges::restore_adjustment(settings);
    const int previousReplayKey=settings.keyCodes[10];
    bool undoBindingPresent=false,replayBindingPresent=false;
    bool pauseBindingPresent=false;
    bool cheeseMessinessPresent=false,cheeseDensityPresent=false;
    bool hitPresent=false,cancelPresent=false;
    std::optional<double> legacyHit,legacyCancel;
    std::ifstream input(std::filesystem::u8path(path));
    Section section = Section::Global;
    bool presetSection=false,recoverySection=false;
    settings.quickPlayModBackup.reset();
    PresetState presetState;
    std::string line;
    while (std::getline(input, line)) {
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped.rfind("//", 0) == 0) continue;
        if (stripped.front() == '[' && stripped.back() == ']') {
            presetSection=stripped=="[PRESETS]";
            recoverySection=stripped=="[QUICKPLAY RECOVERY]";
            section = parse_section_name(
                trim(std::string_view(stripped).substr(1, stripped.size() - 2)),
                section);
            continue;
        }
        const std::size_t separator = stripped.find('=');
        if (separator == std::string::npos) continue;
        const std::string name = trim(std::string_view(stripped).substr(0, separator));
        const std::string value = trim(std::string_view(stripped).substr(separator + 1));
        if (recoverySection) {
            if (name=="Mods") {
                std::array<int,13> values{};std::istringstream in(value);bool valid=true;
                for (auto& mod:values) valid=bool(in>>mod) && valid;
                in>>std::ws;valid=valid && in.eof();
                for (int i=0;i<13;++i) valid=valid && values[i]>=0 && values[i]<=(i==7 ? 3 : i==11 ? 4 : i==12 ? 10 : 2);
                if (valid && values[12]>=1) settings.quickPlayModBackup=values;
            }
            continue;
        }
        if (presetSection) {
            if (name=="State" && !Presets::decode(value,presetState)) presetState={};
            continue;
        }
        if ((section==Section::Garbage && name=="Garbage Multiple") ||
            (section==Section::Game && name=="Garbage Multiplier")) {
            Settings legacy;
            const bool hit=section==Section::Garbage;
            if (assign_setting_text(legacy,hit ? SettingId::HitMultiplier : SettingId::CancellationMultiplier,value)) {
                if (hit) legacyHit=legacy.hitMultiplier;
                else legacyCancel=legacy.cancelMultiplier;
            }
            continue;
        }
        bool control = false;
        if (section == Section::Controls) {
            for (const auto& descriptor : setting_schema()) {
                if (descriptor.keyBinding && descriptor.label == name) {
                    if (loadKeybindings) {
                        undoBindingPresent|=descriptor.id==SettingId::ControlUndo;
                        replayBindingPresent|=descriptor.id==SettingId::ControlReplay;
                        pauseBindingPresent|=descriptor.id==SettingId::ControlPause;
                        const auto keys = parse_control_keys(value);
                        if (keys) set_control_keys(settings, descriptor.controlIndex, *keys);
                    }
                    control = true;
                    break;
                }
            }
        }
        if (!control) {
            if (const SettingDescriptor* descriptor = find_setting(section, name)) {
                if (assign_setting_text(settings, *descriptor, value)) {
                    cheeseMessinessPresent |= descriptor->id==SettingId::CheeseMessiness;
                    cheeseDensityPresent |= descriptor->id==SettingId::CheeseDensity;
                    hitPresent |= descriptor->id==SettingId::HitMultiplier;
                    cancelPresent |= descriptor->id==SettingId::CancellationMultiplier;
                }
            }
        }
    }
    // Canonical names take precedence over old names, regardless of file order.
    if (loadKeybindings && !pauseBindingPresent) {
        if (settings.keyCodes[12]==0) set_control_keys(settings,12,{default_key_codes()[12]});
        const auto pauseKeys=control_keys(settings,12);
        for (std::size_t other=0; other<12; ++other) {
            const auto existing=control_keys(settings,other);
            if (std::any_of(pauseKeys.begin(),pauseKeys.end(),[&](int key) {
                    return std::find(existing.begin(),existing.end(),key)!=existing.end(); })) {
                set_control_keys(settings,12,{});
                break;
            }
        }
    }
    if (!hitPresent && legacyHit) settings.hitMultiplier=*legacyHit;
    if (!cancelPresent && legacyCancel) settings.cancelMultiplier=*legacyCancel;
    // Historical files shared one probability between both Cheese patterns.
    // Copy it once when importing; future edits and saves keep both independent.
    if (cheeseMessinessPresent && !cheeseDensityPresent) settings.cheeseDensity=settings.cheeseMessiness;
    if (!undoBindingPresent && replayBindingPresent && settings.keyCodes[10]==default_key_codes()[11]) {
        set_control_keys(settings, 10, {previousReplayKey>0 && previousReplayKey!=default_key_codes()[11]
            ? previousReplayKey : default_key_codes()[10]});
        set_control_keys(settings, 11, {default_key_codes()[11]});
    }
    settings.presets=std::move(presetState);
    migrate_legacy_modes(settings);
    if (settings.gameMode==3 || settings.gameMode==-3) {
        // Old profiles cannot recover an unknown earlier configuration;
        // preserve their loaded modifiers as the baseline for this visit.
        if (!settings.quickPlayModBackup) settings.quickPlayModBackup=snapshot_quick_play_mods(settings).values;
    } else settings.quickPlayModBackup.reset();
    Presets::record_edits(settings);
}

void load(Settings& settings, const std::string& path) {
    load_settings(settings, path, true);
}

void load_startup_settings(Settings& settings, bool playerTwo, std::string_view importPath) {
    settings.persistencePath = playerTwo ? kDefaultPlayerTwoUserConfigPath : kDefaultUserConfigPath;
    if (!importPath.empty()) {
        load(settings, std::string(importPath));
        return;
    }
    if (std::filesystem::exists(std::filesystem::u8path(settings.persistencePath))) {
        load(settings, settings.persistencePath);
        return;
    }
    const char* legacyPath = playerTwo ? ".tetr/player2.ini" : ".tetr/player1.ini";
    if (!std::filesystem::exists(legacyPath)) return;
#ifdef _WIN32
    // Legacy profiles used evdev codes. Keep the equivalent physical keys
    // already loaded from the Windows binding file while importing game settings.
    load_settings(settings, legacyPath, false);
#else
    load(settings, legacyPath);
#endif
    save(settings);
}

bool save(const Settings& settings) {
    if (settings.persistencePath.empty()) return true;
    const std::filesystem::path path = std::filesystem::u8path(settings.persistencePath);
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    auto copy = Challenges::persistent_settings(settings);
    const auto replayPath = std::filesystem::u8path(copy.replayPath);
    if (replayPath.is_absolute()) {
        const auto root = std::filesystem::current_path(error);
        if (!error) {
            const auto relative = replayPath.lexically_normal().lexically_relative(root);
            if (!relative.empty()) {
                copy.replayPath = relative.generic_u8string();
                if (*relative.begin() != ".." && copy.replayPath.rfind(".tetr/replay/",0) != 0)
                    copy.replayPath = "./" + copy.replayPath;
            }
        }
        error.clear();
    }
    Presets::record_edits(copy);
    Section previous = Section::Global;
    for (const auto& descriptor : setting_schema()) {
        if (descriptor.section != previous) {
            output << '\n' << '[' << section_name(descriptor.section) << "]\n";
            previous = descriptor.section;
        }
        output << (descriptor.keyBinding ? descriptor.label : descriptor.diskName) << " = ";
        if (descriptor.keyBinding) output << control_keys_text(copy, descriptor.controlIndex);
        else {
            std::visit([&](auto value) { output << value.get(); }, bind_setting(copy, descriptor));
            if (descriptor.id == SettingId::SoftDropFactor && copy.softDropFixed) output << 's';
        }
        output << '\n';
    }
    if (!copy.presets.layers.empty()) output<<"\n[PRESETS]\nState = "<<Presets::encode(copy.presets)<<'\n';
    if (copy.quickPlayModBackup) {
        output<<"\n[QUICKPLAY RECOVERY]\nMods =";
        for (const int value:*copy.quickPlayModBackup) output<<' '<<value;
        output<<'\n';
    }
    output.close();
    if (!output) return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}

} // namespace UserConfig
