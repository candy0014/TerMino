#include "../src/app/game_terminal.h"
#include "../src/app/ui_text.h"
#include "../src/app/settings_model.h"
#include "tetr/app/application_state.h"
#include "tetr/app/init.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/pause.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/live_piece_controller.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/render/board_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "support/terminal_screen.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <vector>
#ifdef __linux__
#include <cstdlib>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {
using App::InitDetail::TerminalCheckResult;
using Render::TerminalSize;
constexpr int escape=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x1b : 1;
constexpr int alternateRetry=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x73 : 62;
constexpr int alternateSettings=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x74 : 63;

struct Capture {
    std::ostringstream output;
    std::streambuf* previous=std::cout.rdbuf(output.rdbuf());
    ~Capture() { std::cout.rdbuf(previous); }
};
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    Input::Services input;
    UserConfig::Settings user;
    Replay::ReplaySessionState replay;
    Network::NetworkServices network;
    Player() {
        context.attach_input(input);
        context.attach_user_settings(user);
        context.attach_replay(replay);
        context.attach_network(network);
        user.gameMode=0; user.garbageMode=0;
        user.keyCodes=UserConfig::default_key_codes();
        input.initialized=true;
        input.keyStateProvider=[](int) { return false; };
        Input::refresh_bindings(input,user.keyCodes);
        auto& p=session->player;
        p.presentation.margin=30;
        p.presentation.blockMaxWidth=p.presentation.blockMaxHeight=4;
        p.gameplay.playfield.width=10;
        p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
        p.gameplay.playfield.nextCount=5;
        p.gameplay.playfield.holdEnabled=true;
    }
};

void geometry() {
    Player a,b;
    auto& p=a.session->player;
    const auto ordinary=App::InitDetail::required_game_terminal_size(a.context);
    for (int width:{4,10,32,100}) for (int height:{4,20,100})
        for (int next:{0,1,5,10}) for (bool hold:{false,true}) {
            p.gameplay.playfield.width=width;
            p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=height;
            p.gameplay.playfield.nextCount=next;
            p.gameplay.playfield.holdEnabled=hold;
            const auto required=App::InitDetail::required_game_terminal_size(a.context);
            Capture capture;
            Render::print_border(a.context,p);
            Render::print_message(a.context,p);
            Render::print_score(a.context,p);
            {
                Engine::GamePauseScope pause(a.context);
                a.input.keyStateProvider=[&](int key) {
                    return key==a.user.keyCodes[12] || key==a.user.keyCodes[8];
                };
                Engine::poll_game_pause(a.context,[] {});
            }
            a.input.keyStateProvider=[](int) { return false; };
            Test::TerminalScreen screen;
            screen.feed(capture.output.str());
            assert(screen.columns()<required.columns-1 && screen.rows()<required.rows-1);
        }
    p.gameplay.playfield.width=4;
    p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=4;
    p.gameplay.playfield.nextCount=0;
    a.context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    a.context.config.quickPlay.snowball=Runtime::ModifierMode::Enabled;
    const auto snowball=App::InitDetail::required_game_terminal_size(a.context);
    p.gameplay.playfield.width=20;
    p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
    assert(App::InitDetail::required_game_terminal_size(a.context)==snowball);
    assert(snowball.columns>=30+40+11+2);
    a.context.config.mode.replayMode=true;
    const auto replay=App::InitDetail::required_game_terminal_size(a.context);
    assert(replay.columns>=snowball.columns && replay.rows>snowball.rows);
    Capture capture;
    Engine::render_replay_playback_progress(a.context,p);
    Test::TerminalScreen screen;
    screen.feed(capture.output.str());
    assert(screen.columns()<replay.columns-1 && screen.rows()<replay.rows-1);
    b.session->player.presentation.margin=90;
    b.session->player.gameplay.playfield.width=32;
    b.session->player.gameplay.playfield.visibleHeight=b.session->player.gameplay.playfield.activeHeight=50;
    const auto local=App::InitDetail::required_game_terminal_size(a.context,&b.context);
    assert(local.columns>=90+64+12+2 && local.rows>ordinary.rows);
    a.context.config.mode.replayMode=false;
    a.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    a.session->opponent=b.session->player;
    assert(App::InitDetail::required_game_terminal_size(a.context).columns>=90+64+12+2);
}

