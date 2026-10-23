#ifndef TETR_OPENGL_MENU_POINTER_H
#define TETR_OPENGL_MENU_POINTER_H
#include "input.h"
#include "tetr/frontend/ui_surface.h"
#include <algorithm>
#include <array>
#include <optional>

namespace Graphical {
// Editor wheels and clicks are semantic commands. Settings wheels scroll the
// renderer viewport and never enter the keyboard navigation path.
class MenuPointer {
public:
    std::optional<Frontend::UiCommand> editor_wheel(const Frontend::UiMetadata& m,std::size_t area,float vertical) {
        if(m.kind!=Frontend::UiKind::MapEditor || !m.editor || !m.editor->sequenceFocus || m.editor->generated || area>=2)return {};
        if(editorPage_!=m.page){editorPage_=m.page;editorWheel_={};}
        editorWheel_[area]=std::clamp(editorWheel_[area]+vertical,-16.f,16.f);
        const int steps=static_cast<int>(editorWheel_[area]);if(!steps)return {};
        editorWheel_[area]-=steps;
        Frontend::UiCommand command{Frontend::UiCommandKind::MapScroll,area,{},m.page};command.delta=-steps;return command;
    }
    void cancel(Input::FocusState& input) {
        if(held_)input.update(held_,false);
        held_=0;
    }
    void key(Input::FocusState& input,const Frontend::UiMetadata& m,SDL_Scancode code,std::uint64_t now) {
        if(held_ || !navigable(m))return;
        page_=m.page;press(input,code,now);
    }
    void wheel(Input::FocusState& input,const Frontend::UiMetadata& m,float vertical,std::uint64_t now) {
        if(m.kind!=Frontend::UiKind::Start && m.path!="play-rooms")return;
        if(vertical>0)key(input,m,SDL_SCANCODE_UP,now);
        else if(vertical<0)key(input,m,SDL_SCANCODE_DOWN,now);
    }
    void update(Input::FocusState& input,const Frontend::UiMetadata* m,std::uint64_t now) {
        if(!input.accepting_input()){cancel(input);return;}
        if(held_ && (now>=releaseAt_ || !m || m->page!=page_))cancel(input);
    }
private:
    static bool navigable(const Frontend::UiMetadata& m) {
        return m.kind==Frontend::UiKind::Start || m.kind==Frontend::UiKind::Menu || m.kind==Frontend::UiKind::Choices;
    }
    void press(Input::FocusState& input,SDL_Scancode key,std::uint64_t now) {
        held_=native_key(key);input.update(held_,true);releaseAt_=now+35;
    }
    int held_=0;
    std::array<float,2> editorWheel_{};
    std::uint64_t editorPage_=0;
    std::uint64_t page_=0,releaseAt_=0;
};
}
#endif
