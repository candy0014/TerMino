#include "tetr/config/user_config.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"
#include "tetr/config/map_store.h"
#include "tetr/challenges/challenges.h"
#include "tetr/util/runtime_directory_lock.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <set>
#include <thread>
#include <vector>

namespace TetrTests::shared_storage_test {
namespace {
std::string contents(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
void temporary_settings(const std::filesystem::path& folder) {
    UserConfig::Settings owner;owner.persistencePath=(folder/"player.ini").u8string();
    owner.keybindingPath=(folder/"keys.txt").u8string();owner.keyCodes=UserConfig::default_key_codes();
    owner.username="SAVED";assert(UserConfig::save(owner) && UserConfig::save_keybindings(owner));
    const auto profile=contents(owner.persistencePath),keys=contents(owner.keybindingPath);
    auto temporary=owner;temporary.persistenceEnabled=false;temporary.username="TEMPORARY";
    temporary.fieldWidth=17;UserConfig::set_control_keys(temporary,0,{});
    assert(UserConfig::save(temporary) && UserConfig::save_keybindings(temporary));
    assert(contents(owner.persistencePath)==profile && contents(owner.keybindingPath)==keys);
    assert(temporary.fieldWidth==17 && owner.fieldWidth==10);
    assert(Challenges::history_path(temporary)==Challenges::history_path(owner));
    UserConfig::reset_settings(temporary);assert(!temporary.persistenceEnabled);
    UserConfig::copy_settings_except_bindings(temporary,owner);assert(!temporary.persistenceEnabled);
    assert(UserConfig::save(temporary) && contents(owner.persistencePath)==profile);
}
void write_collections(const std::filesystem::path& folder,int worker) {
    const auto maps=folder/"maps.txt",presets=folder/"presets.txt",historyPath=folder/"player.ini.challenges";
    for(int i=0;i<6;++i) {
        const auto name=std::to_string(worker)+"-"+std::to_string(i);
        Runtime::MapDefinition map;map.name=name;UserConfig::Maps::SaveUndo undo;
        assert(UserConfig::Maps::save(map,undo,maps));
        UserConfig::Presets::SavedPreset preset;preset.name=name;
        preset.fields[UserConfig::SettingId::FieldWidth]=10+worker;
        assert(UserConfig::Presets::save(preset,presets));
        if(i>=2)continue; // Stay within the existing ten-recent-record policy.
        Util::FileWriteLock transaction(std::filesystem::path(historyPath).concat(".transaction"));
        assert(transaction.acquired());Challenges::History history;
        assert(Challenges::load(history,historyPath));
        const auto run=history.nextRunId++;
        history.records.push_back({run,static_cast<std::int64_t>(run)*1000,"sprint-40",{40,30.+worker},false,{}});
        assert(Challenges::save(history,historyPath));
    }

}
void concurrent_collections(const std::filesystem::path& folder) {
    const auto maps=folder/"maps.txt",presets=folder/"presets.txt",historyPath=folder/"player.ini.challenges";
    std::vector<std::thread> writers;
    for(int worker=0;worker<4;++worker)writers.emplace_back([&,worker] {write_collections(folder,worker);});
    for(auto& writer:writers)writer.join();
    UserConfig::Maps::Collection mapData;UserConfig::Presets::Collection presetData;Challenges::History history;
    assert(UserConfig::Maps::load(mapData,maps) && mapData.maps.size()==24 && mapData.nextId==25);
    const auto defaults=UserConfig::Presets::definitions().size();
    assert(UserConfig::Presets::load(presetData,presets) && presetData.presets.size()==24+defaults && presetData.nextId==25+defaults);
    assert(Challenges::load(history,historyPath) && history.records.size()==8 && history.nextRunId==9);
    std::set<std::string> names;for(const auto& map:mapData.maps)names.insert(map.name);assert(names.size()==24);
    names.clear();for(const auto& preset:presetData.presets)if(preset.kind==UserConfig::PresetKind::Custom)names.insert(preset.name);assert(names.size()==24);
    std::set<std::uint64_t> runs;for(const auto& record:history.records)runs.insert(record.runId);assert(runs.size()==8);
}
}
int run(int argc,char** argv) {
    if(argc==4 && std::string(argv[1])=="--worker") {
        write_collections(std::filesystem::u8path(argv[2]),std::stoi(argv[3]));return 0;
    }
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("termino-shared-中文 "+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    temporary_settings(folder);concurrent_collections(folder);std::filesystem::remove_all(folder);
    return 0;
}
} // namespace TetrTests::shared_storage_test
