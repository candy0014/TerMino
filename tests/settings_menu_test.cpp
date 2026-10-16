#include "tetr/app/application_state.h"
#include "tetr/app/settings_menu.h"
#include "tetr/config/multiplayer_config.h"
#include "tetr/app/start_menu.h"
#include "tetr/network/network_services.h"
#include "tetr/config/user_config.h"
#include "tetr/input/services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/config/normalization.h"
#include "tetr/config/presets.h"
#include "tetr/config/map_store.h"
#include "tetr/replay/recent.h"
#include "../src/app/settings_model.h"
#include "../src/app/settings_input.h"
#include "../src/app/settings_view.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/settings_preset_editor.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
constexpr int up=VK_UP, down=VK_DOWN, enter=VK_RETURN, left=VK_LEFT, right=VK_RIGHT;
constexpr int escape=VK_ESCAPE, backspace=VK_BACK, pageUp=VK_PRIOR, pageDown=VK_NEXT;
constexpr int retry='R', settings='P', secondRetry=VK_F8, customSettings=VK_F9, tab=VK_TAB;
#else
constexpr int up=KEY_UP, down=KEY_DOWN, enter=KEY_ENTER, left=KEY_LEFT, right=KEY_RIGHT;
constexpr int escape=KEY_ESC, backspace=KEY_BACKSPACE, pageUp=KEY_PAGEUP, pageDown=KEY_PAGEDOWN;
constexpr int retry=KEY_R, settings=KEY_P, secondRetry=KEY_F8, customSettings=KEY_F9, tab=KEY_TAB;
#endif
struct Player {
    // This test constructs many players in one function. Keep their large
    // board/history buffers off the Windows stack, even with MSVC inlining.
    std::unique_ptr<Runtime::SessionState> ownedSession = std::make_unique<Runtime::SessionState>();
    Runtime::SessionState& session = *ownedSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Input::Services input;
    UserConfig::Settings user;
    Player(App::ApplicationState& app) {
        context.attach_app(app);
        context.attach_input(input);
        context.attach_user_settings(user);
        // Frozen game time must not freeze menu input.
        context.config.simulationPps=2.5;
        user.uiLanguage=App::UiText::language()==App::UiText::Language::English ? 1 : 0;
        user.keyCodes=UserConfig::default_key_codes();
        user.keyCodes[8]=retry;
        user.keyCodes[9]=settings;
        Input::refresh_bindings(input,user.keyCodes);
    }
};
struct Press {
    int key;
    std::string expected;
    std::chrono::milliseconds minimumHold{5};
    bool mustBeRead = true;
    std::vector<int> modifiers;
};
std::string compact_screen(const std::string& output) {
    Test::TerminalScreen screen;
    screen.feed(output);
    auto text=screen.text();
    text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char ch) {
        return ch==' ' || ch=='\n' || ch=='\r' || ch=='\t';
    }),text.end());
    return text;
}

// Each press lasts until the requested UI change is observed, with a release
// between presses. The timeout is a failure guard, not the navigation driver.
struct Script {
    using Clock=std::chrono::steady_clock;
    std::vector<Press> presses;
    std::ostringstream& output;
    std::size_t index=0, offset=0;
    std::size_t initialOutput=output.str().size();
    bool releasing=true, sampled=false, releaseFinal=false, releasedPrevious=false;
    Clock::time_point phaseStart=Clock::now();
    mutable Test::TerminalScreen screen;
    mutable std::size_t processedOutput=0;
    std::string* history=nullptr;
    std::string visible_output() const {
        const auto text=output.str();
        screen.feed(text.substr(processedOutput));
        const bool changed=text.size()!=processedOutput;
        processedOutput=text.size();
        const auto visible=screen.text();
        if (changed && history) *history+=visible+'\n';
        return visible;
    }
    bool read(int key) {
        const auto elapsed=Clock::now()-phaseStart;
        if (elapsed>std::chrono::seconds(2)) {
            throw std::runtime_error("menu did not handle step "+std::to_string(index)+
                ": "+presses.at(index).expected+"; previous key="+std::to_string(index ? presses[index-1].key : 0)+"; output: "+output.str().substr(offset)+"; screen: "+visible_output());
        }
        if (releasing) {
            // The previous key must be sampled as released before starting
            // another press. A timed pulse can disappear between polls when
            // terminal rendering or other parallel tests occupy the CPU.
            if (index>0 && key==presses[index-1].key) releasedPrevious=true;
            // Do not press while NavigationInput is still sampling initially
            // held keys. Wait for this invocation's first rendered screen.
            const bool releaseObserved=index==0 ? output.str().size()>initialOutput :
                !presses[index-1].mustBeRead || releasedPrevious;
            if (releaseObserved && elapsed>std::chrono::milliseconds(index==0 ? 30 : 5)) {
                releasing=false;
                phaseStart=Clock::now();
                offset=output.str().size();
            }
            return false;
        }
        const auto& press=presses.at(index);
        // Reserved or unbound keys may intentionally never be queried.
        if ((sampled || !press.mustBeRead) && elapsed>press.minimumHold && index+1<presses.size() &&
            (press.expected.empty() || (output.str().size()>offset && visible_output().find(press.expected)!=std::string::npos))) {
            ++index;
            releasing=true;
            releasedPrevious=false;
            sampled=false;
            phaseStart=Clock::now();
            return false;
        }
        if (releaseFinal && index+1==presses.size() && sampled && elapsed>press.minimumHold)
            return false;
        if (key==press.key) sampled=true;
        return key==press.key || std::find(press.modifiers.begin(),press.modifiers.end(),key)!=press.modifiers.end();
    }
};
struct Fixture {
    App::ApplicationState app;
    Player first{app}, second{app};
    std::ostringstream output;
    std::string screenHistory;
    std::streambuf* old=std::cout.rdbuf(output.rdbuf());
    Fixture() {
        app.primaryContext=&first.context;
        app.localPlayerTwoContext=&second.context;
        Input::share_key_state(second.input,first.input);
    }
    ~Fixture() { std::cout.rdbuf(old); }
    std::size_t category_index(const std::string& key,int player=0) {
        const bool local=Runtime::selected_game_mode(first.user)==Runtime::GameMode::LocalVersus;
        const auto root=App::Settings::Model::make_tree(player ? second.user : first.user,local,player);
        const auto found=std::find_if(root.children.begin(),root.children.end(),[&](const auto& node){return node.key==key;});
        assert(found!=root.children.end());
        return std::size_t(player)*root.children.size()+std::size_t(found-root.children.begin());
    }
    void select_category(const std::string& key,int player=0) { app.settingsCategoryIndex=category_index(key,player); }
    std::string observed_output() const { return output.str()+screenHistory; }
    bool start(std::vector<Press> presses) {
        Script script{std::move(presses),output};
        script.history=&screenHistory;
        script.releaseFinal=true;
        first.input.keyStateProvider=[&](int key) { return script.read(key); };
        const bool play=App::run_start_menu(first.context);
        first.input.keyStateProvider={};
        assert(script.index+1==script.presses.size() && script.sampled);
        return play;
    }
    App::Settings::MenuResult run(std::vector<Press> presses, bool mapRecovery=false) {
        Script script{std::move(presses),output};
        script.history=&screenHistory;
        first.input.keyStateProvider=[&](int key) { return script.read(key); };
        const auto result=mapRecovery ? App::Settings::run_map_menu(first.context) : App::Settings::run_menu(first.context);
        first.input.keyStateProvider={};
        assert(script.index+1==script.presses.size() && script.sampled);
        return result;
    }
    void choose(App::Settings::Model::SettingItem& item, std::vector<Press> presses) {
        Script script{std::move(presses),output};
        script.history=&screenHistory;
        first.input.keyStateProvider=[&](int key) { return script.read(key); };
        App::Settings::TerminalSettingsView view(first.context,false,0,false);
        App::Settings::choose_value(first.context,view,item,"SETTINGS > MODEL",[] {});
        first.input.keyStateProvider={};
        assert(script.index+1==script.presses.size() && script.sampled);
    }
    void draft(App::Settings::PresetEditorSession& editor,std::vector<Press> presses) {
        Script script{std::move(presses),output};script.history=&screenHistory;
        first.input.keyStateProvider=[&](int key){return script.read(key);};
        App::Settings::run_preset_menu(first.context,editor);
        first.input.keyStateProvider={};
        assert(script.index+1==script.presses.size() && script.sampled);
    }
};
#ifdef __linux__
struct EmptyStdin {
    int saved=dup(STDIN_FILENO), descriptors[2];
    EmptyStdin() {
        assert(pipe(descriptors)==0);
        assert(dup2(descriptors[0],STDIN_FILENO)>=0);
    }
    ~EmptyStdin() {
        dup2(saved,STDIN_FILENO);
        close(saved); close(descriptors[0]); close(descriptors[1]);
        std::cin.clear();
    }
};
#endif
}

namespace {
using Node = App::Settings::Model::SettingNode;
using Part = App::Settings::Model::ValuePart;
using Id = UserConfig::SettingId;
Node& child(Node& node, const std::string& key) {
    for (auto& entry : node.children) if (entry.key == key) return entry;
    throw std::runtime_error("missing tree child: " + key);
}
bool has_child(const Node& node, const std::string& key) {
    for (const auto& entry : node.children) if (entry.key == key) return true;
    return false;
}
void submenu_resets(const Node& node) {
    if(node.key=="presets" || node.key=="maps" || node.key=="challenges") return;
    if (node.kind==App::Settings::Model::NodeKind::Group && node.key!="root")
        assert(has_child(node,"reset"));
    for (const auto& entry:node.children) submenu_resets(entry);
}
bool has_setting(const Node& node, Id id) {
    if (node.item && node.item->id==id) return true;
    for (const auto& entry:node.children) if (has_setting(entry,id)) return true;
    return false;
}
const App::Settings::Model::SettingItem& setting_item(const Node& node, Id id) {
    if (node.item && node.item->id==id) return *node.item;
    for (const auto& entry:node.children) if (has_setting(entry,id)) return setting_item(entry,id);
    throw std::runtime_error("missing setting: "+std::to_string(static_cast<int>(id)));
}
void dependency_checks() {
    using namespace App::Settings::Model;
    UserConfig::Settings user;
    for (int mode:{0,1,2,-2,3,4,5,6}) for (int garbage=0;garbage<=5;++garbage)
        for (int pattern:{1,2,3,-1,-2,-3}) {
            user.gameMode=mode; user.garbageMode=garbage; user.cheesePattern=pattern;
            const auto config=Runtime::normalize_user_config(user,Runtime::selected_game_mode(user));
            auto tree=make_tree(user,mode==6,0);
            const auto kind=config.garbage.mode;
            const bool quickplay=kind==Runtime::GarbageMode::QuickPlay;
            const bool standardMode=mode==0 || mode==1 || mode==2 || mode==-2;
            const auto& sharedGarbage=child(child(tree,"GAME"),"garbage-rules");
            const auto& shape=child(child(child(tree,"GAME"),"garbage-rules"),"cheese");
            assert(shape.title=="Garbage Shape" && has_child(shape,"holes")== (std::abs(pattern)==1));
            assert(!has_setting(sharedGarbage,Id::GarbageMode));
            for (const auto id:{Id::HitMultiplier,Id::CancellationMultiplier,Id::GarbageInTime,
                Id::GarbageEntryMode,Id::GarbageRiseInterval,Id::MessinessWithinAttack,Id::AvoidSameColumn,Id::GarbageCap})
                assert(has_setting(sharedGarbage,id));
            for (const auto id:{Id::LayerHeight,Id::GarbageInterval,Id::SurvivalApm})
                assert(!has_setting(sharedGarbage,id));
            assert(has_setting(sharedGarbage,Id::OpenerPhase));
            assert(has_setting(tree,Id::CheeseMessiness)==(std::abs(pattern)==1));
            assert(has_setting(tree,Id::CheeseDensity)==(std::abs(pattern)==2));
            assert(has_child(child(tree,"MODEL"),"mode-garbage")==standardMode);
            assert(has_setting(tree,Id::LayerHeight)==(standardMode && garbage==3));
            assert(has_setting(tree,Id::GarbageInterval)==(standardMode && garbage==4));
            assert(has_setting(tree,Id::SurvivalApm)==(standardMode && garbage==5));
            assert(has_setting(tree,Id::GarbageInTime) && has_setting(tree,Id::GarbageEntryMode));
            assert(has_setting(tree,Id::GarbageMode)==standardMode);
            assert(has_setting(tree,Id::HitMultiplier) && has_setting(tree,Id::CancellationMultiplier));
            if (standardMode) {
                const auto& garbageMenu=child(child(tree,"MODEL"),"mode-garbage");
                for (const auto id:{Id::HitMultiplier,Id::CancellationMultiplier,Id::GarbageInTime,
                    Id::GarbageEntryMode,Id::MessinessWithinAttack,Id::AvoidSameColumn,Id::GarbageCap}) {
                    if (!has_setting(garbageMenu,id)) continue;
                    std::visit([&](auto reference) {
                        assert(&reference.get()==&std::get<decltype(reference)>(setting_item(sharedGarbage,id).value).get());
                    },setting_item(garbageMenu,id).value);
                }
                for (const auto id:{Id::CheesePattern,Id::HoleCount,Id::CheeseMessiness,Id::CheeseDensity}) {
                    if (!has_setting(shape,id)) continue;
                    std::visit([&](auto reference) {
                        assert(&reference.get()==&std::get<decltype(reference)>(setting_item(garbageMenu,id).value).get());
                    },setting_item(shape,id).value);
                }
                bool hasRefill=false;
                for (const auto& entry:garbageMenu.children)
                    if (entry.item && entry.item->part==Part::CheeseRefill) hasRefill=true;
                assert(hasRefill==(garbage==3));
                const auto& item=setting_item(garbageMenu,Id::GarbageMode);
                assert(std::find(item.choice.values.begin(),item.choice.values.end(),2)==item.choice.values.end());
                assert(setting_item(tree,Id::HitMultiplier).effective==(Runtime::uses_garbage_buffer(kind) || kind==Runtime::GarbageMode::Unclear));
                assert(setting_item(tree,Id::CancellationMultiplier).effective==Runtime::uses_garbage_buffer(kind));
            }
            assert(!has_child(tree,"QUICK PLAY"));
            assert(has_child(child(tree,"MODEL"),"QUICK PLAY")== (mode==3));
            const auto& quickplayMenu=child(child(tree,"GAME"),"QUICK PLAY");
            assert(quickplayMenu.children.size()==14); // All 13 options and reset, in every mode.
            for (const auto& descriptor:UserConfig::setting_schema())
                if (descriptor.section==UserConfig::Section::QuickPlay)
                    assert(has_setting(quickplayMenu,descriptor.id));
            assert(setting_item(tree,Id::QpDefaultFloor).effective==quickplay);
            assert(setting_item(tree,Id::QpExpert).effective==quickplay);
        }
    user={}; user.cheeseMessiness=.13; user.cheeseDensity=.67;
    user.garbageMode=3;
    for (int pattern:{1,2,3,1}) {
        user.cheesePattern=pattern;
        auto tree=make_tree(user,false,0);
        assert(has_setting(tree,Id::CheeseMessiness)==(pattern==1));
        assert(has_setting(tree,Id::CheeseDensity)==(pattern==2));
        assert(user.cheeseMessiness==.13 && user.cheeseDensity==.67);
    }
    auto tree=make_tree(user,false,0);
    reset_subtree(user,child(child(child(child(tree,"GAME"),"garbage-rules"),"cheese"),"reset"),false);
    assert(user.cheeseMessiness==.8 && user.cheeseDensity==.8); // Reset includes the hidden sibling.
    user={};
    for (int mode:{0,3}) for (int enabled:{0,1}) for (int infinite:{0,1}) for (int noHold:{0,1,2}) {
        user.gameMode=mode; user.holdEnabled=enabled; user.infiniteHold=infinite; user.qpNoHold=noHold;
        tree=make_tree(user,false,0);
        assert(has_setting(tree,Id::InfiniteHold)==(enabled!=0));
        assert(has_setting(tree,Id::HoldCount)==(enabled!=0 && !infinite));
    }
    user={}; user.lockDelay=0;
    assert(!has_setting(make_tree(user,false,0),Id::LockResetLimit));
    user.lockDelay=.5;
    assert(has_setting(make_tree(user,false,0),Id::LockResetLimit));
    user.disableSoftDrop=1; user.disableLeft=user.disableRight=1;
    tree=make_tree(user,false,0);
    assert(has_setting(tree,Id::SoftDropFactor) && has_setting(tree,Id::Das) && has_setting(tree,Id::Arr));
    assert(!setting_item(tree,Id::SoftDropFactor).effective && !setting_item(tree,Id::Das).effective &&
        !setting_item(tree,Id::Arr).effective);
    user.disableRight=0;
    assert(has_setting(make_tree(user,false,0),Id::Arr));
    user={};
    for (int bot=0;bot<=9;++bot) {
        user.autoplayMode=bot;
        tree=make_tree(user,false,0);
        assert(has_setting(tree,Id::AutoplayPps)==(bot!=0));
        assert(has_setting(tree,Id::AutoplayWidth)==(bot!=0));
        if (bot) assert(setting_item(tree,Id::AutoplayWidth).effective==(bot==1 || bot>=4));
    }
    user={}; user.qpSnowball=1;
    tree=make_tree(user,false,0);
    assert(has_setting(tree,Id::FieldWidth) && has_setting(tree,Id::FieldHeight));
    assert(!setting_item(tree,Id::FieldWidth).effective && !setting_item(tree,Id::FieldHeight).effective);
    assert(has_setting(tree,Id::FieldInvisible) && has_setting(tree,Id::AntiGravity));
    user.qpSnowball=2;
    tree=make_tree(user,false,0);
    assert(has_setting(tree,Id::Das) && has_setting(tree,Id::Arr));
    assert(!setting_item(tree,Id::Das).effective && !setting_item(tree,Id::Arr).effective);
    user={}; user.garbageMode=3; user.qpDoubleHole=1;
    tree=make_tree(user,false,0);
    assert(has_setting(tree,Id::LayerHeight) && !setting_item(tree,Id::LayerHeight).effective);
    user.qpDoubleHole=0;
    assert(has_setting(make_tree(user,false,0),Id::LayerHeight));
    user={}; user.qpGravity=2; user.qpFool=1; user.qpMessy=2;
    tree=make_tree(user,false,0);
    assert(has_setting(tree,Id::GravityInterval) && has_setting(tree,Id::LockDelay));
    assert(!setting_item(tree,Id::GravityInterval).effective && !setting_item(tree,Id::LockDelay).effective);
    assert(has_setting(tree,Id::RotationSystem) && has_setting(tree,Id::LineClearDelay));
    assert(!setting_item(tree,Id::RotationSystem).effective && !setting_item(tree,Id::LineClearDelay).effective);
    user={}; user.gameMode=3;
    tree=make_tree(user,false,0);
    assert(setting_item(tree,Id::HoldEnabled).effective);
    for (auto id:{Id::FieldWidth,Id::FieldHeight,Id::AntiGravity,Id::NextCount,Id::Ghost,
        Id::RotationSystem,Id::LockDelay,Id::LineClearDelay,Id::GravityInterval}) {
        assert(has_setting(tree,id));
        assert(!setting_item(tree,id).effective);
    }
    assert(has_setting(tree,Id::LockResetLimit) && !setting_item(tree,Id::LockResetLimit).effective);
    // Inactive base settings remain editable and apply when their override ends.
    auto& field=child(child(tree,"GAME"),"playfield");
    assert(assign_scalar(*field.children.front().item,"17"));
    assert(user.fieldWidth==17);
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay).field.width==10);
    user.gameMode=0;
    tree=make_tree(user,false,0);
    assert(setting_item(tree,Id::FieldWidth).effective);
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Zen).field.width==17);
}
App::Settings::Model::SettingItem& projected(Node& node, Part part) {
    for (auto& entry : node.children) {
        if (entry.item && entry.item->part == part) return *entry.item;
    }
    throw std::runtime_error("missing projected control");
}
void model_checks() {
    using namespace App::Settings::Model;
    UserConfig::Settings user;
    auto tree = make_tree(user, false, 0);
    submenu_resets(tree);
    assert(tree.children.front().key == "player");
    assert(child(tree,"player").children.size()==2);
    assert(child(tree,"MODEL").children.size()==4);
    assert(tree.children.size()==12 && !has_child(tree,"QUICK PLAY"));
    assert(!has_child(tree,"copy-other-player"));
    user.gameMode=3;
    tree=make_tree(user,false,0);
    auto& modeQuickplay=child(child(tree,"MODEL"),"QUICK PLAY");
    auto& gameQuickplay=child(child(tree,"GAME"),"QUICK PLAY");
    for (const auto& descriptor:UserConfig::setting_schema()) {
        if (descriptor.section!=UserConfig::Section::QuickPlay) continue;
        const auto& modeItem=setting_item(modeQuickplay,descriptor.id);
        const auto& gameItem=setting_item(gameQuickplay,descriptor.id);
        std::visit([&](auto reference) {
            assert(&reference.get()==&std::get<decltype(reference)>(gameItem.value).get());
        },modeItem.value);
    }
    const auto expertKey="setting-"+std::to_string(static_cast<int>(Id::QpExpert));
    auto& modeExpert=*child(modeQuickplay,expertKey).item;
    auto& gameExpert=*child(gameQuickplay,expertKey).item;
    apply_choice(modeExpert,1);
    assert(user.qpExpert==1 && choice_value(gameExpert)==1);
    apply_choice(gameExpert,2);
    assert(user.qpExpert==2 && choice_value(modeExpert)==2);
    const auto floorKey="setting-"+std::to_string(static_cast<int>(Id::QpDefaultFloor));
    assert(assign_scalar(*child(gameQuickplay,floorKey).item,"7"));
    assert(scalar_value(*child(modeQuickplay,floorKey).item)=="7");
    reset_subtree(user,child(child(tree,"GAME"),"reset"),false);
    assert(user.gameMode==3 && user.qpExpert==0 && user.qpDefaultFloor==1);
    assert(choice_value(modeExpert)==0 && scalar_value(*child(modeQuickplay,floorKey).item)=="1");
    user.gameMode=1; user.racingDistance=99; user.qpExpert=2; user.qpFool=4; user.fieldWidth=17;
    tree=make_tree(user,false,0);
    auto& quickplay=child(child(tree,"GAME"),"QUICK PLAY");
    const auto& quickplayReset=child(quickplay,"reset");
    assert(quickplayReset.kind==NodeKind::ResetSection);
    UserConfig::reset_section(user,quickplayReset.section);
    assert(user.qpExpert==0 && user.qpFool==0 && user.gameMode==0 && user.goal==2 && user.racingDistance==99);
    user.qpExpert=1; user.qpFool=3;
    const auto& modelReset=child(child(tree,"MODEL"),"reset");
    assert(modelReset.kind==NodeKind::ResetSubtree);
    reset_subtree(user,modelReset,false);
    assert(user.gameMode==0 && user.racingDistance==40 && user.qpExpert==0 && user.qpFool==0);
    assert(user.fieldWidth==17);
    user={}; tree=make_tree(user,false,0);
    assert(child(tree,"AUTOPLAY").children.size()==2);
    assert(!has_child(child(tree,"HANDLING"),"gravity"));
    assert(has_child(child(tree,"GAME"),"gravity"));
    assert(child(tree,"HANDLING").children.size()==7); // SDF, DAS, ARR, DCD, two switches, reset.
    assert(has_setting(child(tree,"HANDLING"),Id::PreMove) && has_setting(child(tree,"HANDLING"),Id::DasPrecharge));
    assert(has_child(child(child(tree,"GAME"),"garbage-rules"),"cheese"));
    user.rotationSystem=6;
    tree=make_tree(user,false,0);
    assert(setting_item(child(child(tree,"GAME"),"QUICK PLAY"),Id::QpDefaultFloor).effective);
    user.rotationSystem=0;
    user.gameMode=6;
    tree=make_tree(user,true,1);
    assert(tree.children.size()==13 && tree.children[tree.children.size()-2].key=="copy-other-player");
    assert(tree.children.back().key=="reset");
    assert(child(tree,"copy-other-player").kind==NodeKind::CopyOtherPlayer);
    assert(child(tree,"player").children.size()==4);
    assert(projected(child(child(tree,"GAME"),"timing"),Part::TurnMode).part==Part::TurnMode);
    user.gameMode=0; // Cheese settings are inactive in forced multiplayer garbage.
    user.garbageMode=3; user.cheesePattern=-1; user.holeCount=3;
    tree=make_tree(user,true,1);
    auto& pattern=child(child(child(tree,"GAME"),"garbage-rules"),"cheese");
    auto& style=projected(pattern,Part::CheeseStyle);
    assert(choice_value(style)==1);
    apply_choice(style,2);
    assert(user.cheesePattern==-2); // Pattern changes preserve refill policy.
    tree=make_tree(user,true,1);
    auto& refill=projected(child(child(tree,"MODEL"),"mode-garbage"),Part::CheeseRefill);
    assert(choice_value(refill)==0);
    apply_choice(refill,1);
    assert(user.cheesePattern==2); // Refill changes preserve the pattern.
    assert(!has_child(child(child(child(tree,"GAME"),"garbage-rules"),"cheese"),"holes"));
    user.cheesePattern=1;
    tree=make_tree(user,true,1);
    auto& holeMenu=child(child(child(child(tree,"GAME"),"garbage-rules"),"cheese"),"holes");
    auto& layout=projected(holeMenu,Part::HoleLayout);
    auto& count=projected(holeMenu,Part::HoleMagnitude);
    apply_choice(layout,1); assert(user.holeCount==-3);
    assert(assign_scalar(count,"2") && user.holeCount==-2);
    assert(!assign_scalar(count,"-3") && user.holeCount==-2);
    assert(!assign_scalar(count,"11") && user.holeCount==-2);
    apply_choice(layout,0); assert(user.holeCount==2);
    reset_subtree(user,child(holeMenu,"reset"),false);
    assert(user.holeCount==1 && user.cheesePattern==1);
    user.garbageMode=4; user.cheesePattern=3;
    tree=make_tree(user,true,1);
    auto& timedCheese=child(child(child(tree,"GAME"),"garbage-rules"),"cheese");
    for (const auto& node:timedCheese.children)
        assert(!node.item || node.item->part!=Part::CheeseRefill);
    assert(timedCheese.children.size()==2); // Pattern and reset; timing belongs to the selected mode.
    for (const auto& node:timedCheese.children) {
        assert(node.children.empty()); // No extra Pattern/Generation directories.
        assert(!node.item || node.item->id!=Id::CheeseMessiness); // Checkerboard is not random.
    }
    user.gravityInterval=-.25;
    tree=make_tree(user,true,1);
    auto& gravity=child(child(tree,"GAME"),"gravity");
    auto& mode=projected(gravity,Part::GravityMode);
    assert(!mode.choice.cycleImmediately);
    assert(choice_value(mode)==1 && gravity.children.size()==2);
    apply_choice(mode,2); assert(user.instantGravity==1);
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Zen).handling.gravityInterval.is_instant());
    apply_choice(mode,0); assert(user.instantGravity==0 && user.gravityInterval==.25);
    tree=make_tree(user,true,1);
    auto& period=projected(child(child(tree,"GAME"),"gravity"),Part::GravitySeconds);
    assert(!assign_scalar(period,"0"));
    assert(assign_scalar(period,"0.1") && user.gravityInterval==.1);
    user.gameMode=-2;
    tree=make_tree(user,true,1);
    auto& model=child(tree,"MODEL");
    auto& gameMode=projected(model,Part::GameMode);
    assert(!gameMode.choice.cycleImmediately);
    assert(choice_value(gameMode)==0 && user.goal==1);
    apply_choice(gameMode,0); assert(user.gameMode==0);
    auto& gravityChoice=projected(child(child(tree,"GAME"),"gravity"),Part::GravityMode);
    apply_choice(gravityChoice,5);
    assert(user.gravityProgression==3);
    auto& simulation=child(child(tree,"GAME"),"timing");
    assert(simulation.children.size()==8);
    apply_choice(projected(simulation,Part::SimulationMode),1);
    assert(user.simulationPps==2.5);
    tree=make_tree(user,false,0);
    assert(child(child(tree,"GAME"),"timing").children.size()==8);
    // Signed file values still round-trip through the unchanged config schema.
    const auto file=std::filesystem::temp_directory_path()/"tetr_tree_values.ini";
    user.persistencePath=file.string(); user.cheesePattern=-3; user.holeCount=-2;
    assert(UserConfig::save(user));
    UserConfig::Settings loaded; UserConfig::load(loaded,file.string());
    assert(loaded.cheesePattern==-3 && loaded.holeCount==-2 && loaded.gameMode==0 && loaded.goal==1);
    std::filesystem::remove(file);

    user.gameMode=0;
    user.gravityInterval=-.25; user.instantGravity=1; user.arr=.123;
    tree=make_tree(user,false,0);
    reset_subtree(user,child(child(child(tree,"GAME"),"gravity"),"reset"),false);
    assert(user.gravityInterval==UserConfig::Settings{}.gravityInterval && user.instantGravity==0);
    assert(user.arr==.123); // Nested reset leaves sibling settings alone.
    user.fieldWidth=17; user.nextCount=8; user.turnPieces=3.5;
    tree=make_tree(user,false,0);
    reset_subtree(user,child(child(child(tree,"GAME"),"playfield"),"reset"),false);
    assert(user.fieldWidth==10 && user.nextCount==8 && user.turnPieces==3.5);
    user.cheesePattern=-3; user.cheeseMessiness=.13; user.garbageInterval=7; user.holeCount=-3; user.layerHeight=9;
    user.garbageMode=3;
    user.cancelMultiplier=1.75; user.hitMultiplier=.25; user.garbageCap=3;
    tree=make_tree(user,false,0);
    reset_subtree(user,child(child(child(child(tree,"GAME"),"garbage-rules"),"cheese"),"reset"),false);
    assert(user.cheeseMessiness==UserConfig::Settings{}.cheeseMessiness);
    assert(user.garbageInterval==7); // Shape reset preserves generation timing.
    assert(user.holeCount==1); // Parent reset also includes hidden hole settings.
    assert(user.cheesePattern==-1 && user.layerHeight==9); // Preserve the outer refill and height controls.
    assert(user.garbageMode==3); // Hidden fields also reset; the parent category is preserved.
    reset_subtree(user,child(child(child(tree,"GAME"),"garbage-rules"),"reset"),false);
    assert(user.cheesePattern==-1 && user.layerHeight==9);
    assert(user.cancelMultiplier==1 && user.hitMultiplier==1 && user.garbageCap==8);
    reset_subtree(user,child(child(child(tree,"MODEL"),"mode-garbage"),"reset"),false);
    assert(user.cheesePattern==1 && user.layerHeight==6 && user.garbageInterval==3);
    assert(user.cancelMultiplier==1 && user.hitMultiplier==1 && user.garbageCap==8);
    UserConfig::Settings profile;
    UserConfig::Presets::apply(profile,UserConfig::PresetKind::Marathon);
    profile.obscuredColors=1; profile.squareStyle=0; profile.fieldWidth=17; profile.uiLanguage=1;
    UserConfig::set_control_keys(profile,0,{777,778});
    auto profileTree=make_tree(profile,false,0);
    reset_subtree(profile,child(child(profileTree,"player"),"reset"),false);
    assert(profile.obscuredColors==1 && profile.squareStyle==0 && profile.fieldWidth==10 && profile.uiLanguage==1);
    assert(UserConfig::control_keys(profile,0)==std::vector<int>({777,778}));
    assert(profile.presets.layers.empty() && profile.presets.base.empty());
}
void handling_units_and_hold_alias() {
    using namespace App::Settings::Model;
    UserConfig::Settings user;
    auto tree = make_tree(user,false,0);
    for (const auto id : {Id::Das,Id::Arr}) {
        auto item = setting_item(tree,id);
        for (const auto& [input,expected] : std::vector<std::pair<std::string,double>>{
            {"1f",1.0/60}, {"1F",1.0/60}, {"1.5f",0.025}, {"0.5F",1.0/120},
            {".25f",1.0/240}, {"0f",0}, {"0.1",0.1}, {"0.1s",0.1}, {"0.1S",0.1},
            {" 1.5 F ",0.025}, {"+6f",0.1}, {"1.2e2F",2}}) {
            assert(assign_scalar(item,input));
            const double actual = std::get<std::reference_wrapper<double>>(item.value).get();
            assert(std::abs(actual-expected)<1e-12);
            const auto config = Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
            assert(std::abs((id==Id::Das ? config.handling.das : config.handling.arr).seconds()-expected)<1e-12);
        }
        for (const auto* invalid : {"-1f","nan","infF","1ms","1ff","1fs","f","s","","1e300f"}) {
            const double previous = std::get<std::reference_wrapper<double>>(item.value).get();
            assert(!assign_scalar(item,invalid));
            assert(std::get<std::reference_wrapper<double>>(item.value).get()==previous);
        }
    }
    assert(assign_scalar(*child(tree,"HANDLING").children[1].item,"1.5f"));
    assert(assign_scalar(*child(tree,"HANDLING").children[2].item,"0.5F"));
    const auto file = std::filesystem::temp_directory_path()/"tetr_frame_handling.ini";
    user.persistencePath=file.string();
    assert(UserConfig::save(user));
    UserConfig::Settings loaded;
    UserConfig::load(loaded,file.string());
    assert(std::abs(loaded.das-0.025)<1e-12 && std::abs(loaded.arr-1.0/120)<1e-8);
    std::filesystem::remove(file);

    for (int mode : {0,3,6}) {
        user.gameMode=mode; user.holdEnabled=1;
        tree=make_tree(user,mode==6,0);
        auto& disabled=child(child(tree,"GAME"),"disabled-movement");
        auto disableUndo=setting_item(disabled,Id::DisableUndo);
        assert(disableUndo.effective==(mode!=6));
        apply_choice(disableUndo,1);
        assert(user.disableUndo==1 && Runtime::normalize_user_config(user,Runtime::selected_game_mode(user)).disableUndo);
        auto& disableHold=projected(disabled,Part::HoldDisabled);
        auto allowHold=setting_item(child(child(tree,"GAME"),"pieces"),Id::HoldEnabled);
        assert(choice_value(disableHold)==0);
        apply_choice(disableHold,1);
        assert(choice_value(allowHold)==0);
        assert(!Runtime::hold_available(Runtime::normalize_user_config(user,Runtime::selected_game_mode(user)).field));
        apply_choice(allowHold,1);
        assert(choice_value(disableHold)==0);
        apply_choice(disableHold,1);
        reset_subtree(user,child(disabled,"reset"),false);
        assert(user.holdEnabled==1 && choice_value(disableHold)==0);
        assert(user.disableUndo==0 && choice_value(disableUndo)==0);
    }
}
}