struct Step {
    TerminalSize size;
    std::set<int> first,second;
    int tap=0;
};

void centered_geometry() {
    Player a,b;
    auto& p=a.session->player;
    // Packing removes the legacy margins; visual balance can require extra
    // space beyond the minimum to accommodate the heavier NEXT panel.
    assert(Engine::game_terminal_requirements(a.context).minimum.columns<
        Engine::required_game_terminal_size(a.context).columns);
    for (int width:{4,10,32}) for (bool hold:{false,true}) for (int next:{0,5})
        for (bool garbage:{false,true}) {
        p.gameplay.playfield.width=width;
        p.gameplay.playfield.holdEnabled=hold;
        p.gameplay.playfield.nextCount=next;
        p.presentation.blockMaxWidth=7; p.presentation.blockMaxHeight=6;
        a.context.config.garbage.mode=garbage ? Runtime::GarbageMode::QuickPlay : Runtime::GarbageMode::None;
        const auto required=Engine::required_game_terminal_size(a.context,nullptr,true);
        for (int columns:{required.columns,required.columns+5,200}) {
            Engine::center_game_boards(a.context,{columns,100});
            const auto bounds=Render::board_horizontal_extent(a.context,p,false);
            assert(bounds.left>=1 && bounds.right<=columns-1);
            const auto vertical=Render::board_vertical_extent(a.context,p,false);
            assert(p.presentation.topMargin>=8 && p.presentation.topMargin+vertical.fullBottom<100);
            if (columns==200) {
                const int x=p.presentation.margin,y=p.presentation.topMargin;
                Engine::center_game_boards(a.context,{202,102});
                assert(p.presentation.margin==x+1 && p.presentation.topMargin==y+1);
                Engine::center_game_boards(a.context,{columns,100});
            }
            Capture capture;
            Render::print_border(a.context,p);
            Render::print_message(a.context,p);
            Render::print_score(a.context,p);
            Test::TerminalScreen screen;
            screen.feed(capture.output.str());
            assert(screen.columns()<columns && screen.rows()<100);
        }
    }
    a.context.config.mode.mode=Runtime::GameMode::LocalVersus;
    a.context.config.garbage.mode=b.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    p.gameplay.playfield.width=10;
    b.session->player.gameplay.playfield.width=16;
    const auto both=Engine::required_game_terminal_size(a.context,&b.context,true);
    for (int columns:{both.columns,300}) {
        Engine::center_game_boards(a.context,{columns,100},&b.context);
        const auto left=Render::board_horizontal_extent(a.context,p,false);
        const auto right=Render::board_horizontal_extent(b.context,b.session->player,false);
        assert(left.left>=1 && right.right<=columns-1 && right.left-left.right==4);
        assert(p.presentation.topMargin==b.session->player.presentation.topMargin);
        if (columns==300) {
            const int x=p.presentation.margin;
            Engine::center_game_boards(a.context,{302,100},&b.context);
            assert(p.presentation.margin==x+1);
        }
    }
}

void native_game_resize() {
#ifdef __linux__
    Capture capture;
    const int saved=::dup(STDOUT_FILENO), terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0 && ::dup2(terminal,STDOUT_FILENO)>=0);
    Player a;
    App::ApplicationState app;
    app.primaryContext=&a.context; a.context.attach_app(app);
    auto& p=a.session->player;
    p.gameplay.playfield.field.initialize(10,20);
    p.gameplay.playfield.field.set_cell(19,0,Runtime::BoardCell::piece(0));
    winsize size{}; size.ws_col=100; size.ws_row=80;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    assert(Engine::refresh_game_terminal_layout(a.context));
    assert(p.presentation.margin==40 && p.presentation.topMargin==29);
    const auto initialRevision=p.presentation.terminalLayoutRevision;
    assert(!Engine::refresh_game_terminal_layout(a.context));
    size.ws_col=80;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    assert(Engine::refresh_game_terminal_layout(a.context) && p.presentation.margin==30);
    assert(p.presentation.terminalLayoutRevision>initialRevision);
    Test::TerminalScreen screen;
    screen.feed(capture.output.str());
    assert(screen.at(p.presentation.topMargin+20,p.presentation.margin+1).glyph=="█");
    assert(screen.columns()<80);
    size.ws_row=70;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    assert(Engine::refresh_game_terminal_layout(a.context));
    assert(p.presentation.terminalRows==70 && !Engine::refresh_game_terminal_layout(a.context));
    const auto required=Engine::game_terminal_requirements(a.context).minimum;
    size.ws_col=required.columns;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    assert(Engine::refresh_game_terminal_layout(a.context));
    assert(Render::board_horizontal_extent(a.context,p,false).left==1);
    for (const auto tiny:{TerminalSize{25,12},TerminalSize{1,1},TerminalSize{12,30}}) {
        capture.output.str("");
        size.ws_col=tiny.columns; size.ws_row=tiny.rows;
        assert(::ioctl(terminal,TIOCSWINSZ,&size)==0 && Engine::refresh_game_terminal_layout(a.context));
        Test::TerminalScreen clipped; clipped.feed(capture.output.str());
        assert(clipped.columns()<=tiny.columns && clipped.rows()<=tiny.rows);
        assert(capture.output.str().find('\n')==std::string::npos);
    }
    assert(::dup2(saved,STDOUT_FILENO)>=0);
    ::close(saved); ::close(terminal);
