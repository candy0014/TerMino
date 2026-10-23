#include "tetr/util/runtime_directory_lock.h"
#include "tetr/config/map_store.h"
#include "tetr/config/user_config.h"
#include "tetr/config/settings_schema.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace UserConfig::Maps {
std::filesystem::path default_path() {
    const auto* path=std::getenv("TETR_MAP_FILE");
    return path && *path ? std::filesystem::u8path(path) : std::filesystem::path(".tetr/maps.txt");
}
bool load(Collection& collection,const std::filesystem::path& path) {
    std::error_code error;
    if(!std::filesystem::exists(path,error)) {if(error) return false;collection={};return true;}
    if(std::filesystem::file_size(path,error)>32000000 || error) return false;
    std::ifstream input(path);if(!input) return false;
    std::string header;Collection result;
    if(!std::getline(input,header)) return false;
    std::istringstream metadata(header);std::string magic;int version;char extra;
    if(!(metadata>>magic>>version>>result.nextId) || magic!="TETR_MAPS" || version!=1 || result.nextId<1 || (metadata>>extra)) return false;
    std::set<int> ids;std::string line,block;
    while(std::getline(input,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.empty() && block.empty()) continue;
        block+=line+'\n';
        if(line=="[/map]") {
            Runtime::MapDefinition map;
            if(!Runtime::decode_map(block,map) || map.id<1 || !ids.insert(map.id).second || map.id==std::numeric_limits<int>::max()) return false;
            result.nextId=std::max(result.nextId,map.id+1);result.maps.push_back(std::move(map));block.clear();
            if(result.maps.size()>1000) return false;
        }
    }
    if(input.bad() || !block.empty()) return false;
    collection=std::move(result);return true;
}
static bool write(const Collection& collection,const std::filesystem::path& path) {
    std::string data="TETR_MAPS 1 "+std::to_string(collection.nextId)+'\n';
    for(const auto& map:collection.maps) {
        const auto encoded=Runtime::encode_map(map);
        if(encoded.empty() || data.size()+encoded.size()+1>32000000) return false;
        data+='\n';data+=encoded;
    }
    std::error_code error;
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),error);
    if(error) return false;
    auto temporary=path;temporary+=".tmp";
    std::ofstream output(temporary,std::ios::trunc);if(!output) return false;
    output<<data;
    output.close();if(!output) return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,path,error);return !error;
#endif
}
std::string default_name(const Collection& collection) {
    std::set<std::string> names;for(const auto& map:collection.maps) names.insert(map.name);
    for(int i=1;;++i) {auto name="地图"+std::to_string(i);if(!names.count(name)) return name;}
}
std::optional<Runtime::MapDefinition> find(int id,const std::filesystem::path& path) {
    if(id<=0) return {};
    Collection collection;if(!load(collection,path)) return {};
    for(const auto& map:collection.maps) if(map.id==id) return map;return {};
}
bool save(Runtime::MapDefinition& map,SaveUndo& undo,const std::filesystem::path& path) {
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    if(!Runtime::valid_map(map) || map.name.empty()) return false;
    Collection collection;if(!load(collection,path)) return false;
    auto found=std::find_if(collection.maps.begin(),collection.maps.end(),[&](const auto& existing){return existing.id==map.id;});
    auto saved=map;SaveUndo next;
    for(auto& row:saved.rows) std::replace(row.begin(),row.end(),'.',' ');
    if(map.id==0) {
        if(collection.maps.size()>=1000 || collection.nextId==std::numeric_limits<int>::max()) return false;
        saved.id=collection.nextId++;collection.maps.push_back(saved);
    } else {
        if(found==collection.maps.end()) return false;
        next.previous=*found;*found=saved;
    }
    next.saved=saved;
    if(!write(collection,path)) return false;
    map=std::move(saved);undo=std::move(next);return true;
}
bool undo_save(const SaveUndo& undo,const std::filesystem::path& path) {
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    Collection collection;if(!load(collection,path)) return false;
    const auto found=std::find_if(collection.maps.begin(),collection.maps.end(),[&](const auto& map){return map.id==undo.saved.id;});
    // Preserve other records and reject undo if someone changed this record.
    if(found==collection.maps.end() || *found!=undo.saved) return false;
    if(undo.previous) *found=*undo.previous;else collection.maps.erase(found);
    return write(collection,path);
}
int renumbered_id(int id,const IdChanges& changes) {
    for(const auto& change:changes) if(change.first==id) return change.second;
    return id;
}
void select(Settings& settings,const Runtime::MapDefinition* map) {
    settings.mapId=map ? map->id : 0;
    settings.mapRotationSystem=-1;
    settings.mapGoal=map ? static_cast<int>(map->goal) : 0;
    if(map) {
        if(map->goal==Runtime::MapGoal::ClearInitial) settings.goal=7;
        else if(map->goal==Runtime::MapGoal::AllClear) settings.goal=6;
        if(map->goal==Runtime::MapGoal::AllClear) settings.allClearTarget=1;
    } else if(settings.goal==7) settings.goal=0;
}
void remap_selection(Settings& settings,const IdChanges& changes) {
    const auto id=renumbered_id(settings.mapId,changes);
    if(settings.mapId && !id) {settings.mapGoal=0;settings.mapRotationSystem=-1;if(settings.goal==7) settings.goal=0;}
    settings.mapId=id;
    const auto remap=[&](PresetFields& fields) {
        const auto selected=fields.find(SettingId::MapId);
        if(selected==fields.end()) return;
        const int before=std::get<int>(selected->second),after=renumbered_id(before,changes);
        selected->second=after;
        if(before && !after) {
            if(fields.count(SettingId::MapGoal)) fields[SettingId::MapGoal]=0;
            if(fields.count(SettingId::Goal) && std::get<int>(fields[SettingId::Goal])==7) fields[SettingId::Goal]=0;
            if(fields.count(SettingId::MapRotationSystem)) fields[SettingId::MapRotationSystem]=-1;
        }
    };
    remap(settings.presets.base);
    for(auto& layer:settings.presets.layers) {remap(layer.reference);remap(layer.values);}
    if(settings.challengeAdjustment) {
        remap(settings.challengeAdjustment->before);
        if(settings.challengeAdjustment->snapshot) {
            auto snapshot=*settings.challengeAdjustment->snapshot;
            remap_selection(snapshot,changes);
            settings.challengeAdjustment->snapshot=std::make_shared<const Settings>(std::move(snapshot));
        }
    }
}
bool remove(int id,const std::filesystem::path& path,IdChanges* changes) {
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    Collection collection;if(!load(collection,path)) return false;
    const auto found=std::find_if(collection.maps.begin(),collection.maps.end(),[&](const auto& map){return map.id==id;});
    if(found==collection.maps.end()) return false;
    collection.maps.erase(found);
    IdChanges remap{{id,0}};int next=1;
    for(auto& map:collection.maps) {remap.emplace_back(map.id,next);map.id=next++;}
    collection.nextId=next;
    if(!write(collection,path)) return false;
    if(changes) *changes=std::move(remap);
    return true;
}
} // namespace UserConfig::Maps