void map_menu_interaction() {
    const auto directory=std::filesystem::temp_directory_path()/"termino_map_menu_test";
    const auto file=directory/"maps.txt";
    std::filesystem::remove_all(directory);
    std::string previous;if(const auto* value=std::getenv("TETR_MAP_FILE")) previous=value;
#ifdef _WIN32
    _putenv_s("TETR_MAP_FILE",file.string().c_str());
    constexpr int space=VK_SPACE,optionsKey='P',paintT='7',insertI='I',insertT='T',remove=VK_DELETE,saveMap='S',undoSave=VK_F8,dummy=VK_F10,shift=VK_LSHIFT,ctrl=VK_LCONTROL;
#else
    setenv("TETR_MAP_FILE",file.string().c_str(),1);
    constexpr int space=KEY_SPACE,optionsKey=KEY_P,paintT=KEY_7,insertI=KEY_I,insertT=KEY_T,remove=KEY_DELETE,saveMap=KEY_S,undoSave=KEY_F8,dummy=KEY_F10,shift=KEY_LEFTSHIFT,ctrl=KEY_LEFTCTRL;
#endif
    {
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map",std::chrono::milliseconds(300)},{retry,{}}});
        assert(!std::filesystem::exists(file));
    }
    {
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{right,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {up,"Cell 1,2"},{enter,"FIELD"},{tab,"SEQUENCE"},
            {optionsKey,"EDITOR OPTIONS"},{down,"> Rename ="},{down,"> Width = 10"},{down,"> Height = 20"},{down,"> Generator"},{right,"Generator = Custom sequence"},{optionsKey,"Custom sequence"},
            {insertI,"Insert at 2 / 1"},{tab,"FIELD"},{tab,"SEQUENCE"},
            {left,"Insert at 1 / 1",std::chrono::milliseconds(5),true,{shift}},{insertT,"Insert at 2 / 2"},
            {remove,"Insert at 2 / 1"},{insertI,"Insert at 3 / 2"},
            {right,"CANDIDATES 2 /"},{down,"CANDIDATES 6 /"},{up,"CANDIDATES 2 /"},{left,"CANDIDATES 1 /"},
            {right,"CANDIDATES 2 /"},{enter,"Insert at 4 / 3"},{enter,"Insert at 5 / 4"},
            {optionsKey,"EDITOR OPTIONS"},{down,"> Piece library"},{right,"Select Rotation System"},{down,"> SRS"},{enter,"Piece library = SRS"},{right,"Select Rotation System"},{up,"> SRS+"},{enter,"Piece library = SRS+"},{optionsKey,"CANDIDATES"},
            {escape,"Save before exiting?"},{up,"> Keep editing"},{enter,"Exit cancelled."},
            {escape,"Save before exiting?"},{down,"> Discard and exit"},{enter,"Map editor closed."},{retry,{}}});
        assert(!std::filesystem::exists(file));
    }
    Runtime::MapDefinition first;first.name="地图1";first.goal=Runtime::MapGoal::ClearInitial;UserConfig::Maps::SaveUndo undo;
    assert(UserConfig::Maps::save(first,undo));
    auto same=first;same.id=0;assert(UserConfig::Maps::save(same,undo));
    auto third=first;third.id=0;third.name="地图3";assert(UserConfig::Maps::save(third,undo));
    {
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {space,{}},{optionsKey,"EDITOR OPTIONS"},{optionsKey,"FIELD"},
            {escape,"Map editor closed."},{retry,{}}});
        assert(f.observed_output().find("Save before exiting?")==std::string::npos);
        UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection) && collection.maps.size()==3);
    }
    {
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {paintT,"Brush: T"},{escape,"Save before exiting?"},{enter,"Map editor closed."},{retry,{}}});
        UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection) && collection.maps.size()==4);
        assert(collection.maps.back().name=="地图2" && collection.maps.back().rows.back()[0]=='T');
        assert(UserConfig::Maps::remove(collection.maps.back().id));
    }
    {
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {paintT,"Brush: T"},{optionsKey,"EDITOR OPTIONS"},{down,"> Rename ="},{down,"> Width"},{down,"> Height"},
            {down,"> Generator"},{down,"> Rotation System"},{down,"> Map mode"},{down,"> Exit editor"},
            {enter,"Save before exiting?",std::chrono::milliseconds(300)},
            {down,"> Discard and exit"},{down,"> Keep editing"},{enter,"Exit cancelled."},
            {enter,"Save before exiting?"},{down,"> Discard and exit"},{enter,"Map editor closed."},{retry,{}}});
        UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection) && collection.maps.size()==3);
    }
    {
        Fixture f;f.select_category("maps");f.app.settingsMenu.path={"maps","map-"+std::to_string(first.id)};
        f.app.settingsMenu.selectedEntries["root/maps/map-"+std::to_string(first.id)]="map-edit";
        f.run({{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {paintT,"Brush: T"},{escape,"Save before exiting?"},{down,"> Discard and exit"},{enter,"Map editor closed."},{retry,{}}});
        assert(UserConfig::Maps::find(first.id)==std::optional<Runtime::MapDefinition>{first});
    }
    {
        auto custom=first;custom.id=0;custom.name="旋转选择";custom.generator=Runtime::MapGenerator::CustomSequence;
        custom.sequence={Piece::types_for_rotation_system(Runtime::RotationSystem::Ars).front()};
        assert(UserConfig::Maps::save(custom,undo));
        Fixture f;f.first.user.mapId=custom.id;
        auto tree=App::Settings::Model::make_tree(f.first.user,false,0);
        auto item=setting_item(tree,Id::RotationSystem);
        assert(item.choice.labels.back()=="Custom" && App::Settings::Model::selected_choice(item)==12);
        f.choose(item,{{pageUp,"> SRS+"},{enter,{}}});
        assert(f.first.user.mapRotationSystem==0);
        f.choose(item,{{pageDown,"> Custom"},{enter,{}}});
        assert(f.first.user.mapRotationSystem==-1 && UserConfig::Maps::remove(custom.id));
    }
    {
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{down,"> Do not use a map"},{down,"> #1: 地图1"},{right,"> Use map"},
            {enter,"Press Enter again to confirm this map",std::chrono::milliseconds(300)},{retry,{}}});
        assert(f.first.user.mapId==0);
    }
    {
        Fixture f;f.select_category("maps");f.app.settingsMenu.path={"maps","map-"+std::to_string(first.id)};
        f.app.settingsMenu.selectedEntries["root/maps/map-"+std::to_string(first.id)]="map-use";
        f.first.user.persistencePath=(directory/"profile.ini").string();
        f.run({{right,"Press Enter again to confirm this map"},{enter,"Map selection saved."},{retry,{}}});
        UserConfig::Settings loaded;UserConfig::load(loaded,f.first.user.persistencePath);
        assert(f.first.user.mapId==first.id && loaded.mapId==first.id);
        assert(f.first.user.mapGoal==1 && loaded.mapGoal==1 && f.first.user.goal==7 && loaded.goal==7 && f.first.user.gameMode==0 && loaded.gameMode==0);
        auto tree=App::Settings::Model::make_tree(f.first.user,false,0);assert(!has_setting(tree,Id::MapGoal));
        assert(setting_item(tree,Id::Goal).choice.labels.back()=="Clear initial cells");
        assert(!has_setting(tree,Id::RacingDistance));
        f.first.user.mapId=0;tree=App::Settings::Model::make_tree(f.first.user,false,0);assert(!has_setting(tree,Id::MapGoal));
    }
    {
        Fixture f;f.select_category("maps");f.first.user.mapId=first.id;f.second.user.mapId=same.id;
        UserConfig::Settings inactive;inactive.mapId=third.id;inactive.mapGoal=2;inactive.mapRotationSystem=3;
        inactive.persistencePath=(directory/"inactive.ini").string();assert(UserConfig::save(inactive));
        f.app.savedPlayerOneSettings=inactive;
        f.app.playerTwoSettingsMenu.path={"maps","map-3"};f.app.playerTwoSettingsMenu.selectedEntries["root/maps"]="map-3";
        f.app.settingsMenu.path={"maps","map-"+std::to_string(first.id)};
        f.app.settingsMenu.selectedEntries["root/maps/map-"+std::to_string(first.id)]="map-delete";
        f.run({{enter,"Press Enter again to delete this map",std::chrono::milliseconds(300)},{retry,{}}});
        assert(UserConfig::Maps::find(first.id) && f.first.user.mapId==first.id);
        f.run({{right,"Press Enter again to delete this map"},{right,"Confirmation cancelled."},
            {enter,"Press Enter again to delete this map"},{enter,"Map deleted."},{retry,{}}});
        assert(UserConfig::Maps::find(1)->name==same.name && UserConfig::Maps::find(2)->name==third.name);
        assert(f.first.user.mapId==0 && f.second.user.mapId==1);
        assert(f.app.savedPlayerOneSettings->mapId==2 && f.app.savedPlayerOneSettings->mapGoal==2 && f.app.savedPlayerOneSettings->mapRotationSystem==3);
        assert(f.app.playerTwoSettingsMenu.path==std::vector<std::string>({"maps","map-2"}));
        UserConfig::load(inactive,(directory/"inactive.ini").string());assert(inactive.mapId==2 && inactive.mapGoal==2);
        UserConfig::load(inactive,(directory/"profile.ini").string());assert(inactive.mapId==0);
        same.id=1;third.id=2;
    }
    {
        Fixture f;f.first.user.mapId=777;
        f.run({{enter,"Press Enter again to confirm this map"},{enter,"Map selection saved."},{retry,{}}},true);
        assert(f.first.user.mapId==0 && f.app.settingsMenu.path==std::vector<std::string>{"maps"});
    }
    {
        Fixture f;auto& user=f.first.user;user.fieldWidth=12;user.fieldHeight=16;user.rotationSystem=5;
        user.randomPieces=2;user.qpFool=3;user.qpSnowball=1;user.qpDefaultFloor=7;
        const auto original=user;user.mapId=same.id;user.mapGoal=1;user.mapRotationSystem=3;
        auto ordinary=Runtime::normalize_user_config(original,Runtime::selected_game_mode(original));
        f.first.context.config=ordinary;Runtime::apply_map_config(f.first.context.config,*UserConfig::Maps::find(same.id));
        assert(f.first.context.config.initialMap && f.first.context.config.field.width!=ordinary.field.width);
        f.run({{enter,"Press Enter again to confirm this map"},{enter,"Map selection saved."},{retry,{}}},true);
        assert(user.mapId==0 && user.mapGoal==0 && user.mapRotationSystem==-1);
        assert(user.fieldWidth==original.fieldWidth && user.fieldHeight==original.fieldHeight && user.rotationSystem==original.rotationSystem && user.randomPieces==original.randomPieces);
        assert(user.qpFool==original.qpFool && user.qpSnowball==original.qpSnowball && user.qpDefaultFloor==original.qpDefaultFloor);
        const auto& restored=f.first.context.config;
        assert(!restored.initialMap && restored.field.width==ordinary.field.width && restored.field.height==ordinary.field.height);
        assert(restored.rotationSystem==ordinary.rotationSystem && restored.randomPieces==ordinary.randomPieces);
        assert(restored.quickPlay.fool==ordinary.quickPlay.fool && restored.quickPlay.snowball==ordinary.quickPlay.snowball && restored.quickPlay.defaultFloor==ordinary.quickPlay.defaultFloor);
        assert(restored.legacyRotationRules==ordinary.legacyRotationRules && restored.legacyFoolPieceSet==ordinary.legacyFoolPieceSet);
    }
