#ifndef TETR_INPUT_SERVICES_H
#define TETR_INPUT_SERVICES_H

#include "tetr/input/action.h"
#include "tetr/input/key_events.h"
#include "tetr/input/focus_state.h"

#include <array>
#include <functional>
#include <optional>
#include <set>

#if defined(__linux__) && !defined(__ANDROID__)
#include "tetr/input/keyboard_monitor.h"
#endif

namespace Input {

// Platform input and the currently active typed key bindings.  Configuration
// parsing remains outside this service; callers explicitly refresh bindings
// when UserConfig changes.
struct Services {
    BindingSet bindings;
    int virtualPlayerSlot=-1;
    // Local Versus contexts have different bindings but read one physical
    // keyboard state.  A delegated source avoids two evdev readers competing
    // for the same device events.
    Services* keyStateSource = nullptr;
    // Optional input backend for embedding and deterministic menu tests.
    std::function<bool(int)> keyStateProvider;
    // Application navigation can interrupt gameplay and waits without binding
    // Escape to a gameplay action. Text/menu scopes handle navigation themselves.
    std::function<void()> controlInterrupt;
    // Optional event backend for embedding and deterministic gameplay tests.
    KeyEventBuffer* keyEventSource = nullptr;
    // Optional focus-aware physical backend for embedding/tests.
    FocusState* focusSource = nullptr;
#if defined(__linux__) && !defined(__ANDROID__)
    KeyboardMonitor keyboard;
#endif
    bool initialized = false;

    void set_continue_input_unfocused(bool enabled);
    void start_terminal_input();
    void stop_terminal_input();
    bool terminal_input_active();
    void set_terminal_text_input(bool enabled);
    int read_terminal_character();
    bool accepting_input();

    bool key_down(int keyCode);
    bool raw_key_down(int keyCode);
    bool control_key_pressed(int keyCode);
    // Modal cancellation must not retrigger navigation or a bound UI action.
    void suppress_control_key_until_release(int keyCode);
    // Independent cursors preserve short taps and multiple presses between
    // polls without consuming recorded replay operations or movement input.
    std::vector<KeyEvent> read_control_events(std::uint64_t& cursor);
    void discard_control_events();
    void poll_control_interrupt();
    void begin_gameplay_input();
    void end_gameplay_input();
    bool gameplay_input_active() const { return gameplayInputActive_; }
    // Captured atomically with the latest batch, never read ahead of its events.
    KeyEventBuffer::SampleTime gameplay_sample_time() const { return gameplaySampleTime_; }
    bool gameplay_key_down(int key) const;
    std::optional<std::chrono::steady_clock::time_point> gameplay_press_time(int key) const;
    void poll_gameplay_events();
    // Collect queued input without scanning every bound key's platform state.
    bool gameplay_input_pending();
    const KeyEvent* next_gameplay_event() const;
    void consume_gameplay_event();
    std::vector<KeyEvent> take_spawn_horizontal_events();
    void discard_prestart_actions();
    void wait_for_gameplay_input(std::chrono::microseconds timeout);

private:
    Services& physical_source();
    const Services& physical_source() const;
    FocusState& focus_state();
    const FocusState& focus_state() const;
    void poll_focus();
    void synchronize_focus();
    KeyEventBuffer& event_buffer();
    void poll_platform_events();
    bool read_key_state(int keyCode);
    bool gameplayInputActive_ = false;
    std::uint64_t eventCursor_ = 0;
    std::uint64_t controlEventCursor_ = 0;
    std::set<int> controlPressedKeys_;
    std::set<int> suppressedControlKeys_;
    std::map<int, bool> gameplayDown_;
    std::map<int, std::chrono::steady_clock::time_point> gameplayPressedAt_;
    KeyEventBuffer::SampleTime gameplaySampleTime_;
    std::deque<KeyEvent> pendingEvents_;
    std::uint64_t focusGeneration_=0;
#ifdef _WIN32
    std::mutex consoleMutex_;
    FocusState consoleFocus_;
#elif !defined(__linux__) || defined(__ANDROID__)
    FocusState unavailableFocus_;
    KeyEventBuffer unavailableEvents_;
#endif
};

class ControlInputScope {
public:
    explicit ControlInputScope(Services* first = nullptr, Services* second = nullptr);
    ~ControlInputScope();
    ControlInputScope(const ControlInputScope&) = delete;
    ControlInputScope& operator=(const ControlInputScope&) = delete;
private:
    Services* first_;
    Services* second_;
};

// A game owns one capture scope across its countdown and piece loop. Nested
// users reuse it so starting the piece loop cannot discard precharged input.
class GameplayInputScope {
public:
    explicit GameplayInputScope(Services* services, bool enabled = true);
    ~GameplayInputScope();
    GameplayInputScope(const GameplayInputScope&) = delete;
    GameplayInputScope& operator=(const GameplayInputScope&) = delete;
    void end();
    void restart();
private:
    Services* services_;
    bool owns_ = false;
};

void initialize_services(Services& services);
void share_key_state(Services& target, Services& source);
bool shares_key_state_with(const Services& target, const Services& source) noexcept;
void refresh_bindings(Services& services, const std::array<int, kBindableActionCount>& keyCodes,
    const std::array<std::vector<int>, kBindableActionCount>& additional = {});

} // namespace Input

#endif
