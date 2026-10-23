#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/app/start_menu.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/engine/entry_delay.h"
#include "tetr/engine/line_clear_presentation.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/input/services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include "tetr/frontend/ui_surface.h"
#include "../src/app/ui_text.h"
#ifdef __linux__
#include <unistd.h>
#endif

#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

namespace TetrTests::start_screen_navigation_test {
namespace {
using Runtime::Duration;
#ifdef _WIN32
constexpr int escape = 0x1b;
#else
constexpr int escape = 1;
#endif

struct Player {
    std::unique_ptr<Runtime::SessionState> session = std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session, services};
    Network::NetworkServices network;
    Replay::ReplaySessionState replay;
    Input::Services input;
    UserConfig::Settings user;
    std::function<bool()> escapePressed = [] { return false; };
    explicit Player(App::ApplicationState& app) {
        context.attach_app(app);
        context.attach_network(network);
        context.attach_replay(replay);
        context.attach_input(input);
        context.attach_user_settings(user);
        context.attach_output_mutex(app.terminalOutputMutex);
        user.gameMode = 0;
        user.garbageMode = 0;
        user.nextCount = 1;
        user.keyCodes = UserConfig::default_key_codes();
        input.initialized = true;
        input.keyStateProvider = [this](int key) { return key == escape && escapePressed(); };
        Input::refresh_bindings(input, user.keyCodes);
        App::install_start_screen_navigation(context);
    }
};

struct Fixture {
    App::ApplicationState app;
    Player first{app}, second{app};
    Fixture() {
        app.primaryContext = &first.context;
        app.localPlayerTwoContext = &second.context;
        App::initialize_game(first.context);
    }
};

template<class Action>
void expect_start(Fixture& fixture, Action action) {
    const auto start = std::chrono::steady_clock::now();
    fixture.first.escapePressed = [start] {
        return std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(10);
    };
    bool interrupted = false;
    try { action(); }
    catch (const App::StartScreenRequested&) { interrupted = true; }
    assert(interrupted && fixture.app.startScreenRequested);
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    fixture.first.escapePressed = [] { return false; };
    fixture.app.startScreenRequested = false;
}
}

