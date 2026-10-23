#ifndef TETR_OPENGL_INPUT_H
#define TETR_OPENGL_INPUT_H
#include "tetr/input/focus_state.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Graphical {
// The migration bridge translates physical SDL keys to the existing profile's
// native key namespace. It never opens evdev or polls the Windows keyboard.
int native_key(SDL_Scancode key);
class EventClock {
public:
    EventClock(std::uint64_t ticks=SDL_GetTicksNS(),
        std::chrono::steady_clock::time_point steady=std::chrono::steady_clock::now())
        :ticks_(ticks),steady_(steady) {}
    std::chrono::steady_clock::time_point at(std::uint64_t timestamp) const {
        const auto delta=timestamp>=ticks_?static_cast<std::int64_t>(timestamp-ticks_):
            -static_cast<std::int64_t>(ticks_-timestamp);
        return steady_+std::chrono::nanoseconds(delta);
    }
private:
    std::uint64_t ticks_;
    std::chrono::steady_clock::time_point steady_;
};
void deliver_key(Input::FocusState& input,const SDL_KeyboardEvent& event,const EventClock& clock);
// Call once before starting input consumption, then after each drained SDL
// batch using a timestamp captured BEFORE its first SDL_PollEvent. Processing
// the batch may stall while newer physical releases wait for the next pump.
// Rendering/SwapWindow must not imply that keys are still held.
void complete_input_sample(Input::FocusState& input,const EventClock& clock,
    std::uint64_t timestamp);
// Capture before pumping; only actual keyboard events may extend this bound.
// This keeps new presses responsive when they arrive during a slow pump,
// without treating time spent in later UI/window handlers as sampled input.
class InputSample {
public:
    explicit InputSample(std::uint64_t timestamp=SDL_GetTicksNS()):through_(timestamp) {}
    void deliver(Input::FocusState& input,const SDL_KeyboardEvent& event,const EventClock& clock);
    void complete(Input::FocusState& input,const EventClock& clock) const;
private:
    std::uint64_t through_;
};
// Drain and publish physical input before processing slower UI callbacks.
// Retain text/edit data across later SDL pumps, including environments that
// disable SDL's poll sentinel and pump again before the batch is drained.
struct PolledEvent {
    SDL_Event event;
    bool acceptingInput=false;
    std::shared_ptr<const std::string> text;
    PolledEvent(SDL_Event value,bool accepting):event(value),acceptingInput(accepting) {
        const char* data=event.type==SDL_EVENT_TEXT_INPUT?event.text.text:
            event.type==SDL_EVENT_TEXT_EDITING?event.edit.text:nullptr;
        if(data) {
            text=std::make_shared<const std::string>(data);
            if(event.type==SDL_EVENT_TEXT_INPUT)event.text.text=text->c_str();
            else event.edit.text=text->c_str();
        }
    }
};
// Local editors may withhold presses from shared input; returned events and
// releases remain available, and the normal desktop path delivers every key.
std::vector<PolledEvent> poll_input_events(Input::FocusState& input,const EventClock& clock,
    const std::function<void()>& cancelPointer={},bool completeSample=true,bool deliverKeyboard=true);
// SDL may not report releases after switching windows. Reconcile physical
// releases on focus regain, retaining the shared held-key suppression policy.
void regain_focus(Input::FocusState& input);
}
#endif
