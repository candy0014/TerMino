#include "tetr/app/application_state.h"
#include "tetr/app/start_menu.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/rotation_systems.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/session_state.h"
#include "tetr/util/terminal_text.h"
#include "../src/app/settings_view.h"
#include "tetr/runtime/map_definition.h"
#include "../src/app/settings_input.h"
#include "../src/app/settings_presets.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include <unistd.h>
#include <linux/input.h>
#endif

namespace {
const App::Settings::TerminalSettingsView::TerminalSizeQuery unknownTerminalSize=[] {
    return std::optional<Render::TerminalSize>{};
};
std::pair<int,int> position_of(const std::string& output, const std::string& text) {
    Test::TerminalScreen screen;
    screen.feed(output);
    const auto at=screen.position_of(text);
    assert(at.first!=0);
    return at;
}
std::size_t count_token(const std::string& text, const std::string& token) {
    std::size_t count=0, at=0;
    while ((at=text.find(token,at))!=std::string::npos) { ++count; at+=token.size(); }
    return count;
}
std::string foreground_at(const std::string& output, const std::string& text) {
    const auto at=output.find(text);
    assert(at!=std::string::npos);
    const auto color=output.rfind("\x1b[38;2;",at);
    assert(color!=std::string::npos);
    return output.substr(color,output.find('m',color)-color+1);
}
std::size_t last_box_width(const std::string& output) {
    const auto begin=output.rfind("┌"), end=output.find("┐",begin);
    assert(begin!=std::string::npos && end!=std::string::npos);
    return Util::TerminalText::width(output.substr(begin,end-begin+std::string("┐").size()));
}
int last_content_row(const std::string& output, Render::BoardTextLayout layout) {
    Test::TerminalScreen screen;
    screen.feed(output);
    int bottom=0;
    for (int row=1;row<=screen.rows();++row)
        for (int column=layout.column+1;column<=layout.column+layout.columns;++column) {
            const auto& glyph=screen.at(row,column).glyph;
            if (!glyph.empty() && glyph!=" ") bottom=row;
        }
    return bottom;
}
void draw_game_layout(Runtime::GameContext& context) {
    auto& player=context.session.player;
    Render::print_border(context,player);
    Render::print_message(context,player);
    if (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        Render::print_blighted(context,player);
        Render::print_altitude(context,player);
    } else Render::print_score(context,player);
}
void replay_footer_spacing() {
    // Compare the prompt with actual rendered rows, including optional footers.
    for (bool quickplay:{false,true}) for (int height:{4,20,32})
        for (int pieceHeight:{2,4}) for (int next:{0,5,9})
            for (bool warning:{false,true}) {
                Runtime::SessionState session;
                Runtime::RuntimeServices services;
                Runtime::GameContext context{session,services};
                Replay::ReplaySessionState replay;
                Input::Services input;
                UserConfig::Settings user;
                context.attach_replay(replay); context.attach_input(input);
                context.config=Runtime::normalize_user_config(user,quickplay
                    ? Runtime::GameMode::QuickPlay : Runtime::GameMode::Zen);
                const int retry=UserConfig::default_key_codes()[8];
                input.keyStateProvider=[&](int key) { return key==retry; };
                Input::refresh_bindings(input,UserConfig::default_key_codes());
                auto& player=session.player;
                player.gameplay.playfield.width=10;
                player.gameplay.playfield.visibleHeight=height;
                player.gameplay.playfield.activeHeight=height;
                player.gameplay.playfield.nextCount=next;
                player.gameplay.playfield.holdEnabled=true;
                player.presentation.margin=30;
                player.presentation.blockMaxHeight=pieceHeight;
                player.gameplay.quickPlay.blighted=warning;
                session.quickPlay.speed=warning ? 10 : 1;
                session.quickPlay.altitude=warning ? 98765.4 : 0;
                session.quickPlay.progress.windupUntil=warning ? 1e20 : 0;
                std::ostringstream output;
                auto* previous=std::cout.rdbuf(output.rdbuf());
                draw_game_layout(context);
                const int bottom=last_content_row(output.str(),Render::board_text_layout(context,player));
                output.str("");
                App::run_post_game_interaction(context,player);
                const auto prefix=App::UiText::language()==App::UiText::Language::Chinese ? "按保存回放键" : "Press";
                assert(position_of(output.str(),prefix).first==bottom+1);
                position_of(output.str(),"["+App::UiText::key_label(UserConfig::default_key_codes()[10])+"]");
                std::cout.rdbuf(previous);
            }
    // A shared prompt clears both fields' footers while NEXT remains to the side.
    Runtime::SessionState firstSession, secondSession;
    Runtime::RuntimeServices firstServices, secondServices;
    Runtime::GameContext first{firstSession,firstServices}, second{secondSession,secondServices};
    Replay::ReplaySessionState firstReplay, secondReplay;
    Input::Services firstInput, secondInput;
    first.attach_replay(firstReplay); first.attach_input(firstInput);
    second.attach_replay(secondReplay); second.attach_input(secondInput);
    Input::refresh_bindings(firstInput,UserConfig::default_key_codes());
    Input::refresh_bindings(secondInput,UserConfig::default_player_two_key_codes());
    const int secondRetry=UserConfig::default_player_two_key_codes()[8];
    firstInput.keyStateProvider=[](int) { return false; };
    secondInput.keyStateProvider=[&](int key) { return key==secondRetry; };
    firstSession.player.presentation.margin=30;
    secondSession.player.presentation.margin=90;
    for (auto* context:{&first,&second}) {
        auto& field=context->session.player.gameplay.playfield;
        field.width=10; field.visibleHeight=field.activeHeight=20;
        field.nextCount=context==&first ? 5 : 12;
        field.holdEnabled=true;
    }
    std::ostringstream output;
    auto* previous=std::cout.rdbuf(output.rdbuf());
    draw_game_layout(first); draw_game_layout(second);
    const int bottom=std::max(last_content_row(output.str(),Render::board_text_layout(first,firstSession.player)),
        last_content_row(output.str(),Render::board_text_layout(second,secondSession.player)));
    output.str("");
    App::run_local_versus_post_game_interaction(first,second);
    const auto prefix=App::UiText::language()==App::UiText::Language::Chinese ? "按任一玩家的保存回放键" : "Press";
    assert(position_of(output.str(),prefix).first==bottom+1);
    std::cout.rdbuf(previous);
}
void replay_save_key_labels() {
    const auto previousLanguage=App::UiText::language();
    for (auto language:{App::UiText::Language::Chinese,App::UiText::Language::English}) {
        App::UiText::set_language(language);
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context{session,services};
        Replay::ReplaySessionState replay;
        Input::Services input;
        context.attach_replay(replay);context.attach_input(input);
        auto keys=UserConfig::default_key_codes();
#ifdef _WIN32
        keys[8]=VK_F3;keys[10]='R';
        const int alternate=VK_F8;
#else
        keys[8]=KEY_F3;keys[10]=KEY_R;
        const int alternate=KEY_F8;
#endif
        std::array<std::vector<int>,Input::kBindableActionCount> extra;
        extra[10]={alternate};
        Input::refresh_bindings(input,keys,extra);
        input.keyStateProvider=[&](int key) { return key==keys[8]; };
        auto& player=session.player;
        player.presentation.margin=30;
        player.gameplay.playfield.width=10;
        player.gameplay.playfield.visibleHeight=player.gameplay.playfield.activeHeight=20;
        std::ostringstream output;
        auto* previous=std::cout.rdbuf(output.rdbuf());
        App::run_post_game_interaction(context,player);
        std::cout.rdbuf(previous);
        Test::TerminalScreen screen;screen.feed(output.str());
        assert(screen.text().find("[R / F8]")!=std::string::npos);
        assert(screen.text().find(App::UiText::tr("Press Save Replay "))!=std::string::npos);
        assert(screen.text().find("[S]")==std::string::npos);
    }
    App::UiText::set_language(previousLanguage);
}
void windows_key_names() {
    using App::UiText::key_label;
    using Platform=Input::KeyPlatform;
    const std::pair<int,const char*> cases[]={{0x30,"0"},{0x39,"9"},{0x41,"A"},{0x5a,"Z"},
        {0x60,"Numpad 0"},{0x69,"Numpad 9"},{0x70,"F1"},{0x79,"F10"},{0x7a,"F11"},{0x7b,"F12"},
        {0x7c,"F13"},{0x87,"F24"},{0xa0,"LShift"},{0xa1,"RShift"},{0xa2,"LCtrl"},{0xa3,"RCtrl"},
        {0xa4,"LAlt"},{0xa5,"RAlt"},{0x5b,"LWin"},{0x5c,"RWin"},{0x5d,"Menu"},
        {0x08,"Backspace"},{0x09,"Tab"},{0x14,"CapsLock"},{0x90,"NumLock"},{0x91,"ScrollLock"},
        {0x21,"PgUp"},{0x22,"PgDn"},{0x23,"End"},{0x24,"Home"},{0x2c,"PrintScreen"},
        {0x2d,"Insert"},{0x2e,"Delete"},{0xba,";"},{0xbb,"="},{0xbc,","},{0xbd,"-"},{0xbe,"."},
        {0xbf,"/"},{0xc0,"`"},{0xdb,"["},{0xdc,"\\"},{0xdd,"]"},{0xde,"'"},
        {0x6a,"Numpad *"},{0x6b,"Numpad +"},{0x6e,"Numpad ."},{0xad,"Mute"},{0xb3,"Media Play/Pause"}};
    for (const auto& [code,name]:cases) assert(key_label(code,Platform::Windows)==name);
    for (int code:{0x10,0x11,0x12}) assert(!Input::capture_key_code(code,Platform::Windows));
    for (int code:{0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0x0d,0x1b}) assert(Input::capture_key_code(code,Platform::Windows));
    for (const auto platform:{Platform::Windows,Platform::Evdev}) {
        const std::vector<int> reserved=platform==Platform::Windows
            ? std::vector<int>{0x0d,0x1b,0x09,0x25,0x26,0x27,0x28}
            : std::vector<int>{1,28,96,15,103,105,106,108};
        for (int key:reserved) for (auto action:Input::kBindableActions)
            assert(Input::reserved_binding_key(key,action,platform)==
                (action==Input::Action::Retry || action==Input::Action::OpenSettings));
    }
    assert(!Input::reserved_binding_key(0xa3,Input::Action::Retry,Platform::Windows));
#ifdef _WIN32
    static_assert(VK_LSHIFT==0xa0 && VK_RSHIFT==0xa1 && VK_LCONTROL==0xa2 && VK_RCONTROL==0xa3);
    static_assert(VK_LMENU==0xa4 && VK_RMENU==0xa5 && VK_RETURN==0x0d && VK_ESCAPE==0x1b);
#else
    assert(key_label(KEY_LEFTSHIFT)=="LShift" && key_label(KEY_RIGHTSHIFT)=="RShift");
    assert(key_label(KEY_LEFTCTRL)=="LCtrl" && key_label(KEY_RIGHTCTRL)=="RCtrl");
    assert(key_label(KEY_LEFTALT)=="LAlt" && key_label(KEY_RIGHTALT)=="RAlt");
    assert(key_label(KEY_F13)=="F13" && key_label(KEY_F24)=="F24");
    assert(key_label(KEY_1)=="1" && key_label(KEY_0)=="0");
    assert(key_label(KEY_KP1)=="Numpad 1" && key_label(KEY_KP0)=="Numpad 0");
    assert(Input::capture_key_code(KEY_KPENTER));
#endif
}
void platform_defaults_use_the_same_physical_keys() {
    const std::array<int,13> windowsOne{{37,39,40,32,38,90,65,67,82,80,83,85,113}};
    const std::array<int,13> linuxOne{{105,106,108,57,103,44,30,46,19,25,31,22,60}};
    const auto windowsTwo=windowsOne;
    const auto linuxTwo=linuxOne;
    for (std::size_t i=0;i<windowsOne.size();++i) {
        assert(App::UiText::key_label(windowsOne[i],Input::KeyPlatform::Windows)==
            App::UiText::key_label(linuxOne[i],Input::KeyPlatform::Evdev));
        assert(App::UiText::key_label(windowsTwo[i],Input::KeyPlatform::Windows)==
            App::UiText::key_label(linuxTwo[i],Input::KeyPlatform::Evdev));
    }
#ifdef _WIN32
    assert(UserConfig::default_key_codes()==windowsOne);
    assert(UserConfig::default_player_two_key_codes()==windowsTwo);
#else
    assert(UserConfig::default_key_codes()==linuxOne);
    assert(UserConfig::default_player_two_key_codes()==linuxTwo);
#endif
}
bool proper_name(const std::string& name) {
    return name.empty() || name == "中文" || name == "English" || name == "SRS+" || name == "SRS" || name == "ARS" || name == "M123" ||
        std::any_of(Runtime::kBagRandomizers.begin(), Runtime::kBagRandomizers.end(), [&](const auto& option) { return option.label == name; }) ||
        std::any_of(Runtime::kRotationSystems.begin(), Runtime::kRotationSystems.end(), [&](const auto& option) { return option.label == name; }) ||
        std::any_of(Runtime::kSpinBonuses.begin(), Runtime::kSpinBonuses.end(), [&](const auto& option) { return option.label == name; }) ||
        name.find("bot_") == 0 || name.find("zzztoj_") == 0;
}
void translated_tree(const App::Settings::Model::SettingNode& node) {
    if(node.kind==App::Settings::Model::NodeKind::Separator) return;
    using App::UiText::tr;
    // Library titles already combine the stable ID and localized built-in
    // name, or a literal user name. They must not be translated a second time.
    if (!node.literalTitle) assert(tr(node.title) != node.title);
    if (!node.description.empty() && !node.literalDescription) assert(tr(node.description) != node.description);
    if (node.item) {
        for (const auto& label : node.item->choice.labels)
            assert(proper_name(label) || tr(label) != label);
        if (!node.item->hint.empty()) assert(tr(node.item->hint) != node.item->hint);
    }
    for (const auto& child : node.children) translated_tree(child);
}
void preset_library_rendering() {
    using namespace App::Settings;
    using namespace UserConfig;
    const auto path=std::filesystem::temp_directory_path()/"termino_preset_library_presentation.txt";
    std::filesystem::remove(path);
    const auto* prior=std::getenv("TETR_PRESET_FILE");const std::string previous=prior ? prior : "";
#ifdef _WIN32
    _putenv_s("TETR_PRESET_FILE",path.string().c_str());
#else
    setenv("TETR_PRESET_FILE",path.c_str(),1);
#endif
    // A user name matching a translation key must remain exactly as entered.
    Presets::SavedPreset custom{0,"Presets",{{SettingId::PreMove,1}}};
    assert(Presets::save(custom,path));
    Runtime::SessionState session;Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};Settings user;
    context.attach_user_settings(user);
    const auto oldLanguage=App::UiText::language();
    std::ostringstream output;auto* oldOutput=std::cout.rdbuf(output.rdbuf());
    for(const auto language:{App::UiText::Language::Chinese,App::UiText::Language::English}) {
        App::UiText::set_language(language);
        auto menu=make_preset_menu(user,false);
        const auto expected="#1: "+App::UiText::tr("Classic blocks");
        assert(menu.children[5].literalTitle && menu.children[5].title==expected);
        assert(menu.children.back().title=="#"+std::to_string(custom.id)+": Presets");
        for(const auto& record:menu.children) if(record.presetId) {
            assert(record.children.size()==3);
            assert(record.children[0].title=="Use preset");
            assert(record.children[1].title=="Edit preset" && record.children[1].confirmTwice);
            assert(record.children[2].title=="Delete preset" && record.children[2].confirmTwice);
        }
        output.str("");TerminalSettingsView view(context,false,0,false,{},unknownTerminalSize);
        view.set_menu_path(menu,{},5);view.print_menu(menu,5,"SETTINGS > Presets");
        Test::TerminalScreen screen;screen.feed(output.str());
        assert(screen.text().find(expected)!=std::string::npos);
        assert(screen.text().find("#"+std::to_string(custom.id)+": Presets")!=std::string::npos);
    }
    std::cout.rdbuf(oldOutput);App::UiText::set_language(oldLanguage);
#ifdef _WIN32
    _putenv_s("TETR_PRESET_FILE",previous.c_str());
#else
    if(prior) setenv("TETR_PRESET_FILE",previous.c_str(),1);else unsetenv("TETR_PRESET_FILE");
#endif
    std::filesystem::remove(path);
}
void incremental_rendering() {
    using namespace App::Settings;
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    UserConfig::Settings user;
    context.attach_user_settings(user);
    user.keyCodes=UserConfig::default_key_codes();
    auto root=Model::make_tree(user,false,0);
    TerminalSettingsView view(context,false,0,false,{},unknownTerminalSize);
    Test::TerminalScreen actual;
    std::ostringstream output;
    auto* previous=std::cout.rdbuf(output.rdbuf());
    const auto capture = [&](auto draw) {
        output.str("");
        draw();
        return output.str();
    };
    const auto update = [&](auto draw, bool initial=false) {
        const auto bytes=capture([&] { draw(view); });
        if (!initial) assert(bytes.find("\x1b[2J")==std::string::npos);
        actual.feed(bytes);
        assert((actual.background==Test::TerminalScreen::Color{{12,12,12}}));
        return bytes;
    };
    int comparison=0;
    const auto matches_fresh = [&](auto draw) {
        ++comparison;
        TerminalSettingsView fresh(context,false,0,false,{},unknownTerminalSize);
        Test::TerminalScreen expected;
        expected.feed(capture([&] { draw(fresh); }));
        const int rows=std::max(actual.rows(),expected.rows());
        const int columns=std::max(actual.columns(),expected.columns());
        for (int row=1; row<=rows; ++row)
            for (int column=1; column<=columns; ++column) {
                const auto a=actual.at(row,column), b=expected.at(row,column);
                if (!(a.glyph==b.glyph && a.continuation==b.continuation && a.background==b.background))
                    std::cerr<<"Screen comparison "<<comparison<<" at "<<row<<","<<column
                        <<": actual ["<<a.glyph<<"], expected ["<<b.glyph<<"]\n";
                assert(a.glyph==b.glyph && a.continuation==b.continuation && a.background==b.background);
                if (a.glyph!=" " && !a.glyph.empty()) assert(a.foreground==b.foreground);
            }
    };
    const auto root_menu = [&](auto& target) {
        target.set_menu_path(root,{},3);
        target.print_menu(root,3,"SETTINGS");
    };
    // A previous editor's background must be reset before even the first erase.
    Render::set_rgb(80,235,255,20,52,64);
    update(root_menu,true);
    matches_fresh(root_menu);
    const auto field_menu = [&](auto& target) {
        target.set_menu_path(root,{"GAME","playfield"},0);
        target.print_menu(root.children[3].children[0],0,"SETTINGS > GAME > Playfield");
    };
    update(field_menu);
    matches_fresh(field_menu);
    const auto numeric = [&](const std::string& value, std::size_t caret) {
        return [&,value,caret](auto& target) {
            target.set_menu_path(root,{"GAME","playfield"},0);
            target.print_scalar_input(*root.children[3].children[0].children[0].item,
                "SETTINGS > GAME > Playfield",value,caret);
        };
    };
    update(numeric("",0));
    matches_fresh(numeric("",0));
    const auto inputPosition=actual.position_of("> 场地宽度 = ");
    assert(inputPosition.first!=0);
    assert((actual.at(inputPosition.first,inputPosition.second).foreground==Test::TerminalScreen::Color{{255,188,92}}));
    for (const auto& value : {std::string("1"),std::string("12"),std::string("19"),std::string("1")}) {
        const auto bytes=update(numeric(value,value.size()));
        assert(bytes.size()<256 && bytes.find("┌")==std::string::npos);
        assert(bytes.find("场地宽度")==std::string::npos && bytes.find("游戏机制")==std::string::npos);
        matches_fresh(numeric(value,value.size()));
    }
    const auto caretOnly=update(numeric("1",0));
    assert(caretOnly.find("场地宽度")==std::string::npos && caretOnly.find("┌")==std::string::npos);
    assert(actual.row==inputPosition.first);
    assert(actual.column==inputPosition.second+static_cast<int>(Util::TerminalText::width("> 场地宽度 = ")));
    update(numeric(std::string(80,'9'),20));
    matches_fresh(numeric(std::string(80,'9'),20));
    update(numeric("17",2));
    matches_fresh(numeric("17",2));
    user.fieldWidth=17;
    root=Model::make_tree(user,false,0);
    update(field_menu);
    matches_fresh(field_menu);

    const auto choices = [&](std::size_t selected) {
        return [&,selected](auto& target) {
            target.set_menu_path(root,{"MODEL"},0);
            target.print_choices(*root.children[1].children[0].item,selected,"SETTINGS > MODEL");
        };
    };
    update(choices(0));
    matches_fresh(choices(0));
    const auto choiceMove=update(choices(1));
    assert(choiceMove.find("┌")==std::string::npos && choiceMove.find("配置")==std::string::npos);
    matches_fresh(choices(1));
    auto paged=root.children[5];
    while (paged.children.size()<20) paged.children.push_back(paged.children.front());
    const auto controls = [&](std::size_t selected) {
        return [&,selected](auto& target) {
            target.set_menu_path(root,{"CONTROLS"},selected);
            target.print_menu(paged,selected,"SETTINGS > CONTROLS");
        };
    };
    update(controls(0));
    update(controls(15));
    matches_fresh(controls(15));
    update(controls(0));
    matches_fresh(controls(0));

    const auto pieces = [&](auto& target) {
        target.set_menu_path(root,{"GAME","pieces"},1);
        target.print_menu(root.children[3].children[1],1,"SETTINGS > GAME > Pieces");
    };
    update(pieces);
    user.holdEnabled=0;
    root=Model::make_tree(user,false,0);
    update(pieces);
    matches_fresh(pieces);
    context.config.display.obscuredColors=true;
    update(pieces);
    matches_fresh(pieces);
    const auto hiddenSelection=actual.position_of("> 允许暂存 = 关闭");
    assert(hiddenSelection.first!=0);
    assert((actual.at(hiddenSelection.first,hiddenSelection.second).foreground==Test::TerminalScreen::Color{{54,57,62}}));
    assert((actual.at(hiddenSelection.first,hiddenSelection.second).background==Test::TerminalScreen::Color{{0,0,0}}));
    assert((actual.foreground==Test::TerminalScreen::Color{{0,0,0}}));
    context.config.display.obscuredColors=false;
    App::UiText::set_language(App::UiText::Language::English);
    update(pieces);
    matches_fresh(pieces);
    App::UiText::set_language(App::UiText::Language::Chinese);
    std::cout.rdbuf(previous);
}
void dynamic_settings_viewport() {
    using namespace App::Settings;
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    UserConfig::Settings user;
    context.attach_user_settings(user);
    auto settings=Model::make_tree(user,false,0);
    const auto numeric=*settings.children[3].children[0].children[0].item;
    const auto choice=*settings.children[1].children[0].item;
    const auto network=std::find_if(settings.children.begin(),settings.children.end(),
        [](const auto& node) { return node.section==UserConfig::Section::Network; });
    assert(network!=settings.children.end());
    const auto text=*network->children[0].item;
    assert(Model::uses_inline_input(text));
    Network::NetworkServices connection;context.attach_network(connection);
    // Four narrow columns fit at 104 cells. At 92 cells the root's left
    // border and a wide glyph cross the viewport edge, while TAIL remains.
    Model::SettingNode root;
    root.title="ROOT";
    root.children.resize(1);
    auto& one=root.children[0];
    one.key="one"; one.title="ONE e\xcc\x81中文TAIL";
    one.children.resize(1);
    auto& two=one.children[0];
    two.key="two"; two.title="LEVEL TWO";
    two.children.resize(1);
    auto& three=two.children[0];
    three.key="three"; three.title="LEVEL THREE";
    three.children.push_back(settings.children[3].children[0].children[0]);
    const std::vector<std::string> path{"one","two","three"};
    std::optional<Render::TerminalSize> size=Render::TerminalSize{104,80};
    const auto query=[&] { return size; };
    auto surface=make_settings_surface();
    TerminalSettingsView view(context,false,0,false,surface,query);
    std::ostringstream output;
    auto* previous=std::cout.rdbuf(output.rdbuf());
    Test::TerminalScreen actual;
    const auto capture=[&](auto draw,auto& target) {
        output.str("");
        target.set_menu_path(root,path,0);
        draw(target);
        return output.str();
    };
    const auto update=[&](auto draw) {
        const auto bytes=capture(draw,view);
        assert(bytes.find("⬅️")==std::string::npos);
        actual.feed(bytes);
        assert(!view.terminal_size_changed());
        TerminalSettingsView fresh(context,false,0,false,{},query);
        Test::TerminalScreen expected;
        expected.feed(capture(draw,fresh));
        for (int row=1;row<=std::max(actual.rows(),expected.rows());++row)
            for (int column=1;column<=std::max(actual.columns(),expected.columns());++column) {
                const auto a=actual.at(row,column),e=expected.at(row,column);
                assert(a.glyph==e.glyph && a.continuation==e.continuation && a.background==e.background);
                if (a.glyph!=" " && !a.glyph.empty()) assert(a.foreground==e.foreground);
                if (size && (column<=2 || column>size->columns-8))
                    assert(a.glyph==" " && !a.continuation);
            }
        if (size) assert(actual.columns()<=size->columns-8);
        return bytes;
    };
    const auto menu=[&](auto& target) { target.print_menu(three,0,"ROOT"); };
    update(menu);
    assert(count_token(actual.text(),"┌")==4);
    assert(actual.position_of("┌").second==3);
    size=Render::TerminalSize{92,80};
    assert(view.terminal_size_changed());
    assert(update(menu).find("\x1b[2J")!=std::string::npos);
    assert(count_token(actual.text(),"┌")==3);
    const auto tailRow=actual.position_of("┌").first+1;
    assert(actual.at(tailRow,3).glyph==" "); // Skip a half-visible wide glyph.
    for (int i=0;i<4;++i) assert(actual.at(tailRow,4+i).glyph==std::string("TAIL").substr(i,1));
    size=Render::TerminalSize{93,80};
    update(menu);
    assert(actual.at(tailRow,3).glyph=="文" && actual.at(tailRow,4).continuation);
    for (int i=0;i<4;++i) assert(actual.at(tailRow,5+i).glyph==std::string("TAIL").substr(i,1));
    for (int columns:{40,63,88,100,160}) {
        size=Render::TerminalSize{columns,80};
        update([&](auto& target) { target.print_choices(choice,1,"ROOT"); });
        assert(actual.position_of("> "+App::UiText::tr(choice.choice.labels[1])).first!=0);
        // Numeric input is wider than the viewport, includes UTF-8 and a
        // combining mark, and remains unchanged across repeated resizes.
        const std::string draft=std::string(65,'9')+"中文e\xcc\x81TAIL";
        update([&](auto& target) { target.print_scalar_input(numeric,"ROOT",draft,draft.size()); });
        for (int i=0;i<4;++i)
            assert(actual.at(actual.row,actual.column-4+i).glyph==std::string("TAIL").substr(i,1));
        assert(actual.column>=3 && actual.column<=columns-8);
        update([&](auto& target) { target.print_scalar_input(numeric,"ROOT",draft,0); });
        assert(actual.position_of("> 场地宽度 = ").first!=0);
        assert(actual.column>=3 && actual.column<=columns-8);
        update([&](auto& target) { target.print_edit(text,"ROOT",draft,draft.size()); });
        for (int i=0;i<4;++i)
            assert(actual.at(actual.row,actual.column-4+i).glyph==std::string("TAIL").substr(i,1));
        assert(actual.column>=3 && actual.column<=columns-8);
        update([&](auto& target) { target.print_edit(text,"ROOT",draft,0); });
        assert(actual.position_of("> "+App::UiText::tr(text.label)+" = ").first!=0);
        assert(actual.position_of(App::UiText::tr("New value: ")).first==0);
        assert(actual.column>=3 && actual.column<=columns-8);
        for (const auto& status:{std::string{"Disconnected"},std::string{"Server full (2 players)."},
            std::string{"Connected to wss://termino.candy0014.icu/ws"},
            std::string{"Server unavailable. Reconnect or edit Server IP."}}) {
            connection.connectionMessage=status;
            auto inlineNetwork=three;inlineNetwork.section=UserConfig::Section::Network;
            update([&](auto& target) {target.print_menu(inlineNetwork,0,"ROOT");});
            assert(actual.position_of("┌ "+App::UiText::tr("Connection status")).first==0);
            assert(count_token(actual.text(),"┌")<=4);
        }
    }
    size.reset();
    assert(view.terminal_size_changed());
    update(menu);
    assert(count_token(actual.text(),"┌")==3);
    for (int column=1;column<=5;++column)
        assert(actual.at(tailRow,column).glyph==" "); // Unknown size: empty frame sliver.
    // Re-created views share the dimensions of the last painted surface.
    TerminalSettingsView next(context,false,0,false,surface,query);
    assert(!next.terminal_size_changed());
    size=Render::TerminalSize{104,81};
    assert(next.terminal_size_changed());
    std::cout.rdbuf(previous);
}
}
void spin_bonus_hints() {
    using namespace App::Settings;
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    UserConfig::Settings user;
    context.attach_user_settings(user);
    auto root=Model::make_tree(user,false,0);
    const auto game=std::find_if(root.children.begin(),root.children.end(),[](const auto& node){return node.key=="GAME";});
    assert(game!=root.children.end());
    const auto attack=std::find_if(game->children.begin(),game->children.end(),[](const auto& node){return node.key=="attack-rules";});
    assert(attack!=game->children.end());
    const auto spin=std::find_if(attack->children.begin(),attack->children.end(),[](const auto& node){return node.item && node.item->id==UserConfig::SettingId::SpinBonuses;});
    assert(spin!=attack->children.end());
    assert(spin->item->choice.labels.size()==10);
    const auto compact=[](std::string text) {
        text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char c){return c==' ' || c=='\n' || c=='\r' || c=='\t';}),text.end());
        return text;
    };
    std::ostringstream output;
    auto* previous=std::cout.rdbuf(output.rdbuf());
    for (const auto language:{App::UiText::Language::English,App::UiText::Language::Chinese}) {
        App::UiText::set_language(language);
        TerminalSettingsView view(context,false,0,true,{},unknownTerminalSize);
        view.set_menu_path(root,{"GAME","attack-rules"},0);
        view.print_menu(*attack,static_cast<std::size_t>(spin-attack->children.begin()),"SETTINGS > GAME > Attack rules");
        Test::TerminalScreen screen;
        screen.feed(output.str()); output.str("");
        for (std::size_t i=0;i<Runtime::kSpinBonuses.size();++i) {
            view.print_choices(*spin->item,i,"SETTINGS > GAME > Attack rules > Spin bonuses");
            assert(output.str().find("\x1b[2J")==std::string::npos);
            screen.feed(output.str()); output.str("");
            assert(compact(screen.text()).find(compact(App::UiText::tr(Runtime::kSpinBonuses[i].hint)))!=std::string::npos);
            const auto selection=screen.position_of("> "+std::string(Runtime::kSpinBonuses[i].label));
            assert(selection.first!=0);
            assert((screen.at(selection.first,selection.second).background==Test::TerminalScreen::Color{{56,34,16}}));
        }
    }
    std::cout.rdbuf(previous);
    App::UiText::set_language(App::UiText::Language::Chinese);
}
void rotation_hints() {
    using namespace App::Settings;
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    UserConfig::Settings user; context.attach_user_settings(user);
    auto root=Model::make_tree(user,false,0);
    const auto& game=root.children[3];
    const auto& pieces=game.children[1];
    const auto rotation=std::find_if(pieces.children.begin(),pieces.children.end(),[](const auto& node){return node.item && node.item->id==UserConfig::SettingId::RotationSystem;});
    assert(rotation!=pieces.children.end());
    const auto compact=[](std::string text) {
        text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char c){return c==' ' || c=='\n' || c=='\r' || c=='\t';}),text.end());return text;
    };
    std::ostringstream output; auto* previous=std::cout.rdbuf(output.rdbuf());
    for(const auto language:{App::UiText::Language::Chinese,App::UiText::Language::English}) {
        App::UiText::set_language(language);
        TerminalSettingsView view(context,false,0,true,{},unknownTerminalSize);
        view.print_menu(pieces,static_cast<std::size_t>(rotation-pieces.children.begin()),"SETTINGS > GAME > Pieces");
        Test::TerminalScreen screen; screen.feed(output.str());output.str("");
        for(std::size_t i=0;i<Runtime::kRotationSystems.size();++i) {
            assert(rotation->item->choice.labels[i]==Runtime::kRotationSystems[i].label);
            view.print_choices(*rotation->item,i,"SETTINGS > GAME > Pieces");
            assert(output.str().find("\x1b[2J")==std::string::npos);
            screen.feed(output.str());output.str("");
            assert(compact(screen.text()).find(compact(App::UiText::tr(Runtime::kRotationSystems[i].hint)))!=std::string::npos);
            assert(screen.position_of("> "+std::string(Runtime::kRotationSystems[i].label)).first!=0);
        }
    }
    std::cout.rdbuf(previous);
    App::UiText::set_language(App::UiText::Language::Chinese);
}
void randomizer_hints() {
    using namespace App::Settings;
    Runtime::SessionState session;Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};UserConfig::Settings user;
    user.randomPieces=1;context.attach_user_settings(user);
    auto root=Model::make_tree(user,false,0);auto& pieces=root.children[3].children[1];
    const auto randomizer=std::find_if(pieces.children.begin(),pieces.children.end(),[](const auto& node){return node.item && node.item->id==UserConfig::SettingId::RandomPieces;});
    assert(randomizer!=pieces.children.end() && randomizer->title=="Random rules");
    assert(randomizer->item->choice.labels.size()==UserConfig::choice_options(UserConfig::ChoiceKind::BagRandomizer).labels.size() && !randomizer->item->choice.cycleImmediately);
    assert(Model::selected_choice(*randomizer->item)==7); // Old Random=1 remains TOTAL MAYHEM.
    const auto compact=[](std::string text) {
        text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char c){return c==' ' || c=='\n' || c=='\r' || c=='\t';}),text.end());return text;
    };
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    for(const auto language:{App::UiText::Language::Chinese,App::UiText::Language::English}) {
        App::UiText::set_language(language);TerminalSettingsView view(context,false,0,true,{},unknownTerminalSize);
        const auto at=static_cast<std::size_t>(randomizer-pieces.children.begin());
        view.print_menu(pieces,at,"SETTINGS > GAME > Pieces");
        Test::TerminalScreen screen;screen.feed(output.str());output.str("");
        for(std::size_t i=0;i<Runtime::kBagRandomizers.size();++i) {
            const auto& info=Runtime::kBagRandomizers[i];
            assert(randomizer->item->choice.labels[i]==info.label);
            view.print_choices(*randomizer->item,i,"SETTINGS > GAME > Pieces");
            assert(output.str().find("\x1b[2J")==std::string::npos);
            screen.feed(output.str());output.str("");
            assert(compact(screen.text()).find(compact(App::UiText::tr(info.hint)))!=std::string::npos);
            assert(screen.position_of("> "+std::string(info.label)).first!=0);
            Model::apply_choice(*randomizer->item,static_cast<int>(info.value));
            assert(Model::selected_choice(*randomizer->item)==i);
            view.print_menu(pieces,at,"SETTINGS > GAME > Pieces");
            screen.feed(output.str());output.str("");
            assert(screen.position_of("> "+App::UiText::tr("Random rules")+" = "+std::string(info.label)).first!=0);
        }
    }
    std::cout.rdbuf(previous);App::UiText::set_language(App::UiText::Language::Chinese);
}
void map_preview_layout() {
    using namespace App::Settings;
    App::UiText::set_language(App::UiText::Language::English);
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;context.attach_user_settings(user);
    auto small=std::make_shared<Runtime::MapDefinition>();Runtime::resize_map(*small,8,4);small->rows.back()="TTTTTTTT";
    small->goal=Runtime::MapGoal::AllClear;
    auto huge=std::make_shared<Runtime::MapDefinition>();Runtime::resize_map(*huge,100,100);huge->rows.back()=std::string(100,'I');
    Model::SettingNode menu{"maps","Maps",""};
    Model::SettingNode first{"map-1","First",""};first.mapPreview=small;
    Model::SettingNode second{"map-2","Second",""};second.mapPreview=huge;
    menu.children={first,second,Model::SettingNode{"map-disable","None",""}};
    std::optional<Render::TerminalSize> size=Render::TerminalSize{100,30};
    auto surface=make_settings_surface();TerminalSettingsView view(context,false,0,false,surface,[&]{return size;});
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    view.print_menu(menu,0,"Maps");
    Test::TerminalScreen screen;screen.feed(output.str());
    assert(screen.position_of("Map preview").second>screen.position_of("First").second);
    assert(screen.position_of("Field size: 8 x 4").first>screen.position_of("Map preview").first);
    assert(screen.position_of("Generator: Fixed rotation system").first && screen.position_of("Final goal: All clear field").first);
    const auto preview=screen.position_of("Map preview");
    const auto color=Render::board_cell_colors(Runtime::map_cell('T'),false).foreground;
    int filled=0;for(const auto& row:surface->shown) for(const auto& cell:row) if(cell.glyph=="█" && cell.color==color) ++filled;
    assert(filled==16); // Eight normal-width cells, with no compression.
    auto offset=output.str().size();view.print_selection(menu,0,1,"Maps");screen.feed(output.str().substr(offset));
    assert(screen.position_of("100 x 100").first && !screen.position_of("Map preview").first && !screen.position_of("Thumbnail").first);
    assert(!screen.position_of("HOLD").first && !screen.position_of("NEXT").first);
    for(const auto& row:surface->shown) assert(row.size()<=100);
    offset=output.str().size();view.print_selection(menu,1,2,"Maps");screen.feed(output.str().substr(offset));assert(!screen.position_of("Map preview").first);
    for(const auto terminal:{Render::TerminalSize{40,30},Render::TerminalSize{100,8}}) {
        size=terminal;offset=output.str().size();view.print_menu(menu,1,"Maps");screen.feed(output.str().substr(offset));
        assert(!screen.position_of("Map preview").first);
    }
    // Resizing changes eligibility even while the selected map stays put.
    for(const auto terminal:{Render::TerminalSize{54,30},Render::TerminalSize{53,30},Render::TerminalSize{100,30},Render::TerminalSize{100,20},Render::TerminalSize{100,40}}) {
        size=terminal;assert(view.terminal_size_changed());offset=output.str().size();view.print_menu(menu,0,"Maps");screen.feed(output.str().substr(offset));
        const bool fits=terminal.columns>=54 && terminal.rows>=21;
        assert((screen.position_of("Map preview").first!=0)==fits);
        if(fits) assert(screen.position_of("Final goal").first>preview.first+small->height);
    }
    Runtime::resize_map(*small,10,20);
    for(int rows:{30,40,30,40}) {
        size=Render::TerminalSize{100,rows};offset=output.str().size();view.print_menu(menu,0,"Maps");screen.feed(output.str().substr(offset));
        assert((screen.position_of("Map preview").first!=0)==(rows==40));
        if(rows==40) assert(screen.position_of("Field size").first>screen.position_of("Map preview").first+20);
        if(rows==40) {
            const auto before=surface->shown;
            // Confirmation is written by a newly constructed view, as in the menu.
            TerminalSettingsView message(context,false,0,false,surface,[&]{return size;});
            message.set_menu_path(menu,{},0);message.prepare_layout(menu.children.size(),"Maps");
            offset=output.str().size();message.print_message("Press Enter again to confirm this map. Arrow keys cancel.");screen.feed(output.str().substr(offset));
            assert(screen.position_of("Press Enter again").first>screen.position_of("Map preview").first+20);
            for(std::size_t row=4;row<26;++row) for(std::size_t col=26;col<std::min<std::size_t>(surface->shown[row].size(),before[row].size());++col)
                assert(surface->shown[row][col]==before[row][col]);
        }
    }
    std::cout.rdbuf(previous);App::UiText::set_language(App::UiText::Language::Chinese);
}
void map_editor_setting_viewport() {
    using namespace App::Settings;
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;context.attach_user_settings(user);
    const auto choices=UserConfig::choice_options(UserConfig::ChoiceKind::RotationSystem);
    Model::SettingItem rotation{Model::SettingId::RotationSystem,"Rotation System",std::ref(user.rotationSystem),{choices.labels,choices.values,false},false,false,&user};
    Model::SettingItem width{Model::SettingId::FieldWidth,"Width",std::ref(user.fieldWidth),{},false,false,&user};
    Model::SettingNode menu{"editor","EDITOR OPTIONS",""};
    Model::SettingNode node{"width","Width","",Model::NodeKind::Setting};node.item=width;menu.children={node};
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    for(const auto size:{Render::TerminalSize{96,30},Render::TerminalSize{44,20},Render::TerminalSize{26,14},Render::TerminalSize{16,10},Render::TerminalSize{8,6},Render::TerminalSize{1,1}}) {
        auto surface=make_settings_surface();TerminalSettingsView view(context,false,0,false,surface,[&]{return std::optional<Render::TerminalSize>{size};},true);
        view.set_menu_path(menu,{},0);view.print_choices(rotation,choices.values.size()-1,"EDITOR OPTIONS");
        assert(surface->shown.size()<=static_cast<std::size_t>(size.rows));
        for(const auto& row:surface->shown) assert(row.size()<=static_cast<std::size_t>(size.columns));
        view.print_scalar_input(width,"EDITOR OPTIONS","12",2);
        assert(surface->shown.size()<=static_cast<std::size_t>(size.rows));
        for(const auto& row:surface->shown) assert(row.size()<=static_cast<std::size_t>(size.columns));
    }
    std::cout.rdbuf(previous);
}
int main() {
    using namespace Util::TerminalText;
    using namespace App::Settings;
    static_assert(kVisibleSettingsRows==15);
    static_assert(kVisibleSettingsColumns==3);
    static_assert(first_visible_row(14)==0 && first_visible_row(15)==15);
    App::UiText::set_language(App::UiText::Language::Chinese);
    preset_library_rendering();
    windows_key_names();
    platform_defaults_use_the_same_physical_keys();
    replay_footer_spacing();
    map_preview_layout();
    map_editor_setting_viewport();
    replay_save_key_labels();
    incremental_rendering();
    dynamic_settings_viewport();
    spin_bonus_hints();
    rotation_hints();
    randomizer_hints();
    assert(width("配置 / PPS") == 10);
    assert(width("A中😀e\xcc\x81") == 6);
    assert(clip("配置界面", 7) == "配置...");
    assert(clip("中文路径.txt", 8) == "中文...");
    assert(clip("中文", 1) == ".");
    assert(clip(std::string("\xe4\xb8", 2), 8) == "\xef\xbf\xbd\xef\xbf\xbd");
    assert(clip("a\x1b[b", 20) == "a?[b");
    const std::string deep = "SETTINGS > GAME > GARBAGE > Garbage Shape > Playfield > Pieces > Timing > Player > Hole pattern";
    const auto localized = App::UiText::breadcrumb(deep);
    std::string joined;
    for (const auto& row : App::UiText::breadcrumb_rows(deep, 52)) { assert(width(row) <= 52); joined += row; }
    assert(joined == localized); // No ancestor is elided, even across wrapped rows.
    for (const auto& descriptor : UserConfig::setting_schema()) {
        assert(App::UiText::tr(descriptor.label) != descriptor.label);
    }
    assert(App::UiText::connection_status("Connected to 127.0.0.1") == "已连接 127.0.0.1");
    assert(App::UiText::connection_status("[send] invalid IPv4 address").find("无效") != std::string::npos);

    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};
    UserConfig::Settings user;
    Input::Services input;
    App::ApplicationState app;
    context.attach_user_settings(user); context.attach_input(input); context.attach_app(app);
    user.keyCodes = UserConfig::default_key_codes();
    input.keyStateProvider = [](int) { return false; };
    for (int mode = 0; mode <= 6; ++mode) {
        for (int garbage = 0; garbage <= 5; ++garbage) {
            for (int pattern = 1; pattern <= 3; ++pattern) {
                user.gameMode = mode; user.garbageMode = garbage; user.cheesePattern = pattern;
                user.autoplayMode = 9; user.simulationPps = 2; user.turnPieces = 1;
                translated_tree(Model::make_tree(user, mode == 6, 0));
            }
        }
    }
    user.gameMode = 0;user.gravityProgression=0;
    auto root = Model::make_tree(user, false, 0);
    std::ostringstream output;
    auto* old = std::cout.rdbuf(output.rdbuf());
    TerminalSettingsView view(context, false, 0, true,{},unknownTerminalSize);
    view.print_menu(root, 0, "SETTINGS");
    const auto rootOutput = output.str();
    assert(rootOutput.find("⬅️")==std::string::npos);
    assert(position_of(rootOutput,"> 玩家")==std::make_pair(6,7));
    assert(position_of(rootOutput,"  游戏模式")==std::make_pair(7,7));
    assert(rootOutput.find("\x1b[23;5H当前仅玩家") != std::string::npos);
    assert(rootOutput.find("服务器地址") == std::string::npos);
    assert(rootOutput.find("联机") != std::string::npos); // Entire root fits without paging.
    output.str(""); view.invalidate();
    view.print_menu(root.children.front(), 0, deep);
    Test::TerminalScreen breadcrumbScreen;
    breadcrumbScreen.feed(output.str());
    for (const auto& row : App::UiText::breadcrumb_rows(deep, 52)) assert(breadcrumbScreen.text().find(row) != std::string::npos);
    output.str(""); view.invalidate();
    auto& game=root.children[3];
    auto& field=game.children[0];
    view.set_menu_path(root,{"GAME","playfield"},0);
    view.print_menu(field,0,"SETTINGS > GAME > Playfield");
    const auto rootPosition=position_of(output.str(),"> 游戏机制  >");
    const auto groupPosition=position_of(output.str(),"> 场地  >");
    const auto fieldPosition=position_of(output.str(),"> 场地宽度 = 10");
    assert(rootPosition.second==7 && rootPosition.first==9);
    assert(groupPosition.second>rootPosition.second && fieldPosition.second>groupPosition.second);
    assert(groupPosition.second<45 && fieldPosition.second<85); // Compact columns.
    assert(foreground_at(output.str(),"> 场地宽度 = 10")=="\x1b[38;2;255;188;92m");
    assert(foreground_at(output.str(),"> 场地  >")=="\x1b[38;2;193;168;65m");
    assert(output.str().find("┌")!=std::string::npos && output.str().find("└")!=std::string::npos);
    const auto menuFrame=output.str();
    assert(output.str().find("配置 > 游戏机制 > 场地")!=std::string::npos);
    assert(output.str().find("\x1b[23;5H")!=std::string::npos);
    output.str(""); view.invalidate();
    view.print_edit(*field.children[0].item,"SETTINGS > GAME > Playfield");
    assert(position_of(output.str(),"> 场地宽度 = ")==fieldPosition);
    assert(count_token(output.str(),"┌")==count_token(menuFrame,"┌")); // No fourth column.
    assert(output.str().find("当前值：")==std::string::npos);
    assert(output.str().find("新值：")==std::string::npos);
    assert(output.str().find("配置 > 游戏机制 > 场地 > 场地宽度")!=std::string::npos);
    assert(output.str().find("\x1b[23;5H")!=std::string::npos);
    const auto baseWidth=last_box_width(output.str());
    output.str(""); view.invalidate();
    const std::string largeNumber(80,'9');
    view.print_scalar_input(*field.children[0].item,"SETTINGS > GAME > Playfield",largeNumber,20);
    assert(last_box_width(output.str())>baseWidth);
    assert(output.str().find("> 场地宽度 = "+largeNumber)!=std::string::npos);
    assert(count_token(output.str(),"┌")==count_token(menuFrame,"┌"));
    assert(position_of(output.str(),"> 场地宽度 = ")==fieldPosition);
    assert(output.str().find("Backspace/Delete 编辑")!=std::string::npos);
    assert(output.str().find("Backspace 返回")==std::string::npos);
    output.str(""); view.invalidate();
    view.print_scalar_input(*field.children[0].item,"SETTINGS > GAME > Playfield","1",0);
    assert(last_box_width(output.str())==baseWidth);
    output.str(""); view.invalidate();
    user.gameMode=5;
    auto replayRoot=Model::make_tree(user,false,0);
    const auto& replayMenu=replayRoot.children[1];
    const auto replaySetting=std::find_if(replayMenu.children.begin(),replayMenu.children.end(),
        [](const auto& node) {return node.item && node.item->id==UserConfig::SettingId::ReplayPath;});
    assert(replaySetting!=replayMenu.children.end());
    const auto replayRow=static_cast<std::size_t>(replaySetting-replayMenu.children.begin());
    view.set_menu_path(replayRoot,{"MODEL"},replayRow);
    view.print_menu(replayMenu,replayRow,"SETTINGS > MODEL");
    const auto replayPosition=position_of(output.str(),"> 回放 = ");
    const auto replayBoxCount=count_token(output.str(),"┌");
    output.str(""); view.invalidate();
    view.print_edit(*replaySetting->item,"SETTINGS > MODEL");
    assert(position_of(output.str(),"> 回放 = ")==replayPosition);
    assert(count_token(output.str(),"┌")==replayBoxCount);
    assert(output.str().find("当前值：")==std::string::npos && output.str().find("新值：")==std::string::npos);
    output.str(""); view.invalidate();
    const std::string replayPath="中文目录/练习 rp.txt";
    view.print_scalar_input(*replaySetting->item,"SETTINGS > MODEL",replayPath,replayPath.size());
    assert(position_of(output.str(),"> 回放 = "+replayPath)==replayPosition);
    assert(count_token(output.str(),"┌")==replayBoxCount);
    Test::TerminalScreen replayScreen;
    replayScreen.feed(output.str());
    assert(replayScreen.row==replayPosition.first);
    assert(replayScreen.column==replayPosition.second+static_cast<int>(width("> 回放 = "+replayPath)));
    user.gameMode=0;
    output.str(""); view.invalidate();
    view.set_menu_path(root,{"GAME","gravity"},0);
    const auto gravity=std::find_if(game.children.begin(),game.children.end(),
        [](const auto& node) { return node.key=="gravity"; });
    assert(gravity!=game.children.end());
    view.print_menu(*gravity,0,"SETTINGS > GAME > Gravity");
    assert(output.str().find("配置 > 游戏机制 > 重力")!=std::string::npos);
    assert(output.str().find("> 重力模式 = 定时下落")!=std::string::npos);
    const auto& appearance=root.children[7];
    view.set_menu_path(root,{"OTHERS"},0);
    for (int style:{-4,0,1,10}) {
        user.squareStyle=style;
        output.str(""); view.invalidate();
        view.print_menu(appearance,0,"SETTINGS > OTHERS");
        assert(output.str().find(style<=0 ? "> 方块样式 = 实心" : "> 方块样式 = 间隔")!=std::string::npos);
        assert(output.str().find("自定义")==std::string::npos);
        assert(Model::choice_value(*appearance.children[0].item)==
            Runtime::normalize_user_config(user,Runtime::GameMode::Zen).display.squareStyle);
    }
    user.squareStyle=UserConfig::Settings{}.squareStyle;
    output.str(""); view.invalidate();
    view.set_menu_path(root,{"CONTROLS"},0);
    view.print_edit(*root.children[5].children[0].item,"SETTINGS > CONTROLS");
    assert(last_box_width(output.str())==22); // 18 content columns plus borders/padding.
    assert(output.str().find("当前值：←")!=std::string::npos);
    assert(count_token(output.str(),"┌")==3);
    output.str(""); view.invalidate();
    UserConfig::set_control_keys(user,0,{UserConfig::default_player_two_key_codes()[0],
        UserConfig::default_player_two_key_codes()[1]});
    view.print_edit(*root.children[5].children[0].item,"SETTINGS > CONTROLS");
    assert(last_box_width(output.str())==22);
    assert(output.str().find("当前值：← / →")!=std::string::npos);
    App::UiText::set_language(App::UiText::Language::English);
    output.str(""); view.invalidate();
    view.print_edit(*root.children[5].children[5].item,"SETTINGS > CONTROLS");
    assert(last_box_width(output.str())<40);
    assert(output.str().find(" Rotate Counterclockwise ")!=std::string::npos);
    App::UiText::set_language(App::UiText::Language::Chinese);
    user.keyCodes=UserConfig::default_key_codes(); user.additionalKeyCodes={};
    output.str(""); view.invalidate();
    auto paged=root.children[5];
    const auto repeated=paged.children[0];
    while (paged.children.size()<20) paged.children.push_back(repeated);
    view.set_menu_path(root,{"CONTROLS"},15);
    view.print_menu(paged,15,"SETTINGS > CONTROLS");
    assert(output.str().find("16-20 / 20")!=std::string::npos);
    assert(output.str().find("\x1b[23;5H")!=std::string::npos);
    output.str(""); view.invalidate();
    view.set_menu_path(root,{"MODEL"},0);
    view.print_choices(*root.children[1].children[0].item,0,"SETTINGS > MODEL");
    assert(output.str().find("\x1b[23;5H确认后应用修改")!=std::string::npos);
    output.str(""); view.invalidate();
    user.gameMode=3;
    auto quickplayRoot=Model::make_tree(user,false,0);
    view.set_menu_path(quickplayRoot,{"MODEL","QUICK PLAY"},1);
    const auto quickplay=std::find_if(quickplayRoot.children[1].children.begin(),quickplayRoot.children[1].children.end(),
        [](const auto& node) {return node.key=="QUICK PLAY";});
    assert(quickplay!=quickplayRoot.children[1].children.end());
    view.print_menu(*quickplay,1,"SETTINGS > MODEL > QUICK PLAY");
    user.gameMode=0;
    assert(output.str().find("> Expert = 关闭")!=std::string::npos);
    assert(output.str().find("  Nohold = 关闭")!=std::string::npos);
    assert(output.str().find("  Allspin = 关闭")!=std::string::npos);
    assert(output.str().find("初始楼层")!=std::string::npos);
    assert(output.str().find("专家")==std::string::npos);
    assert(output.str().find("配置 > 游戏模式 > 快速游戏")!=std::string::npos);
    assert(count_token(output.str(),"┌")==3);
    assert(output.str().find("\x1b[23;5H")!=std::string::npos);
    output.str(""); view.invalidate();
    auto inactiveRoot=Model::make_tree(user,false,0);
    assert(std::none_of(inactiveRoot.children[1].children.begin(),inactiveRoot.children[1].children.end(),
        [](const auto& node) {return node.key=="QUICK PLAY";}));
    const auto inactiveQuickplay=std::find_if(inactiveRoot.children[3].children.begin(),inactiveRoot.children[3].children.end(),
        [](const auto& node) {return node.key=="QUICK PLAY";});
    assert(inactiveQuickplay!=inactiveRoot.children[3].children.end());
    view.set_menu_path(inactiveRoot,{"GAME","QUICK PLAY"},1);
    view.print_menu(*inactiveQuickplay,1,"SETTINGS > GAME > QUICK PLAY");
    assert(output.str().find("配置 > 游戏机制 > 快速游戏")!=std::string::npos);
    assert(output.str().find("> Expert = 关闭")!=std::string::npos);
    assert(output.str().find("当前规则下不生效；修改会保存")!=std::string::npos);
    output.str(""); view.invalidate();
    view.set_menu_path(quickplayRoot,{"GAME","playfield"},0);
    view.print_menu(quickplayRoot.children[3].children[0],0,"SETTINGS > GAME > Playfield");
    assert(output.str().find("> 场地宽度 = 10")!=std::string::npos);
    assert(output.str().find("当前规则下不生效；修改会保存")!=std::string::npos);
    output.str(""); view.invalidate();
