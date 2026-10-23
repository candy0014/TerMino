#include "tetr/util/runtime_directory_lock.h"
#include "tetr/config/user_config.h"
#include "config_store.h"

#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"
#include "tetr/config/map_store.h"
#include "tetr/config/profile_store.h"
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
#include <stdexcept>
#include <limits>
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
    copied.persistencePlayer=target.persistencePlayer;copied.persistencePlatform=target.persistencePlatform;
    copied.persistenceEnabled=target.persistenceEnabled;
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

namespace {
using Storage::Json;
std::string player_key(int index) {return index==1?"player2":"player1";}
Settings persistent_copy(const Settings& settings) {
    auto copy=Challenges::persistent_settings(settings);
    std::error_code error;const auto replay=std::filesystem::u8path(copy.replayPath);
    if(replay.is_absolute()) {
        const auto root=std::filesystem::current_path(error);
        if(!error) {
            const auto relative=replay.lexically_normal().lexically_relative(root);
            if(!relative.empty()) {
                copy.replayPath=relative.generic_u8string();
                if(*relative.begin()!=".." && copy.replayPath.rfind(".tetr/replay/",0)!=0)copy.replayPath="./"+copy.replayPath;
            }
        }
    }
    Presets::record_edits(copy);return copy;
}
Json encode_profile(const Settings& settings) {
    auto copy=persistent_copy(settings);Json profile=Json::object();
    // Bindings are numeric arrays within each platform profile. Scalar values
    // keep their actual JSON types, including Unicode strings and paths.
    for(const auto& descriptor:setting_schema()) {
        auto& value=profile["settings"][std::string(section_name(descriptor.section))]
            [std::string(descriptor.keyBinding?descriptor.label:descriptor.diskName)];
        if(descriptor.keyBinding)value=control_keys(copy,descriptor.controlIndex);
        else std::visit([&](auto member){value=member.get();},bind_setting(copy,descriptor));
    }
    profile["presets"]=Presets::encode(copy.presets);
    if(copy.quickPlayModBackup)profile["quick_play_recovery"]=*copy.quickPlayModBackup;
    return profile;
}
bool decode_profile(Settings& settings,const Json& profile) {
    if(!profile.is_object() || !profile.contains("settings") || !profile["settings"].is_object())return false;
    auto copy=settings;Challenges::restore_replay(copy);Challenges::restore_adjustment(copy);
    for(const auto& descriptor:setting_schema()) {
        const auto section=profile["settings"].find(std::string(section_name(descriptor.section)));
        if(section==profile["settings"].end() || !section->is_object())continue;
        const auto value=section->find(std::string(descriptor.keyBinding?descriptor.label:descriptor.diskName));
        if(value==section->end())continue;
        if(descriptor.keyBinding) {
            if(!value->is_array() || value->size()>256)continue;
            std::vector<int> keys;bool valid=true;
            for(const auto& key:*value) {
                if(!key.is_number_integer() || key<0 || key>std::numeric_limits<int>::max()){valid=false;break;}
                keys.push_back(key.get<int>());
            }
            if(valid)set_control_keys(copy,descriptor.controlIndex,keys);
        } else {
            const bool text=std::holds_alternative<std::string Settings::*>(descriptor.member);
            if((text && !value->is_string()) || (!text && !value->is_number()))continue;
            if(std::holds_alternative<int Settings::*>(descriptor.member) &&
                (!value->is_number_integer() || *value<std::numeric_limits<int>::min() || *value>std::numeric_limits<int>::max()))continue;
            if(text)assign_setting_text(copy,descriptor,value->get<std::string>());
            else std::visit([&](auto member) {
                using T=std::decay_t<decltype(member.get())>;
                if constexpr(std::is_same_v<T,int> || std::is_same_v<T,double>)member.get()=value->get<T>();
            },bind_setting(copy,descriptor));
        }
    }
    copy.presets={};
    if(profile.contains("presets") && profile["presets"].is_string())Presets::decode(profile["presets"].get<std::string>(),copy.presets);
    copy.quickPlayModBackup.reset();
    if(profile.contains("quick_play_recovery")) {
        const auto& recovery=profile["quick_play_recovery"];
        if(recovery.is_array() && recovery.size()==13) {
            std::array<int,13> values{};bool valid=true;
            for(int i=0;i<13;++i) {
                if(!recovery[i].is_number_integer() || recovery[i]<0 || recovery[i]>(i==7?3:i==11?4:i==12?10:2)){valid=false;break;}
                values[i]=recovery[i].get<int>();
            }
            if(valid && values[12]>=1)copy.quickPlayModBackup=values;
        }
    }
    migrate_legacy_modes(copy);Presets::record_edits(copy);settings=std::move(copy);return true;
}
const Json* find_profile(const Json& document,const Settings& settings) {
    const auto players=document.find("players");if(players==document.end() || !players->is_object())return nullptr;
    const auto player=players->find(player_key(settings.persistencePlayer));if(player==players->end() || !player->is_object())return nullptr;
    const auto profile=player->find(settings.persistencePlatform);return profile==player->end()?nullptr:&*profile;
}
Json foreign_controls(const std::filesystem::path& file,const std::string& platform,int player) {
    const std::array<int,13> linuxKeys{{105,106,108,57,103,44,30,46,19,25,31,22,60}},
        windowsKeys{{37,39,40,32,38,90,65,67,82,80,83,85,113}};
    const auto& defaults=platform=="windows"?windowsKeys:linuxKeys;
    Json controls=Json::object();
    for(const auto& d:setting_schema())if(d.keyBinding)controls[std::string(d.label)]=Json::array({defaults[d.controlIndex]});
    const auto binding=file.parent_path().parent_path()/(player?"keybinding_player2_"+platform+".txt":"keybinding_"+platform+".txt");
    std::ifstream keys(binding);std::string contents((std::istreambuf_iterator<char>(keys)),{});std::istringstream values(contents);
    const bool lists=contents.find(',')!=std::string::npos || contents.find("none")!=std::string::npos;
    for(const auto& d:setting_schema())if(d.keyBinding) {
        std::string value;if(!(lists?bool(std::getline(values,value)):bool(values>>value)))break;
        if(const auto parsed=parse_control_keys(value))controls[std::string(d.label)]=*parsed;
    }
    std::ifstream input(file);std::string line;bool inControls=false;
    while(std::getline(input,line)) {
        const auto text=trim(line);if(text.empty())continue;
        if(text.front()=='[' && text.back()==']'){inControls=text=="[CONTROLS]";continue;}
        if(!inControls)continue;const auto equals=text.find('=');if(equals==std::string::npos)continue;
        const auto name=trim(std::string_view(text).substr(0,equals));
        if(!controls.contains(name))continue;
        if(const auto parsed=parse_control_keys(std::string_view(text).substr(equals+1)))controls[name]=*parsed;
    }
    return controls;
}
bool migrate_profiles(const std::filesystem::path& path,const Settings& initial) {
    return Storage::update(path,[&](Json& document) {
        if(!document.contains("players"))document["players"]=Json::object();
        for(int player=0;player<2;++player) {
            auto& profiles=document["players"][player_key(player)];if(profiles.is_null())profiles=Json::object();
            if(!profiles.is_object())return false;
            for(const std::string platform:{"legacy","linux","windows"}) {
                if(profiles.contains(platform)) {
                    if(!profiles[platform].is_object() || !profiles[platform].contains("settings") || !profiles[platform]["settings"].is_object())return false;
                    continue;
                }
                const auto source=path.parent_path()/(player_key(player)+(platform=="legacy"?"":"_"+platform)+".ini");
                if(!std::filesystem::is_regular_file(source))continue;
                Settings imported;imported.keyCodes=default_key_codes();
                const auto keyPath=source.parent_path().parent_path()/(player?std::string(kDefaultPlayerTwoKeybindingPath):std::string(kDefaultKeybindingPath));
                if(std::filesystem::is_regular_file(keyPath))load_keybindings(imported,keyPath.u8string());
                const bool native=platform==kConfigPlatform || (platform=="legacy" && std::string(kConfigPlatform)=="linux");
                load_settings(imported,source.u8string(),native);
                auto profile=encode_profile(imported);
                if(!native) {
                    profile["settings"][std::string(section_name(Section::Controls))]=foreign_controls(source,platform=="legacy"?"linux":platform,player);
                    std::ifstream input(source);std::string line;bool presets=false;
                    while(std::getline(input,line)) {
                        const auto text=trim(line);if(text.empty())continue;
                        if(text.front()=='[' && text.back()==']'){presets=text=="[PRESETS]";continue;}
                        const auto equal=text.find('=');
                        if(presets && equal!=std::string::npos && trim(std::string_view(text).substr(0,equal))=="State")
                            profile["presets"]=trim(std::string_view(text).substr(equal+1));
                    }
                }
                profiles[platform]=std::move(profile);
            }
        }
        auto& profiles=document["players"][player_key(initial.persistencePlayer)];
        if(!profiles.contains(initial.persistencePlatform)) {
            auto imported=initial;
            const auto legacy=path.parent_path()/(player_key(initial.persistencePlayer)+".ini");
            const bool existing=std::filesystem::is_regular_file(legacy);
            if(existing)load_settings(imported,legacy.u8string(),std::string(kConfigPlatform)=="linux");
            if(initial.persistencePlayer==0 || existing)profiles[initial.persistencePlatform]=encode_profile(imported);
        }
        if(!document.contains("graphics"))document["graphics"]=Storage::legacy_graphics(path.parent_path()/"graphics.ini");
        if(!document.contains("active_player")) {
            int owner=0;std::ifstream(path.parent_path()/"active_player")>>owner;document["active_player"]=owner==1?1:0;
        }
        return true;
    });
}
}
void load(Settings& settings,const std::string& path) {
    if(!Storage::json_path(std::filesystem::u8path(path))){load_settings(settings,path,true);return;}
    Json document;if(!Storage::read(std::filesystem::u8path(path),document))return;
    if(const auto* profile=find_profile(document,settings))decode_profile(settings,*profile);
}
void load_startup_settings(Settings& settings,bool playerTwo,std::string_view importPath) {
    settings.persistencePath=kClientConfigPath;settings.persistencePlayer=playerTwo?1:0;settings.persistencePlatform=kConfigPlatform;
    if(!importPath.empty()){load(settings,std::string(importPath));return;}
    const auto path=std::filesystem::u8path(settings.persistencePath);
    if(settings.persistenceEnabled && !migrate_profiles(path,settings))throw std::runtime_error("Could not read or save .tetr/config.json; the existing file was preserved.");
    if(std::filesystem::exists(path)) {
        load(settings,settings.persistencePath);
        const auto stem=player_key(settings.persistencePlayer)+"_"+settings.persistencePlatform;
        const auto history=Challenges::history_path(settings),legacy=path.parent_path()/(stem+".ini.challenges");
        if(settings.persistenceEnabled && !std::filesystem::exists(history) && std::filesystem::is_regular_file(legacy)) {
            Util::FileWriteLock lock(history);std::error_code error;
            if(!lock.acquired())throw std::runtime_error("Could not lock the challenge history during migration.");
            if(!std::filesystem::exists(history) && !std::filesystem::copy_file(legacy,history,std::filesystem::copy_options::none,error))
                throw std::runtime_error("Could not migrate the challenge history; the original file was preserved.");
        }
        return;
    }
    const auto native=playerTwo?kLegacyPlayerTwoUserConfigPath:kLegacyUserConfigPath;
    if(std::filesystem::exists(native)){load_settings(settings,native,true);return;}
    const auto legacy=playerTwo?".tetr/player2.ini":".tetr/player1.ini";
    if(std::filesystem::exists(legacy))load_settings(settings,legacy,std::string(kConfigPlatform)=="linux");
}