#endif
}

void vertical_priorities() {
    Player a;
    auto& p=a.session->player;
    p.gameplay.playfield.nextCount=10;
    a.context.config.field.nextCount=10;
    const auto required=Engine::game_terminal_requirements(a.context);
    const auto vertical=Render::board_vertical_extent(a.context,p,false);
    assert(required.minimum.rows<required.full.rows && vertical.coreBottom<vertical.fullBottom);
    Engine::center_game_boards(a.context,required.minimum);
    assert(p.presentation.topMargin<8 && p.presentation.topMargin+vertical.coreBottom<required.minimum.rows);
    Engine::center_game_boards(a.context,{200,vertical.coreBottom+9});
    assert(p.presentation.topMargin==8);
    assert(p.presentation.topMargin+vertical.coreBottom< p.presentation.terminalRows);
    assert(p.presentation.topMargin+vertical.fullBottom>=p.presentation.terminalRows);
    Engine::center_game_boards(a.context,required.full);
    assert(p.presentation.topMargin>=8 && p.presentation.topMargin+vertical.fullBottom<required.full.rows);
    Engine::center_game_boards(a.context,{15,10});
    assert(p.presentation.topMargin+vertical.coreBottom==9);
    assert(p.presentation.margin-22<=0); // The viewport clips the unavailable left area.
}

void native_resize_during_live_piece() {
#ifdef __linux__
    Capture capture;
    const int saved=::dup(STDOUT_FILENO),terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0 && ::dup2(terminal,STDOUT_FILENO)>=0);
    winsize size{}; size.ws_col=100; size.ws_row=80;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    Player a;
    auto& p=a.session->player;
    auto& field=p.gameplay.playfield.field;
    field.initialize(10,20);
    a.context.config.mode.mode=Runtime::GameMode::Zen;
    a.context.config.handling.gravityInterval=Runtime::GravityInterval::disabled();
    Engine::center_game_boards(a.context,{100,80});
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    auto movement=Engine::make_spawn_movement_state(10,piece.type());
    Engine::MovementInputState input;
    Engine::AutoplayCarryState carry;
    bool resized=false;
    int reads=0;
    a.services.clock.waitInterrupt=[&] {
        if (!resized) {
            size.ws_col=80; size.ws_row=60;
            assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
            resized=true;
        }
    };
    a.input.keyStateProvider=[&](int key) {
        assert(++reads<100000);
        return resized && p.presentation.terminalColumns==80 && key==a.user.keyCodes[3];
    };
    const auto result=Engine::run_live_piece_movement(a.context,field,piece,p,movement,input,carry,false,
        [&](Piece::Pose pose) { return piece.can_place(pose,field); },
        [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; },
        [&] { return Engine::run_frame(a.context,field,p,false); });
    assert(result==Rules::PlayResult::ContinueFrame && resized);
    assert(p.presentation.margin==30 && p.presentation.topMargin==19);
    Test::TerminalScreen screen; screen.feed(capture.output.str());
    for (const auto& offset:piece.cells(movement.pose.rotation)) {
        const auto cell=screen.at(p.presentation.topMargin+movement.pose.position.row+offset[0]+1,
            p.presentation.margin+2*(movement.pose.position.col+offset[1])+1);
        assert(cell.glyph=="█" && (cell.foreground==Test::TerminalScreen::Color{{54,235,168}}));
    }
    // A hard drop in the resize frame must erase the repainted spawn pose.
    std::set<std::pair<int,int>> occupied;
    for (const auto& offset:piece.cells(movement.pose.rotation))
        occupied.emplace(movement.pose.position.row+offset[0],movement.pose.position.col+offset[1]);
    for (int row=-Runtime::kDisplayHiddenRows;row<20;++row) for (int col=0;col<10;++col)
        if (!occupied.count({row,col}))
            assert(screen.at(p.presentation.topMargin+row+1,p.presentation.margin+col*2+1).glyph==" ");
    assert(screen.columns()<=80 && screen.rows()<=60 && !p.presentation.activePose);
    assert(::dup2(saved,STDOUT_FILENO)>=0); ::close(saved); ::close(terminal);