#ifdef _WIN32
    const std::array<int,3> customMenuKeys{{'F','H','G'}};
#else
    const std::array<int,3> customMenuKeys{{KEY_F,KEY_H,KEY_G}};
#endif
    UserConfig::set_control_keys(user,8,{customMenuKeys[0],customMenuKeys[1]});
    UserConfig::set_control_keys(user,9,{customMenuKeys[2]});
    view.set_menu_path(root,{},0);
    view.print_menu(root,0,"SETTINGS");
    assert(output.str().find("[F / H] 重开")!=std::string::npos);
    assert(output.str().find("[G] 根目录")!=std::string::npos);
    assert(output.str().find("[R] 重开")==std::string::npos);
    output.str(""); view.invalidate();
    user.disableLeft=1;
    view.set_menu_path(root,{"AUTOPLAY"},0);
    view.print_choices(*root.children[4].children[0].item,3,"SETTINGS > Bot");
    assert(output.str().find("> bot_mech")!=std::string::npos);
    assert(output.str().find("不可用：此机器人不支持禁用左移。")!=std::string::npos);
    assert(foreground_at(output.str(),"> bot_mech")=="\x1b[38;2;255;188;92m");
    user.disableLeft=0;
    user.garbageMode=3; user.cheesePattern=1; user.holeCount=-2;
    auto garbageRoot=Model::make_tree(user,false,0);
    const auto modeGarbageMenu=std::find_if(garbageRoot.children[1].children.begin(),garbageRoot.children[1].children.end(),
        [](const auto& node) {return node.key=="mode-garbage";});
    assert(modeGarbageMenu!=garbageRoot.children[1].children.end());
    const auto garbageMenu=std::find_if(garbageRoot.children[3].children.begin(),garbageRoot.children[3].children.end(),
        [](const auto& node) {return node.key=="garbage-rules";});
    assert(garbageMenu!=garbageRoot.children[3].children.end());
    output.str(""); view.invalidate();
    view.set_menu_path(garbageRoot,{"MODEL","mode-garbage"},2);
    view.print_menu(*modeGarbageMenu,2,"SETTINGS > MODEL > GARBAGE");
    assert(output.str().find("  垃圾行模式 = Cheese 填层")!=std::string::npos);
    assert(output.str().find("  垃圾行形状  >")!=std::string::npos);
    assert(output.str().find("> 补回已消行 = 开启")!=std::string::npos);
    assert(output.str().find("  填层高度 = 6")!=std::string::npos);
    const auto garbageMode=std::find_if(garbageMenu->children.begin(),garbageMenu->children.end(),
        [](const auto& node) {return node.key=="cheese";});
    assert(garbageMode!=garbageMenu->children.end());
    const auto holes=std::find_if(garbageMode->children.begin(),garbageMode->children.end(),
        [](const auto& node) {return node.key=="holes";});
    assert(holes!=garbageMode->children.end());
    output.str(""); view.invalidate();
    view.set_menu_path(garbageRoot,{"GAME","garbage-rules","cheese","holes"},1);
    view.print_menu(*holes,1,"SETTINGS > GAME > GARBAGE > Garbage Shape > Hole pattern");
    assert(output.str().find("配置 > 游戏机制 > 垃圾行 > 垃圾行形状 > 空洞配置")!=std::string::npos);
    assert(output.str().find("Cheese 配置")==std::string::npos);
    assert(count_token(output.str(),"┌")==3);
    assert(output.str().find("> 垃圾行  >")==std::string::npos); // Hide the leftmost ancestor.
    assert(output.str().find("←←←")==std::string::npos);
    assert(output.str().find("⬅️")==std::string::npos);
    const auto fullBox=position_of(output.str(),"┌");
    assert(fullBox.second==9);
    Test::TerminalScreen clippedScreen;
    clippedScreen.feed(output.str());
    assert(clippedScreen.at(fullBox.first,6).glyph=="┐");
    assert(clippedScreen.at(fullBox.first+16,6).glyph=="┘");
    for (int offset=1;offset<=15;++offset) {
        for (int col=1;col<6;++col) assert(clippedScreen.at(fullBox.first+offset,col).glyph==" ");
        assert(clippedScreen.at(fullBox.first+offset,6).glyph=="│");
        assert(clippedScreen.at(fullBox.first+offset,7).glyph==" ");
        assert(clippedScreen.at(fullBox.first+offset,8).glyph==" ");
    }
    assert(position_of(output.str(),"> 垃圾行形状  >").second==11);
    assert(position_of(output.str(),"> 缺口宽度 = 2").second>
        position_of(output.str(),"> 空洞配置  >").second);
    assert(output.str().find("\x1b[23;5H")!=std::string::npos);
    assert(foreground_at(output.str(),"> 缺口宽度 = 2")=="\x1b[38;2;255;188;92m");
    const auto holePosition=position_of(output.str(),"> 缺口宽度 = 2");
    output.str(""); view.invalidate();
    view.print_scalar_input(*holes->children[1].item,"SETTINGS > GAME > GARBAGE > Garbage Shape > Hole pattern","1",1);
    assert(count_token(output.str(),"┌")==3);
    assert(position_of(output.str(),"> 缺口宽度 = 1")==holePosition);
    output.str(""); view.invalidate();
    view.print_choices(*holes->children[0].item,1,"SETTINGS > GAME > GARBAGE > Garbage Shape > Hole pattern");
    assert(count_token(output.str(),"┌")==3);
    assert(output.str().find("> 垃圾行形状  >")==std::string::npos);
    assert(position_of(output.str(),"> 空洞配置  >").second==11); // Keep the rightmost three boxes.
    assert(foreground_at(output.str(),"> 连续缺口")=="\x1b[38;2;255;188;92m");
    output.str(""); view.invalidate();
    context.config.display.obscuredColors=true;
    view.print_choices(*holes->children[0].item,1,"SETTINGS > GAME > GARBAGE > Garbage Shape > Hole pattern");
    assert(foreground_at(output.str(),"> 连续缺口")=="\x1b[38;2;54;57;62m");
    context.config.display.obscuredColors=false;
    output.str(""); view.invalidate();
    view.set_menu_path(root,{"HANDLING"},4);
    view.print_menu(root.children[2],4,"SETTINGS > HANDLING");
    assert(output.str().find("预移动 = 开启")!=std::string::npos);
    assert(output.str().find("DAS预充能 = 开启")!=std::string::npos);
    for (const auto selected : {1u,2u}) {
        output.str(""); view.invalidate();
        view.set_menu_path(root,{"HANDLING"},selected);
        view.print_scalar_input(*root.children[2].children[selected].item,"SETTINGS > HANDLING","1.5f",4);
        assert(output.str().find("支持小数帧")!=std::string::npos);
        assert(output.str().find("s/S")!=std::string::npos && output.str().find("f/F")!=std::string::npos);
    }
    output.str(""); view.invalidate();
    view.set_menu_path(quickplayRoot,{"MODEL","QUICK PLAY"},0);
    view.print_scalar_input(*quickplay->children[0].item,"SETTINGS > MODEL > QUICK PLAY","8",1);
    assert(output.str().find("最低海拔")!=std::string::npos);
    user.keyCodes=UserConfig::default_key_codes(); user.additionalKeyCodes={};
    output.str(""); view.invalidate();
    App::draw_start_menu(context);
    assert(output.str().find("TerMino") != std::string::npos);
    assert(output.str().find("[R] 开始") != std::string::npos);
    assert(output.str().find("[P] 设置") != std::string::npos);
    assert(output.str().find("[Esc] 退出")!=std::string::npos);
    assert(output.str().find("#####")==std::string::npos);
    assert(output.str().find("|__   __|")!=std::string::npos);
    assert(output.str().find("模式：")==std::string::npos);
    assert(output.str().find("Enter 确认") == std::string::npos);
    assert(output.str().find("\x1b[38;2;237;109;0m")!=std::string::npos);
    assert(output.str().find("\x1b[38;2;193;168;65m")!=std::string::npos);
    output.str(""); view.invalidate();
    user.obscuredColors=1; // Startup rendering follows saved preferences before game init.
    App::draw_start_menu(context);
    assert(output.str().find("\x1b[38;2;54;57;62m")!=std::string::npos);
    assert(output.str().find("\x1b[38;2;237;109;0m")==std::string::npos);
    assert(output.str().find("\x1b[38;2;193;168;65m")==std::string::npos);
    user.obscuredColors=0;
    context.config.display.obscuredColors=true;
