#include "input.h"
#include "tetr/config/user_config.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main() {
    using namespace Graphical;
    using namespace std::chrono;
    const SDL_Scancode defaults[]={SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_DOWN,SDL_SCANCODE_SPACE,
        SDL_SCANCODE_UP,SDL_SCANCODE_Z,SDL_SCANCODE_A,SDL_SCANCODE_C,SDL_SCANCODE_R,SDL_SCANCODE_P,
        SDL_SCANCODE_S,SDL_SCANCODE_U,SDL_SCANCODE_F2};
    const auto& native=UserConfig::default_key_codes();
    for(std::size_t i=0;i<native.size();++i) assert(native_key(defaults[i])==native[i]);
    assert(native_key(SDL_SCANCODE_UNKNOWN)==0);
    assert(native_key(SDL_SCANCODE_LSHIFT)!=native_key(SDL_SCANCODE_RSHIFT));
    assert(native_key(SDL_SCANCODE_MINUS)>0 && native_key(SDL_SCANCODE_EQUALS)>0);
    assert(native_key(SDL_SCANCODE_VOLUMEUP)>0 && native_key(SDL_SCANCODE_MEDIA_NEXT_TRACK)>0);
    assert(native_key(SDL_SCANCODE_LANG1)>0 && native_key(SDL_SCANCODE_INTERNATIONAL4)>0);
    const auto origin=steady_clock::now();EventClock clock(1000000,origin);
    assert(clock.at(1012345)-origin==nanoseconds(12345));
    assert(clock.at(987655)-origin==nanoseconds(-12345));
    Input::FocusState input;
    SDL_KeyboardEvent event{};event.scancode=SDL_SCANCODE_LEFT;event.timestamp=1010000;event.down=true;
    deliver_key(input,event,clock);
    assert(input.key_down(native[0]));
    event.repeat=true;event.timestamp=1020000;deliver_key(input,event,clock);
    event.repeat=false;event.down=false;event.timestamp=1030000;deliver_key(input,event,clock);
    std::uint64_t cursor=0;
    const auto events=input.events().read(cursor);
    assert(events.size()==2 && events[0].time==origin+nanoseconds(10000));
    assert(events[1].time==origin+nanoseconds(30000));
    // Loss/regain suppresses held movement, then permits the next physical tap.
    event.down=true;deliver_key(input,event,clock);input.set_focused(false);
    assert(!input.key_down(native[0]));input.set_focused(true);assert(!input.key_down(native[0]));
    event.down=false;deliver_key(input,event,clock);event.down=true;deliver_key(input,event,clock);
    assert(input.key_down(native[0]));
    std::cerr<<"SDL input mapping, timestamps, short taps, repeat and focus: OK\n";
}
