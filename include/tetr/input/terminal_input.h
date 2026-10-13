#ifndef TETR_INPUT_TERMINAL_INPUT_H
#define TETR_INPUT_TERMINAL_INPUT_H

#include "tetr/input/focus_state.h"

#include <string>

namespace Input {

#ifdef __linux__
// One stdin reader handles focus reports during both evdev gameplay and text
// entry. Text editors receive ordinary bytes only, never terminal reports.
class TerminalInput {
public:
    explicit TerminalInput(FocusState& focus):focus_(focus) {}
    void start();
    void stop();
    bool active() const;
    void set_text_input(bool enabled);
    void poll();
    // -1: no byte available, -2: end of input, otherwise an unsigned byte.
    int read_character();

private:
    void poll_locked();
    void feed(unsigned char byte);
    void emit(const std::string& bytes);
    void flush_escape();
    FocusState& focus_;
    mutable std::mutex mutex_;
    bool active_=false,textInput_=false,eof_=false;
    std::string escape_;
    std::chrono::steady_clock::time_point escapeStarted_;
    std::deque<unsigned char> text_;
};
#endif

} // namespace Input

#endif