#ifdef _WIN32
    const std::array<int,8> keys{{'F','H','G','T','Y','V','B','N'}};
#else
    const std::array<int,8> keys{{KEY_F,KEY_H,KEY_G,KEY_T,KEY_Y,KEY_V,KEY_B,KEY_N}};
#endif
    UserConfig::set_control_keys(user,8,{keys[0],keys[1],keys[2],keys[3],keys[4]});
    UserConfig::set_control_keys(user,9,{keys[5],keys[6],keys[7]});
    output.str(""); view.invalidate();
    App::draw_start_menu(context);
    const auto actions=App::UiText::binding_label(user,8)+" 开始   "+
        App::UiText::binding_label(user,9)+" 设置   [Esc] 退出";
    assert(width(actions)>52);
    assert(output.str().find(actions)!=std::string::npos);
    assert(output.str().find("\x1b[38;2;237;109;0m")!=std::string::npos);
    UserConfig::set_control_keys(user,8,{});
    output.str(""); view.invalidate();
    App::draw_start_menu(context);
    assert(output.str().find("[未绑定] 开始")!=std::string::npos);
    std::cout.rdbuf(old);

    const std::string path = ".tetr/replay/中文目录/练习 rp.txt";
    assert(Replay::replay_setting_path("中文目录/练习 rp.txt") == path);
    assert(Replay::replay_path_for_loading(path)->generic_u8string() == path);
    user.persistencePath = (std::filesystem::temp_directory_path()/"tetr_chinese_settings.ini").string();
    user.replayPath = path;
    assert(UserConfig::save(user));
    UserConfig::Settings loaded;
    UserConfig::load(loaded, user.persistencePath);
    assert(loaded.replayPath == path);
    std::filesystem::remove(user.persistencePath);
