#include "../src/app/game_terminal.h"
#include "../src/app/ui_text.h"
#include "tetr/app/settings_model.h"
#include "tetr/app/application_state.h"
#include "tetr/app/init.h"
#include "tetr/app/start_screen_navigation.h"
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

namespace TetrTests::game_terminal_test {
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
    for (int width:{4,10,32,100}) for (int height:{0,1,2,3,4,20,100})
        for (int next:{0,1,5,10}) for (bool hold:{false,true}) {
            p.gameplay.playfield.width=width;
            p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=height;
            p.gameplay.playfield.nextCount=next;
            p.gameplay.playfield.holdEnabled=hold;
            // Each capture represents a fresh terminal, so it has no old
            // statistics region to erase after changing the map height.
            p.presentation.messageTopMargin=-1;
            p.presentation.messageWidths={};
            p.presentation.messageText={};
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
            if(!(screen.columns()<required.columns-1 && screen.rows()<required.rows-1))
                std::cerr<<"geometry width="<<width<<" height="<<height<<" next="<<next<<" hold="<<hold
                    <<" screen="<<screen.columns()<<","<<screen.rows()<<" required="<<required.columns<<","<<required.rows<<'\n';
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

void information_aligned_duels() {
    Player a,b;a.context.config.mode.mode=Runtime::GameMode::LocalVersus;
    a.context.config.garbage.mode=b.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    auto& own=a.session->player;auto& peer=b.session->player;
    own.presentation.blockMaxHeight=peer.presentation.blockMaxHeight=2;
    own.gameplay.playfield.nextCount=peer.gameplay.playfield.nextCount=2;
    own.gameplay.playfield.visibleHeight=own.gameplay.playfield.activeHeight=20;
    int fixedTop=-1;
    for(int height:{12,40,60,100}) {
        peer.gameplay.playfield.visibleHeight=peer.gameplay.playfield.activeHeight=height;
        Engine::center_game_boards(a.context,{180,80},&b.context);
        if(fixedTop<0)fixedTop=own.presentation.topMargin;
        assert(own.presentation.topMargin==fixedTop && fixedTop>=0 && fixedTop+20<80);
        if(height<=60) {
            assert(peer.presentation.topMargin>=0 && peer.presentation.topMargin+height+1<80);
            assert(fixedTop+Render::board_info_top_row(own)==peer.presentation.topMargin+Render::board_info_top_row(peer));
        } else assert(peer.presentation.topMargin+height+1==79);
    }
}
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
        assert(p.presentation.topMargin+Render::board_info_top_row(p)==
            b.session->player.presentation.topMargin+Render::board_info_top_row(b.session->player));
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
void confirmed_clipping() {
    Player a,b;App::ApplicationState app;a.context.attach_app(app);app.primaryContext=&a.context;
    a.input.keyStateProvider=[](int){assert(false && "Terminal size must not consume controls");return false;};
    for(bool room:{false,true})for(int width:{10,40,100})for(int height:{20,100})
        for(const auto size:{TerminalSize{1,1},TerminalSize{40,16},TerminalSize{120,40}}) {
            auto& p=a.session->player;p.gameplay.playfield.width=width;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=height;
            p.gameplay.playfield.field.initialize(width,height);
            a.session->roomScene.enabled=room;a.session->roomScene.ownSlot=0;a.session->roomScene.selfAlive=true;a.session->roomScene.survivors={0};
            app.acceptedGameTerminalSize={{size.columns,size.rows}};
            const auto required=Engine::game_terminal_requirements(a.context);
            app.acceptedGameTerminalRequirements={{required.full.columns,required.full.rows}};
            Capture capture;int queries=0;
            const auto result=App::InitDetail::wait_for_game_terminal(a.context,nullptr,[&]() -> std::optional<TerminalSize>{++queries;return size;});
            assert(result==TerminalCheckResult::Ready && queries==1 && capture.output.str().empty());
            assert(p.gameplay.playfield.width==width && p.gameplay.playfield.visibleHeight==height);
            assert(p.presentation.terminalColumns==size.columns && p.presentation.terminalRows==size.rows);
            Render::print_border(a.context,p);Render::print_message(a.context,p);Render::print_score(a.context,p);
            Test::TerminalScreen screen;screen.feed(capture.output.str());assert(screen.columns()<=size.columns && screen.rows()<=size.rows);
            assert(screen.text().find("请放大终端")==std::string::npos);
        }
    a.session->roomScene.enabled=false;
    Capture capture;
    app.acceptedGameTerminalSize={{30,12}};
    const auto required=Engine::game_terminal_requirements(a.context,&b.context);
    app.acceptedGameTerminalRequirements={{required.full.columns,required.full.rows}};
    assert(App::InitDetail::wait_for_game_terminal(a.context,&b.context,[]()->std::optional<TerminalSize>{return TerminalSize{30,12};})==TerminalCheckResult::Ready);
    assert(capture.output.str().empty());
    assert(App::InitDetail::wait_for_game_terminal(a.context,nullptr,[]()->std::optional<TerminalSize>{return {};})==TerminalCheckResult::Ready);
    Render::begin_output_suppression();
    assert(App::InitDetail::wait_for_game_terminal(a.context,nullptr,[]()->std::optional<TerminalSize>{assert(false);return {};})==TerminalCheckResult::Ready);
    Render::end_output_suppression();
}

void size_confirmation_controls() {
    Player a;App::ApplicationState app;a.context.attach_app(app);app.primaryContext=&a.context;
    a.user.additionalKeyCodes[8]={alternateRetry};
    Input::refresh_bindings(a.input,a.user.keyCodes,a.user.additionalKeyCodes);
    const auto required=Engine::game_terminal_requirements(a.context);
    const TerminalSize small{50,20},large{required.full.columns+4,required.full.rows+4};
    int queries=0;
    a.input.keyStateProvider=[&](int key) {
        return key==alternateRetry && (queries<=2 || queries==6 || queries==7);
    };
    {
        Capture capture;
        const auto result=App::InitDetail::wait_for_game_terminal(a.context,nullptr,[&]()->std::optional<TerminalSize>{
            assert(++queries<12);return queries<4?small:large;
        });
        assert(result==TerminalCheckResult::Ready && queries==8);
        assert(capture.output.str().find("请放大终端")!=std::string::npos);
        assert(capture.output.str().find("终端尺寸已足够")!=std::string::npos);
        assert((app.acceptedGameTerminalSize==std::optional<std::array<int,2>>({{large.columns,large.rows}})));
    }
    // A sufficient terminal starts directly without reading controls.
    a.input.keyStateProvider=[](int){assert(false);return false;};
    {Capture capture;assert(App::InitDetail::wait_for_game_terminal(a.context,nullptr,[&]()->std::optional<TerminalSize>{return large;})==TerminalCheckResult::Ready);assert(capture.output.str().empty());}
    app.acceptedGameTerminalSize.reset();a.session->player.presentation.dynamicLayout=false;
    for(bool back:{false,true}) {
        int frame=0;
        a.user.additionalKeyCodes[9]={alternateSettings};
        Input::refresh_bindings(a.input,a.user.keyCodes,a.user.additionalKeyCodes);
        a.input.keyStateProvider=[&](int key){return frame>=3 && key==(back?escape:alternateSettings);};
        Capture capture;bool returned=false;
        try {
            returned=App::InitDetail::wait_for_game_terminal(a.context,nullptr,[&]()->std::optional<TerminalSize>{assert(++frame<6);return small;})==TerminalCheckResult::OpenSettings;
            assert(!back && returned);
        } catch(const App::StartScreenRequested&) {assert(back && app.startScreenRequested);}
    }
    // Either local player can confirm with their own retry binding.
    Player first,second;int frame=0;
    second.user.keyCodes[8]=alternateRetry;Input::refresh_bindings(second.input,second.user.keyCodes);
    second.input.keyStateProvider=[&](int key){return key==alternateRetry && frame==3;};
    Capture capture;
    assert(App::InitDetail::wait_for_game_terminal(first.context,&second.context,[&]()->std::optional<TerminalSize>{assert(++frame<6);return small;})==TerminalCheckResult::Ready);
    assert(frame==4);
}

void restart_repaints_information() {
    for(int goal=0;goal<=6;++goal) {
        Player a;
        a.user.goal=goal;
        a.user.blitzTime=64;
        for(int round=0;round<2;++round) {
            Capture capture;
            a.session->player.gameplay.scoring.score=12345;
            App::initialize_game(a.context);
            assert(a.session->player.gameplay.scoring.score==0);
            Test::TerminalScreen screen;
            screen.feed(capture.output.str());
            assert(screen.position_of(goal==4?"0/100 P":"0 P").first>0);
            assert(screen.position_of(goal==5?"0/100 A":"0 A").first>0);
            assert(screen.position_of("0.00 PPS").first>0);
            assert(screen.position_of("0.00 APM").first>0);
            assert(screen.position_of(goal==1?"01:04.000":"00:00.000").first>0);
        }
    }
}

void native_init_confirms_before_starting() {
#ifdef __linux__
    Capture capture;
    const int saved=::dup(STDOUT_FILENO),terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0 && ::dup2(terminal,STDOUT_FILENO)>=0);
    winsize size{};size.ws_col=80;size.ws_row=10;assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    Player a;int waits=0;
    a.input.keyStateProvider=[&](int key){return key==a.user.keyCodes[8] && waits>0 && waits<3;};
    a.services.clock.waitInterrupt=[&]{++waits;assert(waits<10);};
    App::initialize_game(a.context);
    assert(waits>=3);
    assert(!a.session->gameStartTime);
    Test::TerminalScreen screen;screen.feed(capture.output.str());
    assert(screen.columns()<=80 && screen.rows()<=10);
    assert(capture.output.str().find("请放大终端")!=std::string::npos);
    assert(::dup2(saved,STDOUT_FILENO)>=0);::close(saved);::close(terminal);
#endif
}

void native_room_checks_before_preparation() {
#ifdef __linux__
    Capture capture;
    const int saved=::dup(STDOUT_FILENO),terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0 && ::dup2(terminal,STDOUT_FILENO)>=0);
    winsize size{};size.ws_col=70;size.ws_row=20;assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
    Player a;App::ApplicationState app;a.context.attach_app(app);app.primaryContext=&a.context;
    a.user.gameMode=4;
    auto& network=a.network;network.initialized=true;network.connection=Network::ConnectionState::Connected;
    network.serverIp=a.user.serverIp;network.multiplayerRoom=true;network.room={"SIZE-CHECK",true,1,32};
    network.roomPlayerIndex=0;network.roomInstance=network.roleRoomInstance=1;
    network.roomOnlineMode=true;network.roomGameMode=4;
    network.roomMembers={{0,"Host",true}};
    struct PromptReached {};
    a.services.clock.waitInterrupt=[&] {
        assert(capture.output.str().find("请放大终端")!=std::string::npos);
        assert(!network.pendingRoomRequest && !network.queuedMatchControl);
        assert(network.matchPhase==Network::RoomMatchPhase::Idle);
        throw PromptReached{};
    };
    bool checked=false;
    try {App::initialize_game(a.context);} catch(const PromptReached&) {checked=true;}
    assert(checked);
    assert(capture.output.str().find("Preparing player configuration")==std::string::npos);
    assert(::dup2(saved,STDOUT_FILENO)>=0);::close(saved);::close(terminal);
#endif
}

}

