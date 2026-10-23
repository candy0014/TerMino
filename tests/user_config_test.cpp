#include "tetr/config/user_config.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"
#include "tetr/input/action.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

namespace TetrTests::user_config_test {
int run(int argc,char** argv){
    {
        UserConfig::Settings fx;fx.persistencePath=(std::filesystem::temp_directory_path()/"tetr_effects_settings_test.ini").u8string();
        fx.effectsIntensity=150;fx.fieldShake=0;fx.particleIntensity=200;fx.landingEffect=123;fx.lineClearEffect=87;fx.hardDropTrail=12;fx.boardBounciness=175;
        assert(UserConfig::save(fx));
        UserConfig::Settings restored;UserConfig::load(restored,fx.persistencePath);
        assert(restored.effectsIntensity==150 && restored.fieldShake==0 && restored.particleIntensity==200);
        assert(restored.landingEffect==123 && restored.lineClearEffect==87 && restored.hardDropTrail==12 && restored.boardBounciness==175);
        std::filesystem::remove(std::filesystem::u8path(fx.persistencePath));
    }
    const auto temporary_path = [](const char* name) {
        return (std::filesystem::temp_directory_path() / std::filesystem::u8path(name)).u8string();
    };
    UserConfig::Settings a,b;
    assert(a.gameMode==0&&a.racingDistance==40&&a.blitzTime==120);
    assert(a.fieldWidth==10&&a.fieldHeight==20&&a.nextCount==5&&a.holdEnabled==1);
    assert(a.gravityInterval==.6&&a.das==.1&&a.arr==.01&&a.lockDelay==.5);
    assert(a.garbageInTime==0.3 && a.garbageEntryMode==0);
    assert(a.garbageRiseInterval==0);
    assert(a.squareStyle==0);
    assert(a.username.rfind("GUEST-",0)==0 && a.username.size()==14);
    assert(a.serverIp=="wss://termino.candy0014.icu/ws"&&a.keybindingPath==UserConfig::kDefaultKeybindingPath);
    a.fieldWidth=77;
    assert(b.fieldWidth==10); // settings objects are independent, not aliases.

    const std::string path=temporary_path("tetr_user_config_stage18_test.txt");
    {
        std::ofstream out(std::filesystem::u8path(path),std::ios::trunc);
        out<<"[MODEL]\n";
        out<<" Model = -2 \n";
        out<<"Width = 13\n";
        out<<"Height=41\n";
        out<<"Speed = -0.25abc\n";
        out<<"Server IP = 192.0.2.5\n";
        out<<"Linux Keyevent = 17\n";
        out<<"[QUICK PLAY]\n";
        out<<"Allspin = 3\n";
        out<<"Invisible = 1\n";
        out<<"[GAME]\n";
        out<<"Invisible = 1\n";
        out<<"Unknown Setting = 999\n";
        out<<"[GARBAGE]\n";
        out<<"GarbageInTime = 1.25\n";
        out<<"Garbage Entry Mode = 2\n";
        out<<"Garbage Rise Interval = 0.125\n";
        out<<"[MODEL]\n";
        out<<"Replay =   replay/example.txt   \n";
    }
    UserConfig::Settings loaded;
    UserConfig::load(loaded,path);
    std::ifstream updatedFile(std::filesystem::u8path(path));
    const std::string updatedText((std::istreambuf_iterator<char>(updatedFile)),std::istreambuf_iterator<char>());
    assert(updatedText.find("Unknown Setting = 999")!=std::string::npos);
    updatedFile.close();
    std::filesystem::remove(std::filesystem::u8path(path));
    assert(loaded.gameMode==0 && loaded.goal==1);
    assert(loaded.fieldWidth==13&&loaded.fieldHeight==41);
    assert(loaded.gravityInterval==-0.25);
    assert(loaded.serverIp=="192.0.2.5");
    assert(loaded.qpAllSpin==3);
    assert(loaded.qpInvisible==1&&loaded.fieldInvisible==1);
    assert(loaded.garbageInTime==1.25 && loaded.garbageEntryMode==2);
    assert(loaded.garbageRiseInterval==.125);
    assert(loaded.replayPath=="replay/example.txt");

    const auto& keys=UserConfig::default_key_codes();
    assert(keys.size()==UserConfig::Settings::kControlCount);
    const auto& playerTwoKeys=UserConfig::default_player_two_key_codes();
    assert(playerTwoKeys.size()==UserConfig::Settings::kControlCount);
    assert(playerTwoKeys==keys);
    assert(playerTwoKeys[9]==keys[9]); // both Settings keys default to P
    assert(keys[10]!=keys[11] && playerTwoKeys[11]==keys[11]);

    {
        UserConfig::Settings source,target;
        source.keyCodes=keys;target.keyCodes=playerTwoKeys;
        source.additionalKeyCodes[0]={701,702};target.additionalKeyCodes[0]={801,802};
        source.keybindingPath="source-keys.txt";target.keybindingPath="target-keys.txt";
        source.persistencePath="source.ini";target.persistencePath="target.ini";
        assert(UserConfig::Presets::apply(source,UserConfig::PresetKind::Delayed));
        // Give every non-binding schema field a different raw value, including
        // dormant parameters and strings outside the active mode's menu.
        for(const auto& descriptor:UserConfig::setting_schema()) {
            if(descriptor.keyBinding) continue;
            std::visit([&](auto member) {
                using T=decltype(member);
                if constexpr(std::is_same_v<T,int UserConfig::Settings::*>) source.*member=target.*member+1;
                else if constexpr(std::is_same_v<T,double UserConfig::Settings::*>) source.*member=target.*member+.125;
                else if constexpr(std::is_same_v<T,std::string UserConfig::Settings::*>) source.*member="copied value";
            },descriptor.member);
        }
        UserConfig::Presets::record_edits(source);
        const auto recovery=UserConfig::Presets::encode(source.presets);
        UserConfig::copy_settings_except_bindings(target,source);
        for(const auto& descriptor:UserConfig::setting_schema()) {
            if(descriptor.keyBinding) continue;
            std::visit([&](auto member) {
                if constexpr(!std::is_same_v<decltype(member),std::monostate>) assert(target.*member==source.*member);
            },descriptor.member);
        }
        assert(target.keyCodes==playerTwoKeys && source.keyCodes==keys);
        assert(target.additionalKeyCodes[0]==std::vector<int>({801,802}) && source.additionalKeyCodes[0]==std::vector<int>({701,702}));
        assert(target.keybindingPath=="target-keys.txt" && target.persistencePath=="target.ini");
        assert(UserConfig::Presets::encode(target.presets)==recovery);
        assert(UserConfig::Presets::undo(target));
        assert(UserConfig::Presets::encode(source.presets)==recovery); // Recovery is a deep copy.
        assert(target.keyCodes==playerTwoKeys && target.additionalKeyCodes[0]==std::vector<int>({801,802}));
    }

    {
        struct BindingFixture {
            std::filesystem::path previous=std::filesystem::current_path();
            std::filesystem::path directory=std::filesystem::temp_directory_path() /
                ("tetr_platform_bindings_" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            BindingFixture() {
                std::filesystem::create_directory(directory);
                std::filesystem::current_path(directory);
            }
            ~BindingFixture() {
                std::filesystem::current_path(previous);
                std::filesystem::remove_all(directory);
            }
        } fixture;
        // A legacy file must not replace the current platform's defaults.
        std::ofstream("keybinding.txt")<<"777\n";
        UserConfig::Settings native;
        UserConfig::load_keybindings(native);
        assert(native.keyCodes==keys);
        assert(native.keybindingPath==UserConfig::kDefaultKeybindingPath);
        native.keyCodes[0]=778;
        assert(UserConfig::save_keybindings(native));
        UserConfig::Settings fallback;
        UserConfig::load_keybindings(fallback,"missing_custom_bindings.txt");
        assert(fallback.keyCodes==native.keyCodes);
        assert(fallback.keybindingPath==UserConfig::kDefaultKeybindingPath);
        UserConfig::Settings second;
        UserConfig::load_player_two_keybindings(second);
        assert(second.keyCodes==playerTwoKeys);
        assert(second.keybindingPath==UserConfig::kDefaultPlayerTwoKeybindingPath);
        assert(UserConfig::save_keybindings(second));
        UserConfig::load_keybindings(fallback);
        assert(fallback.keyCodes==native.keyCodes);
        int legacy=0;
        std::ifstream("keybinding.txt")>>legacy;
        assert(legacy==777);

        std::filesystem::create_directory(".tetr");
        std::ofstream(".tetr/player1.ini")<<"[MODEL]\nWidth = 13\n[CONTROLS]\n"
            "Move Piece Left = 105\nMove Piece Right = 106\nSoft Drop = 108\n"
            "Replay = 22\n[OTHERS]\nObscuredColors = 1\n";
        std::ofstream(".tetr/player2.ini")<<"[MODEL]\nWidth = 14\n[CONTROLS]\nMove Piece Left = 105\n";
        UserConfig::Settings startup;
        UserConfig::load_keybindings(startup);
        UserConfig::load_startup_settings(startup);
        assert(startup.fieldWidth==13 && startup.obscuredColors==1);
        assert(startup.persistencePath==UserConfig::kDefaultUserConfigPath);
#ifdef _WIN32
        assert(startup.keyCodes==native.keyCodes); // Old evdev codes never override virtual keys.
#else
        assert(startup.keyCodes[0]==105 && startup.keyCodes[10]==keys[10]);
#endif
        assert(std::filesystem::exists(startup.persistencePath));
        startup.keyCodes[0]=779;
        assert(UserConfig::save(startup));
        UserConfig::Settings reopened;
        UserConfig::load_startup_settings(reopened);
        assert(reopened.keyCodes[0]==779 && reopened.fieldWidth==13); // Native edits survive restart.
        UserConfig::Settings secondStartup;
        UserConfig::load_player_two_keybindings(secondStartup);
        UserConfig::load_startup_settings(secondStartup,true);
        assert(secondStartup.fieldWidth==14);
        assert(secondStartup.persistencePath==UserConfig::kDefaultPlayerTwoUserConfigPath);
#ifdef _WIN32
        assert(secondStartup.keyCodes==playerTwoKeys);
#else
        assert(secondStartup.keyCodes[0]==105);
#endif
        UserConfig::Settings explicitImport;
        UserConfig::load_startup_settings(explicitImport,false,".tetr/player1.ini");
        assert(explicitImport.keyCodes[0]==105 && explicitImport.fieldWidth==13);
        std::ifstream legacyProfile(".tetr/player1.ini");
        const std::string legacyText((std::istreambuf_iterator<char>(legacyProfile)),{});
        assert(legacyText.find("Move Piece Left = 105")!=std::string::npos);
    }

    const std::string keyPath=temporary_path("tetr_keybinding_stage28_test.txt");
    {
        std::ofstream out(std::filesystem::u8path(keyPath),std::ios::trunc);
        for(std::size_t i=0;i<UserConfig::Settings::kControlCount;i++)
            out<<(i==3?-1:100+static_cast<int>(i))<<"\n";
    }
    UserConfig::Settings bindings;
    UserConfig::load_keybindings(bindings,keyPath);
    assert(bindings.keybindingPath==keyPath);
    assert(bindings.keyCodes[0]==100);
    assert(bindings.keyCodes[3]==UserConfig::default_key_codes()[3]);
    bindings.keyCodes[10]=777;
    bindings.keyCodes[11]=778;
    assert(UserConfig::save_keybindings(bindings));
    UserConfig::Settings bindingsReloaded;
    UserConfig::load_keybindings(bindingsReloaded,keyPath);
    assert(bindingsReloaded.keyCodes[10]==777); // Replay is persisted too.
    assert(bindingsReloaded.keyCodes[11]==778); // Undo has its own persisted binding.
    std::filesystem::remove(std::filesystem::u8path(keyPath));

    // A phantom Windows key must neither replace a valid binding nor persist.
#ifdef _WIN32
    assert(!UserConfig::parse_control_keys("255"));
    assert(UserConfig::parse_control_keys("37, 255, 39")==std::vector<int>({37,39}));
    {
        std::ofstream out(std::filesystem::u8path(keyPath));
        for (std::size_t i=0;i<keys.size();++i) out<<(i==0 ? 255 : keys[i])<<'\n';
    }
    UserConfig::Settings repaired;
    UserConfig::load_keybindings(repaired,keyPath);
    assert(repaired.keyCodes==keys);
    const auto phantomIni=temporary_path("tetr_phantom_key_config.ini");
    {
        std::ofstream out(std::filesystem::u8path(phantomIni));
        out<<"[CONTROLS]\nMove Piece Left = 255\nMove Piece Right = 39, 255\n";
    }
    UserConfig::load(repaired,phantomIni);
    assert(repaired.keyCodes==keys);
    UserConfig::toggle_control_key(repaired,0,255);
    assert(UserConfig::control_keys(repaired,0)==std::vector<int>({keys[0]}));
    UserConfig::set_control_keys(repaired,0,{255,keys[0],255});
    assert(UserConfig::control_keys(repaired,0)==std::vector<int>({keys[0]}));
    repaired.keyCodes[0]=255;
    repaired.additionalKeyCodes[0]={keys[0],255};
    assert(UserConfig::control_keys_text(repaired,0)==std::to_string(keys[0]));
    assert(UserConfig::save_keybindings(repaired));
    UserConfig::load_keybindings(repaired,keyPath);
    assert(repaired.keyCodes==keys);
    std::filesystem::remove(std::filesystem::u8path(keyPath));
    std::filesystem::remove(std::filesystem::u8path(phantomIni));
#endif

    // Legacy eleven-action files used U for Replay. Reserve it for Undo;
    // leave all other old bindings intact, including custom Replay keys.
    for (int replayKey:{keys[11],777}) {
        {
            std::ofstream out(std::filesystem::u8path(keyPath),std::ios::trunc);
            for (int i=0;i<11;++i) out<<(i==10?replayKey:keys[i])<<'\n';
        }
        UserConfig::load_keybindings(bindingsReloaded,keyPath);
        assert(bindingsReloaded.keyCodes[11]==keys[11]);
        assert(bindingsReloaded.keyCodes[10]==(replayKey==keys[11]?keys[10]:777));
        for (int i=0;i<10;++i) assert(bindingsReloaded.keyCodes[i]==keys[i]);
    }
    std::filesystem::remove(std::filesystem::u8path(keyPath));

    const std::string legacyIni=temporary_path("tetr_legacy_undo_keys.ini");
    {
        std::ofstream out(std::filesystem::u8path(legacyIni),std::ios::trunc);
        out<<"[CONTROLS]\nReplay = "<<keys[11]<<'\n';
    }
    UserConfig::Settings migrated;
    migrated.keyCodes=playerTwoKeys;
    UserConfig::load(migrated,legacyIni);
    assert(migrated.keyCodes[10]==playerTwoKeys[10] && migrated.keyCodes[11]==keys[11]);
    {
        std::ofstream out(std::filesystem::u8path(legacyIni),std::ios::trunc);
        out<<"[CONTROLS]\nReplay = "<<keys[11]<<"\nUndo = 778\n";
    }
    UserConfig::load(migrated,legacyIni);
    assert(migrated.keyCodes[10]==keys[11] && migrated.keyCodes[11]==778);
    std::filesystem::remove(std::filesystem::u8path(legacyIni));

    const std::string missingPlayerTwoPath=temporary_path("tetr_missing_player_two_keys.txt");
    std::filesystem::remove(std::filesystem::u8path(missingPlayerTwoPath));
    UserConfig::Settings playerTwoBindings;
    UserConfig::load_player_two_keybindings(playerTwoBindings,missingPlayerTwoPath);
    assert(playerTwoBindings.keybindingPath==missingPlayerTwoPath);
    assert(playerTwoBindings.keyCodes==UserConfig::default_player_two_key_codes());

    loaded.persistencePath=temporary_path("tetr_saved_settings_test.ini");
    loaded.keyCodes=UserConfig::default_player_two_key_codes();
    loaded.turnPieces=0.000001;
    loaded.serverIp="127.0.0.2"; loaded.username="测试用户";
    loaded.uiLanguage=1;
    loaded.continueInputUnfocused=1;
    loaded.hitMultiplier=.123456789; loaded.cancelMultiplier=1.75;
    assert(UserConfig::save(loaded));
    UserConfig::Settings restored;
    UserConfig::load(restored,loaded.persistencePath);
    assert(restored.keyCodes==loaded.keyCodes);
    assert(restored.uiLanguage==1);
    assert(restored.continueInputUnfocused==1);
    assert(restored.hitMultiplier==loaded.hitMultiplier && restored.cancelMultiplier==loaded.cancelMultiplier);
    assert(restored.username==loaded.username);
    assert(restored.turnPieces==loaded.turnPieces && restored.serverIp==loaded.serverIp);
    assert(restored.replayPath==loaded.replayPath && restored.fieldWidth==13);
    loaded.gravityInterval=.25; loaded.instantGravity=1; loaded.obscuredColors=1;
    UserConfig::reset_section(loaded,UserConfig::Section::Game,true);
    assert(loaded.fieldWidth==10 && loaded.turnPieces==0 && loaded.serverIp=="127.0.0.2");
    assert(loaded.gravityInterval==.6 && loaded.instantGravity==0 && loaded.obscuredColors==1);
    UserConfig::reset_settings(loaded,true);
    assert(loaded.gameMode==0 && loaded.serverIp=="127.0.0.2" && loaded.username=="测试用户" && loaded.uiLanguage==1);
    assert(loaded.keyCodes==UserConfig::default_player_two_key_codes());
    assert(loaded.obscuredColors==1);
    assert(UserConfig::save(loaded));
    UserConfig::load(restored,loaded.persistencePath);
    assert(restored.replayPath.empty() && restored.fieldWidth==10);
    assert(restored.obscuredColors==1);
    std::filesystem::remove(std::filesystem::u8path(loaded.persistencePath));

    for (int color:{0,1}) for (bool playerTwo:{false,true}) {
        UserConfig::Settings profile;
        profile.obscuredColors=color;
        profile.gravityInterval=.25; profile.instantGravity=1; profile.das=.3;
        UserConfig::reset_section(profile,UserConfig::Section::Handling,playerTwo);
        assert(profile.gravityInterval==.25 && profile.instantGravity==1 && profile.das==.1);
        profile.squareStyle=0;
        UserConfig::reset_section(profile,UserConfig::Section::Others,playerTwo);
        assert(profile.obscuredColors==0 && profile.squareStyle==0);
        UserConfig::reset_settings(profile,playerTwo);
        assert(profile.obscuredColors==0 && profile.gravityInterval==.6 && profile.instantGravity==0);
    }

    for(bool playerTwo:{false,true}) {
        UserConfig::Settings profile;profile.das=.23;
        UserConfig::Presets::SavedPreset local{0,"Local match",{{UserConfig::SettingId::GameMode,6},{UserConfig::SettingId::Das,.4}}};
        assert(UserConfig::Presets::apply(profile,local));
        assert(std::get<int>(profile.presets.base.at(UserConfig::SettingId::GameMode))==0);
        profile.fieldWidth=17;
        UserConfig::reset_settings(profile,playerTwo);
        assert(profile.gameMode==6 && profile.fieldWidth==10 && profile.das==.23);
        assert(profile.presets.layers.empty() && profile.presets.base.empty());
    }

    const std::string cheesePath=temporary_path("tetr_cheese_probabilities.ini");
    for (int pattern:{1,2,3}) {
        std::ofstream(std::filesystem::u8path(cheesePath))<<"[GARBAGE]\nCheese Model = "<<pattern<<"\nCheese Messiness = 0.13\n";
        UserConfig::Settings legacy;
        UserConfig::load(legacy,cheesePath);
        assert(legacy.cheeseMessiness==.13 && legacy.cheeseDensity==.13);
        legacy.persistencePath=cheesePath;
        legacy.cheeseDensity=.67;
        assert(UserConfig::save(legacy));
        UserConfig::Settings split;
        UserConfig::load(split,cheesePath);
        assert(std::abs(split.cheeseMessiness-.13)<1e-12 && std::abs(split.cheeseDensity-.67)<1e-12);
    }
    for (bool densityFirst:{false,true}) {
        std::ofstream(std::filesystem::u8path(cheesePath))<<"[GARBAGE]\n"<<(densityFirst
            ? "Cheese Density = 0.67\nCheese Messiness = 0.13\n"
            : "Cheese Messiness = 0.13\nCheese Density = 0.67\n");
        UserConfig::Settings split;
        UserConfig::load(split,cheesePath);
        assert(std::abs(split.cheeseMessiness-.13)<1e-12 && std::abs(split.cheeseDensity-.67)<1e-12);
        UserConfig::reset_section(split,UserConfig::Section::Garbage);
        assert(split.cheeseMessiness==.8 && split.cheeseDensity==.8);
    }
    std::filesystem::remove(std::filesystem::u8path(cheesePath));

    const std::string multiplierPath=temporary_path("tetr_garbage_multipliers.ini");
    std::ofstream(std::filesystem::u8path(multiplierPath))<<"[GARBAGE]\nGarbage Multiple = 0.25\n[GAME]\nGarbage Multiplier = 1.75\n";
    UserConfig::Settings legacyMultipliers;
    UserConfig::load(legacyMultipliers,multiplierPath);
    assert(legacyMultipliers.hitMultiplier==.25 && legacyMultipliers.cancelMultiplier==1.75);
    for (const bool canonicalFirst:{false,true}) {
        const std::string old="[GARBAGE]\nGarbage Multiple = 0.25\n[GAME]\nGarbage Multiplier = 1.75\n";
        const std::string canonical="[GARBAGE]\nHit Multiplier = 0.125\n[GAME]\nCancellation Multiplier = 0.5\n";
        std::ofstream(std::filesystem::u8path(multiplierPath))<<(canonicalFirst ? canonical+old : old+canonical);
        UserConfig::Settings imported;
        UserConfig::load(imported,multiplierPath);
        assert(imported.hitMultiplier==.125 && imported.cancelMultiplier==.5);
        imported.persistencePath=multiplierPath;
        assert(UserConfig::save(imported));
        std::ifstream saved(std::filesystem::u8path(multiplierPath));
        const std::string text((std::istreambuf_iterator<char>(saved)),std::istreambuf_iterator<char>());
        assert(text.find("Garbage Multiple =")==std::string::npos && text.find("Garbage Multiplier =")==std::string::npos);
        assert(text.find("Hit Multiplier = 0.125")!=std::string::npos);
        assert(text.find("Cancellation Multiplier = 0.5")!=std::string::npos);
    }
    std::filesystem::remove(std::filesystem::u8path(multiplierPath));

    // Each key has one owner, while a control can keep several independent keys.
    UserConfig::Settings multiple;
    multiple.keyCodes=keys;
    UserConfig::toggle_control_key(multiple,0,777); // Add an unused key.
    assert(UserConfig::control_keys(multiple,0)==std::vector<int>({keys[0],777}));
    UserConfig::toggle_control_key(multiple,1,keys[0]); // Transfer an existing key.
    assert(UserConfig::control_keys(multiple,0)==std::vector<int>({777}));
    assert(UserConfig::control_keys(multiple,1)==std::vector<int>({keys[1],keys[0]}));
    UserConfig::toggle_control_key(multiple,0,777); // Remove the last binding.
    assert(UserConfig::control_keys(multiple,0).empty() && multiple.keyCodes[0]==-1);
    UserConfig::toggle_control_key(multiple,1,keys[1]); // Promote the remaining key.
    assert(UserConfig::control_keys(multiple,1)==std::vector<int>({keys[0]}));
    UserConfig::toggle_control_key(multiple,1,778);
    multiple.keybindingPath=keyPath;
    multiple.persistencePath=temporary_path("tetr_multiple_keys.ini");
    assert(UserConfig::save_keybindings(multiple) && UserConfig::save(multiple));
    UserConfig::Settings keyFile,iniFile;
    UserConfig::load_keybindings(keyFile,keyPath);
    UserConfig::load(iniFile,multiple.persistencePath);
    for (std::size_t i=0;i<keys.size();++i) {
        assert(UserConfig::control_keys(keyFile,i)==UserConfig::control_keys(multiple,i));
        assert(UserConfig::control_keys(iniFile,i)==UserConfig::control_keys(multiple,i));
    }
    iniFile.uiLanguage=1; iniFile.squareStyle=0; iniFile.obscuredColors=1;
    UserConfig::reset_settings(iniFile,true);
    assert(iniFile.uiLanguage==1 && iniFile.squareStyle==0 && iniFile.obscuredColors==1);
    for (std::size_t i=0;i<keys.size();++i)
        assert(UserConfig::control_keys(iniFile,i)==UserConfig::control_keys(multiple,i));
    UserConfig::reset_section(iniFile,UserConfig::Section::Others,true);
    assert(iniFile.uiLanguage==0 && iniFile.squareStyle==0 && iniFile.obscuredColors==0);
    UserConfig::reset_section(iniFile,UserConfig::Section::Controls,true);
    assert(iniFile.keyCodes==playerTwoKeys);
    for (const auto& extra:iniFile.additionalKeyCodes) assert(extra.empty());
    assert(!UserConfig::parse_control_keys("12, invalid"));
    assert(UserConfig::parse_control_keys(" none\r\n")->empty());
    const int escape=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x1b : 1;
    const int enter=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x0d : 28;
    const auto previous=UserConfig::control_keys(multiple,1);
    UserConfig::toggle_control_key(multiple,1,escape);
    UserConfig::toggle_control_key(multiple,1,enter);
    auto withMenuKeys=previous;
    withMenuKeys.insert(withMenuKeys.end(),{escape,enter});
    assert(UserConfig::control_keys(multiple,1)==withMenuKeys);
    UserConfig::set_control_keys(multiple,1,{escape,enter,777});
    assert(UserConfig::control_keys(multiple,1)==std::vector<int>({escape,enter,777}));
    for (std::size_t control:{8u,9u}) {
        UserConfig::set_control_keys(multiple,control,{escape,enter,777});
        assert(UserConfig::control_keys(multiple,control)==std::vector<int>({777}));
        UserConfig::toggle_control_key(multiple,control,escape);
        UserConfig::toggle_control_key(multiple,control,enter);
        assert(UserConfig::control_keys(multiple,control)==std::vector<int>({777}));
    }
    {
        std::ofstream out(std::filesystem::u8path(multiple.persistencePath));
        out<<"[CONTROLS]\nMove Piece Left = "<<escape<<", "<<enter<<'\n';
    }
    UserConfig::load(multiple,multiple.persistencePath);
    assert(UserConfig::control_keys(multiple,0)==std::vector<int>({escape,enter}));
    std::filesystem::remove(std::filesystem::u8path(keyPath));
    std::filesystem::remove(std::filesystem::u8path(multiple.persistencePath));

    // UTF-8 paths must round-trip through the native Windows wide filesystem API.
    UserConfig::Settings unicode;
    unicode.persistencePath=temporary_path("tetr_中文配置_test.ini");
    unicode.keybindingPath=temporary_path("tetr_中文键位_test.txt");
    unicode.fieldWidth=13;
    unicode.uiLanguage=1;
    unicode.keyCodes=playerTwoKeys;
    assert(UserConfig::save(unicode));
    assert(UserConfig::save_keybindings(unicode));
    UserConfig::Settings unicodeRestored;
    UserConfig::load(unicodeRestored,unicode.persistencePath);
    assert(unicodeRestored.fieldWidth==13 && unicodeRestored.uiLanguage==1);
    UserConfig::load_keybindings(unicodeRestored,unicode.keybindingPath);
    assert(unicodeRestored.keyCodes==playerTwoKeys);
    std::filesystem::remove(std::filesystem::u8path(unicode.persistencePath));
    std::filesystem::remove(std::filesystem::u8path(unicode.keybindingPath));

    std::cout<<"user_config_test: OK\n";
    return 0;
}
} // namespace TetrTests::user_config_test