#ifdef __linux__
    // Exercise actual UTF-8 line input, rather than only assigning a model value.
    int descriptors[2]; assert(pipe(descriptors) == 0);
    const int saved = dup(STDIN_FILENO);
    assert(dup2(descriptors[0], STDIN_FILENO) >= 0);
    const auto inputText = path + "\n";
    assert(write(descriptors[1], inputText.data(), inputText.size()) == static_cast<ssize_t>(inputText.size()));
    assert(read_scalar_text(context, [] {}) == path);
    const auto inlineRead=[&](const std::string& keys, const std::string& expected) {
        assert(write(descriptors[1],keys.data(),keys.size())==static_cast<ssize_t>(keys.size()));
        std::string visible;
        std::size_t caret=0;
        assert(read_scalar_text(context, [] {}, [&](const std::string& text, std::size_t at) {
            visible=text; caret=at;
        })==expected);
        assert(visible==expected && caret==expected.size());
    };
    inlineRead(path+"\n",path);
    inlineRead("中文😀x\x1b[D\x1b[D\x7f\x1b[3~\x1b[F.txt\n","中x.txt");
    inlineRead("replay/ab.txt\x1b[H\x1b[3~r\x1b[C\x1b[F\x7f\n","replay/ab.tx");
    for (const bool escape : {false,true}) {
        const std::string keys="RrPp中文"+std::string(escape ? "\x1b" : "\n");
        assert(write(descriptors[1],keys.data(),keys.size())==static_cast<ssize_t>(keys.size()));
        bool savedByEscape=false;
        assert(read_scalar_text(context, [] {}, [](const std::string&,std::size_t) {}, &savedByEscape)=="RrPp中文");
        assert(savedByEscape==escape);
    }
    assert(dup2(saved, STDIN_FILENO) >= 0);
    close(saved); close(descriptors[0]); close(descriptors[1]);
    std::cin.clear();
#endif
    App::UiText::set_language(App::UiText::Language::English);
    assert(App::UiText::tr("SETTINGS") == "SETTINGS");
}
