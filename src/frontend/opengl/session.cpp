#include "tetr/frontend/graphical/session.h"
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/pause.h"
#include "tetr/input/services.h"
#include "tetr/presentation/context_view.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/runtime/game_context.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Graphical {
namespace {
struct LeaveSession {};
// Existing replay encoder/hash and directory rules, with a unique destination.
std::string save_finished_recording(Runtime::GameContext& context) {
    const auto data=Replay::recording_for_save(context);
    if(data.empty() || data.size()>20000000) throw std::runtime_error("Replay is empty or too large to save");
    const auto directory=Replay::replay_directory();
    std::filesystem::create_directories(directory);
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    auto path=directory/("gl-"+std::to_string(stamp)+".txt");
    for(unsigned suffix=1;std::filesystem::exists(path);++suffix)
        path=directory/("gl-"+std::to_string(stamp)+"-"+std::to_string(suffix)+".txt");
    std::ofstream file(path,std::ios::binary);
    file<<data<<Replay::replay_hash_suffix(data);
    file.close();
    if(!file) throw std::runtime_error("Could not save replay");
    return "Saved: "+path.generic_u8string();
}
}

Session::Session(UserConfig::Settings settings,std::string replayPath)
    :worker_([this,settings=std::move(settings),replayPath=std::move(replayPath)]() mutable {
        run(std::move(settings),std::move(replayPath));
    }) {}
Session::~Session() {
    stop_=true;
    if(worker_.joinable()) worker_.join();
}
SessionView Session::view() const { std::lock_guard<std::mutex> lock(mutex_);return view_; }
void Session::request_menu() { menu_=true; }
void Session::request_save() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(view_.status==SessionStatus::Finished) save_=true;
}

void Session::run(UserConfig::Settings settings,std::string replayPath) {
    try {
        Render::DetachedOutputScope output;
        auto state=std::make_unique<Runtime::SessionState>();
        Runtime::RuntimeServices services;
        Runtime::GameContext context(*state,services);
        Input::Services input;
        App::ApplicationState app;
        Replay::ReplaySessionState replay;
        context.attach_input(input);context.attach_app(app);context.attach_replay(replay);
        context.attach_user_settings(settings);context.attach_output_mutex(app.terminalOutputMutex);
        app.primaryContext=&context;
        input.focusSource=&input_;input.keyEventSource=&input_.events();input.initialized=true;
        // No evdev monitor, console session, native key polling or terminal IO.
        settings.persistencePath.clear();
        const bool requestedReplay=!replayPath.empty();
        if(requestedReplay) {settings.gameMode=5;settings.replayPath=replayPath;}
        bool initialized=false,finished=false;
        std::string message;
        auto lastPublish=std::chrono::steady_clock::time_point{};
        const auto publish=[&](bool force=false) {
            const auto now=std::chrono::steady_clock::now();
            if(!initialized || (!force && now-lastPublish<std::chrono::milliseconds(4))) return;
            lastPublish=now;
            SessionView next;
            next.board=Presentation::capture_board_view(context,state->player);
            next.replay=context.config.mode.replayMode;
            next.status=finished?SessionStatus::Finished:context.pause.paused?SessionStatus::Paused:
                !state->gameStartTime?SessionStatus::Countdown:SessionStatus::Playing;
            if(next.replay) {
                next.replayPosition=Replay::playback_progress_elapsed(context,context.now()).seconds();
                next.replayDuration=Replay::playback_duration(context).seconds();
                next.replaySpeed=Replay::playback_speed(context);
                next.replayPaused=Replay::playback_paused(context);
                if(!finished && Replay::playback_paused(context)) next.status=SessionStatus::Paused;
            }
            next.message=message;
            std::lock_guard<std::mutex> lock(mutex_);
            next.revision=view_.revision+1;view_=std::move(next);
        };
        const auto pump=[&] {
            if(stop_ || menu_) throw LeaveSession{};
            publish();
        };
        input.controlInterrupt=[&] {pump();Engine::poll_game_pause(context,pump);};
        services.clock.waitInterrupt=[&] {input.poll_control_interrupt();pump();};
        app.openSettings=[] { throw LeaveSession{}; };
        app.roundFinished=[&] { finished=true;publish(true); };
        for(;;) {
            initialized=false;finished=false;
            App::initialize_game(context);
            if(requestedReplay && !context.config.mode.replayMode)
                throw std::runtime_error("Invalid/unsupported replay: verify its path and checksum. Two-player replay UI is scheduled for phase 3.");
            initialized=true;publish(true);
            App::run(context);
            if(app.replayResume.pending) continue;
            if(!finished) continue; // existing Retry outcome; same initialization
            // Preserve the final frame. Replay controls can still seek backwards.
            try {
                for(;;) {
                    pump();
                    if(save_.exchange(false)) {
                        try {message=save_finished_recording(context);}
                        catch(const std::exception& error) {message=error.what();}
                        publish(true);
                    }
                    if(context.config.mode.replayMode)
                        Engine::update_replay_playback_controls(context,state->player);
                    bool retry=false;
                    for(const auto& [key,action]:input.bindings.entries()) {
                        if(action==Input::Action::Retry && input.key_down(key)) retry=true;
                        if(action==Input::Action::OpenSettings && input.key_down(key)) throw LeaveSession{};
                    }
                    if(retry) break;
                    services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
                }
            } catch(const Engine::ReplaySeekRequested&) {}
        }
    } catch(const LeaveSession&) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Menu;++view_.revision;
    } catch(const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Error;view_.message=error.what();++view_.revision;
    } catch(...) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Error;view_.message="Unexpected session failure";++view_.revision;
    }
}
}