bool migrate_client_config() {
    try {
        Settings first;load_keybindings(first);load_startup_settings(first);
        auto second=first;load_player_two_keybindings(second);load_startup_settings(second,true);return true;
    }catch(const std::exception&) {return false;}
}
bool has_saved_profile(const Settings& settings) {
    if(settings.persistencePath.empty())return false;
    const auto path=std::filesystem::u8path(settings.persistencePath);
    if(!Storage::json_path(path))return std::filesystem::exists(path);
    Json document;if(!Storage::read(path,document))return false;
    const auto* profile=find_profile(document,settings);return profile && profile->is_object() && profile->contains("settings");
}

bool save(const Settings& settings) {
    if (!settings.persistenceEnabled || settings.persistencePath.empty()) return true;
    const std::filesystem::path path = std::filesystem::u8path(settings.persistencePath);
    if(Storage::json_path(path))return Storage::update(path,[&](Json& document) {
        auto& players=document["players"];if(players.is_null())players=Json::object();
        auto& profiles=players[player_key(settings.persistencePlayer)];if(profiles.is_null())profiles=Json::object();
        if(!profiles.is_object())return false;
        profiles[settings.persistencePlatform]=encode_profile(settings);return true;
    });
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    auto copy = persistent_copy(settings);
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

bool Maps::remap_saved_profiles(const std::filesystem::path& file,const IdChanges& changes) {
    if(!std::filesystem::exists(file))return true;
    return Storage::update(file,[&](Json& document) {
        if(!document.contains("players"))return true;
        for(auto& player:document["players"]) {
            if(!player.is_object())return false;
            for(auto& stored:player) {
                if(!stored.is_object() || !stored.contains("settings") || !stored["settings"].is_object())return false;
                Settings settings;
                for(const auto id:{SettingId::MapId,SettingId::MapGoal,SettingId::MapRotationSystem,SettingId::Goal}) {
                    const auto& descriptor=setting_descriptor(id);const auto section=std::string(section_name(descriptor.section));
                    if(stored["settings"].contains(section) && stored["settings"][section].is_object()) {
                        const auto value=stored["settings"][section].find(std::string(descriptor.diskName));
                        if(value!=stored["settings"][section].end() && value->is_number_integer())assign_setting_text(settings,descriptor,value->dump());
                    }
                }
                if(stored.contains("presets") && stored["presets"].is_string())Presets::decode(stored["presets"].get<std::string>(),settings.presets);
                Maps::remap_selection(settings,changes);
                for(const auto id:{SettingId::MapId,SettingId::MapGoal,SettingId::MapRotationSystem,SettingId::Goal}) {
                    const auto& descriptor=setting_descriptor(id);
                    auto& value=stored["settings"][std::string(section_name(descriptor.section))][std::string(descriptor.diskName)];
                    std::visit([&](auto member){value=member.get();},bind_setting(settings,descriptor));
                }
                stored["presets"]=Presets::encode(settings.presets);
            }
        }
        return true;
    });
}
} // namespace UserConfig