#endif
}

void native_local_resize() {
#ifdef __linux__
    Capture capture;
    const int saved=::dup(STDOUT_FILENO),terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0 && ::dup2(terminal,STDOUT_FILENO)>=0);
    Player a,b;
    App::ApplicationState app;
    app.primaryContext=&a.context; app.localPlayerTwoContext=&b.context;
    a.context.attach_app(app); b.context.attach_app(app);
    a.context.config.mode.mode=b.context.config.mode.mode=Runtime::GameMode::LocalVersus;
    a.context.config.garbage.mode=b.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    a.session->player.gameplay.playfield.field.initialize(10,20);
    b.session->player.gameplay.playfield.field.initialize(10,20);
    winsize size{}; size.ws_col=200; size.ws_row=80;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    Engine::center_game_boards(a.context,{200,80},&b.context);
    const int oldSecond=b.session->player.presentation.margin;
    size.ws_col=160; size.ws_row=60;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    assert(Engine::refresh_game_terminal_layout(a.context));
    assert(b.session->player.presentation.margin!=oldSecond);
    assert(b.session->player.presentation.terminalPaintRevision!=app.gameTerminalLayoutRevision);
    assert(Engine::refresh_game_terminal_layout(b.context));
    assert(b.session->player.presentation.terminalPaintRevision==app.gameTerminalLayoutRevision);
    assert(!Engine::refresh_game_terminal_layout(a.context) && !Engine::refresh_game_terminal_layout(b.context));
    const auto output=capture.output.str();
    const auto clear=output.find("\x1b[2J");
    assert(clear!=std::string::npos && output.find("\x1b[2J",clear+1)==std::string::npos);
    Test::TerminalScreen screen; screen.feed(output);
    assert(screen.columns()<=160 && screen.rows()<=60);
    assert(::dup2(saved,STDOUT_FILENO)>=0); ::close(saved); ::close(terminal);
#endif
}
struct ScenarioResult {
    TerminalCheckResult result=TerminalCheckResult::Ready;
    bool escaped=false;
    std::size_t steps=0;
    std::string text;
};

ScenarioResult scenario(Player& a, const std::vector<Step>& steps, Player* b=nullptr) {
    Capture capture;
    a.session->player.presentation.dynamicLayout=false;
    if (b) b->session->player.presentation.dynamicLayout=false;
    std::size_t queries=0,index=0;
    Input::KeyEventBuffer events;
    a.input.keyEventSource=&events;
    a.input.keyStateProvider=[&](int key) { return steps[index].first.count(key)!=0; };
    if (b) b->input.keyStateProvider=[&](int key) { return steps[index].second.count(key)!=0; };
    const auto required=Engine::game_terminal_requirements(a.context,b ? &b->context : nullptr);
    ScenarioResult result;
    const auto validate_frame=[&] {
        const auto bytes=capture.output.str();
        if (bytes.empty()) return;
        Test::TerminalScreen screen;
        screen.feed(bytes);
        assert(screen.columns()<=steps[index].size.columns-1 && screen.rows()<=steps[index].size.rows-1);
        for (const auto frame:{required.minimum,required.full}) {
            if (frame.columns<=steps[index].size.columns && frame.rows<=steps[index].size.rows) {
                const auto corner=screen.at(frame.rows-1,frame.columns-1).glyph;
                assert(corner=="┘" || corner=="┤" || corner=="┴");
            }
        }
        result.text+=screen.text();
        capture.output.str("");
    };
    const auto query=[&]() -> std::optional<TerminalSize> {
        if (queries>1) validate_frame();
        index=queries ? queries-1 : 0;
        assert(index<steps.size()); // The wait must complete at the expected input boundary.
        ++queries;
        if (steps[index].tap) { events.update(steps[index].tap,true); events.update(steps[index].tap,false); }
        return steps[index].size;
    };
    try { result.result=App::InitDetail::wait_for_game_terminal(a.context,b ? &b->context : nullptr,query); }
    catch (const App::StartScreenRequested&) { result.escaped=true; }
    validate_frame();
    result.steps=index+1;
    assert(!a.session->gameStartTime && !a.replay.playback.started);
    assert(!a.input.gameplay_input_active() && (!b || !b->input.gameplay_input_active()));
    a.input.keyEventSource=nullptr;
    a.input.keyStateProvider=[](int) { return false; };
    if (b) b->input.keyStateProvider=[](int) { return false; };
    return result;
}