void positioned_output_batches_console_writes() {
    Player a;auto& p=a.session->player;
    p.presentation.dynamicLayout=true;p.presentation.terminalColumns=120;p.presentation.terminalRows=40;
    struct Counter : std::stringbuf {
        int writes=0;
        std::streamsize xsputn(const char* text,std::streamsize size) override {
            ++writes;return std::stringbuf::xsputn(text,size);
        }
    } counter;
    auto* previous=std::cout.rdbuf(&counter);
    {
        Render::OutputGuard output(a.context);
        for(int col=0;col<4;++col) Render::draw_board_cell(a.context,p,10,col,Runtime::BoardCell::garbage());
    }
    std::cout.rdbuf(previous);
    assert(!counter.str().empty());
#ifdef _WIN32
    assert(counter.writes==1);
#endif
    std::cerr<<"positioned output writes="<<counter.writes<<" bytes="<<counter.str().size()<<'\n';
}

int run(int argc,char** argv) {
    positioned_output_batches_console_writes();
    App::UiText::set_language(App::UiText::Language::Chinese);
    geometry();
    centered_geometry();
    information_aligned_duels();
    vertical_priorities();
    native_game_resize();
    native_resize_during_live_piece();
    native_local_resize();
    confirmed_clipping();
    size_confirmation_controls();
    restart_repaints_information();
    native_init_confirms_before_starting();
    native_room_checks_before_preparation();
    return 0;
}
} // namespace TetrTests::game_terminal_test
