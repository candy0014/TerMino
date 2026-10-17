#include "text_events.h"
#include "input.h"
#include <cassert>
#include <vector>
int main() {
    using K=Frontend::TextEditKind;
    Graphical::TextEvents router;Frontend::TextField field("initial");
    std::vector<K> edits;
    const auto send=[&](Frontend::TextEditEvent event){edits.push_back(event.kind);field.apply(event);};
    SDL_Event event{};event.type=SDL_EVENT_TEXT_EDITING;event.edit.text="中文组合";router.consume(event,send);
    for(auto key:{SDL_SCANCODE_LEFT,SDL_SCANCODE_DELETE,SDL_SCANCODE_BACKSPACE,SDL_SCANCODE_RETURN,SDL_SCANCODE_ESCAPE}) {
        event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=key;router.consume(event,send);
    }
    assert(edits.empty() && field.value=="initial" && !field.done);
    event.type=SDL_EVENT_TEXT_INPUT;event.text.text="中文";router.consume(event,send);
    assert(field.value=="中文" && router.composition.empty());
    event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=SDL_SCANCODE_LEFT;router.consume(event,send);
    event.key.scancode=SDL_SCANCODE_DELETE;router.consume(event,send);assert(field.value=="中");
    event.key.scancode=SDL_SCANCODE_A;event.key.mod=SDL_KMOD_CTRL;router.consume(event,send);
    event.key.scancode=SDL_SCANCODE_V;router.consume(event,send,[]{return "路径 with spaces";});assert(field.value=="路径 with spaces");
    event.key.mod=0;event.key.scancode=SDL_SCANCODE_RETURN;router.consume(event,send);assert(field.done && !field.cancelled);
    // Loss of focus suppresses held actions until release, and clearing the
    // IME composition cannot synthesize a confirm/cancel event.
    Input::FocusState focus;Graphical::EventClock clock;
    event.key.scancode=SDL_SCANCODE_LEFT;event.key.down=true;Graphical::deliver_key(focus,event.key,clock);
    focus.set_focused(false);router.composition="未提交";router.reset();focus.set_focused(true);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_LEFT)) && router.composition.empty());
    event.key.down=false;Graphical::deliver_key(focus,event.key,clock);event.key.down=true;Graphical::deliver_key(focus,event.key,clock);
    assert(focus.key_down(Graphical::native_key(SDL_SCANCODE_LEFT)));
}