void resize_and_controls() {
    Player a,b;
    const int retry=a.user.keyCodes[8], settings=a.user.keyCodes[9];
    const auto required=Engine::game_terminal_requirements(a.context);
    const TerminalSize narrow{required.minimum.columns-1,required.minimum.rows};
    assert(scenario(a,{{required.full,{}}}).steps==1);
    const TerminalSize enlarged{required.full.columns+20,required.full.rows+10};
    const auto confirmed=scenario(a,{{narrow,{}},{required.full,{}},{enlarged,{}},
        {enlarged,{}},{enlarged,{retry}},{enlarged,{}}});
    assert(confirmed.result==TerminalCheckResult::Ready && confirmed.steps==6);
    assert(confirmed.text.find("最小布局：")!=std::string::npos && confirmed.text.find("完整布局：")!=std::string::npos);
    assert(confirmed.text.find("├")!=std::string::npos);
    assert(confirmed.text.find("终端尺寸已足够。")!=std::string::npos);
    assert(confirmed.text.find("按重开键开始游戏。")!=std::string::npos);
    const TerminalSize bothWidths{required.full.columns,required.minimum.rows};
    const auto junctions=scenario(a,{{bothWidths,{}},{bothWidths,{escape}}});
    assert(junctions.text.find(required.full.columns==required.minimum.columns ? "┤" : "┬")!=std::string::npos);
    const auto override=scenario(a,{{narrow,{retry}},{narrow,{}},{narrow,{retry}},{narrow,{}}});
    assert(override.steps==4 && !override.escaped);
    assert(override.text.find("按重开键仍可直接开始游戏")!=std::string::npos);
    for (const auto size:{narrow,required.minimum,required.full,enlarged}) {
        assert(scenario(a,{{narrow,{}},{size,{escape}}}).escaped);
        assert(scenario(a,{{narrow,{}},{size,{settings}}}).result==TerminalCheckResult::OpenSettings);
    }
    a.user.keyCodes[8]=a.user.keyCodes[9]=-1;
    a.user.additionalKeyCodes[8]={alternateRetry}; a.user.additionalKeyCodes[9]={alternateSettings};
    Input::refresh_bindings(a.input,a.user.keyCodes,a.user.additionalKeyCodes);
    const auto tap=scenario(a,{{narrow,{}},{narrow,{}, {},alternateRetry}});
    assert(tap.steps==2 && tap.text.find("F4")!=std::string::npos && tap.text.find("F5")!=std::string::npos);
    assert(scenario(a,{{narrow,{}},{narrow,{}, {},escape}}).escaped);
    for (const auto tiny:{TerminalSize{1,1},TerminalSize{2,2},TerminalSize{5,8},TerminalSize{15,4}})
        assert(scenario(a,{{tiny,{}},{tiny,{alternateRetry}},{tiny,{}}}).steps==3);
    b.user.keyCodes=UserConfig::default_player_two_key_codes();
    Input::refresh_bindings(b.input,b.user.keyCodes);
    assert(scenario(a,{{narrow,{}},{narrow,{}, {b.user.keyCodes[8]}},{narrow,{}}},&b).steps==3);
    const auto localFull=Engine::game_terminal_requirements(a.context,&b.context).full;
    assert(scenario(a,{{narrow,{}},{localFull,{}},{localFull,{}},
        {localFull,{}, {b.user.keyCodes[8]}},{localFull,{}}},&b).steps==5);
    App::UiText::set_language(App::UiText::Language::English);
    const auto english=scenario(a,{{narrow,{}},{required.full,{}},{enlarged,{}},
        {enlarged,{}, {},alternateRetry}});
    assert(english.steps==4);
    assert(english.text.find("Minimum layout:")!=std::string::npos);
    assert(english.text.find("Terminal size is sufficient.")!=std::string::npos);
    assert(english.text.find("Press the restart key to start")!=std::string::npos); // The prompt can wrap.
    App::UiText::set_language(App::UiText::Language::Chinese);
    a.session->player.presentation.dynamicLayout=false;
    Capture capture;
    int queries=0;
    assert(App::InitDetail::wait_for_game_terminal(a.context,nullptr,[&]() -> std::optional<TerminalSize> {
        ++queries; return std::nullopt;
    })==TerminalCheckResult::Ready && queries==1 && capture.output.str().empty());
    Render::begin_output_suppression();
    assert(App::InitDetail::wait_for_game_terminal(a.context,nullptr,[&]() -> std::optional<TerminalSize> {
        assert(false); return std::nullopt;
    })==TerminalCheckResult::Ready);
    Render::end_output_suppression();
}

