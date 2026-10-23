#ifndef TETR_GRAPHICAL_SESSION_H
#define TETR_GRAPHICAL_SESSION_H

#include "tetr/config/user_config.h"
#include "tetr/input/focus_state.h"
#include "tetr/presentation/board_view.h"
#include "tetr/frontend/ui_surface.h"
#include "tetr/frontend/text_input.h"
#include "tetr/frontend/file_dialog.h"
#include "tetr/util/worker_thread.h"
#include <deque>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Graphical {
enum class SessionStatus { Loading, Countdown, Playing, Paused, Finished, Menu, Error, Closed };
using ActionAvailability=std::array<bool,13>;
struct SceneBoard {
    int slot=-1;bool miniature=false;Presentation::BoardView board;
    // Miniatures are sampled images. Their sample grid is not a playfield:
    // preserve the logical aspect ratio and bottom-aligned overflow separately.
    int logicalWidth=0,logicalHeight=0;
    double sampleRows=1;
};
struct LaunchOptions {
    bool startScreen=true,continuousCancellation=false,audio=false,playerTwoActive=false;
    bool touchControls=false;
};
struct SessionView {
    Presentation::BoardView board;
    SessionStatus status = SessionStatus::Loading;
    int countdownDigit = 0;
    bool replay = false, replayPaused = false;
    double replayPosition = 0, replayDuration = 0, replaySpeed = 1;
    std::string message;
    std::uint64_t revision = 0;
    std::uint64_t roundRevision = 0;
    std::shared_ptr<const Frontend::UiFrame> ui;
    std::vector<SceneBoard> peers;
    std::string controls,roomStatus,challengeNotice;
    std::vector<Frontend::UiEntry> controlHints;
    bool spectator=false,room=false,ownBoardVisible=true;
    int focusSlot=-1;
    std::array<ActionAvailability,2> actionEnabled{};
    bool switchTarget=false,endMatch=false;
};
struct SessionControls {
    SessionStatus status=SessionStatus::Loading;
    std::shared_ptr<const Frontend::UiFrame> ui;
    std::string message;
    int pieces=0;
    Piece::Type hold;
    bool replay=false,spectator=false,room=false;
    std::array<ActionAvailability,2> actionEnabled{};
    bool switchTarget=false,endMatch=false;
};

// One writer owns the complete legacy engine context. Consumers get copies;
// neither rendering nor its cadence can mutate/advance gameplay.
class Session : public Frontend::TextInputSession, public Frontend::FileDialogService {
public:
    Session(UserConfig::Settings settings, std::string replayPath = {});
    Session(UserConfig::Settings first,UserConfig::Settings second,LaunchOptions options);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Input::FocusState& input() { return input_; }
    SessionView view() const;
    SessionControls controls() const;
    void request_menu();
    void request_save(); // finished rounds only; never invokes a terminal prompt
    bool take_virtual_controls_editor() {return touchEditor_.exchange(false);}
    void ui_command(Frontend::UiCommand command);
    void begin_text_input() override;
    void end_text_input() override;
    bool owns_text_events() const override {return true;}
    std::optional<Frontend::TextEditEvent> poll_text_event() override;
    bool text_input_active() const {return textMode_;}
    void text_event(Frontend::TextEditEvent event);
    std::shared_ptr<Frontend::FileDialog> open_file(std::string directory) override;
    std::shared_ptr<Frontend::FileDialog> take_file_dialog();
private:
    void run(UserConfig::Settings settings, std::string replayPath);
    void run_full(UserConfig::Settings first,UserConfig::Settings second,LaunchOptions options);
    mutable std::mutex mutex_;
    SessionView view_;
    Input::FocusState input_;
    std::atomic<bool> stop_{false}, menu_{false}, save_{false};
    std::atomic<bool> textMode_{false};
    std::atomic<bool> touchEditor_{false};
    std::deque<Frontend::TextEditEvent> textEvents_;
    std::deque<Frontend::UiCommand> uiCommands_;
    std::shared_ptr<Frontend::FileDialog> fileDialog_;
    bool fileDialogDispatched_=false;
    Util::WorkerThread worker_;
};
}
#endif