#ifdef __linux__
    {
        EmptyStdin stdinGuard;const std::string sizes="12\n6\n";assert(write(stdinGuard.descriptors[1],sizes.data(),sizes.size())==static_cast<ssize_t>(sizes.size()));
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {optionsKey,"EDITOR OPTIONS"},{down,"> Rename ="},{down,"> Width = 10"},{right,"Edit Width"},{dummy,"> Width = 12",std::chrono::milliseconds(5),false},
            {down,"> Height = 20"},{enter,"Edit Height"},{dummy,"> Height = 6",std::chrono::milliseconds(5),false},
            {right,"Edit Height"},{escape,"EDITOR OPTIONS"},{optionsKey,"FIELD 12 x 6"},
            {escape,"Save before exiting?"},{down,"> Discard and exit"},{enter,"Map editor closed."},{retry,{}}});
        assert(f.first.user.fieldWidth==10 && f.first.user.fieldHeight==20);
    }
    {
        EmptyStdin stdinGuard;assert(write(stdinGuard.descriptors[1],"\n",1)==1);
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {paintT,"Brush: T"},{right,"Cell 2,1",std::chrono::milliseconds(5),true,{paintT}},
            {up,"Cell 2,4",std::chrono::milliseconds(260),true,{paintT}},
            {optionsKey,"EDITOR OPTIONS"},{down,"> Rename ="},{down,"> Width = 10"},{down,"> Height = 20"},{down,"> Generator"},{down,"Rotation System = SRS+"},{down,"> Map mode"},
            {right,"Map mode = Clear map"},{right,"Map mode = All clear field"},{left,"Map mode = Clear map"},{optionsKey,"FIELD"},
            {optionsKey,"EDITOR OPTIONS"},{up,"Rotation System = SRS+"},{up,"> Generator"},{up,"> Height = 20"},{up,"> Width = 10"},{up,"> Rename ="},{up,"> Save map"},
            {enter,"Map saved:"},
            {escape,"Map editor closed."},{retry,{}}});
        UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection));
        const auto painted=std::find_if(collection.maps.begin(),collection.maps.end(),[](const auto& map){return map.name=="地图2";});
        assert(painted!=collection.maps.end() && painted->rows[19][0]=='T' && painted->goal==Runtime::MapGoal::ClearInitial);
        for(int row=16;row<20;++row) assert(painted->rows[row][1]=='T');
        assert(painted->rows[15][1]==' ' && UserConfig::Maps::remove(painted->id));
    }
    {
        // Naming uses real text input, while the editor and menu use physical
        // key-state transitions. Leaving after undo must not save the draft.
        EmptyStdin stdinGuard;assert(write(stdinGuard.descriptors[1],"\n",1)==1);
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {paintT,"Brush: T"},{right,"Cell 2,1",std::chrono::milliseconds(5),true,{paintT}},
            {up,"Cell 2,2",std::chrono::milliseconds(5),true,{paintT}},
            {saveMap,"Map saved:"},
            {undoSave,"Last map save undone"},{escape,"Save before exiting?"},{down,"> Discard and exit"},
            {enter,"Map editor closed."},{retry,{}}});
        UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection));
        assert(collection.maps.size()==2 && UserConfig::Maps::default_name(collection)=="地图2");
    }
    for(const int saveBinding:{undoSave,escape,enter,shift}) {
        Fixture f;f.select_category("maps");f.first.user.keyCodes[10]=saveBinding;
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes);
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {saveMap,"Map saved:"},{undoSave,"Last map save undone"},
            {escape,"Map editor closed."},{retry,{}}});
        UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection));assert(collection.maps.size()==2);
    }
    {
        EmptyStdin stdinGuard;const std::string input="PumpkinS\n9\n";
        assert(write(stdinGuard.descriptors[1],input.data(),input.size())==static_cast<ssize_t>(input.size()));
        Fixture f;f.select_category("maps");
        f.run({{right,"> New map"},{enter,"Press Enter again to confirm this map"},{enter,"MAP EDITOR"},
            {saveMap,"Map saved: 地图2"},{optionsKey,"EDITOR OPTIONS"},{down,"> Rename ="},
            {right,"Edit Rename"},{dummy,"Rename = PumpkinS",std::chrono::milliseconds(5),false},{saveMap,"Map saved: PumpkinS"},
            {down,"> Width = 10"},{down,"> Height = 20"},{down,"> Generator"},{down,"Rotation System = SRS+"},
            {right,"Select Rotation System"},{saveMap,"Map saved: PumpkinS"},{pageDown,"> 20260401"},{enter,"Rotation System = 20260401"},
            {down,"Initial floor = 1"},{right,"Edit Initial floor"},{dummy,"Initial floor = 9",std::chrono::milliseconds(5),false},
            {saveMap,"Map saved: PumpkinS"},{optionsKey,"FIELD"},{tab,"SEQUENCE"},{saveMap,"Map saved: PumpkinS"},
            {escape,"Map editor closed."},{retry,{}}});
        UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection) && collection.maps.size()==3);
        const auto& renamed=collection.maps.back();assert(renamed.name=="PumpkinS" && renamed.rotationSystem==Runtime::RotationSystem::AprilFools2026 && renamed.foolFloor==9);
        assert(UserConfig::Maps::remove(renamed.id));
    }

#endif
#ifdef _WIN32
    _putenv_s("TETR_MAP_FILE",previous.c_str());
#else
    if(previous.empty()) unsetenv("TETR_MAP_FILE");else setenv("TETR_MAP_FILE",previous.c_str(),1);
#endif
    std::filesystem::remove_all(directory);
}

int main() try {
    App::UiText::set_language(App::UiText::Language::English);
    for(bool hidden:{false,true}) {
        Fixture f;f.first.context.config.display.obscuredColors=hidden;
        Node menu{"test","Challenges"};
        Node unmet{"unmet","Unqualified"};unmet.challengeQualified=false;
        Node met{"met","Qualified"};met.challengeQualified=true;
        menu.children={unmet,met};
        App::Settings::TerminalSettingsView view(f.first.context,false,0,false,{},[]{return Render::TerminalSize{120,40};});
        view.prepare_layout(2,"Challenges");view.print_menu(menu,1,"Challenges");
        Test::TerminalScreen screen;screen.feed(f.output.str());const auto pos=screen.position_of("Unqualified");
        assert(pos.first>0);const Test::TerminalScreen::Color expected=hidden?Test::TerminalScreen::Color{255,255,255}:Test::TerminalScreen::Color{128,128,128};
        assert(screen.at(pos.first,pos.second).foreground==expected);
    }
    {
        const auto original=std::filesystem::current_path(),dir=std::filesystem::temp_directory_path()/("termino_recent_menu_"+std::to_string(Challenges::utc_now_milliseconds()));
        std::filesystem::create_directories(dir);std::filesystem::current_path(dir);
        const auto replay=Replay::save_recent("{{Model,0}}{end_all,0}","Recent game");assert(replay);
        Fixture f;f.first.user.gameMode=5;f.first.user.replayPath="previous.txt";
        f.first.user.persistencePath=(dir/"player.ini").string();UserConfig::save(f.first.user);
        const auto diskBefore=[&]{std::ifstream file(f.first.user.persistencePath);return std::string(std::istreambuf_iterator<char>(file),{});}();
        f.select_category("MODEL");f.app.settingsMenu.path={"MODEL","recent-replays"};
        assert(f.run({{enter,"Press Enter again to play this replay"},{right,"Confirmation cancelled."},{enter,"Press Enter again to play this replay"},{enter,{}}})==App::Settings::MenuResult::StartGame);
        assert(f.app.replayPreview && f.app.replayPreview->path==replay->generic_u8string());
        assert(f.first.user.gameMode==5 && f.first.user.replayPath=="previous.txt");
        const auto diskAfter=[&]{std::ifstream file(f.first.user.persistencePath);return std::string(std::istreambuf_iterator<char>(file),{});}();assert(diskBefore==diskAfter);
        std::filesystem::remove(*replay);
        auto tree=App::Settings::Model::make_tree(f.first.user,false,0);
        assert(child(child(tree,"MODEL"),"recent-replays").children.front().key=="empty");
        std::filesystem::current_path(original);std::filesystem::remove_all(dir);
    }
    {
        Fixture f;f.first.user.fieldWidth=17;
        f.select_category("challenges");f.app.settingsMenu.path={"challenges","challenges/Sprint","sprint-40"};
        f.app.settingsMenu.selectedEntries["root/challenges/challenges/Sprint/sprint-40"]="change";
        f.run({{enter,"Temporary challenge settings updated."},{retry,{}}});
        assert(f.first.user.fieldWidth==10 && f.first.user.goal==2 && f.first.user.racingDistance==40 && f.first.user.challengeAdjustment);
        assert(!f.first.user.challengeAdjustment->explicitlyStarted);
        f.select_category("challenges");f.app.settingsMenu.path={"challenges","challenges/Sprint"};
        f.app.settingsMenu.selectedEntries["root/challenges/challenges/Sprint"]="sprint-20";
        f.run({{enter,"Score records"},{retry,{}}});
        assert(f.first.user.fieldWidth==10 && f.first.user.goal==2 && f.first.user.challengeAdjustment);
        assert(f.first.user.challengeAdjustment->challengeId=="sprint-40");
    }
    {
        Fixture f;f.first.user.fieldWidth=17;f.second.user.fieldHeight=31;
        f.app.localVersusSettingsAvailable=true;
        f.select_category("challenges");
        const std::string group="challenges/Local battle/Bot duel (bot_chatgpt)",id="local-bot-chatgpt-700";
        f.app.settingsMenu.path={"challenges","challenges/Local battle",group,id};
        std::string key="root";for(const auto& part:f.app.settingsMenu.path)key+='/'+part;
        f.app.settingsMenu.selectedEntries[key]="change";
        f.run({{enter,"Temporary challenge settings updated."},{retry,{}}});
        assert(f.first.user.gameMode==6 && f.second.user.gameMode==6 && f.second.user.autoplayMode==9 && f.second.user.autoplayPps==.7);
        f.first.user.arr=.8;f.second.user.das=.9;
        f.select_category("challenges");f.app.settingsMenu.path={"challenges"};
        f.app.settingsMenu.selectedEntries["root/challenges"]="restore";
        f.run({{enter,"Temporary challenge settings updated."},{retry,{}}});
        assert(f.first.user.gameMode==0 && f.first.user.fieldWidth==17 && f.first.user.arr==.01);
        assert(f.second.user.fieldHeight==31 && f.second.user.das==.1 && f.second.user.autoplayMode==0);
    }
    {
        Fixture f;f.first.user.fieldWidth=17;
        f.select_category("challenges");f.app.settingsMenu.path={"challenges","challenges/Sprint","sprint-40"};
        f.app.settingsMenu.selectedEntries["root/challenges/challenges/Sprint/sprint-40"]="start";
        f.run({{enter,"Press Enter again to start this challenge"},{right,"Confirmation cancelled."},
               {enter,"Press Enter again to start this challenge"},{enter,{}}});
        assert(f.first.user.challengeAdjustment && f.first.user.challengeAdjustment->explicitlyStarted);
        assert(f.first.user.challengeAdjustment->challengeId=="sprint-40" && f.first.user.fieldWidth==10);
    }
    {
        Fixture f;const auto dir=std::filesystem::temp_directory_path()/("termino_challenge_menu_"+std::to_string(Challenges::utc_now_milliseconds()));
        std::filesystem::create_directories(dir);f.first.user.persistencePath=(dir/"player.ini").string();
        const auto* challenge=Challenges::find("sprint-40");assert(challenge && challenge->change(f.first.user));
        f.first.user.replayPath="previous.rep";
        Challenges::History history;history.nextRunId=2;const auto replay=dir/"record.rep";std::ofstream(replay)<<"placeholder";
        history.records.push_back({1,Challenges::utc_now_milliseconds(),challenge->id,{40,20},true,replay.u8string()});
        assert(Challenges::save(history,Challenges::history_path(f.first.user)));
        f.select_category("challenges");f.app.settingsMenu.path={"challenges","challenges/Sprint","sprint-40","records"};
        f.run({{enter,"Press Enter again to play this replay",std::chrono::milliseconds(300)},{right,"Confirmation cancelled."},{enter,"Press Enter again to play this replay"},{enter,{}}});
        const auto relative=replay.lexically_relative(std::filesystem::current_path());
        assert(f.first.user.gameMode==5 && f.first.user.replayPath==(relative.empty()?replay:relative).generic_u8string() && f.first.user.challengeReplay && f.first.user.challengeReplay->pending);
        assert(f.first.user.challengeReplay->challengeId=="sprint-40");
        UserConfig::Settings disk;UserConfig::load(disk,f.first.user.persistencePath);assert(disk.gameMode==0 && disk.replayPath.empty() && disk.goal==0);
        f.first.user.challengeReplay->pending=false;f.run({{escape,{}}});
        assert(f.first.user.gameMode==0 && f.first.user.replayPath=="previous.rep" && !f.first.user.challengeReplay && f.first.user.challengeAdjustment);
        std::filesystem::remove(replay);
        auto tree=App::Settings::Model::make_tree(f.first.user,false,0);
        auto& records=child(child(child(child(tree,"challenges"),"challenges/Sprint"),"sprint-40"),"records");
        assert(!records.children.front().enabled && records.children.front().description=="The saved replay file no longer exists.");
        assert(records.children.front().title.find("  Re")==std::string::npos);
        std::filesystem::remove_all(dir);
    }
    {
        using namespace App::Settings::Model;
        UserConfig::Settings timed;timed.gameMode=2;
        auto tree=make_tree(timed,false,0);auto limit=setting_item(tree,Id::BlitzTime);
        assert(assign_scalar(limit,"1:75.5") && timed.blitzTime==135.5);
        assert(scalar_value(limit)=="02:15.500");
        assert(assign_scalar(limit,"75.125") && timed.blitzTime==75.125);
        assert(scalar_value(limit)=="01:15.125");
        assert(!assign_scalar(limit,"1:bad") && timed.blitzTime==75.125);
        auto das=setting_item(tree,Id::Das);
        assert(assign_scalar(das,"00:00.015") && timed.das==.015);
        assert(scalar_value(das)=="0.015");
        assert(assign_scalar(das,"1.5f") && scalar_value(das)=="0.025");
        auto all=make_tree(timed,false,0,true);
        for(const auto id:{Id::Arr,Id::Dcd,Id::ForcedDas,Id::ForcedArr,Id::ForcedDcd,
            Id::LockDelay,Id::LineClearDelay,Id::EntryDelay,Id::GarbageRiseInterval}) {
            auto delay=setting_item(all,id);
            assert(assign_scalar(delay,"0.125s") && scalar_value(delay)=="0.125");
        }
        auto sdf=setting_item(all,Id::SoftDropFactor);
        assert(assign_scalar(sdf,"20") && scalar_value(sdf)=="20" && !timed.softDropFixed);
        assert(assign_scalar(sdf,"3f") && scalar_value(sdf)=="0.05s" && timed.softDropFixed);
    }
    App::UiText::set_language(App::UiText::Language::English);
    for(int player:{-1,0,1,2}) for(bool draft:{false,true}) {
        Fixture f;f.first.user.das=.073;f.second.user.das=.12;
        if(player>=0) f.first.user.gameMode=f.second.user.gameMode=6;
        App::Settings::PresetEditorSession editor;editor.preset.name="Layer removal draft";
        if(draft) for(auto* user:{&f.first.user,&f.second.user}) {
            user->presetDefined.emplace();
            UserConfig::Presets::define(*user,Id::GameMode);UserConfig::Presets::define(*user,Id::Das);
        }
        const UserConfig::Presets::SavedPreset saved{0,"Last applied preset",{{Id::Das,.2}}};
        if(player!=1) assert(UserConfig::Presets::apply(f.first.user,saved,player>=0));
        if(player==1 || player==2) assert(UserConfig::Presets::apply(f.second.user,saved,true));
        f.select_category("presets",player==1 ? 1 : 0);
        auto& position=player==1 ? f.app.playerTwoSettingsMenu : f.app.settingsMenu;
        position.path={"presets","preset-layers","preset-layer-0"};
        position.selectedEntries["root/presets"]="preset-layers";
        std::vector<Press> presses;
        if(player==2) presses={{tab,"Edit target: Player 2"},{tab,"Both players (Player 1 | Player 2)"}};
        presses.push_back({right,"Press Enter again to remove this preset layer"});
        presses.push_back({enter,draft ? "Preset draft updated." : "Preset changes saved."});
        if(draft) {
            presses.push_back({escape,"> Save and exit"});presses.push_back({down,"> Discard and exit"});
            presses.push_back({enter,{}});f.draft(editor,std::move(presses));
        } else {presses.push_back({retry,{}});f.run(std::move(presses));}
        assert(f.first.user.presets.layers.empty() && f.first.user.das==.073);
        assert(f.second.user.presets.layers.empty() && f.second.user.das==.12);
        if(player>=0) assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
        if(player!=2) {
            assert((position.path==std::vector<std::string>{"presets"}));
            assert(position.selectedEntries.at("root/presets")=="preset-layers");
        }
        // Reopening a stale remembered path must also stop in the parent.
        if(!draft && player!=2) {
            position.path={"presets","preset-layers","preset-layer-0"};
            f.run({{right,"No preset layers to change."},{retry,{}}});
            assert((position.path==std::vector<std::string>{"presets"}));
        }
    }
    {
        Fixture f;f.first.user.gameMode=f.second.user.gameMode=6;
        const UserConfig::Presets::SavedPreset saved{0,"Player 1 layer",{{Id::Das,.2}}};
        assert(UserConfig::Presets::apply(f.first.user,saved,true));
        f.select_category("presets");f.app.settingsMenu.path={"presets","preset-layers","preset-layer-0"};
        f.run({{tab,"> Applied presets"},{right,"No preset layers to change."},
            {tab,"Both players (Player 1 | Player 2)"},{tab,"Edit target: Player 1"},{retry,{}}});
        assert((f.app.playerTwoSettingsMenu.path==std::vector<std::string>{"presets"}));
        assert(f.first.user.presets.layers.size()==1 && f.second.user.presets.layers.empty());
    }
    model_checks();
    handling_units_and_hold_alias();
    map_menu_interaction();
    for(int player:{0,1}) {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.app.localVersusSettingsAvailable=true;
        f.second.user.keyCodes=UserConfig::default_player_two_key_codes();
        f.second.user.keyCodes[8]=secondRetry;
        f.first.user.additionalKeyCodes[0]={customSettings};
        f.second.user.additionalKeyCodes[1]={customSettings};
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes,f.first.user.additionalKeyCodes);
        Input::refresh_bindings(f.second.input,f.second.user.keyCodes,f.second.user.additionalKeyCodes);
        const auto directory=std::filesystem::temp_directory_path()/
            (player ? "tetr_copy_player_two_settings" : "tetr_copy_player_one_settings");
        std::filesystem::remove_all(directory);
        f.first.user.persistencePath=(directory/"one.ini").string();
        f.second.user.persistencePath=(directory/"two.ini").string();
        f.first.user.keybindingPath=(directory/"one-keys.txt").string();
        f.second.user.keybindingPath=(directory/"two-keys.txt").string();
        auto& target=player ? f.second.user : f.first.user;
        auto& source=player ? f.first.user : f.second.user;
        target.das=.41;
        assert(UserConfig::Presets::apply(target,UserConfig::PresetKind::Classic,true));
        source.das=.23;
        assert(UserConfig::Presets::apply(source,UserConfig::PresetKind::Delayed,true));
        assert(UserConfig::Presets::apply(source,UserConfig::PresetKind::Marathon,true));
        source.fieldWidth=17;source.fieldHeight=23;source.autoplayMode=9;source.autoplayWidth=77;
        source.autoplayPps=4.75;source.autoplayHandlingSimulation=1;
        source.serverIp="192.0.2.55";source.replayPath="replay/copied.txt";source.replayTimeMultiplier=2.75;
        source.qpBackfire=-1;source.cheesePattern=-2;source.cheeseDensity=.37;
        source.holeCount=-4;source.turnPieces=2.5;source.simulationPps=3.5;source.disableLeft=1;
        f.first.user.squareStyle=f.second.user.squareStyle=1;
        f.first.user.obscuredColors=f.second.user.obscuredColors=1;
        f.first.user.showSpawnPosition=f.second.user.showSpawnPosition=1;
        UserConfig::Presets::record_edits(source);
        const auto expected=source;
        const auto targetKeys=target.keyCodes;
        const auto targetExtra=target.additionalKeyCodes;
        const auto targetProfile=target.persistencePath,targetBindings=target.keybindingPath;
        const auto previousPresets=UserConfig::Presets::encode(target.presets);
        const auto previousWidth=target.fieldWidth;
        f.select_category("copy-other-player",player);
        f.run({{enter,"Press Enter again to copy",std::chrono::milliseconds(300)},
            {retry,{}}});
        assert(target.fieldWidth==previousWidth && UserConfig::Presets::encode(target.presets)==previousPresets);
        assert(!std::filesystem::exists(targetProfile) && !std::filesystem::exists(source.persistencePath));
        f.run({{right,"Press Enter again to copy"},{right,"Confirmation cancelled."},
            {enter,"Press Enter again to copy"},{left,"Confirmation cancelled."},
            {enter,"Press Enter again to copy"},{down,"> RESET"},
            {up,"> Copy other player's settings"},{enter,"Press Enter again to copy"},
            {retry,{}}});
        assert(target.fieldWidth==previousWidth && UserConfig::Presets::encode(target.presets)==previousPresets);
        assert(!std::filesystem::exists(targetProfile) && !std::filesystem::exists(source.persistencePath));
        f.run({{player ? right : enter,"Press Enter again to copy"},
            {enter,"Other player's settings copied. Changes saved."},{retry,{}}});
        UserConfig::Settings saved,otherSaved;
        UserConfig::load(saved,targetProfile);UserConfig::load(otherSaved,source.persistencePath);
        for(const auto& descriptor:UserConfig::setting_schema()) {
            if(descriptor.keyBinding) continue;
            std::visit([&](auto member) {
                if constexpr(!std::is_same_v<decltype(member),std::monostate>) {
                    assert(target.*member==expected.*member && source.*member==expected.*member);
                    if constexpr(std::is_same_v<decltype(member),double UserConfig::Settings::*>) {
                        assert(std::abs(saved.*member-expected.*member)<1e-12);
                        assert(std::abs(otherSaved.*member-expected.*member)<1e-12);
                    } else {
                        assert(saved.*member==expected.*member && otherSaved.*member==expected.*member);
                    }
                }
            },descriptor.member);
        }
        assert(target.keyCodes==targetKeys && saved.keyCodes==targetKeys);
        assert(target.additionalKeyCodes==targetExtra && saved.additionalKeyCodes==targetExtra);
        assert(source.keyCodes==expected.keyCodes && source.additionalKeyCodes==expected.additionalKeyCodes);
        assert(target.persistencePath==targetProfile && target.keybindingPath==targetBindings);
        assert(UserConfig::Presets::encode(target.presets)==UserConfig::Presets::encode(expected.presets));
        assert(saved.presets.layers.size()==expected.presets.layers.size() && saved.presets.nextSerial==expected.presets.nextSerial);
        for(std::size_t i=0;i<saved.presets.layers.size();++i)
            assert(saved.presets.layers[i].kind==expected.presets.layers[i].kind && saved.presets.layers[i].serial==expected.presets.layers[i].serial);
        assert(UserConfig::Presets::clear(saved,true) && saved.das==.23);
        // Undoing a copied preset restores the source's baseline, not the
        // target's old values, and still preserves the target's bindings.
        assert(UserConfig::Presets::clear(target,true) && target.das==.23);
        assert(target.keyCodes==targetKeys && target.additionalKeyCodes==targetExtra);
        assert(UserConfig::Presets::encode(source.presets)==UserConfig::Presets::encode(expected.presets));
        std::filesystem::remove_all(directory);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.fieldWidth=17;f.second.user.fieldWidth=13;
        f.select_category("copy-other-player");
        f.run({{enter,"Press Enter again to copy"},
            {tab,"Edit target: Player 2 / SETTINGS"},{enter,"Press Enter again to copy"},
            {tab,"Both players (Player 1 | Player 2)"},{enter,"Choose Player 1 or Player 2 to edit this item."},
            {tab,"Edit target: Player 1 / SETTINGS"},{enter,"Press Enter again to copy"},{retry,{}}});
        assert(f.first.user.fieldWidth==17 && f.second.user.fieldWidth==13);
    }
    {
        Fixture f;
        f.select_category("HANDLING");
        f.first.user.persistencePath=(std::filesystem::temp_directory_path()/"tetr_handling_switches.ini").string();
        f.run({{right,"> SDF ="},{down,"> DAS ="},{down,"> ARR ="},{down,"> DCD ="},
            {down,"> Pre-move = ON"},{right,"> Pre-move = OFF"},
            {down,"> DAS Precharge = ON"},{right,"> DAS Precharge = OFF"},{retry,{}}});
        assert(f.first.user.preMove==0 && f.first.user.dasPrecharge==0);
        UserConfig::Settings loaded;
        UserConfig::load(loaded,f.first.user.persistencePath);
        assert(loaded.preMove==0 && loaded.dasPrecharge==0);
        assert(f.second.user.preMove==1 && f.second.user.dasPrecharge==1);
        std::filesystem::remove(f.first.user.persistencePath);
    }
    dependency_checks();
    {
        Fixture f;
        f.first.user.garbageMode=1;
        const auto entryKey="setting-"+std::to_string(static_cast<int>(Id::GarbageEntryMode));
        f.select_category("GAME");
        f.app.settingsMenu.path={"GAME","garbage-rules"};
        f.app.settingsMenu.selectedEntries["root/GAME/garbage-rules"]=entryKey;
        f.run({{right,"> Garbage entry mode = After lock"},{retry,{}}});
        assert(f.first.user.garbageEntryMode==1);
        f.select_category("MODEL");
        f.app.settingsMenu.path={"MODEL","mode-garbage"};
        f.app.settingsMenu.selectedEntries["root/MODEL/mode-garbage"]=entryKey;
        f.run({{right,"> Garbage entry mode = Immediate"},{retry,{}}});
        assert(f.first.user.garbageEntryMode==2);
        f.select_category("GAME");
        f.app.settingsMenu.path={"GAME","garbage-rules"};
        const auto reopened=f.output.str().size();
        f.run({{retry,{}}});
        Test::TerminalScreen screen;
        screen.feed(f.output.str().substr(reopened));
        assert(screen.position_of("> Garbage entry mode = Immediate").first!=0);
    }
    {
        Fixture f;
        f.first.user.garbageMode=1;
        f.select_category("MODEL");
        f.app.settingsMenu.path={"MODEL","mode-garbage"};
        f.app.settingsMenu.selectedEntries["root/MODEL/mode-garbage"]="setting-"+
            std::to_string(static_cast<int>(Id::GarbageEntryMode));
        f.run({{right,"> Garbage entry mode = After lock"},{right,"> Garbage entry mode = Immediate"},
            {right,"> Garbage entry mode = After non-clear"},{retry,{}}});
        assert(f.first.user.garbageEntryMode==0);
    }
