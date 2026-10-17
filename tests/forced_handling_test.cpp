#include "tetr/config/multiplayer_config.h"
#include "tetr/config/normalization.h"
#include "tetr/config/presets.h"
#include "tetr/config/map_store.h"
#include "tetr/runtime/piece.h"
#include "tetr/replay/config_codec.h"
#include "tetr/app/settings_model.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <fstream>
struct TemporaryMaps {
    std::filesystem::path file=std::filesystem::temp_directory_path()/("termino_forced_maps_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt");
    std::string previous;
    TemporaryMaps() {
        if(const auto* value=std::getenv("TETR_MAP_FILE")) previous=value;
#ifdef _WIN32
        _putenv_s("TETR_MAP_FILE",file.string().c_str());
#else
        setenv("TETR_MAP_FILE",file.string().c_str(),1);
#endif
    }
    ~TemporaryMaps() {
        std::filesystem::remove(file);
#ifdef _WIN32
        _putenv_s("TETR_MAP_FILE",previous.c_str());
#else
        if(previous.empty()) unsetenv("TETR_MAP_FILE");else setenv("TETR_MAP_FILE",previous.c_str(),1);
#endif
    }
};
namespace Model=App::Settings::Model;
using Id=UserConfig::SettingId;
const Model::SettingNode* field(const Model::SettingNode& node,Id id) {
    if(node.item && node.item->id==id) return &node;
    for(const auto& child:node.children) if(const auto* found=field(child,id)) return found;
    return nullptr;
}
int main() {
    UserConfig::Settings user;user.das=.37;user.arr=.04;user.dcd=.025;user.softDropFactor=12;
    for(const auto& entry:UserConfig::forced_handling_settings()) {
        assert(!UserConfig::clamped_int(user,entry.enabled));
        const auto tree=Model::make_tree(user,false,0);
        assert(field(tree,entry.enabled) && !field(tree,entry.value));
        UserConfig::assign_setting_text(user,entry.enabled,"1");
        const auto enabled=Model::make_tree(user,false,0);
        assert(field(enabled,entry.value) && !field(enabled,entry.personal)->item->effective);
    }
    assert(UserConfig::assign_setting_text(user,Id::ForcedSdf,"2f"));
    assert(UserConfig::assign_setting_text(user,Id::ForcedDas,"1.5F"));
    assert(UserConfig::assign_setting_text(user,Id::ForcedArr,"0"));
    assert(UserConfig::assign_setting_text(user,Id::ForcedDcd,"0.1s"));
    user.forcedPreMove=0;user.forcedDasPrecharge=0;
    auto effective=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    assert(effective.handling.das.seconds()==.025 && effective.handling.arr.seconds()==0);
    assert(effective.handling.softDropFixed && effective.handling.softDropFactor==2.0/60);
    assert(effective.handling.dcd.seconds()==.1 && !effective.handling.preMove && !effective.handling.dasPrecharge);
    assert(user.das==.37 && user.arr==.04 && user.softDropFactor==12);
    user.persistencePath=(std::filesystem::temp_directory_path()/"termino_forced_handling.ini").string();
    assert(UserConfig::save(user));UserConfig::Settings loaded;UserConfig::load(loaded,user.persistencePath);
    assert(loaded.forceSdf && loaded.forcedSdfFixed && loaded.forcedDas==.025 && loaded.das==.37);
    assert(!UserConfig::assign_setting_text(loaded,Id::ForcedDas,"nan"));
    UserConfig::reset_section(loaded,UserConfig::Section::Game);
    assert(!loaded.forceDas && !loaded.forceSdf && loaded.das==.37);
    effective=Runtime::normalize_user_config(loaded,Runtime::GameMode::Zen);
    assert(effective.handling.das.seconds()==.37 && effective.handling.arr.seconds()==.04);
    UserConfig::Settings host,guest;host.forceDas=1;host.forcedDas=.015;host.das=.08;
    guest.das=.4;guest.arr=.07;guest.uiLanguage=1;guest.username="Local";
    host.fieldWidth=13;host.rotationSystem=1;
    const auto hostConfig=Runtime::normalize_user_config(host,Runtime::GameMode::Multiplayer);
    const auto decoded=UserConfig::decode_multiplayer_config(UserConfig::encode_multiplayer_config(host,hostConfig,true));
    assert(decoded && decoded->settings.forceDas && decoded->settings.forcedDas==.015);
    auto unsafe=host;unsafe.serverIp="server\x1b[2J";
    assert(!UserConfig::decode_multiplayer_config(UserConfig::encode_multiplayer_config(unsafe,hostConfig,true)));
    const auto match=UserConfig::apply_multiplayer_config(guest,Runtime::normalize_user_config(guest,Runtime::GameMode::Multiplayer),*decoded,true);
    assert(match.handling.das.seconds()==.015 && match.handling.arr.seconds()==guest.arr);
    assert(UserConfig::copy_multiplayer_config(guest,*decoded));
    assert(guest.forceDas && guest.forcedDas==.015 && guest.das==.4 && guest.arr==.07 && guest.username=="Local" && guest.uiLanguage==1);
    auto view=Model::make_host_tree(host,{});
    assert(field(view,Id::ForcedDas) && view.children.back().key=="copy-host-config");
    auto readonly=*field(view,Id::ForcedDas)->item;
    assert(readonly.readOnly && !Model::assign_scalar(readonly,"0.9") && host.forcedDas==.015);
    auto toggle=*field(view,Id::ForceDas)->item;Model::apply_choice(toggle,0);assert(host.forceDas==1);
    for(const auto& category:view.children) if(category.key=="NETWORK")
        for(const auto& row:category.children) if(!row.item) assert(!row.enabled);
    TemporaryMaps temporary;
    Runtime::MapDefinition existing;existing.name="Existing";UserConfig::Maps::SaveUndo undo;
    assert(UserConfig::Maps::save(existing,undo));
    auto remote=existing;remote.name="Host map";remote.rows.back()[0]='G';remote.generator=Runtime::MapGenerator::CustomSequence;
    remote.sequence=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    auto withMap=*decoded;withMap.map=std::make_shared<const Runtime::MapDefinition>(remote);
    assert(UserConfig::copy_multiplayer_config(guest,withMap) && guest.mapId!=existing.id);
    const auto imported=UserConfig::Maps::find(guest.mapId);assert(imported && imported->rows==remote.rows && imported->sequence==remote.sequence);
    assert(UserConfig::Maps::find(existing.id)->name=="Existing");
    const auto importedId=guest.mapId;
    assert(UserConfig::copy_multiplayer_config(guest,withMap) && guest.mapId==importedId);
    UserConfig::Maps::Collection maps;assert(UserConfig::Maps::load(maps) && maps.maps.size()==2);
    std::ofstream(temporary.file)<<"Invalid library";
    const auto before=guest;
    assert(!UserConfig::copy_multiplayer_config(guest,withMap) && guest.mapId==before.mapId && guest.fieldWidth==before.fieldWidth && guest.das==before.das);
    UserConfig::Settings preset;preset.das=.23;
    assert(UserConfig::Presets::apply(preset,UserConfig::PresetKind::Classic));
    assert(preset.das==.23 && preset.forceDas && preset.forcedDas==16/60.0988);
    assert(UserConfig::Presets::undo(preset) && !preset.forceDas && preset.das==.23);
    std::filesystem::remove(user.persistencePath);
    std::cerr<<"forced_handling: OK\n";
}
