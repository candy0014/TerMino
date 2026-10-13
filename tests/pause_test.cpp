#include "tetr/app/application_state.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/pause.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/undo.h"
#include "tetr/input/services.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/runtime/game_context.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include "support/terminal_screen.h"

namespace {
struct Fixture {
    App::ApplicationState app;
    std::unique_ptr<Runtime::SessionState> session = std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    UserConfig::Settings user;
    Replay::ReplaySessionState replay;
    Input::Services input;
    Engine::PieceLoopState loop;
    Fixture() {
        user.keyCodes = UserConfig::default_key_codes();
        user.fieldWidth = 4; user.fieldHeight = 6; user.nextCount = 0;
        context.attach_app(app); context.attach_input(input); context.attach_user_settings(user);
        context.attach_replay(replay);
        context.config = Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        app.primaryContext = &context;
        Input::refresh_bindings(input,user.keyCodes);
        input.keyStateProvider = [](int) { return false; };
        auto& field = session->player.gameplay.playfield;
        field.width = 4; field.visibleHeight = 6; field.nextCount = 0;
        session->player.pieces.allowedPieces = Piece::types_for_rotation_system(context.config.rotationSystem);
        Engine::initialize_board(context,session->player);
        Engine::initialize_board_timers(context,session->player);
        replay.data = Replay::encode_config_prefix(context.config) + "}";
        Engine::begin_undo_history(context,&loop);
        App::install_start_screen_navigation(context);
    }
};
void paused_undo_and_resume() {
    Fixture f;
    Engine::GamePauseScope scope(f.context);
    auto& player=f.session->player;
    player.gameplay.scoring.placedBlocks=1;
    player.gameplay.playfield.field.set_cell(5,0,Runtime::BoardCell::garbage());
    Engine::capture_undo_snapshot(f.context);
    const int pause=f.user.keyCodes[12],undo=f.user.keyCodes[11];
    const auto origin=std::chrono::steady_clock::now();
    f.input.keyStateProvider=[&](int key) {
        if (key==pause) return !f.context.pause.paused;
        return key==undo && std::chrono::steady_clock::now()-origin>std::chrono::milliseconds(15);
    };
    bool undone=false;
    try { f.input.key_down(f.user.keyCodes[0]); }
    catch (const Engine::UndoRequested&) { undone=true; }
    assert(undone && f.context.pause.paused && f.replay.pauseCount==1);
    const auto frozen=f.context.real_now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(f.context.real_now()==frozen);
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(f.context.pause.paused && f.replay.pauseCount==1 && f.replay.undoCount==1);
    assert(player.gameplay.scoring.placedBlocks==0);
    assert(!player.gameplay.playfield.field.cell(5,0).occupied());
    const auto restored=f.context.now();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(f.context.now()==restored);
    const auto resumeAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(15);
    f.input.keyStateProvider=[&](int key) { return key==pause && std::chrono::steady_clock::now()>=resumeAt; };
    f.input.key_down(f.user.keyCodes[0]);
    assert(!f.context.pause.paused && f.replay.pauseCount==1);
    assert(f.context.pause.removedTime>=Runtime::Duration::from_seconds(.03));
    assert((f.context.now()-restored).seconds()<.01);
    assert((f.context.real_now()-frozen).seconds()<.01);
    const auto saved=Replay::recording_for_save(f.context);
    Runtime::RuntimeConfig decoded;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(saved,decoded,prefix));
    assert(prefix.undoCount==1 && prefix.pauseCount==1);
    // A held resume key does not count as another pause.
    f.input.key_down(f.user.keyCodes[0]);
    assert(!f.context.pause.paused && f.replay.pauseCount==1);
    f.input.keyStateProvider=[](int) { return false; };
    f.input.key_down(f.user.keyCodes[0]);
    const auto secondPause=std::chrono::steady_clock::now();
    f.input.keyStateProvider=[&](int key) {
        if (key==pause) return !f.context.pause.paused;
#ifdef _WIN32
        constexpr int escape=0x1b;
#else
        constexpr int escape=1;
#endif
        return key==escape && std::chrono::steady_clock::now()-secondPause>std::chrono::milliseconds(10);
    };
    bool exited=false;
    try { f.input.key_down(f.user.keyCodes[0]); }
    catch (const App::StartScreenRequested&) { exited=true; }
    assert(exited && f.app.startScreenRequested && f.replay.pauseCount==2);
    scope.end();
    assert(!f.context.pause.active && !f.context.pause.paused);
}
void disabled_and_legacy_metadata() {
    Fixture f;
    f.user.disablePause=1;
    f.context.config=Runtime::normalize_user_config(f.user,Runtime::GameMode::Zen);
    assert(!Engine::pause_enabled(f.context));
    const auto encoded=Replay::encode_config_prefix(f.context.config)+"}";
    Runtime::RuntimeConfig decoded;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(encoded,decoded,prefix));
    assert(decoded.disablePause && prefix.pauseCount==0);
    assert(!Replay::decode_config_prefix("{{pause_count,-1}}",decoded,prefix));
    assert(!Replay::decode_config_prefix("{{pause_count,18446744073709551616}}",decoded,prefix));
    for (const auto mode : {Runtime::GameMode::Multiplayer,Runtime::GameMode::LocalVersus}) {
        f.context.config.disablePause=false; f.context.config.mode.mode=mode;
        assert(!Engine::pause_enabled(f.context));
    }
    f.context.config.mode.mode=Runtime::GameMode::Zen;
    f.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    assert(!Engine::pause_enabled(f.context));
    Fixture viewer;
    viewer.context.config.mode.replayMode=true;
    Engine::begin_undo_history(viewer.context,&viewer.loop);
    viewer.replay.playback.paused=true;
    assert(Engine::restore_previous_undo_snapshot(viewer.context));
    assert(viewer.replay.playback.paused);
}
void migration_display_and_short_press() {
    const auto path=std::filesystem::temp_directory_path()/"termino_pause_migration.txt";
    const auto defaults=UserConfig::default_key_codes();
    {
        std::ofstream file(path);
        for (std::size_t i=0;i<12;++i) file<<(i==0 ? defaults[12] : defaults[i])<<'\n';
    }
    UserConfig::Settings loaded;
    UserConfig::load_keybindings(loaded,path.string());
    assert(loaded.keyCodes[0]==defaults[12] && loaded.keyCodes[12]==-1);
    {
        std::ofstream file(path);
        file<<"[CONTROLS]\nMove Piece Left = "<<defaults[12]<<'\n';
    }
    loaded.keyCodes=defaults;
    UserConfig::load(loaded,path.string());
    assert(loaded.keyCodes[0]==defaults[12] && loaded.keyCodes[12]==-1);
    std::filesystem::remove(path);
    Fixture f;
    f.context.config.mode.replayMode=true;
    f.session->player.presentation.margin=4;
    f.replay.undoCount=5; f.replay.pauseCount=7;
    std::ostringstream output;
    auto* prior=std::cout.rdbuf(output.rdbuf());
    Engine::render_replay_playback_progress(f.context,f.session->player);
    std::cout.rdbuf(prior);
    Test::TerminalScreen screen;
    screen.feed(output.str());
    const auto undo=screen.position_of("撤回：5");
    const auto pause=screen.position_of("暂停：7");
    // The narrow area at the left edge wraps the two counters onto short rows.
    assert(undo.first!=0 && pause.first==undo.first+1 && pause.second==undo.second);
    f.context.config.mode.replayMode=false;
    Input::KeyEventBuffer events;
    f.input.keyEventSource=&events;
    Engine::GamePauseScope scope(f.context);
    events.update(defaults[12],true);
    events.update(defaults[12],false);
    const auto start=std::chrono::steady_clock::now();
    f.input.keyStateProvider=[start](int key) {
#ifdef _WIN32
        constexpr int escape=0x1b;
#else
        constexpr int escape=1;
#endif
        return key==escape && std::chrono::steady_clock::now()-start>std::chrono::milliseconds(5);
    };
    bool interrupted=false;
    try { f.input.key_down(defaults[0]); }
    catch (const App::StartScreenRequested&) { interrupted=true; }
    assert(interrupted && f.replay.pauseCount==8 && f.context.pause.paused);
    f.input.keyEventSource=nullptr;
}
void event_timestamps_exclude_only_prior_pause_time() {
    Fixture f;
    const auto native=std::chrono::steady_clock::now();
    const auto origin=f.services.clock.at(native);
    using Runtime::Duration;
    f.context.pause.intervals={
        {origin+Duration::from_seconds(1),origin+Duration::from_seconds(3),{}},
        {origin+Duration::from_seconds(4),origin+Duration::from_seconds(6),Duration::from_seconds(2)}};
    f.context.pause.removedTime=Duration::from_seconds(4);
    for (const auto& [wall,expected] : {std::pair{0,0},std::pair{2,1},std::pair{3,1},
            std::pair{4,2},std::pair{5,2},std::pair{7,3}}) {
        const auto actual=f.context.real_time_at(native+std::chrono::seconds(wall));
        assert(std::abs((actual-origin).seconds()-expected)<1e-9);
    }
    f.context.pause.paused=true;
    f.context.pause.started=origin+Duration::from_seconds(8);
    assert(std::abs((f.context.real_time_at(native+std::chrono::seconds(9))-origin).seconds()-4)<1e-9);
}
}
int main() {
    std::ostringstream output;
    auto* previous=std::cout.rdbuf(output.rdbuf());
    paused_undo_and_resume();
    disabled_and_legacy_metadata();
    migration_display_and_short_press();
    event_timestamps_exclude_only_prior_pause_time();
    std::cout.rdbuf(previous);
}
