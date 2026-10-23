#include "tetr/util/runtime_directory_lock.h"
#include "tetr/challenges/challenges.h"
#include "tetr/config/presets.h"
#include "tetr/runtime/game_modes.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <cstdlib>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace UserConfig::Presets {
namespace {
using Id=SettingId;
constexpr std::size_t maxLayers=256;
PresetValue read(const Settings& settings,Id id) { return read_value(settings,id); }
void write(Settings& settings,Id id,const PresetValue& value) { write_value(settings,id,value); }
void mechanism_handling(PresetFields& values) {
    for(const auto& entry:forced_handling_settings()) {
        const auto value=values.find(entry.personal);
        if(value==values.end() || values.count(entry.enabled) || values.count(entry.value)) continue;
        values.emplace(entry.enabled,1);values.emplace(entry.value,value->second);values.erase(value);
        if(entry.personal==Id::SoftDropFactor) {
            const auto fixed=values.find(Id::SoftDropFixed);
            if(fixed!=values.end()) {values.emplace(Id::ForcedSdfFixed,fixed->second);values.erase(fixed);}
        }
    }
}
// Presets own separate concerns: Classic supplies rules, Delayed handling,
// and Marathon a goal/speed curve. Inactive and personal settings stay intact.
PresetFields fields(PresetKind kind) {
    const bool classic=kind==PresetKind::Classic,delayed=kind==PresetKind::Delayed;
    const bool marathon=kind==PresetKind::Marathon;
    const double hz=classic ? 60.0988 : 60.0;
    PresetFields result;
    const auto rule=[&](Id id,PresetValue value){if(classic) result.emplace(id,value);};
    const auto handling=[&](Id id,PresetValue value){if(classic || delayed) result.emplace(id,value);};
    const auto progression=[&](Id id,PresetValue value){if(classic || marathon) result.emplace(id,value);};
    for (const auto& descriptor:setting_schema()) {
        const auto id=descriptor.id;
        if(id>=Id::ForceSdf && id<=Id::ForcedDasPrecharge) continue;
        if (descriptor.keyBinding || descriptor.section==Section::Network || descriptor.section==Section::Others) continue;
        switch (id) {
            case Id::ChallengeRules: rule(id,0);break;
            case Id::GameMode: progression(id,0);break;
            case Id::Goal: if(marathon) result.emplace(id,2);break;
            case Id::GarbageTarget: case Id::PieceTarget: case Id::AttackTarget: case Id::AllClearTarget: break;
            case Id::RacingDistance: if(marathon) result.emplace(id,150);break;
            case Id::FieldWidth: rule(id,10);break;
            case Id::FieldHeight: rule(id,20);break;
            case Id::NextCount: rule(id,1);break;
            case Id::HoldEnabled: rule(id,0);break;
            case Id::HoldCount: rule(id,1);break;
            case Id::RotationSystem: rule(id,9);break;
            case Id::RandomPieces: rule(id,6);break;
            case Id::Ghost: rule(id,0);break;
            case Id::GravityInterval: progression(id,classic ? 48/hz : 1.0);break;
            case Id::GravityProgression: progression(id,classic ? 1 : 2);break;
            case Id::GravityLevelLines: progression(id,10);break;
            case Id::GravityAcceleration: progression(id,1.0);break;
            case Id::SoftDropFactor: handling(id,classic ? 2/hz : 20.0);break;
            case Id::SoftDropFixed: handling(id,classic ? 1 : 0);break;
            case Id::Das: handling(id,(classic ? 16.0 : 10.0)/hz);break;
            case Id::Arr: handling(id,(classic ? 6.0 : 2.0)/hz);break;
            case Id::Dcd: handling(id,0.0);break;
            case Id::PreMove: case Id::DasPrecharge: handling(id,classic ? 0 : 1);break;
            case Id::LockDelay: handling(id,classic ? 0.0 : 30/hz);break;
            case Id::LockResetLimit: handling(id,classic ? 0 : 15);break;
            case Id::LineClearDelay: handling(id,(classic ? 17.0 : 30.0)/hz);break;
            case Id::EntryDelay: handling(id,(classic ? 10.0 : 6.0)/hz);break;
            case Id::SpinBonuses: rule(id,9);break;
            case Id::B2BReward: rule(id,2);break;
            case Id::PerfectClearEnabled: case Id::PerfectClearCharge: rule(id,0);break;
            case Id::HalfClearEnabled: case Id::GarbageSpecialAttack: case Id::B2BCharging: rule(id,0);break;
            case Id::Disable180: case Id::DisableHardDrop: rule(id,1);break;
            case Id::GarbageMode: progression(id,0);break;
            case Id::InstantGravity: progression(id,0);break;
            case Id::FieldInvisible: case Id::AntiGravity: case Id::InfiniteHold: rule(id,0);break;
            case Id::InfiniteMovement: handling(id,0);break;
            case Id::DisableLeft: case Id::DisableRight: case Id::DisableClockwise: case Id::DisableCounterclockwise:
            case Id::DisableSoftDrop: rule(id,0);break;
            // Preserve undo, pause, bot choice/speed, simulation/turn timing,
            // dormant modifiers, reward amounts and unrelated mode parameters.
            case Id::DisableUndo: case Id::DisablePause: case Id::AutoplayMode:
            case Id::SimulationPps: case Id::TurnPieces:
            case Id::B2BRewardAmount: case Id::B2BChargeThreshold: case Id::B2BChargeReduction: case Id::AttackMultiplier:
            case Id::PerfectClearAttack: case Id::HalfClearAttack: case Id::HalfClearCharge:
            case Id::QpExpert: case Id::QpNoHold: case Id::QpMessy: case Id::QpGravity: case Id::QpVolatile:
            case Id::QpDoubleHole: case Id::QpInvisible: case Id::QpAllSpin: case Id::QpBackfire:
            case Id::QpSnakeEyes: case Id::QpSnowball: case Id::QpFool:
            case Id::BlitzTime: case Id::ReplayPath: case Id::ReplayTimeMultiplier:
            case Id::CheesePattern: case Id::HoleCount: case Id::LayerHeight: case Id::GarbageInterval:
            case Id::CheeseMessiness: case Id::CheeseDensity: case Id::SurvivalApm:
            case Id::HitMultiplier: case Id::CancellationMultiplier: case Id::GarbageInTime: case Id::GarbageEntryMode:
            case Id::GarbageRiseInterval:
            case Id::MessinessWithinAttack: case Id::AvoidSameColumn: case Id::GarbageCap: case Id::OpenerPhase:
            case Id::QpDefaultFloor: case Id::AutoplayPps: case Id::AutoplayWidth: case Id::AutoplayHandlingSimulation:
            case Id::MapId: case Id::MapRotationSystem: case Id::MapGoal: break;
            case Id::InvisibilityDelay: case Id::InvisibilityInterval: case Id::InvisibilityDuration: break;
            default: throw std::logic_error("unreviewed preset setting");
        }
    }
    // Built-in handling is a game mechanism; it never rewrites personal feel.
    if(classic || delayed) mechanism_handling(result);
    return result;
}
bool marathon_parameter(Id id) {
    return id==Id::GameMode || id==Id::Goal || id==Id::GravityInterval || id==Id::GravityProgression ||
        id==Id::GravityLevelLines || id==Id::GravityAcceleration || id==Id::InstantGravity;
}
void recompose(Settings& settings,bool keepMatchMode) {
    const auto before=snapshot_quick_play_mods(settings);
    const int matchMode=settings.gameMode;
    for (const auto& [id,value]:settings.presets.base) write(settings,id,value);
    for (const auto& [id,value]:settings.presets.base)
        if (const auto* layer=source(settings,id)) write(settings,id,layer->values.at(id));
    if (keepMatchMode) settings.gameMode=matchMode;
    auto& base=settings.presets.base;
    for (auto it=base.begin();it!=base.end();) {
        if (settings.presetDefined) {
            if (source(settings,it->first) || !settings.presets.undefinedBase.count(it->first))
                settings.presetDefined->insert(it->first);
            else settings.presetDefined->erase(it->first);
        }
        if (!source(settings,it->first)) {
            settings.presets.undefinedBase.erase(it->first);
            it=base.erase(it);
        }
        else ++it;
    }
    reconcile_quick_play_mods(settings,before);
    record_edits(settings);
}
void write_fields(std::ostream& out,const PresetFields& values) {
    out<<values.size();
    for (const auto& [id,value]:values) {
        out<<' '<<static_cast<int>(id)<<' '<<value.index()<<' ';
        std::visit([&](const auto& scalar) {
            using T=std::decay_t<decltype(scalar)>;
            if constexpr (std::is_same_v<T,std::string>) out<<std::quoted(scalar);
            else if constexpr (std::is_same_v<T,std::vector<int>>) {
                out<<scalar.size();for(int key:scalar) out<<' '<<key;
            } else out<<scalar;
        },value);
    }
}
bool read_fields(std::istream& in,PresetFields& values) {
    std::size_t count=0;
    if (!(in>>count) || count>setting_schema().size()) return false;
    for (std::size_t i=0;i<count;++i) {
        int number=0,type=0;
        if (!(in>>number>>type)) return false;
        const auto id=static_cast<Id>(number);
        const auto it=std::find_if(setting_schema().begin(),setting_schema().end(),[&](const auto& descriptor){return descriptor.id==id;});
        if (it==setting_schema().end()) return false;
        PresetValue value;
        if (type==0 && !it->keyBinding && std::holds_alternative<int Settings::*>(it->member)) {
            int scalar=0;if (!(in>>scalar)) return false;value=scalar;
        } else if (type==1 && std::holds_alternative<double Settings::*>(it->member)) {
            double scalar=0;if (!(in>>scalar) || !std::isfinite(scalar)) return false;value=scalar;
        } else if(type==2 && std::holds_alternative<std::string Settings::*>(it->member)) {
            std::string scalar;if(!(in>>std::quoted(scalar)) || scalar.size()>4096 || scalar.find_first_of("\r\n")!=std::string::npos) return false;value=std::move(scalar);
        } else if(type==3 && it->keyBinding) {
            std::size_t size=0;if(!(in>>size) || size>256) return false;
            std::vector<int> keys;std::set<int> seen;
            for(std::size_t j=0;j<size;++j) {int key;if(!(in>>key) || key<0 || key>65535 || !seen.insert(key).second) return false;keys.push_back(key);}
            value=std::move(keys);
        } else return false;
        if (!values.emplace(id,value).second) return false;
    }
    return true;
}
}
const std::vector<Definition>& definitions() {
    static const std::vector<Definition> list={
        {PresetKind::Classic,"Classic blocks","NES-style rules: NRS, one NEXT, no Hold/hard drop/ghost; slow DAS and ARR. Fixed minimum entry/clear delays.",fields(PresetKind::Classic)},
        {PresetKind::Delayed,"Delayed blocks","Tetris Effect-style handling only: DAS 10f, ARR 2f, soft drop 20x, entry 6f and clear 30f; existing rules and goal stay.",fields(PresetKind::Delayed)},
        {PresetKind::Marathon,"Marathon","150-line goal with Marathon gravity progression; keeps existing rotation, randomizer, Hold, NEXT and handling. Combines with Classic in either order.",fields(PresetKind::Marathon)}
    };
    return list;
}
const Definition& definition(PresetKind kind) {
    for (const auto& entry:definitions()) if(entry.kind==kind) return entry;
    throw std::logic_error("unknown preset");
}
const PresetLayer* source(const Settings& settings,Id id) {
    // Keep the built-in Classic/Marathon composition, while a later custom
    // layer can override either one just like any other explicit setting.
    if (marathon_parameter(id)) {
        for (auto it=settings.presets.layers.rbegin();it!=settings.presets.layers.rend();++it) {
            if (!it->values.count(id)) continue;
            if (it->kind==PresetKind::Custom || it->kind==PresetKind::Marathon) return &*it;
        }
    }
    for (auto it=settings.presets.layers.rbegin();it!=settings.presets.layers.rend();++it)
        if (it->values.count(id)) return &*it;
    return nullptr;
}
bool modified(const PresetLayer& layer) { return layer.values!=layer.reference; }
void record_edits(Settings& settings) {
    for (auto& layer:settings.presets.layers)
        for (auto& [id,value]:layer.values) if (source(settings,id)==&layer &&
            (!settings.challengeAdjustment || !settings.challengeAdjustment->before.count(id))) value=read(settings,id);
}
bool apply(Settings& settings,PresetKind kind,bool keepMatchMode) {
    Collection collection;if(!load(collection)) return false;
    const auto found=std::find_if(collection.presets.begin(),collection.presets.end(),[&](const auto& entry){return entry.kind==kind;});
    return found!=collection.presets.end() && apply(settings,*found,keepMatchMode);
}
bool remove(Settings& settings,std::uint64_t serial,bool keepMatchMode) {
    Challenges::restore_adjustment(settings);
    const auto it=std::find_if(settings.presets.layers.begin(),settings.presets.layers.end(),[&](const auto& layer){return layer.serial==serial;});
    if (it==settings.presets.layers.end()) return false;
    record_edits(settings);
    settings.presets.layers.erase(it);
    recompose(settings,keepMatchMode);
    return true;
}
bool undo(Settings& settings,bool keepMatchMode) {
    return !settings.presets.layers.empty() && remove(settings,settings.presets.layers.back().serial,keepMatchMode);
}
bool clear(Settings& settings,bool keepMatchMode) {
    Challenges::restore_adjustment(settings);
    if (settings.presets.layers.empty()) return false;
    record_edits(settings);
    settings.presets.layers.clear();recompose(settings,keepMatchMode);return true;
}
void commit(Settings& settings) { Challenges::restore_adjustment(settings);settings.presets.base.clear();settings.presets.layers.clear();settings.presets.undefinedBase.clear(); }
std::string encode(const PresetState& state) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<2<<' '<<state.nextSerial<<' ';write_fields(out,state.base);out<<' '<<state.layers.size();
    for (const auto& layer:state.layers) {
        out<<' '<<layer.serial<<' '<<static_cast<int>(layer.kind)<<' ';
        out<<std::quoted(layer.name)<<' ';write_fields(out,layer.reference);out<<' ';write_fields(out,layer.values);
    }
    return out.str();
}
bool decode(std::string_view text,PresetState& state) {
    if (text.size()>1024*1024) return false;
    std::istringstream in{std::string(text)};in.imbue(std::locale::classic());
    PresetState result;int version=0;std::size_t count=0;
    if (!(in>>version>>result.nextSerial) || (version!=1 && version!=2) || !result.nextSerial || !read_fields(in,result.base) ||
        !(in>>count) || count>maxLayers) return false;
    std::uint64_t previous=0;
    std::set<Id> used;
    for (std::size_t i=0;i<count;++i) {
        PresetLayer layer;int kind=0;
        if (!(in>>layer.serial>>kind) || kind<0 || kind>(version==1 ? 2 : 3) || layer.serial<=previous || layer.serial>=result.nextSerial ||
            (version==2 && (!(in>>std::quoted(layer.name)) || layer.name.size()>256 || layer.name.find_first_of("\r\n")!=std::string::npos)) ||
            !read_fields(in,layer.reference) || !read_fields(in,layer.values) || layer.values.empty() ||
            layer.reference.size()!=layer.values.size()) return false;
        layer.kind=static_cast<PresetKind>(kind);previous=layer.serial;
        for (const auto& [id,value]:layer.values) {
            if (!layer.reference.count(id) || !result.base.count(id) ||
                value.index()!=layer.reference.at(id).index() || value.index()!=result.base.at(id).index()) return false;
            used.insert(id);
        }
        result.layers.push_back(std::move(layer));
    }
    if (used.size()!=result.base.size()) return false;
    in>>std::ws;if (!in.eof()) return false;
    state=std::move(result);return true;
}
PresetValue read_value(const Settings& settings,Id id) {
    const auto& descriptor=setting_descriptor(id);
    if(descriptor.keyBinding) return control_keys(settings,descriptor.controlIndex);
    return std::visit([&](auto member)->PresetValue {
        if constexpr (!std::is_same_v<decltype(member),std::monostate>) return settings.*member;
        else throw std::logic_error("invalid preset field");
    },descriptor.member);
}
void write_value(Settings& settings,Id id,const PresetValue& value) {
    const auto& descriptor=setting_descriptor(id);
    if(descriptor.keyBinding) {set_control_keys(settings,descriptor.controlIndex,std::get<std::vector<int>>(value));return;}
    std::visit([&](auto member) {
        if constexpr (!std::is_same_v<decltype(member),std::monostate>) {
            using T=std::decay_t<decltype(settings.*member)>;
            settings.*member=std::get<T>(value);
        } else throw std::logic_error("invalid preset field");
    },descriptor.member);
}
std::string name(const PresetLayer& layer) {
    return !layer.name.empty() ? layer.name : definition(layer.kind).name;
}
bool defined(const Settings& settings,Id id) {return !settings.presetDefined || settings.presetDefined->count(id);}
void define(Settings& settings,Id id) {
    if(settings.presetDefined) settings.presetDefined->insert(id);
    // An edit to a formerly undefined base stays defined after removing layers.
    // Owned edits instead keep the original undefined base for normal undo.
    if(!source(settings,id)) settings.presets.undefinedBase.erase(id);
}
void undefine(Settings& settings,Id id) {
    if(!settings.presetDefined) return;
    const auto before=snapshot_quick_play_mods(settings);
    settings.presetDefined->erase(id);
    settings.presets.base.erase(id);settings.presets.undefinedBase.erase(id);
    for(auto& layer:settings.presets.layers) {layer.reference.erase(id);layer.values.erase(id);}
    auto& layers=settings.presets.layers;
    layers.erase(std::remove_if(layers.begin(),layers.end(),[](const auto& layer){return layer.values.empty();}),layers.end());
    Settings defaults;defaults.keyCodes=default_key_codes();write(settings,id,read(defaults,id));
    reconcile_quick_play_mods(settings,before);
}
PresetFields draft_fields(const Settings& settings) {
    PresetFields fields;
    for(const auto& descriptor:setting_schema())
        if(defined(settings,descriptor.id)) fields.emplace(descriptor.id,read(settings,descriptor.id));
    return fields;
}
bool apply(Settings& settings,const SavedPreset& preset,bool keepMatchMode,bool playerTwo) {
    Challenges::restore_adjustment(settings);
    if(settings.presets.layers.size()>=maxLayers || settings.presets.nextSerial==std::numeric_limits<std::uint64_t>::max()) return false;
    auto values=playerTwo && preset.playerTwoFields ? *preset.playerTwoFields : preset.fields;
    migrate_legacy_modes(settings);
    if(const auto assigned=values.find(Id::GameMode);assigned!=values.end()) {
        const int old=std::get<int>(assigned->second);
        if(old==1 || old==-1 || old==2 || old==-2) {
            assigned->second=0;
            values.try_emplace(Id::Goal,old==1 || old==-1 ? 2 : 1);
            if(old==2) values[Id::GravityProgression]=3;
        }
    }
    if(keepMatchMode) values.erase(Id::GameMode);
    if(values.empty()) return true;
    record_edits(settings);
    for(const auto& [id,value]:values) {
        if(!settings.presets.base.count(id) && settings.presetDefined && !defined(settings,id)) settings.presets.undefinedBase.insert(id);
        settings.presets.base.emplace(id,read(settings,id));
    }
    settings.presets.layers.push_back({settings.presets.nextSerial++,preset.kind,values,values,preset.name});
    recompose(settings,keepMatchMode);return true;
}
bool is_local_versus(const SavedPreset& preset) {
    const auto mode=preset.fields.find(Id::GameMode);
    return mode!=preset.fields.end() && std::get<int>(mode->second)==static_cast<int>(Runtime::GameMode::LocalVersus);
}
std::filesystem::path default_path() {
    const auto* path=std::getenv("TETR_PRESET_FILE");
    return path && *path ? std::filesystem::u8path(path) : std::filesystem::path(".tetr/presets.txt");
}
bool load(Collection& collection,const std::filesystem::path& path) {
    std::error_code error;
    if(!std::filesystem::exists(path,error)) {
        if(error) return false;
        Collection result;
        for(const auto& definition:definitions()) result.presets.push_back({result.nextId++,definition.name,definition.fields,definition.kind});
        collection=std::move(result);return true;
    }
    if(std::filesystem::file_size(path,error)>8000000 || error) return false;
    std::ifstream in(path);in.imbue(std::locale::classic());
    Collection result;std::string magic;int version=0;std::size_t count=0;
    if(!(in>>magic>>version>>result.nextId>>count) || magic!="TETR_PRESETS" || (version!=1 && version!=2) || result.nextId<1 || count>1000) return false;
    std::set<int> ids;
    for(std::size_t i=0;i<count;++i) {
        SavedPreset preset;int kind=0;
        if(!(in>>preset.id>>kind>>std::quoted(preset.name)) || kind<0 || kind>3 || preset.id<1 || preset.id>=result.nextId ||
            !ids.insert(preset.id).second || preset.name.empty() || preset.name.size()>256 ||
            preset.name.find_first_of("\r\n")!=std::string::npos || !read_fields(in,preset.fields)) return false;
        if(version==2) {
            int second=0;if(!(in>>second) || (second!=0 && second!=1)) return false;
            if(second) {preset.playerTwoFields.emplace();if(!read_fields(in,*preset.playerTwoFields)) return false;}
        }
        preset.kind=static_cast<PresetKind>(kind);
        if(preset.kind==PresetKind::Classic || preset.kind==PresetKind::Delayed) {
            mechanism_handling(preset.fields);
            if(preset.playerTwoFields) mechanism_handling(*preset.playerTwoFields);
        }
        result.presets.push_back(std::move(preset));
    }
    in>>std::ws;if(!in.eof() || in.bad()) return false;
    collection=std::move(result);return true;
}
std::string default_name(const Collection& collection) {
    for(int i=1;;++i) {
        auto name=std::string("预设")+std::to_string(i);
        if(std::none_of(collection.presets.begin(),collection.presets.end(),[&](const auto& preset){return preset.name==name;})) return name;
    }
}
static bool write_collection(const Collection& collection,const std::filesystem::path& path) {
    std::ostringstream data;data.imbue(std::locale::classic());data<<std::setprecision(17);
    data<<"TETR_PRESETS 2 "<<collection.nextId<<' '<<collection.presets.size()<<'\n';
    for(const auto& entry:collection.presets) {
        data<<entry.id<<' '<<static_cast<int>(entry.kind)<<' '<<std::quoted(entry.name)<<' ';write_fields(data,entry.fields);
        data<<' '<<static_cast<bool>(entry.playerTwoFields);
        if(entry.playerTwoFields) {data<<' ';write_fields(data,*entry.playerTwoFields);}
        data<<'\n';
    }
    if(data.str().size()>8000000) return false;
    // Validate before replacing an existing library.
    std::istringstream check; // Fields are validated with the same reader used on load.
    for(const auto& entry:collection.presets) {
        for(const auto* fields:{&entry.fields,entry.playerTwoFields ? &*entry.playerTwoFields : nullptr}) {
            if(!fields) continue;
            std::ostringstream encoded;encoded<<std::setprecision(17);write_fields(encoded,*fields);
            check.str(encoded.str());check.clear();PresetFields parsed;if(!read_fields(check,parsed)) return false;
        }
    }
    std::error_code error;
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),error);
    if(error) return false;
    auto temporary=path;temporary+=".tmp";
    std::ofstream out(temporary,std::ios::trunc);if(!out) return false;
    out<<data.str();out.close();if(!out) return false;
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
#else
    std::filesystem::rename(temporary,path,error);if(error) return false;
