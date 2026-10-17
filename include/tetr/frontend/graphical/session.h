#ifndef TETR_GRAPHICAL_SESSION_H
#define TETR_GRAPHICAL_SESSION_H

#include "tetr/config/user_config.h"
#include "tetr/input/focus_state.h"
#include "tetr/presentation/board_view.h"
#include "tetr/frontend/ui_surface.h"
#include "tetr/frontend/text_input.h"
#include <deque>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Graphical {
enum class SessionStatus { Loading, Countdown, Playing, Paused, Finished, Menu, Error, Closed };
struct SceneBoard {int slot=-1;bool miniature=false;Presentation::BoardView board;};
struct LaunchOptions {
    bool startScreen=true,continuousCancellation=false,audio=false,playerTwoActive=false;
};
struct SessionView {
    Presentation::BoardView board;
    SessionStatus status = SessionStatus::Loading;
    bool replay = false, replayPaused = false;
    double replayPosition = 0, replayDuration = 0, replaySpeed = 1;
    std::string message;
    std::uint64_t revision = 0;
    std::shared_ptr<const Frontend::UiFrame> ui;
    std::vector<SceneBoard> peers;
    std::string controls,roomStatus,challengeNotice;
    bool spectator=false;
    int focusSlot=-1;
};

// One writer owns the complete legacy engine context. Consumers get copies;
// neither rendering nor its cadence can mutate/advance gameplay.
class Session : public Frontend::TextInputSession {
public:
    Session(UserConfig::Settings settings, std::string replayPath = {});
    Session(UserConfig::Settings first,UserConfig::Settings second,LaunchOptions options);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Input::FocusState& input() { return input_; }
    SessionView view() const;
    void request_menu();
    void request_save(); // finished rounds only; never invokes a terminal prompt
    void begin_text_input() override;
    void end_text_input() override;
    bool owns_text_events() const override {return true;}
    std::optional<Frontend::TextEditEvent> poll_text_event() override;
    bool text_input_active() const {return textMode_;}
    void text_event(Frontend::TextEditEvent event);
private:
    void run(UserConfig::Settings settings, std::string replayPath);
    void run_full(UserConfig::Settings first,UserConfig::Settings second,LaunchOptions options);
    mutable std::mutex mutex_;
    SessionView view_;
    Input::FocusState input_;
    std::atomic<bool> stop_{false}, menu_{false}, save_{false};
    std::atomic<bool> textMode_{false};
    std::deque<Frontend::TextEditEvent> textEvents_;
    std::thread worker_;
};
}
#endif