#ifdef __linux__
    {
        EmptyStdin stdinGuard;
        const std::string value="0.125\n";
        assert(write(stdinGuard.descriptors[1],value.data(),value.size())==static_cast<ssize_t>(value.size()));
        Fixture f;
        f.select_category("GAME");
        f.app.settingsMenu.path={"GAME","garbage-rules"};
        f.app.settingsMenu.selectedEntries["root/GAME/garbage-rules"]="setting-"+
            std::to_string(static_cast<int>(Id::GarbageRiseInterval));
        f.run({{right,"Edit Garbage rise speed (seconds)"},
            {-1,"> Garbage rise speed (seconds) = 0.125",std::chrono::milliseconds(5),false},{retry,{}}});
        assert(f.first.user.garbageRiseInterval==.125);
        assert(f.second.user.garbageRiseInterval==0);
    }
    {
        Fixture f;
        EmptyStdin stdinInput;
        f.first.user.gameMode=5;
        f.select_category("MODEL");
        const std::string path="中文目录/练习 rp.txt";
        const auto text=path+"\n";
        Script script{{{right,"> Model = Replay"},{down,"> Replay ="},{right,"Edit Replay"},
            {settings,"> Replay = .tetr/replay/"+path},{retry,{}}},f.output};
        script.history=&f.screenHistory;
        bool supplied=false;
        f.first.input.keyStateProvider=[&](int key) {
            const bool pressed=script.read(key);
            // Exercise Settings while the scalar editor owns input. Preloading
            // the pipe can finish editing before this press is observed.
            if (key==settings && pressed && script.index==3 && !supplied) {
                supplied=true;
                assert(write(stdinInput.descriptors[1],text.data(),text.size())==static_cast<ssize_t>(text.size()));
            }
            return pressed;
        };
        assert(App::Settings::run_menu(f.first.context)==App::Settings::MenuResult::StartGame);
        assert(supplied && script.index+1==script.presses.size() && script.sampled);
        f.first.input.keyStateProvider={};
        assert(f.first.user.replayPath==".tetr/replay/"+path);
        assert((f.app.settingsMenu.path==std::vector<std::string>{"MODEL"}));
        assert(f.observed_output().find("New value:")==std::string::npos);
        const std::string blank="\n";
        assert(write(stdinInput.descriptors[1],blank.data(),blank.size())==static_cast<ssize_t>(blank.size()));
        f.run({{right,"Edit Replay"},{retry,{}}});
        assert(f.first.user.replayPath==".tetr/replay/"+path);
    }
#endif
    {
        Fixture f;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{down,"> Pieces  >"},{right,"> Num of Next ="},
            {down,"> Open Hold = ON"},{right,"> Open Hold = OFF"},{left,"> Pieces  >"},
            {left,"> GAME"},{retry,{}}});
        const auto clear=f.observed_output().find("\x1b[2J");
        assert(clear!=std::string::npos);
        assert(f.observed_output().find("\x1b[2J",clear+1)==std::string::npos);
    }
    {
        Fixture f;
        auto tree=App::Settings::Model::make_tree(f.first.user,false,0);
        auto item=*child(tree,"MODEL").children[0].item;
        item.choice={};
        for (int i=0;i<32;++i) { item.choice.labels.push_back("Option "+std::to_string(i)); item.choice.values.push_back(i); }
        f.choose(item,{{pageDown,"> Option 15"},{pageDown,"> Option 30"},{down,"> Option 31"},
            {down,"> Option 0"},{up,"> Option 31"},{pageUp,"> Option 16"},{pageUp,"> Option 1"},
            {down,"> Option 2"},{enter,{}}});
        assert(f.first.user.gameMode==2);
        for (const auto& page:{"1-15 / 32","16-30 / 32","31-32 / 32"})
            assert(f.observed_output().find(page)!=std::string::npos);
        f.first.user.gameMode=14;
        f.choose(item,{{down,"> Option 15"},{up,"> Option 14"},{right,{}}});
        assert(f.first.user.gameMode==14); // Automatic boundary paging and both confirmation keys.
    }
    {
        Fixture f;
        f.select_category("GAME");
        f.first.user.gravityInterval=.25;
        f.run({{right,"> Playfield  >"},{down,"> Pieces  >"},{down,"> Timing  >"},
            {down,"> Disabled actions  >"},{down,"> Gravity  >"},{right,"> Gravity mode = Periodic"},
            {enter,"Select Gravity mode"},{down,"> Disabled"},{enter,"> Gravity mode = Disabled"},
            {right,"Select Gravity mode"},{down,"> Instant (20G)"},{enter,"> Gravity mode = Instant (20G)"},
            {enter,"Select Gravity mode"},{up,"> Disabled"},{up,"> Periodic"},{enter,"> Gravity mode = Periodic"},{retry,{}}});
        assert(f.first.user.gravityInterval==.25 && f.first.user.instantGravity==0);
        assert(f.observed_output().find("Select Gravity mode")!=std::string::npos);
    }
    {
        Fixture f;
        f.select_category("HANDLING");
        f.first.user.gravityInterval=.25; f.first.user.instantGravity=1; f.first.user.das=.3;
        f.run({{right,"> SDF ="},{up,"> RESET"},{enter,"Press Enter again to confirm RESET"},
            {enter,"RESET complete."},{retry,{}}});
        assert(f.first.user.gravityInterval==.25 && f.first.user.instantGravity==1);
        assert(f.first.user.das==UserConfig::Settings{}.das);
    }
    {
        Fixture f;
        f.select_category("GAME");
        f.first.user.gravityInterval=.25; f.first.user.instantGravity=1; f.first.user.das=.3;
        f.run({{right,"> Playfield  >"},{up,"> RESET"},{enter,"Press Enter again to confirm RESET"},
            {enter,"RESET complete."},{retry,{}}});
        assert(f.first.user.gravityInterval==UserConfig::Settings{}.gravityInterval && f.first.user.instantGravity==0);
        assert(f.first.user.das==.3);
    }
    {
        Fixture f;
        f.select_category("OTHERS");
        f.first.user.squareStyle=10; // Historical default renders as the spaced style.
        f.run({{right,"> Square Version = Spaced"},{enter,"> Square Version = Filled"},
            {right,"> Square Version = Spaced"},{retry,{}}});
        assert(f.first.user.squareStyle==1);
        assert(f.observed_output().find("Custom")==std::string::npos);
    }
    {
        Fixture f;
        f.first.user.obscuredColors=1; f.first.user.fieldWidth=17; f.first.user.squareStyle=0;
        f.run({{right,"Player 1 [current]"},{up,"> RESET"},{enter,"Press Enter again to confirm RESET"},
            {enter,"> RESET"},{retry,{}}});
        assert(f.first.user.uiLanguage==1 && f.first.user.squareStyle==0);
        assert(f.first.user.obscuredColors==1 && f.first.user.fieldWidth==10);
        assert(f.first.context.config.display.obscuredColors);
        App::UiText::set_language(App::UiText::Language::English);
    }
    {
        Fixture f;
        f.select_category("GAME");
        f.first.user.fieldWidth=17; f.first.user.nextCount=8; f.first.user.turnPieces=3.5;
        f.run({{right,"> Playfield  >"},{right,"> Width = 17"},{up,"> RESET"},
            {enter,"Press Enter again to confirm RESET"},{enter,"RESET complete."},{retry,{}}});
        assert(f.first.user.fieldWidth==10 && f.first.user.nextCount==8 && f.first.user.turnPieces==3.5);
    }
    {
        Fixture f;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{down,"> Pieces  >"},{down,"> Timing  >"},
            {down,"> Disabled actions  >"},{right,"> Move Piece Left = OFF"},
            {enter,"> Move Piece Left = ON"},{down,"> Move Piece Right = OFF"},
            {right,"> Move Piece Right = ON"},{up,"> Move Piece Left = ON"},
            {up,"> RESET"},{enter,"Press Enter again to confirm RESET"},{enter,"RESET complete."},{retry,{}}});
        assert(f.first.user.disableLeft==0 && f.first.user.disableRight==0);
    }
    {
        Fixture f;
        f.select_category("AUTOPLAY");
        f.first.user.disableLeft=1;
        f.run({{right,"> Bot = Disabled"},{right,"Select Bot"},{down,"> bot_while1"},
            {down,"> bot_candy0014"},{down,"> bot_mech"},{enter,"> Bot = bot_mech"},{retry,{}}});
        assert(f.first.user.autoplayMode==3); // Unavailable choices remain selectable.
        assert(f.observed_output().find("Unavailable: Left movement cannot be disabled.")!=std::string::npos);
        assert(f.observed_output().find("\x1b[38;2;128;128;128m")!=std::string::npos);
    }
    {
        Fixture f;
        Network::NetworkServices network;
        f.first.context.attach_network(network);
        f.first.user.serverIp="invalid host";
        f.select_category("NETWORK");
        assert(f.start({{settings,"SETTINGS"},{right,"> Server IP ="},
            {down,"> Username ="},{down,"> CONNECT / RECONNECT"},{right,"invalid server address"},{retry,{}}}));
        assert(f.first.user.gameMode==4);
    }
#ifdef __linux__
    {
        EmptyStdin stdinGuard;Fixture f;f.select_category("NETWORK");
        const std::string address="wss://termino.candy0014.icu/ws";
        const auto typed=address+"\n";
        assert(write(stdinGuard.descriptors[1],typed.data(),typed.size())==static_cast<ssize_t>(typed.size()));
        f.run({{right,"> Server IP ="},{right,"Edit Server IP"},
            {-1,"> Server IP = wss://te...4.icu/ws",std::chrono::milliseconds(5),false},{retry,{}}});
        assert(f.first.user.serverIp==address);
        assert(f.observed_output().find("New value:")==std::string::npos);
    }
    {
        EmptyStdin stdinGuard;Fixture f;f.select_category("NETWORK");
        const std::string address="wss://a-very-long-public-server-address.example/room-gateway/ws";
        f.first.user.serverIp=address;
        const std::string typed="\x1b[F\b9\n";
        assert(write(stdinGuard.descriptors[1],typed.data(),typed.size())==static_cast<ssize_t>(typed.size()));
        f.run({{right,"> Server IP ="},{right,"Edit Server IP"},
            {-1,"> Server IP =",std::chrono::milliseconds(5),false},{retry,{}}});
        assert(f.first.user.serverIp==address.substr(0,address.size()-1)+"9");
        assert(f.observed_output().find(address)!=std::string::npos);
    }
#endif
    {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        f.first.user.serverIp="invalid host";f.select_category("NETWORK");
        f.run({{right,"> Server IP ="},{down,"> Username ="},{down,"> CONNECT / RECONNECT"},
            {enter,"invalid server address"},{down,"> DISCONNECT"},{enter,"> DISCONNECT"},{escape,{}}});
        assert(f.first.user.gameMode==0 && network.connection==Network::ConnectionState::Disconnected);
        assert(f.observed_output().find("> Rooms")==std::string::npos);
        assert(f.observed_output().find("┌ Connection status")==std::string::npos);
    }
#ifdef __linux__
    {
        EmptyStdin stdinGuard; Fixture f; f.select_category("NETWORK");
        const std::string typed = "离线用户名\n";
        assert(write(stdinGuard.descriptors[1],typed.data(),typed.size()) == static_cast<ssize_t>(typed.size()));
        f.run({{right,"> Server IP ="},{down,"> Username ="},{enter,"Edit Username"},
            {-1,"> Username = 离线用户名",std::chrono::milliseconds(5),false},{retry,{}}});
        assert(f.first.user.username == "离线用户名");
    }