void restart_confirmation_requirements() {
    Player a,b;
    App::ApplicationState app;
    a.context.attach_app(app);
    auto& field=a.session->player.gameplay.playfield;
    field.width=14;
    const auto required=Engine::game_terminal_requirements(a.context).full;
    const TerminalSize narrow{required.columns-1,required.rows-1};
    const int retry=a.user.keyCodes[8];
    const auto confirm=[&](Player* second=nullptr) {
        const auto result=scenario(a,{{narrow,{}},{narrow,{}, {},retry}},second);
        assert(result.result==TerminalCheckResult::Ready && result.steps==2);
        assert(result.text.find("请放大终端")!=std::string::npos);
    };
    const auto skip=[&] {
        const auto result=scenario(a,{{narrow,{}}});
        assert(result.result==TerminalCheckResult::Ready && result.steps==1 && result.text.empty());
    };
    confirm();
    skip(); // Identical window and requirements across restarts.

    field.width+=6;
    assert(Engine::game_terminal_requirements(a.context).full.columns>required.columns);
    confirm(); // A wider layout needs a new confirmation without a window resize.
    skip();
    field.width-=6;
    skip(); // Smaller requirements do not prompt, but become the new baseline.
    field.width+=6;
    confirm();

    const auto beforeHeight=Engine::game_terminal_requirements(a.context).full;
    field.visibleHeight=field.activeHeight=60;
    field.width-=6;
    const auto taller=Engine::game_terminal_requirements(a.context).full;
    assert(taller.columns<beforeHeight.columns && taller.rows>beforeHeight.rows);
    confirm(); // Height growth still prompts when width shrinks.
    skip();
    field.visibleHeight=field.activeHeight=20;
    skip();

    confirm(&b); // Switching to two boards increases the shared requirements.
    skip();
    app.observe_terminal_size(narrow.columns+1,narrow.rows);
    app.observe_terminal_size(narrow.columns,narrow.rows);
    confirm(); // Resizing away and back still invalidates confirmation.

    field.width=10;
    const auto ample=Engine::game_terminal_requirements(a.context).full;
    assert(scenario(a,{{ample,{}}}).text.empty());
    field.width=14;
    const auto wider=Engine::game_terminal_requirements(a.context).full;
    assert(wider.columns>ample.columns);
    const auto afterAutomatic=scenario(a,{{ample,{}},{ample,{}, {},retry}});
    assert(afterAutomatic.steps==2 && afterAutomatic.text.find("请放大终端")!=std::string::npos);
    const auto full=Engine::game_terminal_requirements(a.context).full;
    assert(scenario(a,{{{full.columns+20,full.rows+20},{}}}).text.empty());
    field.width+=1;
    assert(scenario(a,{{{full.columns+20,full.rows+20},{}}}).text.empty());
}