int run(int argc,char** argv) {
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    {
        Fixture f;
        const int confirm=Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x0d : 28;
        const int retry=f.first.user.keyCodes[8];
        int retryReads=0,guard=0;
        bool retryRequested=false;
        f.first.input.keyStateProvider=[&](int key) {
            assert(++guard<1000);
            if(key==retry) {
                if(++retryReads==4) { retryRequested=true; return true; }
                return false;
            }
            // Press Enter first; only the later bound Start key may launch.
            return key==confirm && retryReads==2;
        };
        assert(App::run_start_menu(f.first.context));
        assert(retryRequested && retryReads>=5);
    }
    {
        Fixture f;
        App::UiText::set_language(App::UiText::Language::English);
        Frontend::UiFrame frame;
        Frontend::UiSurfaceScope surface([&](Frontend::UiFrame value){frame=std::move(value);});
        const auto check=[&](const char* start,const char* settings) {
            App::draw_start_menu(f.first.context);
            assert(frame.metadata.entries.at(0).value==start);
            assert(frame.metadata.entries.at(1).value==settings);
            assert(frame.metadata.entries.at(2).value=="[Esc]");
        };
        check("[R]","[P]");
        const auto keys=f.first.user.keyCodes;
        UserConfig::set_control_keys(f.first.user,8,{keys[8],keys[10]});
        UserConfig::set_control_keys(f.first.user,9,{keys[9],keys[11]});
        check("[R / S]","[P / U]");
        f.first.user.gameMode=6;
        UserConfig::set_control_keys(f.second.user,8,{keys[10],keys[12]});
        UserConfig::set_control_keys(f.second.user,9,{keys[11],keys[12]});
        check("[R / S / F2]","[P / U / F2]");
        f.first.user.gameMode=0;
        UserConfig::set_control_keys(f.first.user,8,{});
        UserConfig::set_control_keys(f.first.user,9,{});
        check("[Unbound]","[Unbound]");
    }
#ifdef __linux__
    for(int variant:{0,1,2}) {
        const bool local=variant==1,boundEscape=variant==2;
        // A held cancelling Escape stays on the result screen; a fresh press
        // after release still invokes normal start-screen navigation.
        int descriptors[2];assert(pipe(descriptors)==0);
        const int saved=dup(STDIN_FILENO);assert(saved>=0 && dup2(descriptors[0],STDIN_FILENO)>=0);
        App::UiText::set_language(App::UiText::Language::English);
        Fixture f;if(local) App::initialize_game(f.second.context);
        if(boundEscape) {
            UserConfig::set_control_keys(f.first.user,10,{escape});
            Input::refresh_bindings(f.first.input,f.first.user.keyCodes,f.first.user.additionalKeyCodes);
        }
        App::UiText::set_language(App::UiText::Language::English);
        const auto offset=output.str().size();int resultPolls=0;
        bool saveReleased=false;
        f.first.input.keyStateProvider=[&](int key) {
            const auto text=output.str().substr(offset);
            const bool prompt=text.find("Custom replay name:")!=std::string::npos;
            const bool cancelled=text.find("Replay save cancelled.")!=std::string::npos;
            if(boundEscape && key==escape) {
                if(!prompt) return true;
                if(!saveReleased) {saveReleased=true;return false;}
                return true;
            }
            if(key==f.first.user.keyCodes[10]) return !prompt;
            if(key==escape) return prompt;
            if(key==f.first.user.keyCodes[8] && cancelled) return ++resultPolls>=8;
            return false;
        };
        if(local) App::run_local_versus_post_game_interaction(f.first.context,f.second.context);
        else App::run_post_game_interaction(f.first.context,f.first.session->player);
        assert(resultPolls>=8 && !f.app.startScreenRequested);
        assert(output.str().substr(offset).find("Replay save cancelled.")!=std::string::npos);
        f.first.input.keyStateProvider=[](int) {return false;};
        if(boundEscape) {
            assert(!f.first.input.key_down(escape));
            UserConfig::set_control_keys(f.first.user,10,{});
            Input::refresh_bindings(f.first.input,f.first.user.keyCodes,f.first.user.additionalKeyCodes);
        }
        f.first.input.key_down(f.first.user.keyCodes[8]);
        f.first.input.keyStateProvider=[](int key) {return key==escape;};
        bool interrupted=false;
        try { f.first.input.key_down(f.first.user.keyCodes[8]); }
        catch(const App::StartScreenRequested&) {interrupted=true;}
        assert(interrupted);
        assert(dup2(saved,STDIN_FILENO)>=0);close(saved);close(descriptors[0]);close(descriptors[1]);std::cin.clear();
    }
#endif
    {
        Fixture f;
        UserConfig::set_control_keys(f.first.user,3,{escape});
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes,f.first.user.additionalKeyCodes);
        f.first.escapePressed=[] { return true; };
        assert(f.first.input.key_down(escape));
        assert(f.first.input.bindings.action_for(escape)==Input::Action::HardDrop);
        assert(!f.app.startScreenRequested);
        UserConfig::set_control_keys(f.first.user,3,{});
        Input::refresh_bindings(f.first.input,f.first.user.keyCodes,f.first.user.additionalKeyCodes);
        expect_start(f,[&] { f.first.services.clock.sleep_for(Duration::from_seconds(2)); });
    }
    {
        Fixture f;
        f.first.user.gameMode=f.second.user.gameMode=6;
        UserConfig::set_control_keys(f.first.user,3,{escape});
        App::initialize_game(f.first.context);
        Input::share_key_state(f.second.input,f.first.input);
        f.first.escapePressed=[] { return true; };
        assert(f.first.input.key_down(escape));
        assert(f.second.input.key_down(escape));
        assert(!f.app.startScreenRequested);
    }
    {
        Fixture f;
        expect_start(f, [&] { App::run(f.first.context); });
        f.first.context.config.mode.mode = Runtime::GameMode::Racing;
        expect_start(f, [&] { Engine::run_start_countdown(f.first.context,
            f.first.session->player, f.first.session->opponent); });
        f.first.context.config.mode.mode = Runtime::GameMode::Zen;
        f.first.session->player.timing.entryDelayEnd = f.first.context.real_now() + Duration::from_seconds(2);
        expect_start(f, [&] { Engine::wait_for_entry_delay(f.first.context, f.first.session->player,
            [] { return Rules::PlayResult::ContinueFrame; }); });
        Engine::LineClearPresentationCallbacks callbacks;
        callbacks.now = [&] { return f.first.context.real_now(); };
        const auto animationStart = f.first.context.real_now();
        callbacks.animationStart = [animationStart] { return animationStart; };
        callbacks.clearColumn = [](int) {};
        callbacks.pumpFrame = [] { return Rules::PlayResult::ContinueFrame; };
        callbacks.recordReplayMarker = [](Engine::LineClearPresentationMarker) {};
        callbacks.flush = [] {};
        callbacks.idleWait = [&] { f.first.services.clock.sleep_for(Duration::from_seconds(.001)); };
        expect_start(f, [&] { Engine::run_line_clear_presentation(10, 1, Duration::from_seconds(2), false, callbacks); });
        expect_start(f, [&] { App::run_post_game_interaction(f.first.context, f.first.session->player); });
        // Long waits are interruptible even with no gameplay keys bound.
        f.first.input.bindings = {};
        expect_start(f, [&] { f.first.services.clock.sleep_for(Duration::from_seconds(2)); });
        Network::connect_local_duplex(f.first.network, f.second.network);
        expect_start(f, [&] { Network::receive_lobby_seed(f.first.context); });
        expect_start(f, [&] { Network::wait_for_lobby_signal(f.first.context, Network::ServerLobbySignal::RoundReady); });
        expect_start(f, [&] { Network::exchange_lobby_player_setup(f.first.context, {}); });
    }
    for (const bool rebuilding : {false, true}) {
        Fixture f;
        f.first.context.config.mode.replayMode = true;
        f.first.replay.playback.paused = true;
        f.first.replay.playback.rebuilding = rebuilding;
        if (rebuilding) {
            Replay::Operation due;
            due.kind = Replay::OperationKind::EndAll;
            f.first.replay.operations.push(due);
        }
        f.first.escapePressed = [] { return true; };
        bool interrupted = false;
        try { Engine::update_replay_playback_controls(f.first.context, f.first.session->player); }
        catch (const App::StartScreenRequested&) { interrupted = true; }
        assert(interrupted && f.app.startScreenRequested);
    }
    {
        Fixture f;
        Input::KeyEventBuffer events;
        f.first.input.keyEventSource = &events;
        // A short press during a busy frame is retained until navigation polls.
        events.update(escape, true);
        events.update(escape, false);
        bool interrupted = false;
        try { f.first.input.key_down(f.first.user.keyCodes[8]); }
        catch (const App::StartScreenRequested&) { interrupted = true; }
        assert(interrupted && f.app.startScreenRequested);
        f.app.startScreenRequested = false;
        { Input::ControlInputScope controls(&f.first.input);
          events.update(escape, true); events.update(escape, false); }
        f.first.input.key_down(f.first.user.keyCodes[8]);
        assert(!f.app.startScreenRequested);
        int reads = 0;
        f.first.escapePressed = [&] { ++reads; return reads < 4 || reads >= 8; };
        // Returning with Escape still held does not immediately quit; a fresh
        // press on the Start screen does quit.
        Render::begin_output_suppression();
        assert(!App::run_start_menu(f.first.context) && reads >= 8);
        assert(!Render::output_suppressed());
        f.first.input.keyEventSource = nullptr;
    }
    {
        Fixture f;
        f.first.user.gameMode = f.second.user.gameMode = 6;
        App::initialize_game(f.first.context);
        // Skip the countdown to exercise Escape on the two worker threads.
        f.first.context.config.garbage.mode = f.second.context.config.garbage.mode = Runtime::GarbageMode::None;
        f.first.session->player.gameplay.scoring.wins = 2;
        f.second.session->player.gameplay.scoring.wins = 3;
        const auto mainThread = std::this_thread::get_id();
        Input::share_key_state(f.second.input, f.first.input);
        f.first.escapePressed = [mainThread] { return std::this_thread::get_id() != mainThread; };
        bool interrupted = false;
        try { App::run(f.first.context); }
        catch (const App::StartScreenRequested&) { interrupted = true; }
        assert(interrupted && f.app.startScreenRequested);
        assert(f.first.session->player.gameplay.scoring.wins == 2);
        assert(f.second.session->player.gameplay.scoring.wins == 3);
    }
    std::cout.rdbuf(previous);
    return 0;
}
} // namespace TetrTests::start_screen_navigation_test