#endif
    {
        Fixture f; Network::NetworkServices network; f.first.context.attach_network(network);
        network.connection = Network::ConnectionState::Connected;
        network.room = {"BIG",true,4,8}; network.roomPlayerIndex = 2;
        network.roomMembers = {{0,"Creator",true},{1,"Guest B",false},{2,"Guest C",false},{3,"Guest D",false}};
        f.select_category("NETWORK");
        const auto result = f.run({{right,"> Server IP ="},{down,"> Username ="},{down,"> CONNECT / RECONNECT"},
            {down,"> DISCONNECT"},{down,"> Host configuration"},{down,"> Room player limit"},{down,"> Battle player limit"},
            {down,"> Player status"},{down,"> Room users"},{enter,"Guest C"},{left,"> Room users"},{escape,{}}});
        assert(result == App::Settings::MenuResult::StartScreen);
        const auto output = f.observed_output();
        for (const auto* label : {"Creator","Guest B","Guest C","Guest D","HOST","4/8"}) assert(output.find(label) != std::string::npos);
        assert(output.find("Transfer host")==std::string::npos && output.find("Force host configuration")==std::string::npos);
        assert(output.find("Host configuration")!=std::string::npos && output.find("Host handling")==std::string::npos);
        assert(output.find("> Prepare match")==std::string::npos);
    }
    {
        Fixture f; Network::NetworkServices network; f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected; network.room={"BIG",true,4,8}; network.roomPlayerIndex=0;
        network.roomPolicyAvailable=true;
        network.roomMembers={{0,"Creator",true},{1,"Guest B",false},{2,"Guest C",false},{3,"Guest D",false}};
        f.select_category("NETWORK");f.app.settingsMenu.path={"NETWORK"};f.app.settingsMenu.selectedEntries["root/NETWORK"]="room-users";
        f.run({{enter,"> Creator"},{down,"> Guest B"},{down,"> Guest C"},{enter,"> Battle"},
            {down,"> Spectator"},{down,"> Offline"},{down,"> Transfer host"},{enter,"Waiting for the server..."},
            {left,"> Room users"},{escape,{}}});
        assert(network.pendingRoomRequest && network.pendingRoomRequest->operation==Network::RoomOperation::TransferHost && network.pendingRoomRequest->targetSlot==2);
        assert(network.pendingRoomRequest->name=="Guest C");
        assert(f.observed_output().find("Host only")==std::string::npos);
    }
    {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected;network.room={"KICK",true,3,32};network.roomPlayerIndex=0;
        network.multiplayerRoom=true;network.roomMembers={{0,"Host",true},{1,"Guest",false},{2,"Offline guest",false,Network::PlayerRole::Offline}};
        f.select_category("NETWORK");f.app.settingsMenu.path={"NETWORK"};f.app.settingsMenu.selectedEntries["root/NETWORK"]="room-users";
        f.run({{enter,"> Host"},{down,"> Guest"},{down,"> Offline guest"},{enter,"> Battle"},
            {down,"> Spectator"},{down,"> Offline"},{down,"> Transfer host"},{down,"> Kick player"},
            {enter,"Waiting for the server..."},{left,"> Room users"},{escape,{}}});
        assert(network.pendingRoomRequest && network.pendingRoomRequest->operation==Network::RoomOperation::Kick);
        assert(network.pendingRoomRequest->targetSlot==2 && network.pendingRoomRequest->name=="Offline guest");
    }
    for(bool offline:{false,true})for(int desired:{0,1,2}) {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected;network.room={"MANAGE",true,3,32};network.roomPlayerIndex=0;
        network.multiplayerRoom=true;network.roomMembers={{0,"Host",true},{1,"Guest",false,offline?Network::PlayerRole::Offline:Network::PlayerRole::Spectator},{2,"Other",false}};
        f.select_category("NETWORK");f.app.settingsMenu.path={"NETWORK"};f.app.settingsMenu.selectedEntries["root/NETWORK"]="room-users";
        std::vector<Press> presses{{enter,"> Host"},{down,"> Guest"},{enter,"> Battle"}};
        if(desired>=1)presses.push_back({down,"> Spectator"});
        if(desired>=2)presses.push_back({down,"> Offline"});
        presses.insert(presses.end(),{{enter,"Waiting for the server..."},{left,"> Room users"},{escape,{}}});
        f.run(presses);
        assert(network.pendingRoomRequest && network.pendingRoomRequest->operation==Network::RoomOperation::SetMemberRole &&
            network.pendingRoomRequest->targetSlot==1 && network.pendingRoomRequest->name=="Guest" &&
            network.pendingRoomRequest->memberRole==static_cast<Network::PlayerRole>(desired));
    }
    for(int desired:{0,1}) {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected;network.room={"SELF",true,2,32};network.roomPlayerIndex=0;
        network.multiplayerRoom=true;network.roomMembers={{0,"Host",true,Network::PlayerRole::Offline},{1,"Guest",false}};
        f.first.user.gameMode=0;f.select_category("NETWORK");
        f.app.settingsMenu.path={"NETWORK"};f.app.settingsMenu.selectedEntries["root/NETWORK"]="room-users";
        std::vector<Press> presses{{enter,"> Host"},{enter,"> Battle"}};
        if(desired==1)presses.push_back({down,"> Spectator"});
        presses.insert(presses.end(),{{enter,"Waiting for the server..."},{left,"> Room users"},{escape,{}}});
        f.run(presses);
        assert(network.pendingRoomRequest && network.pendingRoomRequest->operation==Network::RoomOperation::SetMemberRole &&
            network.pendingRoomRequest->targetSlot==0 && network.pendingRoomRequest->memberRole==static_cast<Network::PlayerRole>(desired));
        assert(f.first.user.gameMode==4 && network.roomOnlineMode==true && network.roomGameMode==4);
    }
    {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected;network.room={"FULL",true,3,32};network.roomPlayerIndex=0;
        network.multiplayerRoom=true;network.battleLimit=2;
        network.roomMembers={{0,"Host",true},{1,"Guest",false,Network::PlayerRole::Spectator},{2,"Other",false}};
        f.select_category("NETWORK");f.app.settingsMenu.path={"NETWORK"};f.app.settingsMenu.selectedEntries["root/NETWORK"]="room-users";
        f.run({{enter,"> Host"},{down,"> Guest"},{enter,"> Battle"},{enter,"Battle player limit reached."},
            {left,"> Guest"},{left,"> Room users"},{escape,{}}});
        assert(!network.pendingRoomRequest);
    }
    {
        Fixture f; Network::NetworkServices network; f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected; network.room={"SMALL",true,2,2}; network.roomPlayerIndex=0;
        network.roomPolicyAvailable=true; network.roomMembers={{0,"Creator",true},{1,"Guest",false}};
        const auto presetBase= f.first.user.presets.base;
        f.select_category("NETWORK");
        f.run({{right,"> Server IP ="},{down,"> Username ="},{down,"> CONNECT / RECONNECT"},{down,"> DISCONNECT"},
            {down,"> Prepare match"},{down,"> Force host configuration"},
            {enter,"Waiting for the server..."},
            {escape,{}}});
        assert(network.pendingRoomRequest && network.pendingRoomRequest->operation==Network::RoomOperation::SetPolicy &&
            !network.pendingRoomRequest->policy.forceRules);
        assert(f.first.user.presets.base==presetBase);
        assert(f.observed_output().find("Select Force host configuration")==std::string::npos);
    }
    {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected;network.multiplayerRoom=true;
        network.room={"SIZE-FIRST",true,1,32};network.roomPlayerIndex=0;
        network.roomMembers={{0,"Host",true}};
        f.select_category("NETWORK");f.app.settingsMenu.path={"NETWORK"};
        f.app.settingsMenu.selectedEntries["root/NETWORK"]="room-start";
        assert(f.run({{enter,{}}})==App::Settings::MenuResult::StartGame);
        assert(network.startFromMenu && f.first.user.gameMode==4);
        assert(!network.pendingRoomRequest && !network.queuedMatchControl);
        assert(network.matchPhase==Network::RoomMatchPhase::Idle);
    }
    {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected;network.multiplayerRoom=true;
        network.room={"CANCEL",true,2,32};network.roomPlayerIndex=0;network.matchCanceled=true;
        network.roomMembers={{0,"Host",true},{1,"Guest",false}};
        f.select_category("NETWORK");f.app.settingsMenu.path={"NETWORK"};
        f.app.settingsMenu.selectedEntries["root/NETWORK"]="room-start";
        bool returned=false;
        try {App::wait_for_room_ready(f.first.context);} catch(const App::RoomSettingsRequested&) {returned=true;}
        assert(returned && !f.app.startScreenRequested);
        assert(f.app.settingsMenu.path==std::vector<std::string>{"NETWORK"});
        assert(f.app.settingsMenu.selectedEntries.at("root/NETWORK")=="room-start");
        network.matchCanceled=false;
        f.run({{escape,{}}});
        Test::TerminalScreen restored;restored.feed(f.observed_output());
        assert(restored.position_of("> Prepare match").first>0);
    }
    {
        Fixture f;Network::NetworkServices network;f.first.context.attach_network(network);
        network.connection=Network::ConnectionState::Connected;network.room={"VIEW",true,2,2};network.roomInstance=1;
        network.roomPlayerIndex=1;network.roomPolicyAvailable=true;network.roomMembers={{0,"Host",true},{1,"Guest",false}};
        UserConfig::Settings host;host.gameMode=4;host.fieldWidth=13;host.forceDas=1;host.forcedDas=.015;
        f.first.user.das=.4;f.first.user.username="Guest";
        network.roomPreview.incoming.payload=UserConfig::encode_multiplayer_config(host,Runtime::normalize_user_config(host,Runtime::GameMode::Multiplayer),true);
        f.select_category("NETWORK");
        f.run({{right,"> Server IP ="},{down,"> Username ="},{down,"> CONNECT / RECONNECT"},{down,"> DISCONNECT"},
            {down,"> Host configuration"},{down,"> Room player limit"},{down,"> Battle player limit"},{down,"> Player status"},
            {down,"> Room users"},{down,"> View host configuration"},{enter,"> Player"},
            {down,"> MODEL"},{enter,"> Model ="},{enter,"This configuration is read-only."},
            {left,"> MODEL"},{up,"> Player"},{up,"> Copy host configuration"},
            {enter,"Press Enter again to copy the host configuration."},{enter,"Host configuration copied. Changes saved."},
            {left,"> View host configuration"},{escape,{}}});
        assert(f.first.user.fieldWidth==13 && f.first.user.das==.4 && f.first.user.forceDas && f.first.user.forcedDas==.015);
        assert(f.first.user.username=="Guest" && f.first.user.uiLanguage==1);
        assert(f.app.settingsMenu.path==std::vector<std::string>{"NETWORK"});
        assert(f.app.settingsMenu.selectedEntries.at("root/NETWORK")=="room-view-host");
        assert(f.observed_output().find("Force host handling")==std::string::npos);
    }
    {
        Fixture f; Network::NetworkServices network; f.first.context.attach_network(network);
        network.connection = Network::ConnectionState::Connected;
        f.select_category("NETWORK");
        f.run({{right,"> Server IP ="},{down,"> Username ="},{down,"> CONNECT / RECONNECT"},{down,"> DISCONNECT"},
            {down,"> Create room"},{enter,"> Room name"},
            {down,"> Room type"},{enter,"Select Room type"},{down,"> Private"},
            {enter,"> Room type"},{left,"> Create room"},{down,"> Join by name"},
            {enter,"> Room name"},{left,"> Join by name"},{escape,{}}});
        assert(network.room.name.empty()); // Menu navigation alone cannot start a game.
        const auto raw=f.output.str();const auto firstClear=raw.find("\x1b[2J");
        assert(firstClear!=std::string::npos && raw.find("\x1b[2J",firstClear+1)==std::string::npos);
    }
    {
        Fixture f;
        assert(f.start({{retry,{}}}));
        assert(f.observed_output().find("[R] Start")!=std::string::npos);
        assert(f.observed_output().find("[P] Settings")!=std::string::npos);
        assert(f.observed_output().find("Up/Down Select")==std::string::npos);
        assert(f.observed_output().find("> Start playing")==std::string::npos);
    }
    {
        Fixture f;
        assert(!f.start({{escape,{}}}));
    }
    {
        Fixture f;
        int reads=0;
        f.first.input.keyStateProvider=[&](int key) {
            if (++reads>100) throw std::runtime_error("start screen ignored retry binding");
            if (key==up || key==down || key==enter || key==right) return reads<30;
            return key==retry && reads>35 && reads<50;
        };
        assert(App::run_start_menu(f.first.context));
        assert(reads>=50); // Navigation and Enter cannot start or select an action.
        f.first.input.keyStateProvider={};
    }
    {
        Fixture f;
        f.first.user.keyCodes[8]=secondRetry;
        f.first.user.keyCodes[9]=customSettings;
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes);
        assert(f.start({{retry,"",std::chrono::milliseconds(20),false},
            {customSettings,"SETTINGS"},{escape,"TerMino"},{secondRetry,{}}}));
        assert(f.observed_output().find("[F8] Start")!=std::string::npos);
        assert(f.observed_output().find("[F9] Settings")!=std::string::npos);
        assert(f.observed_output().find("[R] Start")==std::string::npos);
        assert(f.observed_output().find("[P] Settings")==std::string::npos);
    }
    {
        Fixture f;
        f.first.user.keyCodes[9]=customSettings;
        assert(!f.start({{settings,"",std::chrono::milliseconds(20),false},{escape,{}}}));
        assert(f.observed_output().find("SETTINGS")==std::string::npos);
    }
    {
        Fixture f;
        UserConfig::set_control_keys(f.first.user,8,{retry,secondRetry});
        assert(f.start({{secondRetry,{}}}));
        assert(f.observed_output().find("[R / F8] Start")!=std::string::npos);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.second.user.keyCodes[8]=secondRetry;
        assert(f.start({{secondRetry,{}}}));
        assert(f.observed_output().find("[R / F8] Start")!=std::string::npos);
    }
    {
        Fixture f;
        assert(f.start({{settings,"SETTINGS"},{retry,{}}}));
    }
    {
        Fixture f;
        assert(f.start({{settings,"SETTINGS"},{down,"> MODEL"},{right,"> Model = Zen"},
            {right,"Select Model"},{escape,"TerMino"},{retry,{}}}));
        assert(!f.app.startScreenRequested);
    }
    for (bool choice : {false,true}) {
        Fixture f;
        std::vector<Press> presses{{down,"> MODEL"},{enter,"> Model = Zen"}};
        if (choice) presses.push_back({enter,"Select Model"});
        presses.push_back({escape,{}});
        assert(f.run(presses)==App::Settings::MenuResult::StartScreen);
        assert(f.app.startScreenRequested);
    }
    {
        Fixture f;
        assert(f.run({{down,"> MODEL"},{enter,"> Model = Zen"},{enter,"Select Model"},
            {down,"> QuickPlay"},{backspace,"> Model = Zen"},{backspace,"> MODEL"},
            {retry,{}}})==App::Settings::MenuResult::StartGame);
        assert(f.first.user.gameMode==0 && !f.app.startScreenRequested);
    }
    {
        Fixture f;
        assert(f.start({{settings,"SETTINGS"},{down,"> MODEL"},
            {right,"> Model = Zen"},{right,"Select Model"},{down,"> QuickPlay"},
            {right,"> Model = QuickPlay"},{left,"> MODEL"},{escape,"TerMino"},{retry,{}}}));
        assert(f.first.user.gameMode==3);
    }
    {
        App::UiText::set_language(App::UiText::Language::Chinese);
        Fixture f;
        assert(f.start({{settings,"玩家 1 / 配置"},{escape,"TerMino"},{retry,{}}}));
        assert(f.observed_output().find("[R] 开始")!=std::string::npos);
        App::UiText::set_language(App::UiText::Language::English);
    }
    {
        App::UiText::set_language(App::UiText::Language::Chinese);
        Fixture f;
        const auto directory=std::filesystem::temp_directory_path()/"tetr_ui_language_menu_test";
        f.first.user.persistencePath=(directory/"player1.ini").string();
        f.second.user.persistencePath=(directory/"player2.ini").string();
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.fieldWidth=17; f.second.user.fieldWidth=13;
        f.select_category("OTHERS");
        assert(f.start({{settings,"玩家 1 / 配置"},{right,"> 方块样式"},
            {down,"> 隐藏颜色"},{down,"> 界面语言 = 中文"},{right,"> Interface language = English"},
            {left,"> OTHERS"},{escape,"[R] Start"},{retry,{}}}));
        UserConfig::Settings one,two;
        UserConfig::load(one,f.first.user.persistencePath);
        UserConfig::load(two,f.second.user.persistencePath);
        assert(one.uiLanguage==1 && two.uiLanguage==1);
        assert(one.fieldWidth==17 && two.fieldWidth==13);
        App::UiText::initialize_language(one.uiLanguage);
        assert(App::UiText::language()==App::UiText::Language::English);
        f.run({{right,"> Interface language = English"},{right,"> 界面语言 = 中文"},
            {right,"> Interface language = English"},{left,"> OTHERS"},{retry,{}}});
        assert(f.first.user.uiLanguage==1 && App::UiText::language()==App::UiText::Language::English);
        assert(f.start({{settings,"SETTINGS"},{right,"> Interface language = English"},
            {right,"> 界面语言 = 中文"},
            {left,"> 外观"},{escape,"[R] 开始"},{retry,{}}}));
        UserConfig::load(one,f.first.user.persistencePath);
        UserConfig::load(two,f.second.user.persistencePath);
        assert(one.uiLanguage==0 && two.uiLanguage==0);
        std::filesystem::remove_all(directory);
        App::UiText::set_language(App::UiText::Language::English);
    }
    {
        Fixture f;
        f.select_category("OTHERS");
        f.first.user.obscuredColors=1; f.first.user.squareStyle=0;
        f.run({{right,"> Square Version"},{up,"> RESET"},
            {enter,"Press Enter again to confirm RESET"},{enter,"> 重置"},{retry,{}}});
        assert(f.first.user.uiLanguage==0 && f.second.user.uiLanguage==0);
        assert(f.first.user.obscuredColors==0 && f.first.user.squareStyle==0);
        assert(f.second.user.obscuredColors==0 && f.second.user.squareStyle==0);
        assert(!f.first.context.config.display.obscuredColors && !f.second.context.config.display.obscuredColors);
        assert(App::UiText::language()==App::UiText::Language::Chinese);
        App::UiText::set_language(App::UiText::Language::English);
    }
    {
        Fixture f;
        f.run({{right,"Player 1 [current]"},{left,"> Player = Player 1"},{retry,{}}});
        const auto output=f.observed_output();
        assert(output.find("> Player 2")==std::string::npos);
        assert(output.find("CATEGORIES")==std::string::npos);
    }
    {
        Fixture f;
        f.run({{down,"> MODEL"},{right,"> Model = Zen"},{right,"Select Model"},
               {down,"> QuickPlay"},{left,"> Model = Zen"},{pageDown,"> RESET"},
               {left,"> MODEL"},{retry,{}}});
        assert(f.first.user.gameMode==0); // Left cancels a choice without exiting its parent.
    }
    {
        Fixture f;
        f.run({{down,"> MODEL"},{right,"> Model = Zen"},{down,"> Goal = None"},{right,"Select Goal"},
               {down,"> Time"},{down,"> Lines"},{right,"> Goal = Lines"},{down,"> Line count = 40"},
               {left,"> MODEL"},{retry,{}}});
        assert(f.first.user.gameMode==0 && f.first.user.goal==2);
        assert(f.observed_output().find("Time limit =")==std::string::npos);
    }
    {
        Fixture f;
        f.select_category("MODEL");
        f.app.settingsMenu.path={"MODEL"};
        f.app.settingsMenu.selectedEntries["root/MODEL"]="mode-garbage";
        f.first.user.garbageMode=3;
        f.run({{right,"> Garbage Model = Cheese-layer"},{down,"> Garbage Shape  >"},
               {right,"> Garbage shape = Standard holes"},
               {right,"> Garbage shape = Random density"},{down,"> Density ="},
               {left,"> Garbage Shape  >"},{down,"> Refill cleared lines = ON"},{right,"> Refill cleared lines = OFF"},{retry,{}}});
        assert(f.first.user.cheesePattern==-2);
    }
    {
        Fixture f;
        f.select_category("MODEL");
        f.app.settingsMenu.path={"MODEL"};
        f.app.settingsMenu.selectedEntries["root/MODEL"]="mode-garbage";
        f.first.user.garbageMode=3; f.first.user.cheeseMessiness=.13; f.first.user.cheeseDensity=.67;
        f.run({{right,"> Garbage Model = Cheese-layer"},{down,"> Garbage Shape  >"},
            {right,"> Garbage shape = Standard holes"},{right,"> Garbage shape = Random density"},
            {down,"> Density = 0.67"},{up,"> Garbage shape = Random density"},
            {right,"> Garbage shape = Checkerboard"},{down,"> RESET"},
            {up,"> Garbage shape = Checkerboard"},{right,"> Garbage shape = Standard holes"},
            {down,"> Hole pattern  >"},{down,"> Cheese Messiness = 0.13"},{retry,{}}});
        assert(f.first.user.cheeseMessiness==.13 && f.first.user.cheeseDensity==.67);
    }
    {
        Fixture f;
        f.select_category("MODEL");
        f.app.settingsMenu.path={"MODEL"};
        f.app.settingsMenu.selectedEntries["root/MODEL"]="mode-garbage";
        f.first.user.garbageMode=3; f.first.user.holeCount=3;
        f.run({{right,"> Garbage Model = Cheese-layer"},{down,"> Garbage Shape  >"},
               {right,"> Garbage shape = Standard holes"},{down,"> Hole pattern  >"},{right,"> Layout = Scattered"},
               {right,"> Layout = Contiguous"},
               {down,"> Run length = 3"},{left,"> Hole pattern  >"},{retry,{}}});
        assert(f.first.user.holeCount==-3);
    }
    {
        Fixture f;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{right,"> Width = 10"},{down,"> Height = 20"},
               {left,"> Playfield  >"},{right,"> Height = 20"},{retry,{}}});
        assert((f.app.settingsMenu.path==std::vector<std::string>{"GAME","playfield"}));
        const auto reopened=f.output.str().size();
        f.run({{retry,{}}});
        Test::TerminalScreen screen;
        screen.feed(f.output.str().substr(reopened));
        assert(screen.position_of("> Height = 20").first!=0);
        assert(screen.text().find("SETTINGS > GAME > Playfield")!=std::string::npos);
        assert(f.app.settingsMenu.selectedEntries.at("root/GAME/playfield")=="setting-"+std::to_string(static_cast<int>(Id::FieldHeight)));
    }
    {
        Fixture f;
        f.select_category("MODEL");
        f.app.settingsMenu.path={"MODEL"};
        f.app.settingsMenu.selectedEntries["root/MODEL"]="mode-garbage";
        f.first.user.garbageMode=3; f.first.user.holeCount=3;
        assert(f.run({{right,"> Garbage Model = Cheese-layer"},{down,"> Garbage Shape  >"},
            {right,"> Garbage shape = Standard holes"},{down,"> Hole pattern  >"},
            {right,"> Layout = Scattered"},{right,"> Layout = Contiguous"},
            {down,"> Run length = 3"},{escape,{}}})==App::Settings::MenuResult::StartScreen);
        assert((f.app.settingsMenu.path==std::vector<std::string>{"MODEL","mode-garbage","cheese","holes"}));
        assert(f.start({{settings,"> Run length = 3"},{retry,{}}}));
        assert((f.app.settingsMenu.path==std::vector<std::string>{"MODEL","mode-garbage","cheese","holes"}));
        // Changing the selected garbage pattern removes the saved holes branch.
        f.first.user.cheesePattern=2;
        const auto reopened=f.output.str().size();
        f.run({{retry,{}}});
        Test::TerminalScreen screen;
        screen.feed(f.output.str().substr(reopened));
        assert((f.app.settingsMenu.path==std::vector<std::string>{"MODEL","mode-garbage","cheese"}));
        assert(screen.position_of("> Garbage shape = Random density").first!=0);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.app.settingsMenu.path={"GAME","playfield"};
        f.app.settingsMenu.selectedEntries["root/GAME/playfield"]="setting-"+std::to_string(static_cast<int>(Id::FieldHeight));
        f.app.playerTwoSettingsMenu.path={"HANDLING"};
        f.app.playerTwoSettingsMenu.selectedEntries["root/HANDLING"]="setting-"+std::to_string(static_cast<int>(Id::Das));
        for (const auto& [category,player,title,selection] : std::vector<std::tuple<std::string,int,std::string,std::string>>{
            {"GAME",0,"Player 1 / Playfield","> Height = 20"}, {"HANDLING",1,"Player 2 / HANDLING","> DAS = 0.1"},
            {"GAME",0,"Player 1 / Playfield","> Height = 20"}}) {
            f.select_category(category,player);
            const auto reopened=f.output.str().size();
            f.run({{retry,{}}});
            Test::TerminalScreen screen;
            screen.feed(f.output.str().substr(reopened));
            assert(screen.text().find(title)!=std::string::npos);
            assert(screen.position_of(selection).first!=0);
        }
        assert((f.app.settingsMenu.path==std::vector<std::string>{"GAME","playfield"}));
        assert((f.app.playerTwoSettingsMenu.path==std::vector<std::string>{"HANDLING"}));
    }
    {
        Fixture f;
        f.app.settingsMenu.path={"GAME","playfield"};
        f.select_category("NETWORK"); // An explicit recovery redirect takes precedence.
        f.run({{retry,{}}});
        Test::TerminalScreen screen;
        screen.feed(f.output.str());
        assert(f.app.settingsMenu.path.empty());
        assert(screen.position_of("> NETWORK").first!=0);
    }
    {
        Fixture f;
        // Held Down repeats using real time even though game time is frozen.
        f.first.user.gameMode=3;
        f.run({{down,"> MODEL"},{right,"> Model = QuickPlay"},{down,"> QUICK PLAY"},
            {right,"> Default floor ="},{down,"> Expert = Close"},{right,"> Expert = Open"},
            {left,"> QUICK PLAY"},{retry,{}}});
        assert(f.first.user.qpExpert==1);
        assert(f.observed_output().find("SETTINGS > MODEL > QUICK PLAY")!=std::string::npos);
    }
    {
        Fixture f;
        f.run({{up,"> RESET"},{up,"> Presets"},{up,"> Maps"},{up,"> OTHERS"},{up,"> NETWORK"},{right,"> Server IP ="},{left,"> NETWORK"},
               {pageUp,"> Player = Player 1"},{retry,{}}});
    }
    {
        Fixture f;
        f.select_category("CONTROLS");
        const auto file=std::filesystem::temp_directory_path()/"tetr_escape_key_capture.txt";
        f.first.user.keybindingPath=file.string();
        f.run({{right,"> Move Piece Left ="},
               {right,"Edit Move Piece Left"},{escape,{}},{retry,{}}});
        assert(UserConfig::control_keys(f.first.user,0)==std::vector<int>({left,escape}));
        assert(f.first.input.bindings.action_for(escape)==Input::Action::MoveLeft);
        assert(!f.app.startScreenRequested);
        std::filesystem::remove(file);
    }
#ifdef _WIN32
    {
        // Phantom 255 states must not stall release detection or become a key.
        Fixture f;
        int ticks=0,phantomReads=0;
        f.first.input.keyStateProvider=[&](int key) {
            if (key==255) {++phantomReads;return true;}
            return key==customSettings && ticks>=2 && ticks<4;
        };
        const int captured=App::Settings::capture_key(f.first.context,Input::Action::MoveLeft,[&] {
            if (++ticks>10) throw std::runtime_error("phantom key blocked binding capture");
        });
        assert(captured==customSettings && ticks==4 && phantomReads==0);
    }
