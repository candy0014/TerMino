#ifndef TETR_RUNTIME_GAME_CONTEXT_H
#define TETR_RUNTIME_GAME_CONTEXT_H

#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/runtime_services.h"
#include "tetr/runtime/session_state.h"
#include "tetr/runtime/time.h"

#include <stdexcept>
#include <mutex>
#include <memory>
#include <algorithm>
#include <vector>
#include <functional>
#include <atomic>

namespace Runtime { struct UndoHistory; }

namespace App { struct ApplicationState; }
namespace Autoplay { class Strategy; }
namespace Network { struct NetworkServices; }
namespace Input { struct Services; }
namespace UserConfig { struct Settings; }
namespace Replay { struct ReplaySessionState; }

namespace Runtime {

// UI control state stays outside undo snapshots. Excluding paused wall time
// from the context clock freezes movement, lock, animation and game timers alike.
struct GamePauseState {
    struct Interval {
        TimePoint begin, end;
        Duration removedBefore;
    };
    bool active = false;
    bool paused = false;
    bool keyDown = false;
    bool polling = false;
    TimePoint started{};
    Duration removedTime{};
    std::vector<Interval> intervals;
    // The engine supplies horizontal precharge without advancing game timers.
    std::function<void()> prechargeMovement;
};

class GameContext {
public:
    RuntimeConfig config;
    SessionState& session;
    RuntimeServices& services;
    // Process-local history, deliberately kept outside the snapshotted session.
    std::shared_ptr<UndoHistory> undoHistory;
    GamePauseState pause;
    std::shared_ptr<std::atomic<bool>> autoplaySearchCancelled;

    GameContext(SessionState& sessionState, RuntimeServices& runtimeServices)
        : session(sessionState), services(runtimeServices) {}

    bool uses_piece_time() const noexcept {
        return config.simulationPps > 0.0 && !config.mode.replayMode;
    }
    TimePoint real_now() const {
        if (config.mode.replayMode) return services.clock.now();
        return (pause.paused ? pause.started : services.clock.now()) - pause.removedTime;
    }
    TimePoint real_time_at(std::chrono::steady_clock::time_point time) const {
        const auto stamp=services.clock.at(time);
        if (config.mode.replayMode) return stamp;
        const auto interval=std::lower_bound(pause.intervals.begin(),pause.intervals.end(),stamp,
            [](const GamePauseState::Interval& item,TimePoint value) { return item.end<value; });
        if (interval!=pause.intervals.end())
            return std::min(stamp,interval->begin)-interval->removedBefore;
        return (pause.paused ? std::min(stamp,pause.started) : stamp)-pause.removedTime;
    }
    TimePoint now() const {
        if (uses_piece_time()) return session.simulationTime;
        if (config.mode.replayMode) return real_now();
        return session.turnWaitStarted.value_or(real_now()) - session.turnWaitDuration - session.clockOffset;
    }

    void attach_app(App::ApplicationState& value) noexcept { app_ = &value; }
    void attach_network(Network::NetworkServices& value) noexcept { network_ = &value; }
    void attach_replay(Replay::ReplaySessionState& value) noexcept { replay_ = &value; }
    void attach_autoplay(Autoplay::Strategy& value) noexcept { autoplay_ = &value; }
    void attach_input(Input::Services& value) noexcept { input_ = &value; }
    void attach_user_settings(UserConfig::Settings& value) noexcept { userSettings_ = &value; }
    void attach_output_mutex(std::recursive_mutex& value) noexcept { outputMutex_ = &value; }

    App::ApplicationState* app_component() const noexcept { return app_; }
    Network::NetworkServices* network_component() const noexcept { return network_; }
    Replay::ReplaySessionState* replay_component() const noexcept { return replay_; }
    Autoplay::Strategy* autoplay_component() const noexcept { return autoplay_; }
    Input::Services* input_component() const noexcept { return input_; }
    const UserConfig::Settings* user_settings_component() const noexcept { return userSettings_; }

    Input::Services& input_services() {
        if (!input_) throw std::logic_error("GameContext has no Input::Services attached");
        return *input_;
    }
    const Input::Services& input_services() const {
        if (!input_) throw std::logic_error("GameContext has no Input::Services attached");
        return *input_;
    }
    UserConfig::Settings& user_settings() {
        if (!userSettings_) throw std::logic_error("GameContext has no UserConfig::Settings attached");
        return *userSettings_;
    }
    const UserConfig::Settings& user_settings() const {
        if (!userSettings_) throw std::logic_error("GameContext has no UserConfig::Settings attached");
        return *userSettings_;
    }
    std::recursive_mutex* output_mutex() const noexcept { return outputMutex_; }

private:
    App::ApplicationState* app_ = nullptr;
    Network::NetworkServices* network_ = nullptr;
    Replay::ReplaySessionState* replay_ = nullptr;
    Autoplay::Strategy* autoplay_ = nullptr;
    Input::Services* input_ = nullptr;
    UserConfig::Settings* userSettings_ = nullptr;
    std::recursive_mutex* outputMutex_ = nullptr;
};

} // namespace Runtime

#endif
