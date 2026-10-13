#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "tetr/app/application_state.h"
#include "tetr/app/init.h"
#include "tetr/app/game.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/config/user_config.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/rules/garbage_queue.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>

// Fast transport fixture, not a BOT benchmark or an actual FT7 result.
struct DropBot : Autoplay::Strategy {
    bool initialize(Runtime::GameContext&) override { return true; }
    Piece::Type choose_position(Runtime::GameContext&, Runtime::PlayerSessionState&,
        Runtime::Playfield field, const std::vector<Piece::Type>& next,
        const std::vector<Piece::Type>&, Piece::Pose spawn, Autoplay::HoldInput,
        Piece::Pose& target, Runtime::RotationRecord& rotation) override {
        const auto piece = Piece::get_definition(next[0]);
        target = spawn;
        while (piece.can_place(target.translated(1, 0), field)) ++target.position.row;
        rotation = Runtime::RotationRecord::none();
        return next[0];
    }
    std::string movement_script(Runtime::GameContext&, Runtime::Playfield,
        Piece::Definition, Piece::Pose, Piece::Pose, Runtime::RotationRecord) override { return "H"; }
};
struct Side {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};
    UserConfig::Settings settings;
    Replay::ReplaySessionState replay;
    Network::NetworkServices network;
    Input::Services input;
    DropBot bot;
    Side(App::ApplicationState& app, int index) {
        context.attach_app(app); context.attach_network(network); context.attach_input(input);
        context.attach_autoplay(bot); context.attach_replay(replay); context.attach_user_settings(settings);
        context.attach_output_mutex(app.terminalOutputMutex);
        settings.gameMode=6; settings.autoplayMode=9; settings.autoplayPps=settings.simulationPps=4;
        settings.turnPieces=1;
        replay.tetrioRecorder=std::make_shared<Replay::TetrioRecorder>(1,index);
    }
};

static void check_garbage_packet_boundaries() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    context.config.garbage.incomingDelay=Runtime::Duration::from_seconds(0);
    auto& p=session.player;
    auto& f=p.gameplay.playfield;
    f.width=10; f.visibleHeight=20; f.activeHeight=20; f.field.initialize(10,20);
    p.gameplay.scoring.placedBlocks=20; // outside Opener Phase
    p.garbage.tetrioRandom.emplace(1);
    Engine::GarbageService::initialize(p, context.config.garbage.holePattern);
    for(int lines : {3,7,4}) Engine::GarbageService::add_buffer(context,p,f.field,lines);
    const auto offset=[&](int attack, int clears) {
        assert(Engine::GarbageService::offset_buffer(context,p,f.field,attack,clears)==0);
    };
    offset(2,1); // partial cancellation consumes no random draw
    assert(Rules::pending_garbage(p.garbage.buffer)==12);
    assert(p.garbage.tetrioRandom->random.state()==1);
    offset(1,1); // now the first packet is fully cancelled
    assert(p.garbage.tetrioRandom->random.state()==282475249);
    assert(p.garbage.tetrioRandom->column==1);
    Runtime::TetrioGarbageRandom expected(1);
    expected.finish_packet(10);
    std::vector<int> holes;
    for(int row=0;row<7;++row) holes.push_back(expected.row(10));
    expected.finish_packet(10);
    holes.push_back(expected.row(10));
    offset(0,0); // seven-row packet plus one row of the next: cap is eight
    assert(Rules::pending_garbage(p.garbage.buffer)==3 && f.garbageHeight==8);
    assert(p.garbage.tetrioRandom->random.state()==expected.random.state());
    for(int row=0;row<8;++row) for(int col=0;col<10;++col)
        assert(f.field.cell(12+row,col).occupied()==(col!=holes[row]));
    offset(2,1); // partially cancel the packet whose first row already rose
    assert(p.garbage.tetrioRandom->random.state()==expected.random.state());
    const int lastHole=expected.row(10);
    expected.finish_packet(10);
    offset(0,0);
    assert(p.garbage.buffer.empty() && f.garbageHeight==9);
    assert(p.garbage.tetrioRandom->random.state()==expected.random.state());
    assert(p.garbage.tetrioRandom->column==expected.column);
    assert(!f.field.cell(19,lastHole).occupied());
}
int main(int argc, char** argv) {
    assert(argc == 2);
    App::ApplicationState app;
    app.automaticLocalMatch = app.headlessLocalMatch = true;
    Side a(app,0), b(app,1);
    app.primaryContext=&a.context; app.localPlayerTwoContext=&b.context;
    Render::begin_output_suppression();
    check_garbage_packet_boundaries();
    App::initialize_game(a.context);
    assert(Render::output_suppressed());
    assert(a.session.player.pieces.tetrioRandom && b.session.player.pieces.tetrioRandom);
    App::run(a.context);
    assert(Render::output_suppressed());
    const int wa=a.session.player.gameplay.scoring.wins, wb=b.session.player.gameplay.scoring.wins;
    assert(wa+wb==1);
    assert(a.session.player.gameplay.scoring.placedBlocks>1); // test environment caps ignored
    const auto directory=std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    auto& ar=*a.replay.tetrioRecorder; auto& br=*b.replay.tetrioRecorder;
    const auto end=std::max(ar.last_frame(),br.last_frame())+1;
    std::ofstream(directory/"round_1.audit.json") << '[' << ar.audit_json() << ',' << br.audit_json() << ']';
    std::ofstream(directory/"fixture.ttrm") << "{\"version\":1,\"replay\":{\"leaderboard\":[{\"wins\":" << wa
        << "},{\"wins\":" << wb << "}],\"rounds\":[[" << ar.round_json(a.context,wa,end) << ','
        << br.round_json(b.context,wb,end) << "]]}}";
    a.replay.tetrioRecorder.reset(); b.replay.tetrioRecorder.reset();
    App::initialize_game(a.context);
    assert(!a.session.player.pieces.tetrioRandom && !a.session.player.garbage.tetrioRandom);
    assert(!b.session.player.pieces.tetrioRandom && !b.session.player.garbage.tetrioRandom);
    Render::end_output_suppression();
}
