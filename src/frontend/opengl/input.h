#ifndef TETR_OPENGL_INPUT_H
#define TETR_OPENGL_INPUT_H
#include "tetr/input/focus_state.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>

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
inline void deliver_key(Input::FocusState& input,const SDL_KeyboardEvent& event,const EventClock& clock) {
    if(event.repeat) return;
    if(const int key=native_key(event.scancode);key>0)
        input.update(key,event.down,clock.at(event.timestamp));
}
// SDL may not report releases after switching windows. Reconcile physical
// releases on focus regain, retaining the shared held-key suppression policy.
void regain_focus(Input::FocusState& input);
}
#endif
