#ifndef TETR_INPUT_KEYBOARD_MONITOR_H
#define TETR_INPUT_KEYBOARD_MONITOR_H

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include "tetr/input/key_events.h"
#include "tetr/input/terminal_input.h"

#ifdef __linux__

// Linux keyboard monitor. It automatically opens every readable keyboard
// event device and merges their key states. The monitor periodically rescans
// /proc/bus/input/devices, so USB keyboards can be hot-plugged without
// restarting the game or selecting an eventN manually.
class KeyboardMonitor {
public:
    KeyboardMonitor();
    ~KeyboardMonitor();

    KeyboardMonitor(const KeyboardMonitor&) = delete;
    KeyboardMonitor& operator=(const KeyboardMonitor&) = delete;

    void init();
    void stop();
    bool keydown(int keycode);
    Input::KeyEventBuffer& events() { return focus_.events(); }
    Input::FocusState& focus_state() { return focus_; }
    const Input::FocusState& focus_state() const { return focus_; }
    Input::TerminalInput& terminal_input() { return terminalInput_; }

private:
    void eventLoop();

    std::atomic<bool> running;
    std::thread monitorThread;
    std::mutex stateMutex;
    // Number of monitored keyboards currently holding each key. A count is
    // required because the same key can be held on two keyboards at once.
    std::map<int, int> keyPressCounts;
    Input::FocusState focus_;
    Input::TerminalInput terminalInput_{focus_};
};

#endif // __linux__

#endif