#endif
    {
        Fixture f;
        f.select_category("CONTROLS");
        f.run({{right,"> Move Piece Left ="},{pageDown,"> RESET"},
            {pageUp,"> Move Piece Left ="},{retry,{}}});
    }
    {
        // Left is navigation in value editors, but must remain available as
        // a real binding in the keyboard editor. Release is required to commit.
        Fixture f;
        f.select_category("CONTROLS");
        const auto file=std::filesystem::temp_directory_path()/"tetr_tree_key_capture.txt";
        f.first.user.keybindingPath=file.string();
        f.run({{right,"> Move Piece Left ="},{down,"> Move Piece Right ="},
               {down,"> Soft Drop ="},{down,"> Hard Drop ="},{down,"> Rotate Clockwise ="},{down,"> Rotate Counterclockwise ="},
               {down,"> Rotate 180 ="},{right,"Edit Rotate 180"},{left,{}},{retry,{}}});
        assert(UserConfig::control_keys(f.first.user,6)==std::vector<int>({UserConfig::default_key_codes()[6],left}));
        assert(f.first.user.keyCodes[0]==-1);
        std::filesystem::remove(file);
    }
    {
        Fixture f;
        f.select_category("CONTROLS");
        const auto file=std::filesystem::temp_directory_path()/"tetr_multiple_key_capture.txt";
        f.first.user.keybindingPath=file.string();
        UserConfig::set_control_keys(f.first.user,0,{left,secondRetry});
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes,f.first.user.additionalKeyCodes);
        f.run({{right,"> Move Piece Left ="},{right,"Edit Move Piece Left"},{secondRetry,{}},
            {right,"Edit Move Piece Left"},{customSettings,{}},
            {right,"Edit Move Piece Left"},{left,{}},{retry,{}}});
        assert(UserConfig::control_keys(f.first.user,0)==std::vector<int>({customSettings}));
        assert(f.first.input.bindings.action_for(customSettings)==Input::Action::MoveLeft);
        assert(f.first.input.bindings.action_for(left)==Input::Action::Unknown);
        UserConfig::Settings saved;
        UserConfig::load_keybindings(saved,file.string());
        assert(UserConfig::control_keys(saved,0)==std::vector<int>({customSettings}));
        std::filesystem::remove(file);
    }
    {
        Fixture f;
        f.first.user.keyCodes[8]=secondRetry;
        f.first.user.keyCodes[9]=customSettings;
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes,f.first.user.additionalKeyCodes);
        assert(f.first.input.bindings.action_for(retry)==Input::Action::Unknown);
        assert(f.run({{retry,{},std::chrono::milliseconds(15),false},{secondRetry,{}}})==App::Settings::MenuResult::StartGame);
        assert(f.observed_output().find("[F8] Restart")!=std::string::npos);
        assert(f.observed_output().find("[F9] Root")!=std::string::npos);
        assert(f.observed_output().find("[R] Restart")==std::string::npos);
    }
    {
        Fixture f;
        f.select_category("CONTROLS");
        const auto file=std::filesystem::temp_directory_path()/"tetr_reserved_key_capture.txt";
        f.first.user.keybindingPath=file.string();
        f.run({{right,"> Move Piece Left ="},{right,"Edit Move Piece Left"},{enter,{}},{retry,{}}});
        assert(UserConfig::control_keys(f.first.user,0)==std::vector<int>({left,enter}));
        assert(f.first.input.bindings.action_for(enter)==Input::Action::MoveLeft);
        assert(compact_screen(f.output.str()).find("Enter,Esc,arrowsandTabcanbebound")!=std::string::npos);
        std::filesystem::remove(file);
    }
    for (const auto id:{Id::ControlRetry,Id::ControlOpenSettings}) {
        Fixture f;
        f.select_category("CONTROLS");
        f.app.settingsMenu.path={"CONTROLS"};
        f.app.settingsMenu.selectedEntries["root/CONTROLS"]="setting-"+std::to_string(static_cast<int>(id));
        const auto file=std::filesystem::temp_directory_path()/
            (id==Id::ControlRetry ? "tetr_retry_reserved_capture.txt" : "tetr_config_reserved_capture.txt");
        f.first.user.keybindingPath=file.string();
        const std::string label=id==Id::ControlRetry ? "Retry Game" : "Open Config";
        const int accepted=id==Id::ControlRetry ? secondRetry : customSettings;
        std::vector<Press> presses{{right,"Edit "+label}};
        for (int key:{enter,up,down,left,right,tab})
            presses.push_back({key,"",std::chrono::milliseconds(15),false});
        presses.push_back({accepted,{}});
        presses.push_back({id==Id::ControlRetry ? accepted : retry,{}});
        f.run(presses);
        const std::size_t control=id==Id::ControlRetry ? 8 : 9;
        assert(UserConfig::control_keys(f.first.user,control)==
            std::vector<int>({id==Id::ControlRetry ? retry : settings,accepted}));
        const auto hint=compact_screen(f.output.str());
        assert(hint.find("cannotuseEnter,Esc,arrowsorTab.")!=std::string::npos);
        std::filesystem::remove(file);
    }
    {
        Fixture f;
        f.select_category("CONTROLS");
        f.app.settingsMenu.path={"CONTROLS"};
        f.app.settingsMenu.selectedEntries["root/CONTROLS"]=
            "setting-"+std::to_string(static_cast<int>(Id::ControlRetry));
        const auto keys=f.first.user.keyCodes;
        assert(f.run({{right,"Edit Retry Game"},{escape,{}}})==App::Settings::MenuResult::StartScreen);
        assert(f.first.user.keyCodes==keys);
    }
#ifdef __linux__
    for (const auto id : {Id::Das,Id::Arr}) {
        EmptyStdin stdinGuard;
        const std::string value="1.5F\n";
        assert(write(stdinGuard.descriptors[1],value.data(),value.size())==static_cast<ssize_t>(value.size()));
        Fixture f;
        f.select_category("HANDLING");
        const std::string label=id==Id::Das ? "DAS" : "ARR";
        std::vector<Press> presses{{right,"> SDF ="},{down,"> DAS ="}};
        if (id==Id::Arr) presses.push_back({down,"> ARR ="});
        presses.insert(presses.end(),{{right,"Edit "+label},
            {-1,"> "+label+" = 0.025",std::chrono::milliseconds(5),false},{retry,{}}});
        f.run(presses);
        assert(std::abs((id==Id::Das ? f.first.user.das : f.first.user.arr)-0.025)<1e-12);
        assert(f.observed_output().find("decimal frames")!=std::string::npos);
    }
    {
        EmptyStdin stdinGuard;
        const std::string value="17\n";
        assert(write(stdinGuard.descriptors[1],value.data(),value.size())==static_cast<ssize_t>(value.size()));
        Fixture f;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{right,"> Width = 10"},{right,"Edit Width"},
            {-1,"> Width = 17",std::chrono::milliseconds(5),false},{retry,{}}});
        assert(f.first.user.fieldWidth==17);
        assert(f.observed_output().find("New value:")==std::string::npos);
    }
    for (const auto& value : {std::string("19\x7f" "7\n"), std::string("12\x1b[D9\x1b[3~\n")}) {
        const int expected=value[1]=='9' ? 17 : 19;
        EmptyStdin stdinGuard;
        assert(write(stdinGuard.descriptors[1],value.data(),value.size())==static_cast<ssize_t>(value.size()));
        Fixture f;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{right,"> Width = 10"},{right,"Edit Width"},
            {-1,"> Width = "+std::to_string(expected),std::chrono::milliseconds(5),false},{retry,{}}});
        assert(f.first.user.fieldWidth==expected);
    }
    {
        EmptyStdin stdinGuard;
        Fixture f;
        f.select_category("GAME");
        Script script{{{right,"> Playfield  >"},{right,"> Width = 10"},{right,"Edit Width"},
            {left,"",std::chrono::milliseconds(10),false},
            {backspace,"",std::chrono::milliseconds(10),false},{retry,{}}},f.output};
        bool supplied=false;
        f.first.input.keyStateProvider=[&](int key) {
            const bool pressed=script.read(key);
            if (script.index==5 && !supplied) {
                supplied=true;
                assert(write(stdinGuard.descriptors[1],"18\n",3)==3);
            }
            return pressed;
        };
        assert(App::Settings::run_menu(f.first.context)==App::Settings::MenuResult::StartGame);
        assert(supplied && f.first.user.fieldWidth==18);
        f.first.input.keyStateProvider={};
    }
    {
        EmptyStdin stdinGuard;
        Fixture f;
        f.select_category("GAME");
        assert(f.run({{right,"> Playfield  >"},{right,"> Width = 10"},
            {right,"Edit Width"},{escape,{}}})==App::Settings::MenuResult::StartScreen);
        assert(f.first.user.fieldWidth==10 && f.app.startScreenRequested);
    }
    {
        const std::string value="17";
        EmptyStdin stdinGuard;
        assert(write(stdinGuard.descriptors[1],value.data(),value.size())==static_cast<ssize_t>(value.size()));
        Fixture f;
        f.select_category("GAME");
        assert(f.run({{right,"> Playfield  >"},{right,"> Width = 10"},
            {right,"Edit Width"},{escape,{}}})==App::Settings::MenuResult::StartScreen);
        assert(f.first.user.fieldWidth==17);
        assert(f.app.startScreenRequested);
    }
    {
        EmptyStdin stdinGuard;
        assert(write(stdinGuard.descriptors[1],"invalid",7)==7);
        Fixture f;
        f.select_category("HANDLING");
        const auto previous=f.first.user.das;
        assert(f.run({{right,"> SDF ="},{down,"> DAS ="},
            {right,"Edit DAS"},{escape,{}}})==App::Settings::MenuResult::StartScreen);
        assert(f.first.user.das==previous && f.app.startScreenRequested);
        assert(f.observed_output().find("Invalid value; the previous value is unchanged.")!=std::string::npos);
    }
    {
        EmptyStdin stdinGuard;
        Fixture f;
        f.select_category("GAME");
        Script script{{{right,"> Playfield  >"},{right,"> Width = 10"},
            {right,"Edit Width"},{retry,"> Width = 17",std::chrono::milliseconds(200)},
            {escape,{}}},f.output};
        bool supplied=false;
        f.first.input.keyStateProvider=[&](int key) {
            const bool pressed=script.read(key);
            if (key==retry && pressed && script.index==3 && !supplied) {
                supplied=true;
                assert(write(stdinGuard.descriptors[1],"17\n",3)==3);
            }
            return pressed;
        };
        assert(App::Settings::run_menu(f.first.context)==App::Settings::MenuResult::StartScreen);
        assert(script.index+1==script.presses.size() && script.sampled);
        assert(supplied && f.first.user.fieldWidth==17);
        f.first.input.keyStateProvider={};
    }
    for (const bool escapeSaves : {false,true}) {
        EmptyStdin stdinGuard;
        Fixture f;
        f.first.user.gameMode=5;
        f.select_category("MODEL");
        f.first.user.persistencePath=(std::filesystem::temp_directory_path()/
            (escapeSaves ? "tetr_escape_scalar.ini" : "tetr_enter_scalar.ini")).string();
        std::vector<Press> presses{{right,"> Model = Replay"},{down,"> Replay ="},
            {right,"Edit Replay"},{retry,"",std::chrono::milliseconds(25),false},
            {settings,"",std::chrono::milliseconds(25),false}};
        if (escapeSaves) presses.push_back({escape,{}});
        else {
            presses.push_back({-1,"> Replay = .tetr/replay/RP",std::chrono::milliseconds(5),false});
            presses.push_back({retry,{}});
        }
        Script script{presses,f.output};
        bool wroteR=false,wroteP=false,wroteEnter=false;
        f.first.input.keyStateProvider=[&](int key) {
            const bool pressed=script.read(key);
            if (script.index==3 && !wroteR) { wroteR=true; assert(write(stdinGuard.descriptors[1],"R",1)==1); }
            if (script.index==4 && !wroteP) { wroteP=true; assert(write(stdinGuard.descriptors[1],"P",1)==1); }
            if (!escapeSaves && script.index==5 && !wroteEnter) {
                wroteEnter=true; assert(write(stdinGuard.descriptors[1],"\n",1)==1);
            }
            return pressed;
        };
        const auto result=App::Settings::run_menu(f.first.context);
        assert(result==(escapeSaves ? App::Settings::MenuResult::StartScreen : App::Settings::MenuResult::StartGame));
        assert(script.index+1==presses.size() && script.sampled);
        assert(wroteR && wroteP && f.first.user.replayPath==".tetr/replay/RP");
        assert((f.app.settingsMenu.path==std::vector<std::string>{"MODEL"}));
        UserConfig::Settings loaded;
        UserConfig::load(loaded,f.first.user.persistencePath);
        assert(loaded.replayPath==".tetr/replay/RP");
        std::filesystem::remove(f.first.user.persistencePath);
        f.first.input.keyStateProvider={};
    }