void native_init_wait() {
#ifdef __linux__
    Capture capture;
    const int saved=::dup(STDOUT_FILENO), terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0 && ::dup2(terminal,STDOUT_FILENO)>=0);
    winsize size{}; size.ws_col=80; size.ws_row=10;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    Player a;
    std::set<int> down;
    a.input.keyStateProvider=[&](int key) { return down.count(key)!=0; };
    int phase=0;
    a.services.clock.waitInterrupt=[&] {
        const auto output=capture.output.str();
        if (phase==0 && output.find("请放大终端")!=std::string::npos) {
            size.ws_col=size.ws_row=200;
            assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
            phase=1;
        } else if (phase==1 && output.find("终端尺寸已足够。")!=std::string::npos) {
            assert(!a.session->gameStartTime && !a.replay.playback.started);
            phase=2;
        } else if (phase==2) {
            down={a.user.keyCodes[8]}; phase=3;
        } else if (phase==3) {
            down.clear(); phase=4;
        }
    };
    App::initialize_game(a.context);
    assert(phase==4 && !a.session->gameStartTime);
    const auto output=capture.output.str();
    assert(output.find("按重开键仍可直接开始游戏")!=std::string::npos);
    assert(output.rfind("\x1b[2J")>output.find("按重开键开始游戏。")); // Game drawn after confirmation.
    assert(::dup2(saved,STDOUT_FILENO)>=0);
    ::close(saved); ::close(terminal);
#endif
}

void native_settings_recalculation() {
#ifdef __linux__
    Capture capture;
    const int saved=::dup(STDOUT_FILENO), terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0 && ::dup2(terminal,STDOUT_FILENO)>=0);
    winsize size{}; size.ws_col=80; size.ws_row=10;
    assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    Player a;
    App::ApplicationState app;
    app.primaryContext=&a.context;
    a.context.attach_app(app);
    app.settingsCategoryIndex=3;
    app.settingsMenu.path={"GAME","playfield"};
    const auto tree=App::Settings::Model::make_tree(a.user,false,0);
    app.settingsMenu.selectedEntries["root/GAME/playfield"]=tree.children[3].children[0].children[0].key;
    std::set<int> down;
    Input::KeyEventBuffer events;
    a.input.keyEventSource=&events;
    a.input.keyStateProvider=[&](int key) { return down.count(key)!=0; };
    int phase=0;
    a.services.clock.waitInterrupt=[&] {
        const auto output=capture.output.str();
        if (phase==0 && output.find("请放大终端")!=std::string::npos) {
            down={a.user.keyCodes[9]}; phase=1;
        } else if (phase==1 && output.find("场地宽度")!=std::string::npos) {
            a.user.fieldWidth=24;
            down.clear(); phase=2;
        } else if (phase==2) {
            down={a.user.keyCodes[8]}; phase=3;
        } else if (phase==3) {
            down.clear(); phase=4;
        } else if (phase==4 && a.context.config.field.width==24 &&
            output.rfind("请放大终端")>output.find("场地宽度")) {
            const auto required=App::InitDetail::required_game_terminal_size(a.context,nullptr,true);
            assert(output.find("完整布局："+std::to_string(required.columns))!=std::string::npos);
            size.ws_col=size.ws_row=200;
            assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
            phase=5;
        } else if (phase==5 && output.find("终端尺寸已足够。")!=std::string::npos) {
            // Both callbacks may run within one sleep; retain the press edge.
            events.update(a.user.keyCodes[8],true);
            down={a.user.keyCodes[8]}; phase=6;
        } else if (phase==6) {
            events.update(a.user.keyCodes[8],false);
            down.clear(); phase=7;
        }
    };
    App::initialize_game(a.context);
    assert(phase==7 && a.context.config.field.width==24 && !a.session->gameStartTime);
    assert(::dup2(saved,STDOUT_FILENO)>=0);
    ::close(saved); ::close(terminal);
#endif
}
}

int main() {
    App::UiText::set_language(App::UiText::Language::Chinese);
    geometry();
    centered_geometry();
    vertical_priorities();
    native_game_resize();
    native_resize_during_live_piece();
    native_local_resize();
    resize_and_controls();
    restart_confirmation_requirements();
    native_init_wait();
    native_settings_recalculation();
}
