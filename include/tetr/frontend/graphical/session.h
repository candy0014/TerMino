#ifndef TETR_GRAPHICAL_SESSION_H
#define TETR_GRAPHICAL_SESSION_H

#include "tetr/config/user_config.h"
#include "tetr/input/focus_state.h"
#include "tetr/presentation/board_view.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Graphical {
enum class SessionStatus { Loading, Countdown, Playing, Paused, Finished, Menu, Error };
struct SessionView {
    Presentation::BoardView board;
    SessionStatus status = SessionStatus::Loading;
    bool replay = false, replayPaused = false;
    double replayPosition = 0, replayDuration = 0, replaySpeed = 1;
    std::string message;
    std::uint64_t revision = 0;
};

// One writer owns the complete legacy engine context. Consumers get copies;
// neither rendering nor its cadence can mutate/advance gameplay.
class Session {
public:
    Session(UserConfig::Settings settings, std::string replayPath = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Input::FocusState& input() { return input_; }
    SessionView view() const;
    void request_menu();
    void request_save(); // finished rounds only; never invokes a terminal prompt
private:
    void run(UserConfig::Settings settings, std::string replayPath);
    mutable std::mutex mutex_;
    SessionView view_;
    Input::FocusState input_;
    std::atomic<bool> stop_{false}, menu_{false}, save_{false};
    std::thread worker_;
};
}
#endif