#endif
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.second.user.keyCodes[8]=secondRetry;
        Input::refresh_bindings(f.second.input,f.second.user.keyCodes);
        f.run({{right,"> Player 1 [current]"},{down,"> Player 2"},
               {right,"> Edit target = Player 2"},{down,"> MODEL"},{right,"> Model = Local Versus"},
               {settings,"Player 2 / SETTINGS"},{secondRetry,{}}});
        assert(f.app.settingsCategoryIndex==f.category_index("MODEL",1));
    }
    {
        // Player switching preserves independent settings and each branch's
        // selection without exposing the other player's fields in one list.
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{down,"> Pieces  >"},
               {right,"> Num of Next ="},{down,"> Open Hold = ON"},
               {right,"> Open Hold = OFF"},{settings,"SETTINGS"},
               {pageUp,"> Edit target = Player 1"},{right,"> Player 1 [current]"},
               {down,"> Player 2"},{right,"> Edit target = Player 2"},
               {down,"> GAME"},{right,"> Playfield  >"},{down,"> Pieces  >"},
               {right,"> Num of Next ="},{down,"> Open Hold = ON"},
               {settings,"SETTINGS"},{pageUp,"> Edit target = Player 2"},
               {right,"> Player 2 [current]"},{up,"> Player 1"},
               {right,"> Edit target = Player 1"},{down,"> GAME"},
               {right,"> Pieces  >"},{right,"> Open Hold = OFF"},{retry,{}}});
        assert(f.first.user.holdEnabled==0 && f.second.user.holdEnabled==1);
        assert(f.app.settingsMenu.selectedEntries.at("root/GAME")=="pieces");
        assert(f.app.playerTwoSettingsMenu.selectedEntries.at("root/GAME")=="pieces");
    }
    {
        Fixture f;
        f.run({{down,"> MODEL"},{right,"> Model = Zen"},{right,"Select Model"},
               {down,"> QuickPlay"},
               {down,"> Multiplayer"},{down,"> Local Versus"},{right,"> Model = Local Versus"},
               {left,"> MODEL"},{up,"> Edit target = Player 1"},{right,"> Player 1 [current]"},
               {down,"> Player 2"},{retry,{}}});
        assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.fieldWidth=17; f.second.user.fieldWidth=13;
        f.run({{right,"> Player 1 [current]"},{down,"> Player 2"},{right,"> Edit target = Player 2"},
               {down,"> MODEL"},{right,"> Model = Local Versus"},{right,"Select Model"},
               {up,"> Multiplayer"},{up,"> QuickPlay"},
               {up,"> Zen"},{right,"> Model = Zen"},{left,"> MODEL"},
               {up,"> Player = Player 1"},{right,"> Player 1 [current]"},{retry,{}}});
        assert(f.first.user.gameMode==0 && f.first.user.fieldWidth==13);
        assert(f.app.savedPlayerOneSettings->fieldWidth==17);
        assert(f.observed_output().find("Player 1 only")!=std::string::npos);
        f.first.user.fieldWidth=12; f.first.user.gameMode=6;
        App::Settings::initialize_local_versus_player_settings(f.first.context);
        assert(f.first.user.fieldWidth==17 && f.second.user.fieldWidth==12);
    }
    {
        Fixture f;
        f.select_category("GAME");
        f.first.user.fieldWidth=16; f.first.user.turnPieces=3.5;
        f.run({{right,"> Playfield  >"},{up,"> RESET"},
               {enter,"Press Enter again to confirm RESET"},{enter,"> RESET"},{retry,{}}});
        assert(f.first.user.fieldWidth==10 && f.first.user.turnPieces==0);
    }
    for (bool category : {false,true}) {
        Fixture f;
        f.first.user.fieldWidth=16;
        std::vector<Press> presses;
        if (category) { f.select_category("GAME"); presses.push_back({right,"> Playfield  >"}); }
        presses.push_back({up,"> RESET"});
        presses.push_back({enter,"Press Enter again to confirm RESET",std::chrono::milliseconds(300)});
        presses.push_back({retry,{}});
        f.run(presses);
        assert(f.first.user.fieldWidth==16);
    }
    {
        Fixture f;
        f.first.user.fieldWidth=16;
        f.run({{up,"> RESET"},{enter,"Press Enter again to confirm RESET"},
               {left,{}},{enter,"Press Enter again to confirm RESET"},
               {down,"> Player = Player 1"},{up,"> RESET"},
               {enter,"Press Enter again to confirm RESET"},{retry,{}}});
        assert(f.first.user.fieldWidth==16);
    }
    {
        Fixture f;
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Classic);
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Marathon);
        const auto file=std::filesystem::temp_directory_path()/"tetr_preset_full_reset_menu.ini";
        f.first.user.persistencePath=file.string();
        f.first.user.gameMode=3; f.first.user.garbageMode=2;
        f.first.user.qpExpert=1; f.first.user.fieldWidth=16;
        f.first.user.autoplayMode=1; f.first.user.serverIp="192.0.2.5";
        f.first.user.gravityInterval=.8; f.first.user.instantGravity=1;
        f.first.user.das=.15; f.first.user.squareStyle=0; f.first.user.obscuredColors=1;
        const auto keys=f.first.user.keyCodes;
        f.run({{up,"> RESET"},{enter,"Press Enter again to confirm RESET"},
               {enter,"SETTINGS"},{retry,{}}});
        assert(f.first.user.gameMode==0 && f.first.user.garbageMode==0);
        assert(f.first.user.qpExpert==0 && f.first.user.fieldWidth==10);
        assert(f.first.user.autoplayMode==0 && f.first.user.serverIp=="192.0.2.5");
        assert(f.first.user.gravityInterval==UserConfig::Settings{}.gravityInterval && f.first.user.instantGravity==0);
        assert(f.first.user.das==.15 && !f.first.user.forceDas && !f.first.user.forceSdf);
        assert(f.first.user.uiLanguage==1);
        assert(f.first.user.keyCodes==keys);
        assert(f.first.user.squareStyle==0 && f.first.user.obscuredColors==1);
        assert(f.first.user.presets.layers.empty() && f.first.user.presets.base.empty());
        assert(!UserConfig::Presets::undo(f.first.user));
        UserConfig::Settings loaded;UserConfig::load(loaded,file.string());
        assert(loaded.presets.layers.empty() && loaded.presets.base.empty() && loaded.gameMode==0);
        std::filesystem::remove(file);
    }
    {
        Fixture f; Network::NetworkServices network; f.first.context.attach_network(network);
        f.first.user.gameMode=4; f.first.user.serverIp="wss://saved/ws"; f.first.user.username="Saved username";
        f.first.user.fieldWidth=19; network.connection=Network::ConnectionState::Connected;
        network.room={"Kept room",false,1,2}; network.roomPlayerIndex=0; network.roomPolicy={true};
        network.roomMembers={{0,"Saved username",true}};
        f.run({{up,"> RESET"},{enter,"Press Enter again to confirm RESET"},{enter,"SETTINGS"},{retry,{}}});
        assert(f.first.user.gameMode==4 && f.first.user.serverIp=="wss://saved/ws" && f.first.user.username=="Saved username");
        assert(f.first.user.fieldWidth==10 && network.room.name=="Kept room" && network.roomPolicy==Network::RoomPolicy({true}));
        auto tree=App::Settings::Model::make_tree(f.first.user,false,0);
        App::Settings::Model::reset_subtree(f.first.user,child(child(tree,"MODEL"),"reset"),false);
        assert(f.first.user.gameMode==4);
    }
    for(int player:{0,1,2}) {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.fieldWidth=17; f.second.user.fieldWidth=13;
        const auto directory=std::filesystem::temp_directory_path()/("tetr_local_root_reset_"+std::to_string(player));
        f.first.user.persistencePath=(directory/"one.ini").string();
        f.second.user.persistencePath=(directory/"two.ini").string();
        assert(UserConfig::save(f.first.user) && UserConfig::save(f.second.user));
        f.select_category("player",player==1 ? 1 : 0);
        std::vector<Press> presses;
        if(player==2) presses={{tab,"> Edit target = Player 2"},{tab,"> Edit target = Both players"}};
        presses.push_back({up,"> RESET"});
        presses.push_back({enter,player==2 ? "Press Enter again to reset both players" : "Press Enter again to confirm RESET"});
        presses.push_back({enter,"RESET complete."});presses.push_back({retry,{}});
        f.run(std::move(presses));
        assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
        assert(f.first.user.fieldWidth==(player==1 ? 17 : 10));
        assert(f.second.user.fieldWidth==(player==0 ? 13 : 10));
        assert(!f.app.savedPlayerOneSettings && f.app.singlePlayerOwner==0);
        UserConfig::Settings one,two;
        UserConfig::load(one,f.first.user.persistencePath);UserConfig::load(two,f.second.user.persistencePath);
        assert(one.gameMode==6 && two.gameMode==6);
        assert(one.fieldWidth==f.first.user.fieldWidth && two.fieldWidth==f.second.user.fieldWidth);
        std::filesystem::remove_all(directory);
    }
    {
        Fixture f;
        const auto directory=std::filesystem::temp_directory_path()/"tetr_settings_owner_test";
        f.first.user.persistencePath=(directory/"player1.ini").string();
        f.second.user.persistencePath=(directory/"player2.ini").string();
        f.first.user.gameMode=6; f.first.user.fieldWidth=17;
        f.second.user.gameMode=0; f.second.user.fieldWidth=13;
        f.second.user.squareStyle=1; f.second.user.obscuredColors=1;
        assert(UserConfig::save(f.first.user) && UserConfig::save(f.second.user));
        App::Settings::adopt_single_player_settings(f.second.context);
        f.first.user.fieldWidth=19;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{down,"> Pieces  >"},
               {right,"> Num of Next ="},{down,"> Open Hold ="},{right,"> Open Hold = OFF"},{retry,{}}});
        f.select_category("OTHERS");
        f.run({{right,"> Square Version = Spaced"},{right,"> Square Version = Filled"},
            {down,"> ObscuredColors = ON"},{right,"> ObscuredColors = OFF"},
            {down,"> Interface language = English"},{right,"> 界面语言 = 中文"},{retry,{}}});
        App::UiText::set_language(App::UiText::Language::English);
        UserConfig::Settings one,two;
        UserConfig::load(one,(directory/"player1.ini").string());
        UserConfig::load(two,(directory/"player2.ini").string());
        assert(one.fieldWidth==17 && one.gameMode==6 && one.holdEnabled==1);
        assert(two.fieldWidth==19 && two.gameMode==0 && two.holdEnabled==0);
        assert(one.uiLanguage==0 && two.uiLanguage==0);
        assert(one.squareStyle==0 && two.squareStyle==0 && one.obscuredColors==0 && two.obscuredColors==0);
        assert(f.app.savedPlayerOneSettings->squareStyle==0 && f.app.savedPlayerOneSettings->obscuredColors==0);
        int owner=0; std::ifstream(directory/"active_player")>>owner; assert(owner==1);
        f.first.user.gameMode=6;
        App::Settings::initialize_local_versus_player_settings(f.first.context);
        assert(f.first.user.fieldWidth==17 && f.second.user.fieldWidth==19);
        assert(f.first.user.holdEnabled==1 && f.second.user.holdEnabled==0);
        assert(f.first.user.squareStyle==f.second.user.squareStyle && f.first.user.obscuredColors==f.second.user.obscuredColors);
        std::filesystem::remove(directory/"player1.ini");
        std::filesystem::remove(directory/"player2.ini");
        std::filesystem::remove(directory/"active_player");
        std::filesystem::remove(directory);
        App::UiText::set_language(App::UiText::Language::English);
    }
    {
        Fixture f;
        const auto directory=std::filesystem::temp_directory_path()/"tetr_shared_appearance_menu_test";
        f.first.user.persistencePath=(directory/"player1.ini").string();
        f.second.user.persistencePath=(directory/"player2.ini").string();
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.fieldWidth=17; f.second.user.fieldWidth=13;
        f.first.user.das=.15; f.second.user.das=.3;
        f.second.user.squareStyle=1; f.second.user.obscuredColors=1; // Old independent preferences.
        f.second.user.keyCodes[0]=customSettings;
        Input::refresh_bindings(f.second.input,f.second.user.keyCodes);
        const auto firstKeys=f.first.user.keyCodes, secondKeys=f.second.user.keyCodes;
        f.app.localVersusSettingsAvailable=true;
        f.select_category("OTHERS",1); // Player 2 / Appearance.
        f.run({{right,"> Square Version = Filled"},{right,"> Square Version = Spaced"},
            {down,"> ObscuredColors = OFF"},{right,"> ObscuredColors = ON"},
            {settings,"SETTINGS"},{pageUp,"> Edit target = Player 2"},
            {right,"> Player 2 [current]"},{up,"> Player 1"},{right,"> Edit target = Player 1"},
            {up,"> RESET"},{up,"> Copy other player's settings"},{up,"> Presets"},{up,"> Maps"},{up,"> OTHERS"},{right,"> Square Version = Spaced"},
            {down,"> ObscuredColors = ON"},{retry,{}}});
        assert(f.first.user.squareStyle==1 && f.second.user.squareStyle==1);
        assert(f.first.user.obscuredColors==1 && f.second.user.obscuredColors==1);
        assert(f.first.context.config.display.squareStyle==1 && f.second.context.config.display.squareStyle==1);
        assert(f.first.context.config.display.obscuredColors && f.second.context.config.display.obscuredColors);
        UserConfig::Settings one,two;
        UserConfig::load(one,f.first.user.persistencePath);
        UserConfig::load(two,f.second.user.persistencePath);
        assert(one.squareStyle==1 && two.squareStyle==1 && one.obscuredColors==1 && two.obscuredColors==1);
        assert(one.fieldWidth==17 && two.fieldWidth==13 && one.das==.15 && two.das==.3);
        assert(one.keyCodes==firstKeys && two.keyCodes==secondKeys);
        f.select_category("OTHERS",1);
        f.app.playerTwoSettingsMenu={};
        f.run({{right,"> Square Version = Spaced"},{up,"> RESET"},
            {enter,"Press Enter again to confirm RESET"},{enter,"> 重置"},{retry,{}}});
        UserConfig::load(one,f.first.user.persistencePath);
        UserConfig::load(two,f.second.user.persistencePath);
        assert(one.squareStyle==0 && two.squareStyle==0 && one.obscuredColors==0 && two.obscuredColors==0);
        assert(one.uiLanguage==0 && two.uiLanguage==0);
        assert(one.fieldWidth==17 && two.fieldWidth==13 && one.das==.15 && two.das==.3);
        assert(one.keyCodes==firstKeys && two.keyCodes==secondKeys);
        std::filesystem::remove_all(directory);
        App::UiText::set_language(App::UiText::Language::English);
    }
    {
        Fixture f;
        const auto directory=std::filesystem::temp_directory_path()/"tetr_shared_appearance_unused_player_test";
        f.first.user.persistencePath=(directory/"player1.ini").string();
        f.second.user.persistencePath=(directory/"player2.ini").string();
        f.second.user.fieldWidth=13;
        f.select_category("OTHERS");
        f.run({{right,"> Square Version = Filled"},{right,"> Square Version = Spaced"},{retry,{}}});
        assert(f.second.user.squareStyle==1 && f.second.user.fieldWidth==13);
        assert(f.second.context.config.display.squareStyle==1);
        assert(!f.app.localVersusSettingsAvailable);
        assert(std::filesystem::exists(f.first.user.persistencePath));
        assert(!std::filesystem::exists(f.second.user.persistencePath));
        std::filesystem::remove_all(directory);
    }
    {
        App::UiText::set_language(App::UiText::Language::English);
        Fixture f;
        f.select_category("OTHERS");
        f.run({{right,"> Square Version"},{down,"> ObscuredColors"},
            {down,"> Interface language"},{down,"> Show spawn position = ON"},
            {retry,{}}});
        assert(f.first.user.showSpawnPosition==1 && f.second.user.showSpawnPosition==1);
        assert(f.first.context.config.display.showSpawnPosition && f.second.context.config.display.showSpawnPosition);
        f.run({{right,"> Show spawn position = OFF"},{retry,{}}});
        assert(f.first.user.showSpawnPosition==0 && f.second.user.showSpawnPosition==0);
        assert(!f.first.context.config.display.showSpawnPosition && !f.second.context.config.display.showSpawnPosition);
    }
    {
        App::UiText::set_language(App::UiText::Language::English);
        Fixture f;
        const auto directory=std::filesystem::temp_directory_path()/"tetr_unfocused_input_menu_test";
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.app.localVersusSettingsAvailable=true;
        f.first.user.persistencePath=(directory/"one.ini").string();
        f.second.user.persistencePath=(directory/"two.ini").string();
        f.select_category("OTHERS");
        assert(f.first.user.continueInputUnfocused==0 && f.second.user.continueInputUnfocused==0);
        f.run({{right,"> Square Version"},{down,"> ObscuredColors"},
            {down,"> Interface language"},{down,"> Show spawn position"},
            {down,"> Continue input when unfocused = OFF"},
            {right,"> Continue input when unfocused = ON"},{retry,{}}});
        assert(f.first.user.continueInputUnfocused==1 && f.second.user.continueInputUnfocused==1);
        UserConfig::Settings one,two;
        UserConfig::load(one,f.first.user.persistencePath); UserConfig::load(two,f.second.user.persistencePath);
        assert(one.continueInputUnfocused==1 && two.continueInputUnfocused==1);
        f.run({{down,"> RESET"},{enter,"Press Enter again to confirm RESET"},
            {enter,"> 重置"},{retry,{}}});
        assert(f.first.user.continueInputUnfocused==0 && f.second.user.continueInputUnfocused==0);
        UserConfig::load(one,f.first.user.persistencePath); UserConfig::load(two,f.second.user.persistencePath);
        assert(one.continueInputUnfocused==0 && two.continueInputUnfocused==0);
        assert(App::UiText::tr("Continue input when unfocused")=="失去焦点时继续输入");
        std::filesystem::remove_all(directory);
        App::UiText::set_language(App::UiText::Language::English);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.fieldWidth=17;f.second.user.fieldWidth=13;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{right,"> Width = 17"},
            {tab,"> Width = 13"},{tab,"> Width = 17 | 13"},{tab,"> Width = 17"},{retry,{}}});
        assert(f.first.user.fieldWidth==17 && f.second.user.fieldWidth==13);
        assert((f.app.settingsMenu.path==std::vector<std::string>{"GAME","playfield"}));
        assert((f.app.playerTwoSettingsMenu.path==f.app.settingsMenu.path));
        assert(f.app.settingsMenu.selectedEntries.at("root/GAME/playfield")==
            f.app.playerTwoSettingsMenu.selectedEntries.at("root/GAME/playfield"));
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.holdEnabled=0;
        f.select_category("GAME",1);
        f.app.playerTwoSettingsMenu.path={"GAME","pieces"};
        f.app.playerTwoSettingsMenu.selectedEntries["root/GAME/pieces"]=
            "setting-"+std::to_string(static_cast<int>(Id::HoldCount));
        f.run({{tab,"> Hold count ="},{tab,"> Open Hold = OFF"},{retry,{}}});
        assert(f.app.settingsMenu.selectedEntries.at("root/GAME/pieces")==
            "setting-"+std::to_string(static_cast<int>(Id::HoldEnabled)));
        assert(f.first.user.holdEnabled==0 && f.second.user.holdEnabled==1);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.cheesePattern=2;f.second.user.cheesePattern=1;
        f.select_category("GAME",1);
        f.app.playerTwoSettingsMenu.path={"GAME","garbage-rules","cheese","holes"};
        f.app.playerTwoSettingsMenu.selectedEntries["root/GAME/garbage-rules/cheese/holes"]=
            "setting-"+std::to_string(static_cast<int>(Id::HoleCount))+"-"+
            std::to_string(static_cast<int>(Part::HoleMagnitude));
        f.run({{tab,"> Hole count ="},{tab,"> Garbage shape = Random density"},{retry,{}}});
        assert((f.app.settingsMenu.path==std::vector<std::string>{"GAME","garbage-rules","cheese"}));
        assert(f.first.user.cheesePattern==2 && f.second.user.cheesePattern==1);
    }
    {
        // Mixed switches open a chooser. Target remains Both during editing,
        // and it stays selected for the next edit without copying profiles.
        Fixture f;
        const auto directory=std::filesystem::temp_directory_path()/"tetr_both_settings_test";
        f.first.user.persistencePath=(directory/"one.ini").string();
        f.second.user.persistencePath=(directory/"two.ini").string();
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.holdEnabled=0;
        f.first.user.fieldWidth=17;f.second.user.fieldWidth=13;
        const auto oneKeys=f.first.user.keyCodes,twoKeys=f.second.user.keyCodes;
        f.select_category("GAME");
        f.run({{right,"> Playfield  >"},{down,"> Pieces  >"},{right,"> Num of Next ="},
            {down,"> Open Hold = OFF"},{tab,"> Open Hold = ON"},{tab,"> Open Hold = OFF | ON"},
            {right,"Select Open Hold"},{tab,"",std::chrono::milliseconds(25)},
            {down,"> ON"},{enter,"> Open Hold = ON"},
            {down,"> Infinite Hold = OFF"},{right,"> Infinite Hold = ON"},{retry,{}}});
        assert(f.first.user.holdEnabled==1 && f.second.user.holdEnabled==1);
        assert(f.first.user.infiniteHold==1 && f.second.user.infiniteHold==1);
        UserConfig::Settings one,two;
        UserConfig::load(one,f.first.user.persistencePath);UserConfig::load(two,f.second.user.persistencePath);
        assert(one.holdEnabled==1 && two.holdEnabled==1 && one.infiniteHold==1 && two.infiniteHold==1);
        assert(one.fieldWidth==17 && two.fieldWidth==13 && one.keyCodes==oneKeys && two.keyCodes==twoKeys);
        std::filesystem::remove_all(directory);
        // The Both target lasts only for the current menu session.
        f.run({{settings,"Edit target: Player 1 / SETTINGS"},{retry,{}}});
    }
    {
        // Holding Tab advances once; an old conflicting Retry binding is ignored.
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.keyCodes[8]=tab;
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes);
        assert(f.first.input.bindings.action_for(tab)==Input::Action::Unknown);
        f.run({{tab,"> Edit target = Player 2",std::chrono::milliseconds(120)},
            {tab,"> Edit target = Both players"},{tab,"> Edit target = Player 1"},{retry,{}}});
        assert(f.app.settingsCategoryIndex==0);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.b2bCharging=f.second.user.b2bCharging=0;
        f.first.user.b2bChargeThreshold=9;f.second.user.b2bChargeThreshold=12;
        f.select_category("GAME");
        f.app.settingsMenu.path={"GAME","attack-rules","b2b-reward"};
        f.run({{tab,"Player 2 / B2B reward"},{tab,"Both players (Player 1 | Player 2)"},
            {up,"> RESET"},{enter,"Press Enter again to reset both players"},
            {enter,"> RESET"},{retry,{}}});
        assert(f.first.user.b2bCharging==1 && f.second.user.b2bCharging==1);
        assert(f.first.user.b2bChargeThreshold==4 && f.second.user.b2bChargeThreshold==4);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Classic,true);
        UserConfig::Presets::apply(f.second.user,UserConfig::PresetKind::Marathon,true);
        f.first.user.das=.31;f.second.user.gravityInterval=.9;
        f.run({{tab,"> Edit target = Player 2"},{tab,"> Edit target = Both players"},
            {up,"> RESET"},{enter,"Press Enter again to reset both players"},
            {enter,"RESET complete."},{retry,{}}});
        assert(f.first.user.presets.layers.empty() && f.first.user.presets.base.empty());
        assert(f.second.user.presets.layers.empty() && f.second.user.presets.base.empty());
        assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
        assert(!UserConfig::Presets::undo(f.first.user) && !UserConfig::Presets::undo(f.second.user));
    }
    for(bool both:{false,true}) {
        const std::vector<std::pair<std::string,std::string>> actions{
            {"preset-1","Press Enter again to apply this preset"},
            {"preset-2","Press Enter again to apply this preset"},
            {"preset-3","Press Enter again to apply this preset"},
            {"preset-undo","Press Enter again to undo the latest"},
            {"preset-clear","Press Enter again to undo all presets"},
            {"preset-commit","Press Enter again to keep current values"},
            {"preset-remove","Press Enter again to remove this preset layer"}
        };
        for(const auto& [key,prompt]:actions) {
            Fixture f;
            f.first.user.gameMode=f.second.user.gameMode=both ? 6 : 0;
            f.app.localVersusSettingsAvailable=both;
            f.first.user.das=.073;f.second.user.das=.12;
            assert(UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Classic,both));
            assert(UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Delayed,both));
            assert(UserConfig::Presets::apply(f.second.user,UserConfig::PresetKind::Marathon,both));
            assert(UserConfig::Presets::apply(f.second.user,UserConfig::PresetKind::Delayed,both));
            const auto firstPresets=UserConfig::Presets::encode(f.first.user.presets);
            const auto secondPresets=UserConfig::Presets::encode(f.second.user.presets);
            const auto firstDas=f.first.user.das,secondDas=f.second.user.das;
            const auto firstPath=std::filesystem::temp_directory_path()/"tetr_preset_confirmation_one.ini";
            const auto secondPath=std::filesystem::temp_directory_path()/"tetr_preset_confirmation_two.ini";
            std::filesystem::remove(firstPath);std::filesystem::remove(secondPath);
            f.first.user.persistencePath=firstPath.string();f.second.user.persistencePath=secondPath.string();
            f.select_category("presets");
            f.app.settingsMenu.path={"presets"};
            if(key=="preset-remove") f.app.settingsMenu.path={"presets","preset-layers","preset-layer-0"};
            const bool usingPreset=key=="preset-1" || key=="preset-2" || key=="preset-3";
            if(usingPreset) f.app.settingsMenu.path={"presets",key};
            const auto menuPath=usingPreset ? "root/presets/"+key : key=="preset-remove" ? "root/presets/preset-layers/preset-layer-0" : "root/presets";
            f.app.settingsMenu.selectedEntries[menuPath]=usingPreset ? "preset-use" : key;
            std::vector<Press> presses;
            if(both) {
                presses.push_back({tab,"Edit target: Player 2"});
                presses.push_back({tab,"Both players (Player 1 | Player 2)"});
            }
            // Holding Enter never confirms, Right cancels, and navigation
            // invalidates confirmation even when the menu has only one row.
            presses.insert(presses.end(),{{enter,prompt,std::chrono::milliseconds(300)},
                {right,"Confirmation cancelled."},{right,prompt},{down,{}},{up,{}},{enter,prompt},{retry,{}}});
            f.run(std::move(presses));
            assert(UserConfig::Presets::encode(f.first.user.presets)==firstPresets);
            assert(UserConfig::Presets::encode(f.second.user.presets)==secondPresets);
            assert(f.first.user.das==firstDas && f.second.user.das==secondDas);
            assert(!std::filesystem::exists(firstPath) && !std::filesystem::exists(secondPath));
        }
    }
    {
        Fixture f;
        f.select_category("presets");
        f.first.user.das=.073;f.first.user.arr=.004;
        const auto file=std::filesystem::temp_directory_path()/"tetr_preset_stack_menu.ini";
        f.first.user.persistencePath=file.string();
        f.run({{right,"> New preset"},{down,"> Undo latest layer = None"},{down,"> Undo all presets"},
            {down,"> Keep current values"},{down,"> Applied presets"},{down,"> #1: Classic blocks"},
            {right,"> Use preset"},{right,"Press Enter again to apply this preset"},{enter,"Preset changes saved."},
            {left,"Undo latest layer = Classic blocks"},{down,"> #2: Delayed blocks"},
            {right,"> Use preset"},{right,"Press Enter again to apply this preset"},{enter,"Preset changes saved."},
            {left,"Undo latest layer = Delayed blocks"},{down,"> #3: Marathon"},
            {right,"> Use preset"},{right,"Press Enter again to apply this preset"},{enter,"Preset changes saved."},
            {left,"Undo latest layer = Marathon"},{pageUp,"> New preset"},{down,"> Undo latest layer = Marathon"},
            {right,"Press Enter again to undo the latest"},{enter,"> Undo latest layer = Delayed blocks"},
            {right,"Press Enter again to undo the latest"},{enter,"> Undo latest layer = Classic blocks"},
            {right,"Press Enter again to undo the latest"},{enter,"> Undo latest layer = None"},{retry,{}}});
        assert(f.first.user.das==.073 && f.first.user.arr==.004 && f.first.user.presets.layers.empty());
        UserConfig::Settings loaded;UserConfig::load(loaded,file.string());
        assert(loaded.das==.073 && loaded.arr==.004 && loaded.presets.layers.empty());
        std::filesystem::remove(file);
    }
    {
        Fixture f;
        f.select_category("presets");
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Classic);
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Marathon);
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Delayed);
        f.app.settingsMenu.path={"presets","preset-layers"};
        f.run({{down,"> 2. Marathon"},{right,"> Remove this layer"},
            {right,"Press Enter again to remove this preset layer"},{enter,"> 2. Delayed blocks"},{retry,{}}});
        assert(f.first.user.presets.layers.size()==2);
        assert(f.first.user.presets.layers[0].kind==UserConfig::PresetKind::Classic);
        assert(f.first.user.presets.layers[1].kind==UserConfig::PresetKind::Delayed);
        assert(std::abs(f.first.user.forcedDas-10.0/60)<1e-12 && f.first.user.das==UserConfig::Settings{}.das);
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.das=.07;f.second.user.das=.12;
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Classic,true);
        f.select_category("presets");f.app.settingsMenu.path={"presets"};
        f.app.settingsMenu.selectedEntries["root/presets"]="preset-2";
        f.run({{tab,"> #2: Delayed blocks"},{tab,"> #2: Delayed blocks"},
            {right,"> Use preset"},{right,"Press Enter again to apply this preset"},{enter,"Preset changes saved."},
            {left,"Undo latest layer = Delayed blocks"},{pageUp,"> New preset"},{down,"> Undo latest layer = Delayed blocks"},
            {right,"Press Enter again to undo the latest"},{enter,"> Undo latest layer = Classic blocks | None"},
            {right,"Press Enter again to undo the latest"},{enter,"> Undo latest layer = None"},{retry,{}}});
        assert(f.first.user.das==.07 && f.second.user.das==.12);
        assert(f.first.user.presets.layers.empty() && f.second.user.presets.layers.empty());
        assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
    }
    {
        Fixture f;
        UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Delayed);
        const double current=f.first.user.das;
        f.select_category("presets");f.app.settingsMenu.path={"presets"};
        f.app.settingsMenu.selectedEntries["root/presets"]="preset-commit";
        f.run({{right,"Press Enter again to keep current values"},
            {enter,"Preset changes saved."},{retry,{}}});
        assert(f.first.user.das==current && f.first.user.presets.layers.empty());
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        f.first.user.das=.073;f.second.user.das=.12;
        assert(UserConfig::Presets::apply(f.first.user,UserConfig::PresetKind::Classic,true));
        assert(UserConfig::Presets::apply(f.second.user,UserConfig::PresetKind::Delayed,true));
        f.select_category("presets");f.app.settingsMenu.path={"presets"};
        f.app.settingsMenu.selectedEntries["root/presets"]="preset-clear";
        f.run({{tab,"Edit target: Player 2"},{tab,"Both players (Player 1 | Player 2)"},
            {right,"Press Enter again to undo all presets"},{enter,"Preset changes saved."},{retry,{}}});
        assert(f.first.user.das==.073 && f.second.user.das==.12);
        assert(f.first.user.presets.layers.empty() && f.second.user.presets.layers.empty());
        assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
    }
    {
        Fixture f;
        f.select_category("GAME");f.app.settingsMenu.path={"GAME","gravity"};
        f.run({{right,"Select Gravity mode"},{up,"> Marathon progression"},
            {enter,"> Gravity mode = Marathon progression"},{down,"> Starting interval (seconds)"},
            {down,"> Lines per level = 10"},{down,"> Acceleration factor = 1"},{retry,{}}});
        assert(f.first.user.gravityProgression==2 && f.first.user.presets.layers.empty());
    }
    {
        const auto path=std::filesystem::temp_directory_path()/"termino_preset_authoring_menu.txt";
        std::filesystem::remove(path);
        const auto* prior=std::getenv("TETR_PRESET_FILE");const std::string previous=prior ? prior : "";
#ifdef _WIN32
        _putenv_s("TETR_PRESET_FILE",path.string().c_str());
        constexpr int savePreset='S',undefined='U';
#else
        setenv("TETR_PRESET_FILE",path.c_str(),1);
        constexpr int savePreset=KEY_S,undefined=KEY_U;
#endif
        for(int presetMode:{-1,0,6}) for(int selectedPlayer:{0,1}) for(bool draft:{false,true}) {
            UserConfig::Presets::SavedPreset saved{0,"Application scope",{{Id::PreMove,0},{Id::Das,.143}}};
            saved.playerTwoFields=UserConfig::PresetFields{{Id::PreMove,1},{Id::Arr,.005}};
            if(presetMode>=0) {
                saved.fields[Id::GameMode]=presetMode;(*saved.playerTwoFields)[Id::GameMode]=presetMode;
            }
            assert(UserConfig::Presets::save(saved,path));
            Fixture f;f.first.user.gameMode=f.second.user.gameMode=6;
            f.first.user.preMove=1;f.second.user.preMove=0;
            f.first.user.das=.23;f.second.user.das=.34;f.second.user.arr=.007;
            f.first.user.fieldWidth=17;f.second.user.fieldWidth=13;
            App::Settings::PresetEditorSession editor;editor.preset.name="Combined draft";
            const auto directory=std::filesystem::temp_directory_path()/"termino_preset_scope_players";
            if(draft) {
                for(auto* user:{&f.first.user,&f.second.user}) {
                    user->presetDefined.emplace();
                    for(auto id:{Id::GameMode,Id::PreMove,Id::Das,Id::Arr}) UserConfig::Presets::define(*user,id);
                }
            } else {
                f.first.user.persistencePath=(directory/"one.ini").string();
                f.second.user.persistencePath=(directory/"two.ini").string();
                assert(UserConfig::save(f.first.user) && UserConfig::save(f.second.user));
            }
            const auto record="preset-"+std::to_string(saved.id);
            f.select_category("presets",selectedPlayer);
            auto& position=selectedPlayer ? f.app.playerTwoSettingsMenu : f.app.settingsMenu;
            position.path={"presets",record};position.selectedEntries["root/presets/"+record]="preset-use";
            std::vector<Press> presses{{right,"Press Enter again to apply this preset"},
                {enter,draft ? "Preset draft updated." : "Preset changes saved."}};
            if(draft) {
                presses.push_back({savePreset,"Preset saved."});presses.push_back({escape,{}});
                f.draft(editor,std::move(presses));
            } else {presses.push_back({retry,{}});f.run(std::move(presses));}
            const bool changesOne=presetMode==6 || selectedPlayer==0;
            const bool changesTwo=presetMode==6 || selectedPlayer==1;
            assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
            assert(f.first.user.preMove==(changesOne ? 0 : 1) && f.first.user.das==(changesOne ? .143 : .23));
            assert(f.second.user.preMove==(changesTwo ? 1 : 0) && f.second.user.arr==(changesTwo ? .005 : .007));
            assert(f.second.user.das==.34 && f.first.user.fieldWidth==17 && f.second.user.fieldWidth==13);
            assert(f.first.user.presets.layers.size()==(changesOne ? 1u : 0u));
            assert(f.second.user.presets.layers.size()==(changesTwo ? 1u : 0u));
            if(draft) {
                assert(std::get<double>(editor.preset.fields.at(Id::Das))==f.first.user.das);
                assert(editor.preset.playerTwoFields);
                assert(std::get<double>(editor.preset.playerTwoFields->at(Id::Arr))==f.second.user.arr);
            } else {
                UserConfig::Settings one,two;
                UserConfig::load(one,f.first.user.persistencePath);UserConfig::load(two,f.second.user.persistencePath);
                assert(UserConfig::Presets::encode(one.presets)==UserConfig::Presets::encode(f.first.user.presets));
                assert(UserConfig::Presets::encode(two.presets)==UserConfig::Presets::encode(f.second.user.presets));
                std::filesystem::remove_all(directory);
            }
            std::filesystem::remove(path);
        }
        for(int variant:{0,1,2}) for(bool promoted:{false,true}) {
            UserConfig::Presets::SavedPreset saved{0,"Enter Local Versus",{{Id::GameMode,6},{Id::PreMove,0},{Id::Das,.143}}};
            if(variant==0) saved.playerTwoFields=UserConfig::PresetFields{{Id::PreMove,1},{Id::Arr,.005}};
            if(variant==1) saved.playerTwoFields=UserConfig::PresetFields{};
            assert(UserConfig::Presets::save(saved,path));
            Fixture f;f.first.user.preMove=1;f.second.user.preMove=0;
            f.first.user.das=.23;f.second.user.das=.34;f.second.user.arr=.007;
            f.first.user.fieldWidth=17;f.second.user.fieldWidth=13;
            f.app.localVersusSettingsAvailable=true;
            if(promoted) {
                f.first.user.gameMode=6;f.second.user.gameMode=0;
                App::Settings::adopt_single_player_settings(f.second.context);
                assert(f.app.singlePlayerOwner==1 && f.app.savedPlayerOneSettings);
            }
            const auto record="preset-"+std::to_string(saved.id);
            f.select_category("presets");
            auto& position=promoted ? f.app.playerTwoSettingsMenu : f.app.settingsMenu;
            position.path={"presets",record};position.selectedEntries["root/presets/"+record]="preset-use";
            f.run({{right,"Press Enter again to apply this preset"},{enter,"Preset changes saved."},{retry,{}}});
            assert(f.first.user.gameMode==6 && f.second.user.gameMode==6 && f.app.singlePlayerOwner==0);
            assert(f.first.user.preMove==0 && f.first.user.das==.143);
            assert(f.second.user.preMove==(variant==0 ? 1 : 0));
            assert(f.second.user.das==(variant==2 ? .143 : .34));
            assert(f.second.user.arr==(variant==0 ? .005 : .007));
            assert(f.first.user.fieldWidth==17 && f.second.user.fieldWidth==13);
            assert(f.first.user.presets.layers.size()==1);
            assert(f.second.user.presets.layers.size()==(variant==1 ? 0u : 1u));
            std::filesystem::remove(path);
        }
        {
            UserConfig::Presets::SavedPreset saved{0,"Atomic Local preset",{{Id::GameMode,6},{Id::PreMove,0}}};
            saved.playerTwoFields=UserConfig::PresetFields{{Id::PreMove,1}};
            assert(UserConfig::Presets::save(saved,path));
            Fixture f;f.first.user.gameMode=f.second.user.gameMode=6;
            for(int i=0;i<256;++i) assert(UserConfig::Presets::apply(f.first.user,saved,true,false));
            const auto beforeOne=UserConfig::Presets::encode(f.first.user.presets);
            const auto beforeTwo=UserConfig::Presets::encode(f.second.user.presets);
            const auto value=f.second.user.preMove;
            const auto record="preset-"+std::to_string(saved.id);
            f.select_category("presets",1);f.app.playerTwoSettingsMenu.path={"presets",record};
            f.app.playerTwoSettingsMenu.selectedEntries["root/presets/"+record]="preset-use";
            f.run({{right,"Press Enter again to apply this preset"},
                {enter,"Preset action failed; existing data is unchanged."},{retry,{}}});
            assert(UserConfig::Presets::encode(f.first.user.presets)==beforeOne);
            assert(UserConfig::Presets::encode(f.second.user.presets)==beforeTwo && f.second.user.preMove==value);
            std::filesystem::remove(path);
        }
        {
            UserConfig::Presets::SavedPreset saved{0,"Editable preset",{{Id::PreMove,0},{Id::Das,.143}}};
            assert(UserConfig::Presets::save(saved,path));
            Fixture f;f.first.user.preMove=1;f.first.user.das=.237;
            const auto personal=UserConfig::Presets::draft_fields(f.first.user);
            const auto record="preset-"+std::to_string(saved.id);
            const auto selectEdit=[&] {
                f.select_category("presets");f.app.settingsMenu.path={"presets",record};
                f.app.settingsMenu.selectedEntries["root/presets/"+record]="preset-edit";
            };
            auto tree=App::Settings::Model::make_tree(f.first.user,false,0);
            const auto& entry=child(child(tree,"presets"),record);
            assert(entry.title=="#"+std::to_string(saved.id)+": "+saved.name && entry.literalTitle);
            selectEdit();
            f.run({{right,"Press Enter again to edit this preset",std::chrono::milliseconds(80)},
                {right,"Confirmation cancelled."},{retry,{}}});
            assert(UserConfig::Presets::find(saved.id,path)->fields==saved.fields);
            selectEdit();
            f.run({{right,"Press Enter again to edit this preset"},{enter,"> Rename preset"},
                {down,"> Save preset"},{down,"> Player"},{down,"> MODEL"},{down,"> HANDLING"},
                {right,"> SDF = Undefined"},{down,"> DAS = 0.143"},{down,"> ARR = Undefined"},
                {down,"> DCD = Undefined"},{down,"> Pre-move = OFF"},{right,"> Pre-move = ON"},
                {savePreset,"Preset saved."},{right,"> Pre-move = OFF"},
                {escape,"> Save and exit"},{down,"> Discard and exit"},{enter,"Preset editor closed."},{retry,{}}});
            UserConfig::Presets::Collection library;assert(UserConfig::Presets::load(library,path));
            assert(library.presets.size()==4 && library.nextId==5);
            saved=*UserConfig::Presets::find(saved.id,path);
            assert(saved.fields.size()==2 && std::get<int>(saved.fields.at(Id::PreMove))==1);
            assert(std::get<double>(saved.fields.at(Id::Das))==.143 && !saved.playerTwoFields);
            assert(UserConfig::Presets::draft_fields(f.first.user)==personal && f.first.user.presets.layers.empty());
            selectEdit();
            f.run({{right,"Press Enter again to edit this preset"},{enter,"> Rename preset"},
                {down,"> Save preset"},{down,"> Player"},{down,"> MODEL"},{down,"> HANDLING"},
                {right,"> SDF = Undefined"},{down,"> DAS = 0.143"},{down,"> ARR = Undefined"},
                {down,"> DCD = Undefined"},{down,"> Pre-move = ON"},
                {escape,"Preset editor closed."},{retry,{}}});
            // Deleting the library record must leave the applied snapshot
            // usable for undo, including after decoding its recovery state.
            assert(UserConfig::Presets::apply(f.first.user,saved));
            const auto recovery=UserConfig::Presets::encode(f.first.user.presets);
            f.select_category("presets");f.app.settingsMenu.path={"presets",record};
            f.app.settingsMenu.selectedEntries["root/presets/"+record]="preset-delete";
            f.run({{right,"Press Enter again to delete this preset",std::chrono::milliseconds(80)},
                {up,"> Edit preset"},{retry,{}}});
            assert(UserConfig::Presets::find(saved.id,path));
            f.app.settingsMenu.selectedEntries["root/presets/"+record]="preset-delete";
            f.run({{right,"Press Enter again to delete this preset"},{enter,"Preset deleted."},{retry,{}}});
            assert(!UserConfig::Presets::find(saved.id,path));
            assert(UserConfig::Presets::load(library,path) && library.presets.size()==3 && library.nextId==5);
            assert(UserConfig::Presets::encode(f.first.user.presets)==recovery);
            UserConfig::PresetState restored;assert(UserConfig::Presets::decode(recovery,restored));
            f.first.user.presets=std::move(restored);
            assert(UserConfig::Presets::undo(f.first.user));
            assert(UserConfig::Presets::draft_fields(f.first.user)==personal);
            std::filesystem::remove(path);
        }
        {
            UserConfig::Presets::SavedPreset saved{0,"Edit two players",{{Id::GameMode,6},{Id::PreMove,0},{Id::Das,.143}}};
            saved.playerTwoFields=UserConfig::PresetFields{{Id::GameMode,6},{Id::PreMove,1},{Id::Arr,.005}};
            assert(UserConfig::Presets::save(saved,path));
            Fixture f;f.first.user.gameMode=f.second.user.gameMode=6;
            f.first.user.preMove=1;f.second.user.preMove=0;
            const auto firstPersonal=UserConfig::Presets::draft_fields(f.first.user);
            const auto secondPersonal=UserConfig::Presets::draft_fields(f.second.user);
            const auto record="preset-"+std::to_string(saved.id);
            // Opening from P2 must still load the saved logical P1/P2 tables
            // independently, including each table's undefined parameters.
            f.select_category("presets",1);f.app.playerTwoSettingsMenu.path={"presets",record};
            f.app.playerTwoSettingsMenu.selectedEntries["root/presets/"+record]="preset-edit";
            f.run({{right,"Press Enter again to edit this preset"},{enter,"> Rename preset"},
                {down,"> Save preset"},{down,"> Edit target"},{down,"> MODEL"},{down,"> HANDLING"},
                {right,"> SDF = Undefined"},{down,"> DAS = 0.143"},{down,"> ARR = Undefined"},
                {down,"> DCD = Undefined"},{down,"> Pre-move = OFF"},{tab,"Edit target: Player 2"},
                {up,"> DCD = Undefined"},{up,"> ARR = 0.005"},{up,"> DAS = Undefined"},
                {down,"> ARR = 0.005"},{down,"> DCD = Undefined"},{down,"> Pre-move = ON"},
                {right,"> Pre-move = OFF"},{savePreset,"Preset saved."},
                {escape,"Preset editor closed."},{retry,{}}});
            const auto updated=UserConfig::Presets::find(saved.id,path);assert(updated);
            assert(updated->fields==saved.fields);
            (*saved.playerTwoFields)[Id::PreMove]=0;
            assert(updated->playerTwoFields==saved.playerTwoFields);
            assert(UserConfig::Presets::draft_fields(f.first.user)==firstPersonal);
            assert(UserConfig::Presets::draft_fields(f.second.user)==secondPersonal);
            std::filesystem::remove(path);
        }
        {
            Fixture f;f.first.user.preMove=1;f.first.user.das=.237;
            f.select_category("presets");f.app.settingsMenu.path={"presets"};
            f.app.settingsMenu.selectedEntries["root/presets"]="preset-new";
            f.run({{right,"> Rename preset"},{down,"> Save preset"},{down,"> Player"},{down,"> MODEL"},
                {down,"> HANDLING"},{right,"> SDF = Undefined"},{down,"> DAS = Undefined"},
                {down,"> ARR = Undefined"},{down,"> DCD = Undefined"},{down,"> Pre-move = Undefined"},
                {right,"> Pre-move = OFF"},{savePreset,"Preset saved."},{undefined,"> Pre-move = Undefined"},
                {escape,"> Save and exit"},{up,"> Keep editing"},{enter,"> Pre-move = Undefined"},
                {savePreset,"Preset saved."},{escape,"> New preset"},{retry,{}}});
            UserConfig::Presets::Collection library;assert(UserConfig::Presets::load(library,path));
            assert(library.presets.size()==4 && library.presets.back().fields.empty());
            assert(f.first.user.preMove==1 && f.first.user.das==.237 && !f.first.user.presetDefined);
        }
        {
            Fixture f;App::Settings::PresetEditorSession editor;
            f.first.user.fieldWidth=17;editor.original=f.first.user;editor.preset.name="Scoped reset";editor.baselineName=editor.preset.name;
            f.first.user.presetDefined.emplace();f.first.user.fieldWidth=13;f.first.user.das=.237;
            UserConfig::Presets::define(f.first.user,Id::FieldWidth);UserConfig::Presets::define(f.first.user,Id::Das);
            f.select_category("GAME");f.app.settingsMenu.path={"GAME","playfield"};
            f.app.settingsMenu.selectedEntries["root/GAME/playfield"]="reset";
            f.draft(editor,{{right,"> All undefined"},{down,"> Use original player settings"},{enter,"RESET complete. Preset draft updated."},
                {right,"> All undefined"},{down,"> Use original player settings"},{down,"> Use default settings"},
                {enter,"Width = 10"},{right,"> All undefined"},{up,"> Cancel reset"},{enter,"RESET cancelled."},
                {right,"> All undefined"},{enter,"Width = Undefined"},{savePreset,"Preset saved."},{escape,{}}});
            assert(!UserConfig::Presets::defined(f.first.user,Id::FieldWidth) && f.first.user.das==.237);
            assert(UserConfig::Presets::defined(f.first.user,Id::Das));
            assert(editor.preset.fields.size()==1 && std::get<double>(editor.preset.fields.at(Id::Das))==.237);
        }
        {
            Fixture f;f.select_category("presets");f.app.settingsMenu.path={"presets"};
            f.app.settingsMenu.selectedEntries["root/presets"]="preset-new";
            f.run({{right,"> Rename preset"},{up,"> RESET"},{up,"> Presets"},
                {right,"> Undo latest layer = None"},{down,"> Undo all presets"},
                {down,"> Keep current values"},{down,"> Applied presets"},{down,"> #1: Classic blocks"},
                {right,"> Use preset"},{right,"Press Enter again to apply this preset"},{enter,"Preset draft updated."},
                {savePreset,"Preset saved."},{escape,"> New preset"},{retry,{}}});
            UserConfig::Presets::Collection library;assert(UserConfig::Presets::load(library,path));
            assert(library.presets.size()==6);
            assert(std::get<int>(library.presets.back().fields.at(Id::RotationSystem))==9);
            assert(!library.presets.back().fields.count(Id::AutoplayMode) && f.first.user.presets.layers.empty());
        }
#ifdef __linux__
        {
            EmptyStdin stdin;
            Fixture f;App::Settings::PresetEditorSession editor;editor.preset.name="Original";
            f.first.user.presetDefined.emplace();
            auto tree=App::Settings::Model::make_tree(f.first.user,false,0);
            App::Settings::TerminalSettingsView view(f.first.context,false,0);
            view.set_menu_path(tree,{},0);
            const std::string name="自建 S 预设\n";
            assert(write(stdin.descriptors[1],name.data(),name.size())==static_cast<ssize_t>(name.size()));
            assert(App::Settings::rename_preset(f.first.context,view,editor));
            assert(editor.preset.name=="自建 S 预设");
            const char cancel=27;assert(write(stdin.descriptors[1],&cancel,1)==1);
            assert(App::Settings::rename_preset(f.first.context,view,editor) && editor.preset.name=="自建 S 预设");
        }
        {
            const auto maps=std::filesystem::temp_directory_path()/"termino_preset_editor_maps.txt";
            std::filesystem::remove(maps);
            const auto* oldMap=std::getenv("TETR_MAP_FILE");const std::string previousMap=oldMap ? oldMap : "";
            setenv("TETR_MAP_FILE",maps.c_str(),1);
            Fixture f;f.select_category("presets");f.app.settingsMenu.path={"presets"};
            f.app.settingsMenu.selectedEntries["root/presets"]="preset-new";
            f.run({{right,"> Rename preset"},{up,"> RESET"},{up,"> Presets"},{up,"> Maps"},
                {right,"> New map"},{right,"Press Enter again to confirm this map option"},
                {enter,"MAP EDITOR"},{KEY_1,"Brush: G"},{savePreset,"Map saved:"},
                {escape,"Map editor closed."},{escape,"> New preset"},{retry,{}}});
            UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection,maps));
            assert(collection.maps.size()==1 && collection.maps.front().rows.back().front()=='G');
            assert(f.first.user.mapId==0);
            if(oldMap) setenv("TETR_MAP_FILE",previousMap.c_str(),1);else unsetenv("TETR_MAP_FILE");
            std::filesystem::remove(maps);
        }
