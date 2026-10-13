#include "settings_maps.h"
#include "tetr/config/map_store.h"
#include "tetr/config/presets.h"
#include "ui_text.h"
#include "tetr/app/application_state.h"
#include "tetr/runtime/game_context.h"
#include <algorithm>
#include <set>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace App::Settings {
namespace {
bool remap_profile(const std::filesystem::path& path,const UserConfig::Settings& profile) {
    std::ifstream input(path,std::ios::binary);if(!input) return false;
    const std::string data{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    if(input.bad()) return false;
    // Windows cannot replace a profile while its read handle is still open.
    input.close();
    std::string updated,section;
    const auto trim=[](const std::string& text) {
        const auto first=text.find_first_not_of(" \t\r"),last=text.find_last_not_of(" \t\r");
        return first==std::string::npos ? std::string{} : text.substr(first,last-first+1);
    };
    for(std::size_t start=0;start<data.size();) {
        const auto end=data.find('\n',start);auto line=data.substr(start,end-start);
        const auto text=trim(line);
        if(!text.empty() && text.front()=='[' && text.back()==']') section=text.substr(1,text.size()-2);
        const auto equals=line.find('=');
        if(equals!=std::string::npos) {
            const auto key=trim(line.substr(0,equals));std::optional<int> value;
            if(section=="PRESETS" && key=="State") line=line.substr(0,equals+1)+" "+UserConfig::Presets::encode(profile.presets);
            if(section=="MAPS" && key=="Selected") value=profile.mapId;
            if(!profile.mapId && section=="MAPS" && key=="Rotation System") value=profile.mapRotationSystem;
            if(!profile.mapId && section=="MODEL" && key=="Map Mode") value=profile.mapGoal;
            if(value) {
                const auto first=line.find_first_not_of(" \t",equals+1);
                if(first!=std::string::npos) {
                    auto last=first;if(line[last]=='-' || line[last]=='+') ++last;
                    while(last<line.size() && line[last]>='0' && line[last]<='9') ++last;
                    if(last>first) line.replace(first,last-first,std::to_string(*value));
                }
            }
        }
        updated+=line;if(end==std::string::npos) break;updated+='\n';start=end+1;
    }
    auto temporary=path;temporary+=".tmp";
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);if(!output) return false;
    output<<updated;output.close();if(!output) return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code error;std::filesystem::rename(temporary,path,error);return !error;
#endif
}
}
bool map_action(Model::NodeKind kind) {
    using K=Model::NodeKind;
    return kind==K::MapNew || kind==K::MapUse || kind==K::MapEdit || kind==K::MapDelete || kind==K::MapDisable;
}
Model::SettingNode make_map_menu(UserConfig::Settings& user) {
    using Node=Model::SettingNode;using K=Model::NodeKind;using UiText::tr;
    Node menu{"maps","Maps","Open a map's menu directly. Creating, using, editing and deleting maps require confirmation."};
    Node create{"map-new","New map","Open the field and sequence editor.",K::MapNew};create.confirmTwice=true;
    Node disable{"map-disable","Do not use a map","Use the ordinary field and piece settings.",K::MapDisable};disable.confirmTwice=true;
    menu.children={std::move(create),std::move(disable)};
    UserConfig::Maps::Collection collection;
    if(!UserConfig::Maps::load(collection)) {
        menu.children.front().enabled=false;menu.description="Could not read the map collection. Existing maps will not be overwritten.";
        menu.summary=tr("Unavailable");return menu;
    }
    menu.summary=tr("None");
    for(const auto& map:collection.maps) {
        Node record{"map-"+std::to_string(map.id),"#"+std::to_string(map.id)+": "+map.name,"Select, edit or delete this map."};
        record.mapId=map.id;record.literalTitle=true;
        record.mapPreview=std::make_shared<const Runtime::MapDefinition>(map);
        for(const auto& action:{Node{"map-use","Use map","Use this map's starting field and piece generation.",K::MapUse},
                Node{"map-edit","Edit map","Edit the field and sequence; S saves the map immediately.",K::MapEdit},
                Node{"map-delete","Delete map","Delete this map from the collection; other maps with the same name remain.",K::MapDelete}}) {
            auto child=action;child.confirmTwice=true;child.mapId=map.id;child.mapPreview=record.mapPreview;record.children.push_back(std::move(child));
        }
        if(user.mapId==map.id) menu.summary=map.name+" (#"+std::to_string(map.id)+")";
        menu.children.push_back(std::move(record));
    }
    if(user.mapId && menu.summary==tr("None")) {
        menu.summary=tr("Unavailable");
        menu.description="The selected map is unavailable. Choose another map or do not use a map.";
    }
    return menu;
}
bool delete_map(Runtime::GameContext& context,int id,bool& settingsSaved,const std::vector<Runtime::GameContext*>& sharedOwners,UserConfig::Maps::IdChanges* remapped) {
    UserConfig::Maps::IdChanges changes;
    const auto mapPath=UserConfig::Maps::default_path();
    if(!UserConfig::Maps::remove(id,mapPath,&changes)) return false;
    if(remapped) *remapped=changes;
    settingsSaved=UserConfig::Presets::remap_maps(changes) && settingsSaved;
    auto* app=context.app_component();
    std::set<UserConfig::Settings*> owners;
    std::set<std::filesystem::path> savedPaths;
    const auto update=[&](UserConfig::Settings* settings) {
        if(!settings || !owners.insert(settings).second) return;
        const auto before=settings->mapId;
        const auto previousPresets=UserConfig::Presets::encode(settings->presets);
        UserConfig::Maps::remap_selection(*settings,changes);
        if(!settings->presetDefined && !settings->persistencePath.empty()) {
            const auto path=std::filesystem::path(settings->persistencePath).lexically_normal();
            if(savedPaths.insert(path).second && (before!=settings->mapId || previousPresets!=UserConfig::Presets::encode(settings->presets))) settingsSaved=UserConfig::save(*settings) && settingsSaved;
        }
    };
    update(&context.user_settings());
    std::vector<App::ApplicationState*> applications;
    if(app) applications.push_back(app);
    for(auto* sharedOwner:sharedOwners) if(sharedOwner) {
        update(&sharedOwner->user_settings());
        if(sharedOwner->app_component() && std::find(applications.begin(),applications.end(),sharedOwner->app_component())==applications.end())
            applications.push_back(sharedOwner->app_component());
    }
    for(auto* app:applications) {
        if(app->primaryContext) update(&app->primaryContext->user_settings());
        if(app->localPlayerTwoContext) update(&app->localPlayerTwoContext->user_settings());
        if(app->savedPlayerOneSettings) update(&*app->savedPlayerOneSettings);
        const auto remapKey=[&](const std::string& key) {
            if(key.rfind("map-",0)!=0 || key.size()<=4 || !std::all_of(key.begin()+4,key.end(),[](char c){return c>='0' && c<='9';})) return key;
            const auto found=std::find_if(changes.begin(),changes.end(),[&](const auto& change){return key=="map-"+std::to_string(change.first);});
            return found==changes.end() ? key : found->second ? "map-"+std::to_string(found->second) : std::string("map-new");
        };
        const auto remapPath=[&](const std::string& path) {
            std::string result;std::size_t start=0;
            while(start<path.size()) {
                const auto end=path.find('/',start);const auto token=path.substr(start,end-start);
                if(!result.empty()) result+='/';result+=remapKey(token);
                if(end==std::string::npos) break;start=end+1;
            }
            return result;
        };
        for(auto* state:{&app->settingsMenu,&app->playerTwoSettingsMenu}) {
            std::map<std::string,std::string> entries;
            for(const auto& entry:state->selectedEntries) entries[remapPath(entry.first)]=remapKey(entry.second);
            state->selectedEntries=std::move(entries);
            for(auto& key:state->path) key=remapKey(key);
            const auto deleted=std::find(state->path.begin(),state->path.end(),"map-new");
            if(deleted!=state->path.end()) state->path.erase(deleted,state->path.end());
        }
    }
    // Also keep inactive player profiles in the collection's directory valid.
    std::error_code error;const auto directory=mapPath.parent_path().empty() ? std::filesystem::path(".") : mapPath.parent_path();
    std::filesystem::directory_iterator iterator(directory,error);
    for(;!error && iterator!=std::filesystem::directory_iterator{};iterator.increment(error)) {
        const auto path=iterator->path().lexically_normal();
        if(path.extension()!=".ini" || savedPaths.count(path)) continue;
        UserConfig::Settings profile;UserConfig::load(profile,path.string());
        const auto before=profile.mapId;const auto previousState=UserConfig::Presets::encode(profile.presets);
        UserConfig::Maps::remap_selection(profile,changes);
        if(before!=profile.mapId || previousState!=UserConfig::Presets::encode(profile.presets)) settingsSaved=remap_profile(path,profile) && settingsSaved;
    }
    return true;
}
}