#endif
    return true;
}
bool save(SavedPreset& preset,const std::filesystem::path& path) {
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    if(preset.name.empty() || preset.name.size()>256 || preset.name.find_first_of("\r\n")!=std::string::npos) return false;
    Collection collection;if(!load(collection,path)) return false;
    auto saved=preset;
    if(!saved.id) {
        if(collection.presets.size()>=1000 || collection.nextId==std::numeric_limits<int>::max()) return false;
        saved.id=collection.nextId++;collection.presets.push_back(saved);
    } else {
        const auto found=std::find_if(collection.presets.begin(),collection.presets.end(),[&](const auto& entry){return entry.id==saved.id;});
        if(found==collection.presets.end()) return false;*found=saved;
    }
    if(!write_collection(collection,path)) return false;
    preset=std::move(saved);return true;
}
std::optional<SavedPreset> find(int id,const std::filesystem::path& path) {
    Collection collection;if(!load(collection,path)) return {};
    const auto found=std::find_if(collection.presets.begin(),collection.presets.end(),[&](const auto& preset){return preset.id==id;});
    return found==collection.presets.end() ? std::nullopt : std::optional<SavedPreset>{*found};
}
bool remove_saved(int id,const std::filesystem::path& path) {
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    Collection collection;if(!load(collection,path)) return false;
    const auto found=std::find_if(collection.presets.begin(),collection.presets.end(),[&](const auto& preset){return preset.id==id;});
    if(found==collection.presets.end()) return false;
    collection.presets.erase(found);return write_collection(collection,path);
}
bool remap_maps(const std::vector<std::pair<int,int>>& changes,const std::filesystem::path& path) {
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    Collection collection;if(!load(collection,path)) return false;
    bool updated=false;
    for(auto& preset:collection.presets) {
        bool changed=false;
        for(auto* fields:{&preset.fields,preset.playerTwoFields ? &*preset.playerTwoFields : nullptr}) {
            if(!fields) continue;
            const auto found=fields->find(Id::MapId);
            if(found==fields->end()) continue;
            const int previous=std::get<int>(found->second);
            for(const auto& [oldId,newId]:changes) if(oldId==previous && oldId!=newId) {
                found->second=newId;changed=true;
                if(!newId) {
                    if(fields->count(Id::MapGoal)) (*fields)[Id::MapGoal]=0;
                    if(fields->count(Id::Goal) && std::get<int>((*fields)[Id::Goal])==7) (*fields)[Id::Goal]=0;
                    if(fields->count(Id::MapRotationSystem)) (*fields)[Id::MapRotationSystem]=-1;
                }
                break;
            }
        }
        updated=updated || changed;
    }
    return !updated || write_collection(collection,path);
}

}