#endif
        {
            Fixture f;f.first.user.preMove=f.second.user.preMove=1;
            f.select_category("presets");f.app.settingsMenu.path={"presets"};
            f.app.settingsMenu.selectedEntries["root/presets"]="preset-new";
            f.run({{right,"> Rename preset"},{down,"> Save preset"},{down,"> Player"},{down,"> MODEL"},
                {right,"> Model = Undefined"},{right,"Select Model"},
                {down,"> QuickPlay"},{down,"> Multiplayer"},{down,"> Local Versus"},{enter,"Edit target: Player 1"},
                {left,"> MODEL"},{up,"> Edit target"},{right,"> Player 1"},{down,"> Player 2"},
                {enter,"Edit target: Player 2"},{down,"> MODEL"},{down,"> HANDLING"},{right,"> SDF = Undefined"},
                {down,"> DAS = Undefined"},{down,"> ARR = Undefined"},{down,"> DCD = Undefined"},
                {down,"> Pre-move = Undefined"},{right,"> Pre-move = OFF"},{savePreset,"Preset saved."},
                {tab,"Pre-move = Undefined | OFF"},{undefined,"> Pre-move = Undefined"},
                {tab,"Edit target: Player 1"},{right,"> Pre-move = OFF"},{tab,"Edit target: Player 2"},
                {escape,"> Save and exit"},{down,"> Discard and exit"},{enter,"> New preset"},{retry,{}}});
            UserConfig::Presets::Collection library;assert(UserConfig::Presets::load(library,path));
            const auto saved=library.presets.back();
            assert(std::get<int>(saved.fields.at(Id::GameMode))==6 && !saved.fields.count(Id::PreMove));
            assert(saved.playerTwoFields && std::get<int>(saved.playerTwoFields->at(Id::PreMove))==0);
            assert(f.first.user.gameMode==0 && f.second.user.gameMode==0 && f.first.user.preMove==1 && f.second.user.preMove==1);
            f.select_category("presets");f.app.settingsMenu.path={"presets"};
            f.app.settingsMenu.selectedEntries["root/presets"]="preset-"+std::to_string(saved.id);
            f.run({{right,"> Use preset"},{right,"Press Enter again to apply this preset"},{enter,"Preset changes saved."},
                {tab,"Edit target: Player 2"},{retry,{}}});
            assert(f.first.user.gameMode==6 && f.second.user.gameMode==6);
            assert(f.first.user.preMove==1 && f.second.user.preMove==0);
        }
        {
            Fixture f;App::Settings::PresetEditorSession editor;
            f.first.user.gameMode=f.second.user.gameMode=6;
            f.first.user.fieldWidth=17;f.second.user.fieldWidth=23;
            editor.original=f.first.user;editor.originalTwo=f.second.user;
            editor.preset.name="Both reset";editor.baselineName=editor.preset.name;
            f.first.user.presetDefined.emplace();f.second.user.presetDefined.emplace();
            UserConfig::Presets::define(f.first.user,Id::GameMode);UserConfig::Presets::define(f.second.user,Id::GameMode);
            f.app.localVersusSettingsAvailable=true;
            f.select_category("GAME");f.app.settingsMenu.path={"GAME","playfield"};
            f.app.settingsMenu.selectedEntries["root/GAME/playfield"]="reset";
            f.draft(editor,{{tab,"Edit target: Player 2"},{right,"> All undefined"},
                {down,"> Use original player settings"},{enter,"Width = 23"},
                {tab,"Width = Undefined | 23"},{right,"> All undefined"},{down,"> Use original player settings"},
                {enter,"Width = 17 | 23"},{savePreset,"Preset saved."},{tab,"Edit target: Player 1"},
                {pageUp,"> Width = 17"},{down,"> Height = 20"},{down,"> Invisible = OFF"},{right,"> Invisible = Delayed invisibility"},
                {tab,"Edit target: Player 2"},{escape,"> Save and exit"},{down,"> Discard and exit"},{enter,{}}});
            assert(f.first.user.fieldWidth==17 && f.second.user.fieldWidth==23);
            assert(f.first.user.fieldInvisible==1 && f.second.user.fieldInvisible==0);
            assert(editor.dirty(f.second.context)); // P1 changes still count when P2 is displayed.
            assert(editor.preset.playerTwoFields && std::get<int>(editor.preset.playerTwoFields->at(Id::FieldWidth))==23);
        }
        {
            Fixture f;App::Settings::PresetEditorSession editor;
            f.first.user.gameMode=f.second.user.gameMode=6;f.first.user.preMove=1;f.second.user.preMove=0;
            f.first.user.presetDefined.emplace();f.second.user.presetDefined.emplace();
            for(auto* user:{&f.first.user,&f.second.user}) {
                UserConfig::Presets::define(*user,Id::GameMode);UserConfig::Presets::define(*user,Id::PreMove);
            }
            editor.preset.name="Mode round trip";editor.baselineName=editor.preset.name;
            f.app.localVersusSettingsAvailable=true;
            f.select_category("MODEL",1);f.app.playerTwoSettingsMenu.path={"MODEL"};
            f.draft(editor,{{right,"Select Model"},{up,"> Multiplayer"},{up,"> QuickPlay"},{up,"> Zen"},{enter,"> Model = Zen"},{savePreset,"Preset saved."},{escape,{}}});
            assert(f.app.singlePlayerOwner==1 && f.app.savedPlayerOneSettings);
            assert(std::get<int>(editor.preset.fields.at(Id::GameMode))==0);
            assert(editor.preset.playerTwoFields && std::get<int>(editor.preset.playerTwoFields->at(Id::GameMode))==0);
            f.select_category("MODEL");
            f.draft(editor,{{right,"Select Model"},{down,"> QuickPlay"},{down,"> Multiplayer"},
                {down,"> Local Versus"},{enter,"Edit target: Player 1"},{savePreset,"Preset saved."},{escape,{}}});
            assert(f.app.singlePlayerOwner==0 && f.first.user.gameMode==6 && f.second.user.gameMode==6);
            assert(f.first.user.preMove==1 && f.second.user.preMove==0);
            assert(std::get<int>(editor.preset.fields.at(Id::PreMove))==1);
            assert(editor.preset.playerTwoFields && std::get<int>(editor.preset.playerTwoFields->at(Id::PreMove))==0);
        }
#ifdef _WIN32
        _putenv_s("TETR_PRESET_FILE",previous.c_str());
#else
        if(prior) setenv("TETR_PRESET_FILE",previous.c_str(),1);else unsetenv("TETR_PRESET_FILE");
#endif
        std::filesystem::remove(path);
    }
    std::cerr<<"settings_menu_test: OK\n";
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
