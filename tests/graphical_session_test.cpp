#include "tetr/frontend/graphical/session.h"
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/input/services.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/presentation/context_view.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "input.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {
using namespace std::chrono_literals;
const char* phase="initialization";
template<class Predicate> Graphical::SessionView wait(Graphical::Session& session,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+8s;
    for(;;) {
        auto view=session.view();
        if(predicate(view)) return view;
        if(view.status==Graphical::SessionStatus::Error) {std::cerr<<view.message<<'\n';assert(false);}
        if(std::chrono::steady_clock::now()>=deadline) {
            std::cerr<<"Timeout in "<<phase<<": status="<<static_cast<int>(view.status)
                <<" pieces="<<view.board.pieces<<" elapsed="<<view.board.elapsed.seconds()<<'\n';
            assert(false);
        }
        std::this_thread::sleep_for(2ms);
    }
}
void tap(Graphical::Session& session,int key) {
    session.input().update(key,true);std::this_thread::sleep_for(20ms);
    session.input().update(key,false);std::this_thread::sleep_for(25ms);
}
}
int main() {
    const auto original=std::filesystem::current_path();
    const auto directory=std::filesystem::temp_directory_path()/("termino-graphical-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);std::filesystem::current_path(directory);
    UserConfig::Settings settings;settings.keyCodes=UserConfig::default_key_codes();
    settings.goal=4;settings.pieceTarget=2;settings.gravityInterval=10;
    Presentation::BoardView final;
    std::filesystem::path recording;
    {
        Graphical::Session live(settings);
        wait(live,[](const auto& view){return view.status==Graphical::SessionStatus::Playing && view.board.active.has_value();});
        tap(live,settings.keyCodes[7]);
        assert(live.view().board.hold.valid());
        tap(live,settings.keyCodes[0]);tap(live,settings.keyCodes[4]);
        tap(live,settings.keyCodes[3]);
        phase="first drop";
        wait(live,[](const auto& view){return view.board.pieces==1;});
        const auto frozenCopy=live.view();
        tap(live,settings.keyCodes[11]);
        phase="undo";
        wait(live,[](const auto& view){return view.board.pieces==0;});
        assert(frozenCopy.board.pieces==1); // value-owned mailbox
        tap(live,settings.keyCodes[12]);
        phase="pause";
        auto paused=wait(live,[](const auto& view){return view.status==Graphical::SessionStatus::Paused;});
        std::this_thread::sleep_for(60ms);
        assert(std::abs(live.view().board.elapsed.seconds()-paused.board.elapsed.seconds())<.02);
        tap(live,settings.keyCodes[12]);
        tap(live,settings.keyCodes[3]);tap(live,settings.keyCodes[1]);tap(live,settings.keyCodes[3]);
        phase="goal";
        final=wait(live,[](const auto& view){return view.status==Graphical::SessionStatus::Finished;}).board;
        live.request_save();
        phase="save";
        wait(live,[](const auto& view){return view.message.find("Saved:")==0;});
        for(const auto& file:std::filesystem::directory_iterator(".tetr/replay"))
            if(file.is_regular_file() && file.path().filename().string().find("gl-")==0) recording=std::filesystem::absolute(file.path());
        assert(!recording.empty());
        live.request_menu();wait(live,[](const auto& view){return view.status==Graphical::SessionStatus::Menu;});
    }
    Graphical::SessionView playback;
    {
        phase="graphical replay";
        Graphical::Session viewer(settings,recording.generic_u8string());
        playback=wait(viewer,[](const auto& view){return view.status==Graphical::SessionStatus::Finished;});
        assert(playback.replay && playback.board.pieces==final.pieces);
        phase="replay pause/speed/seek";
        tap(viewer,settings.keyCodes[12]);
        wait(viewer,[](const auto& view){return view.replayPaused;});
        tap(viewer,Graphical::native_key(SDL_SCANCODE_MINUS));
        wait(viewer,[&](const auto& view){return view.replaySpeed==playback.replaySpeed/2;});
        tap(viewer,Graphical::native_key(SDL_SCANCODE_LEFT));
        const auto rewound=wait(viewer,[](const auto& view){return view.status==Graphical::SessionStatus::Paused && view.replayPosition<.01;});
        std::this_thread::sleep_for(40ms);
        assert(viewer.view().replayPosition==rewound.replayPosition);
        tap(viewer,Graphical::native_key(SDL_SCANCODE_EQUALS));
        tap(viewer,settings.keyCodes[12]);
        playback=wait(viewer,[](const auto& view){return view.status==Graphical::SessionStatus::Finished;});
    }
    // Replay the saved GUI recording via the original App initialization/run
    // entry points without the graphical worker. Compare semantic final state.
    {
        Render::DetachedOutputScope output;
        auto state=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
        Runtime::GameContext context(*state,services);Input::Services input;Input::FocusState focus;
        App::ApplicationState app;Replay::ReplaySessionState replay;
        settings.gameMode=5;settings.replayPath=recording.generic_u8string();
        context.attach_input(input);context.attach_user_settings(settings);context.attach_app(app);context.attach_replay(replay);
        app.primaryContext=&context;input.focusSource=&focus;input.keyEventSource=&focus.events();input.initialized=true;
        bool finished=false;app.roundFinished=[&]{finished=true;};
        const auto deadline=std::chrono::steady_clock::now()+8s;
        services.clock.waitInterrupt=[&]{assert(std::chrono::steady_clock::now()<deadline);};
        App::initialize_game(context);App::run(context);assert(finished);
        const auto terminal=Presentation::capture_board_view(context,state->player);
        assert(terminal.pieces==final.pieces && terminal.score==final.score && terminal.lines==final.lines && terminal.attack==final.attack);
        assert(terminal.cells.size()==final.cells.size());
        for(std::size_t i=0;i<final.cells.size();++i) {
            assert(terminal.cells[i].value==final.cells[i].value);
            assert(playback.board.cells[i].value==final.cells[i].value);
        }
        state->player.timing.remoteGameTime=Runtime::Duration::from_seconds(2);
        auto& remote=state->player.presentation;remote.remotePlacementRevision=1;
        remote.remoteSpinPiece=Piece::Type::from_index(5);remote.remoteMini=true;
        remote.remoteClearLines=2;remote.remotePerfectClear=true;
        using G=Network::RemoteGarbageCell;
        remote.garbageBufferDisplay={static_cast<int>(G::Ready),static_cast<int>(G::EarlyBuffer),static_cast<int>(G::Incoming),static_cast<int>(G::Empty)};
        const auto remoteHud=Presentation::capture_board_view(context,state->player);
        assert(remoteHud.spinPiece.valid() && remoteHud.mini && remoteHud.clearLines==2 && remoteHud.perfectClear);
        assert(remoteHud.garbage==std::vector<int>({1,1,1}));
        assert(remoteHud.garbageReadyIn[0]==0 && remoteHud.garbageReadyIn[1]==.1 && remoteHud.garbageReadyIn[2]==1);
        Render::end_output_suppression();assert(Render::output_suppressed());
        assert(!Render::terminal_size());
    }
    assert(!Render::output_suppressed());
    // Malformed recordings report an error, never silently start a live game.
    {Graphical::Session bad(settings,"missing.txt");wait(bad,[](const auto& view){return view.status==Graphical::SessionStatus::Error;});}
    std::filesystem::current_path(original);std::filesystem::remove_all(directory);
    std::cerr<<"Graphical session: live/hold/undo/pause/save/replay/original-run equivalence/cancel OK\n";
}
