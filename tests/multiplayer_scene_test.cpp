#include "tetr/engine/multiplayer_scene.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/network/network_services.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/runtime/game_context.h"
#include "tetr/render/board_renderer.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/lobby_protocol.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/input/services.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/terminal_canvas.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <memory>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>

namespace TetrTests::multiplayer_scene_test {
void save_screen(const Test::TerminalScreen& screen,const char* path,int columns,int rows) {
    std::ofstream out(path);out<<"{\"columns\":"<<columns<<",\"rows\":"<<rows<<",\"cells\":[";
    bool first=true;
    for(int row=1;row<=rows;++row) for(int col=1;col<=columns;++col) {
        const auto cell=screen.at(row,col);if(!first) out<<',';first=false;
        out<<'['<<col-1<<','<<row-1<<','<<std::quoted(cell.glyph)<<",["<<cell.foreground[0]<<','<<cell.foreground[1]<<','<<cell.foreground[2]<<"],["<<cell.background[0]<<','<<cell.background[1]<<','<<cell.background[2]<<"]]";
    }
    out<<"]}";
}
int run(int argc,char** argv) {
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};Network::NetworkServices network;context.attach_network(network);
    UserConfig::Settings user;user.gameMode=4;context.attach_user_settings(user);
    Input::Services input;input.initialized=true;input.keyStateProvider=[](int) {return false;};context.attach_input(input);
    Input::refresh_bindings(input,UserConfig::default_key_codes());
    context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Multiplayer);
    context.pause.started=services.clock.now();context.pause.paused=true;
    network.multiplayerRoom=true;network.roomPlayerIndex=0;network.matchEpoch=1;network.matchPhase=Network::RoomMatchPhase::Playing;
    const int count=argc>1?std::stoi(argv[1]):32;
    for(int slot=0;slot<count;++slot) {
        network.roomMembers.push_back({slot,"P"+std::to_string(slot),slot==0,Network::PlayerRole::Battle,true,true,true});
        Network::LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.blockMaxWidth=3;setup.blockMaxHeight=2;
        setup.nextCount=3;setup.nextPreview={Piece::Type{0},Piece::Type{1},Piece::Type{2}};setup.holdEnabled=true;
        network.playerSetups[slot]=setup;
    }
    Engine::initialize_multiplayer_scene(context);
    network.onlineInbox.push_back(std::string(Network::encode_server_lobby_signal(Network::ServerLobbySignal::RoundReady)));
    assert(!Network::try_receive_multiplayer_frame(context));
    assert(network.onlineInbox.size()==1); // Gameplay polling must preserve the start handshake.
    network.onlineInbox.clear();
    auto& local=session->player;local.gameplay.playfield.width=10;local.gameplay.playfield.visibleHeight=20;
    local.gameplay.playfield.activeHeight=20;local.gameplay.playfield.nextCount=3;local.gameplay.playfield.holdEnabled=true;
    local.pieces.nextPreview={Piece::Type{0},Piece::Type{1},Piece::Type{2}};
    local.gameplay.playfield.field.initialize(10,20);
    const auto decorate=[](Runtime::PlayerSessionState& p) {
        auto& field=p.gameplay.playfield.field;field.initialize(p.gameplay.playfield.width,p.gameplay.playfield.visibleHeight);
        for(int row=15;row<20;++row) for(int col=0;col<10;++col) if(col!=4) {
            const auto cell=row==15?Runtime::BoardCell::piece(col%7):row==16?Runtime::BoardCell::garbage():
                row==17?Runtime::BoardCell::solid_garbage():row==18?Runtime::BoardCell::timed_solid(3):Runtime::BoardCell::timed_release();
            field.set_cell(row,col,cell);
        }
        p.gameplay.hold.piece=Piece::Type{0};p.presentation.activePose=Piece::Pose{{0,4},Runtime::Rotation{}};
        p.presentation.activePieceType=Piece::Type{0};
    };
    decorate(local);for(auto& [slot,p]:session->roomScene.players) decorate(p);
    // The final two fields should visually straddle the terminal center;
    // reserved blank space for erasing side text must not shift them right.
    if(count>=2) {
        const auto survivors=session->roomScene.survivors;session->roomScene.survivors={0,1};
        Engine::position_multiplayer_scene(context,{240,80});
        const auto first=local.presentation,second=session->roomScene.players.at(1).presentation;
        const int fieldCenters=first.margin+local.gameplay.playfield.width+second.margin+session->roomScene.players.at(1).gameplay.playfield.width;
        assert(std::abs(fieldCenters-240)<=6);
        assert(first.topMargin==second.topMargin);
        session->roomScene.survivors=survivors;
    }
    if(count>2) {
        Engine::position_multiplayer_scene(context,{120,40});const auto compact=session->roomScene.miniSlots.size();
        assert(compact>0 && compact<=std::size_t(count-1));
        if(count==32)assert(compact<std::size_t(count-1));
        Engine::position_multiplayer_scene(context,{400,100});
        assert(session->roomScene.miniSlots.size()==std::size_t(count-1));
        assert(Render::board_horizontal_extent(context,local,false).left>1);
        for(std::size_t i=0;i<session->roomScene.miniSlots.size();++i) {
            const auto& p=session->roomScene.players.at(session->roomScene.miniSlots[i]);
            const int right=session->roomScene.galleryColumn+int(i)%session->roomScene.miniColumns*session->roomScene.miniCellWidth+
                std::min(session->roomScene.miniCellWidth-3,p.gameplay.playfield.width*2)+2;
            const int bottom=session->roomScene.galleryTop+int(i)/session->roomScene.miniColumns*session->roomScene.miniCellHeight+
                std::min(session->roomScene.miniCellHeight-3,p.gameplay.playfield.visibleHeight)+3;
            assert(right<400 && bottom<100);
        }
    }
    const auto render=[&](bool force) {
        session->roomScene.lastPaint=context.real_now()-Runtime::Duration::from_seconds(0.1);
        std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());Engine::draw_multiplayer_scene(context,force);std::cout.rdbuf(previous);
        return output.str();
    };
    const auto capture=[&] {
        const auto output=render(true);
        Test::TerminalScreen screen;screen.feed(output);return screen;
    };
    auto screen=capture();
    {
        const auto origin=context.real_now();const auto clock=context.pause.started;
        std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
        context.pause.started+=Runtime::Duration::from_seconds(.010);
        Engine::draw_multiplayer_scene(context);
        assert(std::abs((session->roomScene.lastPaint-origin).seconds()-1./120)<1e-6);
        context.pause.started+=Runtime::Duration::from_seconds(.008);
        Engine::draw_multiplayer_scene(context);
        assert(std::abs((session->roomScene.lastPaint-origin).seconds()-2./120)<1e-6);
        std::cout.rdbuf(previous);context.pause.started=clock;session->roomScene.lastPaint={};
    }
    {
        const auto origin=context.real_now();const auto clock=context.pause.started;const auto timing=local.timing;
        const auto enabled=network.boardSync.enabled;network.boardSync.enabled=true;local.timing.framePoll=origin;
        std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
        context.pause.started+=Runtime::Duration::from_seconds(.010);
        Engine::run_frame(context,local.gameplay.playfield.field,local,true,true);
        assert(std::abs((local.timing.framePoll-origin).seconds()-1./120)<1e-6);
        context.pause.started+=Runtime::Duration::from_seconds(.008);
        Engine::run_frame(context,local.gameplay.playfield.field,local,true,true);
        assert(std::abs((local.timing.framePoll-origin).seconds()-2./120)<1e-6);
        std::cout.rdbuf(previous);context.pause.started=clock;local.timing=timing;network.boardSync.enabled=enabled;session->roomScene.lastPaint={};
    }
    if(count==2) {
        // An unchanged paused peer should produce no terminal output: names
        // and statistics must not become a 60 Hz console workload.
        const auto unchanged=render(false);assert(unchanged.empty());
    }
    assert(screen.rows()<=40 && screen.columns()<=120);
    for(int slot=0;slot<count;++slot) {
        const auto full=Engine::full_multiplayer_slots(context);
        const bool shown=std::find(full.begin(),full.end(),slot)!=full.end() ||
            std::find(session->roomScene.miniSlots.begin(),session->roomScene.miniSlots.end(),slot)!=session->roomScene.miniSlots.end();
        if(shown) assert(screen.position_of("P"+std::to_string(slot)+" ").first>0);
    }
    if(count>2) {
        const int col=session->roomScene.galleryColumn+1;
        for(int row=1;row<=screen.rows();++row) for(int x=col;x<=screen.columns();++x)
            assert(screen.at(row,x).glyph!="N" && screen.at(row,x).glyph!="H"); // No NEXT/HOLD side panels in miniatures.
        // The falling piece at the top stays out of the miniature.
        assert(screen.at(session->roomScene.galleryTop+2,col+1).glyph==" ");
    }
    if(argc>2) save_screen(screen,argv[2],120,40);
    if(count>3) {
        network.roomMembers[0].alive=false;network.roomMembers[0].killer=2;Engine::update_multiplayer_scene(context);
        assert(session->roomScene.focusSlot==2);
        network.roomMembers[2].alive=false;network.roomMembers[2].killer=3;Engine::update_multiplayer_scene(context);
        assert(session->roomScene.focusSlot==3);
        {std::ostringstream ignored;auto* previous=std::cout.rdbuf(ignored.rdbuf());Engine::cycle_multiplayer_focus(context);std::cout.rdbuf(previous);}
        assert(session->roomScene.focusSlot!=3);
        for(auto& m:network.roomMembers) m.alive=m.slot==1 || m.slot==3;
        Engine::update_multiplayer_scene(context);screen=capture();
        assert(screen.position_of("P1 ").first>0 && screen.position_of("P3 ").first>0);
        assert(screen.position_of("P0 ").first==0);
        const auto firstPair=Engine::full_multiplayer_slots(context);
        {std::ostringstream ignored;auto* previous=std::cout.rdbuf(ignored.rdbuf());Engine::cycle_multiplayer_focus(context);std::cout.rdbuf(previous);}
        const auto switchedPair=Engine::full_multiplayer_slots(context);
        assert(firstPair.size()==2 && switchedPair.size()==2 && firstPair.front()!=switchedPair.front());
        assert(switchedPair.front()==session->roomScene.focusSlot);
    }
    // A sequence of remote HOLD/NEXT/active/checkpoint updates must have the
    // same final cells as a fresh redraw, without clearing the screen.
    const int remoteSlot=session->roomScene.survivors.back();
    if(remoteSlot!=session->roomScene.ownSlot) {
        auto& remote=session->roomScene.players.at(remoteSlot);screen=capture();
        for(int step=0;step<8;++step) {
            remote.gameplay.hold.piece=step==7?Piece::Type::none():Piece::Type{step%7};
            remote.gameplay.hold.remoteAvailable=step%2==0;
            remote.pieces.nextPreview={Piece::Type{(step+1)%7},Piece::Type{(step+2)%7},Piece::Type{(step+3)%7}};
            remote.presentation.activePieceType=Piece::Type{step%7};
            remote.presentation.activePose=Piece::Pose{{step+1,3+step%3},Runtime::Rotation{step%4}};
            remote.presentation.nextSpawnType=remote.pieces.nextPreview.front();
            remote.presentation.garbageBufferDisplay=step%2?std::vector<int>{1,2,3}:std::vector<int>{};
            if(step==3) remote.gameplay.playfield.field.set_cell(14,8,Runtime::BoardCell::timed_solid(2));
            if(step==4) remote.gameplay.playfield.field.set_cell(14,8,Runtime::BoardCell{});
            const auto update=render(false);assert(update.find("\x1b[2J")==std::string::npos);
            assert(update.size()<14000);screen.feed(update);
            const auto fresh=capture();
            for(int row=1;row<=40;++row) for(int col=1;col<=120;++col) {
                const auto a=screen.at(row,col),b=fresh.at(row,col);
                if(a.glyph!=b.glyph || a.background!=b.background ||
                   (a.glyph!=" " && !a.glyph.empty() && a.foreground!=b.foreground)) {
                    std::cerr<<"remote update mismatch step="<<step<<" row="<<row<<" col="<<col<<" old="<<a.glyph<<" new="<<b.glyph<<'\n';
                    assert(false);
                }
            }
        }
        // A placement followed by a motion checkpoint never replays the old
        // placement to erase the newly spawned active piece.
        Network::BoardStateSnapshot locked;locked.field=remote.gameplay.playfield.field;locked.placement=Network::RemotePlacementSnapshot{};
        locked.currentPiece=Piece::Type{0};Engine::apply_remote_board_state(context,remote,locked,false);
        assert(!remote.presentation.activePose);
        locked.placement.reset();locked.activePose={{3,3},Runtime::Rotation{}};
        Engine::apply_remote_board_state(context,remote,locked,false);
        render(false);assert(remote.presentation.activePose==locked.activePose);
        for(int row=0;row<10;++row) for(int col=0;col<10;++col)
            locked.field.set_cell(row,col,Runtime::BoardCell::garbage());
        Engine::apply_remote_board_state(context,remote,locked,false);
        assert(!remote.presentation.activePose && !remote.presentation.activePieceType);
    }
    if(session->roomScene.survivors.size()==2) {
        const auto final=session->roomScene.survivors;
        const auto layout=Engine::full_multiplayer_slots(context);
        session->gameStartTime=context.now()-Runtime::Duration::from_seconds(10);
        for(auto& m:network.roomMembers) if(m.slot==layout.front()) {m.alive=false;m.killer=layout.back();}
        network.matchPhase=Network::RoomMatchPhase::Finished;network.winnerSlot=layout.back();
        Engine::update_multiplayer_scene(context);screen=capture();
        assert(Engine::full_multiplayer_slots(context)==layout); // A right-side winner keeps its board on the right.
        assert(screen.position_of("WIN").first>0 && screen.position_of("LOSE").first>0);
        for(int slot:final) assert(screen.position_of("P"+std::to_string(slot)+" ").first>0);
        auto& remote=session->roomScene.players.at(final.back());
        assert(remote.timing.completedGameTime && remote.timing.completedGameTime->seconds()==10);
        context.pause.started+=Runtime::Duration::from_seconds(5);
        Engine::update_multiplayer_scene(context);
        std::ostringstream frozen;auto* old=std::cout.rdbuf(frozen.rdbuf());Render::print_message(context,remote);std::cout.rdbuf(old);
        screen.feed(frozen.str());
        assert(screen.text().find("10.00")!=std::string::npos && screen.text().find("15.00")==std::string::npos);
    }
    // Scores belong to Zen/Blitz; battle names occupy exactly the same row.
    local.gameplay.scoring.score=12345678;local.presentation.username="abcdef0123456789uvwxyz";
    for(const auto mode:{Runtime::GameMode::Zen,Runtime::GameMode::Blitz,Runtime::GameMode::Racing,
        Runtime::GameMode::QuickPlay,Runtime::GameMode::Multiplayer,Runtime::GameMode::LocalVersus}) {
        context.config.mode.mode=mode;
        std::ostringstream out;auto* old=std::cout.rdbuf(out.rdbuf());Render::print_score(context,local);std::cout.rdbuf(old);
        Test::TerminalScreen footer;footer.feed(out.str());
        if(mode==Runtime::GameMode::Zen || mode==Runtime::GameMode::Blitz) assert(footer.text().find("12345678")!=std::string::npos);
        else assert(footer.text().find("12345678")==std::string::npos);
        if(mode==Runtime::GameMode::Multiplayer || mode==Runtime::GameMode::LocalVersus)
            assert(footer.position_of("...").first==local.presentation.topMargin+local.gameplay.playfield.visibleHeight+2);
    }
    context.config.mode.mode=Runtime::GameMode::Multiplayer;
    if(session->roomScene.finalists.size()==2) {
        context.pause={};session->gameStartTime.reset();
        network.connection=Network::ConnectionState::Connected;network.matchPhase=Network::RoomMatchPhase::Countdown;
        bool checked=false;std::ostringstream digits;auto* previous=std::cout.rdbuf(digits.rdbuf());
        const auto result=Engine::run_start_countdown(context,local,session->opponent,nullptr,[&] {
            if(!checked) {
                Test::TerminalScreen countdown;countdown.feed(digits.str());
                for(int slot:Engine::full_multiplayer_slots(context)) {
                    const auto& p=slot==session->roomScene.ownSlot?local:session->roomScene.players.at(slot);
                    assert(countdown.at(p.presentation.topMargin+2,p.presentation.margin+8).glyph=="┏");
                }
                checked=true;
            }
            return false;
        });
        std::cout.rdbuf(previous);assert(checked && result==Engine::CountdownResult::Completed);
    }
    App::UiText::set_language(App::UiText::Language::English);
    network.room={"WAIT",true,count,32};network.matchPhase=Network::RoomMatchPhase::Preparing;
    network.connection=Network::ConnectionState::Connected;network.roomPlayerIndex=0;
    for(auto& m:network.roomMembers) m.ready=m.slot<2;
    App::TerminalCanvas waiting;std::size_t offset=0;
    std::ostringstream out;auto* old=std::cout.rdbuf(out.rdbuf());
    App::paint_room_waiting(context,waiting,{80,35},1,offset);std::cout.rdbuf(old);
    Test::TerminalScreen readyScreen;readyScreen.feed(out.str());
    assert(readyScreen.position_of("> Start match now").first>0);
    assert(readyScreen.position_of("Cancel match").first==0);
    assert(readyScreen.position_of("[HOST]").first>0);
    if(count>2) assert(readyScreen.position_of("Not ready").first>0);
    assert(readyScreen.rows()<=35 && readyScreen.columns()<=80);
    if(argc>3) save_screen(readyScreen,argv[3],80,35);
    // Remote work must leave captured local actions for the movement loop.
    Input::KeyEventBuffer queuedInput;input.keyEventSource=&queuedInput;input.set_continue_input_unfocused(true);
    {
        Input::GameplayInputScope capture(&input);
        queuedInput.update(UserConfig::default_key_codes()[1],true);
        const auto painted=session->roomScene.lastPaint;
        session->roomScene.lastPaint=context.real_now()-Runtime::Duration::from_seconds(1);
        std::ostringstream deferred;auto* old=std::cout.rdbuf(deferred.rdbuf());
        Engine::poll_multiplayer_scene(context);Engine::draw_multiplayer_scene(context);
        std::cout.rdbuf(old);
        assert(input.next_gameplay_event() && deferred.str().empty());
        input.consume_gameplay_event();session->roomScene.lastPaint=painted;
    }
    input.keyEventSource=nullptr;
    std::cout<<"multiplayer scene passed\n";
    return 0;
}
} // namespace TetrTests::multiplayer_scene_test
