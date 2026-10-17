#ifndef TETR_OPENGL_TEXT_EVENTS_H
#define TETR_OPENGL_TEXT_EVENTS_H
#include "tetr/frontend/text_input.h"
#include <SDL3/SDL.h>
#include <functional>
namespace Graphical {
class TextEvents {
public:
    std::string composition;
    void reset() {composition.clear();}
    void consume(const SDL_Event&,const std::function<void(Frontend::TextEditEvent)>&,
        const std::function<std::string()>& clipboard={});
};
}
#endif
