#include "tetr/config/user_config.h"
#include "tetr/config/profile_store.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"
#include "tetr/config/map_store.h"
#include "tetr/challenges/challenges.h"
#include "tetr/app/settings_menu.h"
#include "tetr/app/application_state.h"
#include "tetr/input/services.h"
#include "tetr/runtime/game_context.h"
#include "../src/config/config_store.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <thread>

namespace TetrTests::client_config_test {
namespace {
using Json=UserConfig::Storage::Json;
std::string contents(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
Json document() {Json data;assert(UserConfig::Storage::read(UserConfig::kClientConfigPath,data));return data;}
UserConfig::Settings startup(bool second=false) {
    UserConfig::Settings s;
    if(second)UserConfig::load_player_two_keybindings(s);else UserConfig::load_keybindings(s);
    UserConfig::load_startup_settings(s,second);return s;
}
}
int run(int,char**) {
    namespace fs=std::filesystem;
    const auto old=fs::current_path(),directory=fs::temp_directory_path()/fs::u8path("termino-json-中文 "+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory/".tetr");fs::current_path(directory);
    struct Cleanup {fs::path old,directory;~Cleanup(){fs::current_path(old);fs::remove_all(directory);}} cleanup{old,directory};
    UserConfig::Settings native;native.persistencePath=UserConfig::kLegacyUserConfigPath;native.keyCodes=UserConfig::default_key_codes();
    native.fieldWidth=17;native.softDropFactor=.025;native.softDropFixed=1;native.mapId=2;native.gameMode=3;
    native.username="中文玩家";native.replayPath=".tetr/replay/中文 录像.txt";
    native.quickPlayModBackup=UserConfig::snapshot_quick_play_mods(native).values;(*native.quickPlayModBackup)[12]=1;
    UserConfig::set_control_keys(native,0,{901,902});UserConfig::set_control_keys(native,3,{});
    assert(UserConfig::Presets::apply(native,UserConfig::PresetKind::Delayed));
    assert(UserConfig::save(native));
    auto expected=native;UserConfig::load(expected,native.persistencePath);
    const auto presets=UserConfig::Presets::encode(expected.presets);
    const auto nativeBytes=contents(native.persistencePath);
    std::ofstream(".tetr/player1.ini")<<"[MODEL]\nWidth = 13\n";
    std::ofstream(".tetr/player2.ini")<<"[MODEL]\nWidth = 14\n[MAPS]\nSelected = 3\n";
    const auto foreign=std::string(UserConfig::kConfigPlatform)=="linux"?"windows":"linux";
    const auto foreignPath=fs::path(".tetr")/(std::string("player1_")+foreign+".ini");
    std::ofstream(foreignPath)<<"[MODEL]\nWidth = 19\n[CONTROLS]\nMove Piece Left = 37, 91\n";
    std::ofstream(".tetr/graphics.ini")<<"width=1440\nheight=900\nframe_limit=144\nvsync=0\nmaximized=1\n";
    std::ofstream(".tetr/active_player")<<1;
    const auto legacyHistory=fs::path(std::string(UserConfig::kLegacyUserConfigPath)+".challenges");
    std::ofstream(legacyHistory)<<"TETR_CHALLENGES 2 1 0\n";
    auto first=startup(),second=startup(true);
    assert(first.persistencePath==second.persistencePath && first.persistencePlayer==0 && second.persistencePlayer==1);
    assert(UserConfig::profile_identity(first)==UserConfig::kLegacyUserConfigPath && UserConfig::profile_identity(first)!=UserConfig::profile_identity(second));
    assert(first.fieldWidth==17 && second.fieldWidth==14 && first.username=="中文玩家" && first.softDropFixed);
    assert(UserConfig::control_keys(first,0)==std::vector<int>({901,902}) && UserConfig::control_keys(first,3).empty());
    assert(UserConfig::Presets::encode(first.presets)==presets && first.quickPlayModBackup==native.quickPlayModBackup);
    assert(contents(native.persistencePath)==nativeBytes && contents(Challenges::history_path(first))==contents(legacyHistory));
    assert(Challenges::history_path(first)!=Challenges::history_path(second));
    auto data=document();assert(data["version"]==1 && data["players"]["player1"].contains("legacy"));
    assert(data["players"]["player1"][foreign]["settings"]["CONTROLS"]["Move Piece Left"]==Json::array({37,91}));
    const auto graphics=UserConfig::load_graphics_settings();
    assert(graphics.width==1440 && graphics.height==900 && graphics.frameLimit==144 && !graphics.vsync && graphics.maximized);
    assert(UserConfig::load_active_player()==1);
    first.username="转义\\与\"引号";first.replayPath=".tetr/replay/中文 = 文件.txt";
    first.das=.12345678901234567;assert(UserConfig::save(first));
    second.username="第二玩家";assert(UserConfig::save(second));
    auto reloaded=startup();assert(reloaded.username==first.username && reloaded.replayPath==first.replayPath && reloaded.das==first.das);
    assert(startup(true).username==second.username);
    std::thread a([&]{for(int i=0;i<20;++i){first.fieldWidth=10+i%10;assert(UserConfig::save(first));}});
    std::thread b([&]{for(int i=0;i<20;++i){second.fieldWidth=20+i%10;assert(UserConfig::save(second));}});
    std::thread c([&]{for(int i=0;i<20;++i){auto g=graphics;g.width=1200+i;assert(UserConfig::save_graphics_settings(UserConfig::kClientConfigPath,g));assert(UserConfig::save_active_player(UserConfig::kClientConfigPath,i%2));}});
    a.join();b.join();c.join();assert(startup().fieldWidth==19 && startup(true).fieldWidth==29);
    assert(UserConfig::load_graphics_settings().width==1219 && UserConfig::load_active_player()==1);
    assert(UserConfig::save_keybindings(first) && !fs::exists(UserConfig::kDefaultKeybindingPath));
    assert(UserConfig::Maps::remap_saved_profiles(UserConfig::kClientConfigPath,{{1,0},{2,1},{3,2}}));
    assert(startup().mapId==1 && startup(true).mapId==2);
    {
        Runtime::SessionState states[2];Runtime::RuntimeServices services[2];Input::Services input[2];
        Runtime::GameContext one(states[0],services[0]),two(states[1],services[1]);App::ApplicationState app;
        one.attach_app(app);two.attach_app(app);one.attach_user_settings(first);two.attach_user_settings(second);
        one.attach_input(input[0]);two.attach_input(input[1]);
        app.primaryContext=&one;app.localPlayerTwoContext=&two;app.localVersusSettingsAvailable=true;
        first.squareStyle=0;second.squareStyle=1;
        assert(App::Settings::synchronize_shared_appearance(two,true));
        assert(startup().squareStyle==1 && startup(true).squareStyle==1);
        App::Settings::adopt_single_player_settings(two);
        assert(first.persistencePlayer==1 && app.savedPlayerOneSettings->persistencePlayer==0);
        App::Settings::initialize_local_versus_player_settings(one);
        assert(first.persistencePlayer==0 && second.persistencePlayer==1);
        assert(startup().fieldWidth==19 && startup(true).fieldWidth==29);
    }
    UserConfig::reset_settings(second,true);assert(second.persistencePlayer==1 && second.persistencePath==UserConfig::kClientConfigPath);
    UserConfig::copy_settings_except_bindings(second,first);assert(second.persistencePlayer==1);
    data=document();data["future_field"]={{"keep",true}};assert(UserConfig::Storage::write(UserConfig::kClientConfigPath,data));
    assert(UserConfig::save(first) && document()["future_field"]["keep"]==true);
    // Invalid/unsupported data cannot be replaced by an unrelated player or
    // by a window save. Imports of old INI files remain independently usable.
    for(const auto invalid:{"{ broken", "{\"version\":42,\"players\":{}}", "{\"version\":1,\"players\":[]}"}) {
        std::ofstream(UserConfig::kClientConfigPath)<<invalid;
        assert(!UserConfig::save(first) && !UserConfig::save_graphics_settings(UserConfig::kClientConfigPath,graphics));
        assert(contents(UserConfig::kClientConfigPath)==invalid);
        bool rejected=false;try{startup();}catch(const std::exception&){rejected=true;}assert(rejected);
    }
    UserConfig::Settings imported;UserConfig::load(imported,native.persistencePath);assert(imported.fieldWidth==17);
    fs::remove(UserConfig::kClientConfigPath);UserConfig::Settings temporary;temporary.persistenceEnabled=false;
    UserConfig::load_startup_settings(temporary);assert(temporary.fieldWidth==17 && !fs::exists(UserConfig::kClientConfigPath));
    fs::create_directory("fresh");fs::current_path("fresh");
    auto fresh=startup(),partner=fresh;UserConfig::load_startup_settings(partner,true);
    assert(UserConfig::has_saved_profile(fresh) && !UserConfig::has_saved_profile(partner));
    Runtime::SessionState states[2];Runtime::RuntimeServices services[2];Input::Services input[2];
    Runtime::GameContext one(states[0],services[0]),two(states[1],services[1]);App::ApplicationState app;
    one.attach_app(app);two.attach_app(app);one.attach_user_settings(fresh);two.attach_user_settings(partner);
    one.attach_input(input[0]);two.attach_input(input[1]);app.primaryContext=&one;app.localPlayerTwoContext=&two;
    fresh.fieldWidth=21;App::Settings::initialize_local_versus_player_settings(one);
    assert(partner.fieldWidth==21 && partner.persistencePlayer==1 && UserConfig::has_saved_profile(partner));
    return 0;
}
}
