#include "text_events.h"
namespace Graphical {
void TextEvents::consume(const SDL_Event& event,const std::function<void(Frontend::TextEditEvent)>& send,
    const std::function<std::string()>& clipboard) {
    using K=Frontend::TextEditKind;
    if(event.type==SDL_EVENT_TEXT_INPUT) {send({K::Insert,event.text.text});composition.clear();return;}
    if(event.type==SDL_EVENT_TEXT_EDITING) {composition=event.edit.text;return;}
    if(event.type!=SDL_EVENT_KEY_DOWN || !composition.empty())return;
    std::optional<K> edit;
    switch(event.key.scancode) {
    case SDL_SCANCODE_LEFT:edit=K::Left;break;case SDL_SCANCODE_RIGHT:edit=K::Right;break;
    case SDL_SCANCODE_HOME:edit=K::Home;break;case SDL_SCANCODE_END:edit=K::End;break;
    case SDL_SCANCODE_BACKSPACE:edit=K::Backspace;break;case SDL_SCANCODE_DELETE:edit=K::Delete;break;
    case SDL_SCANCODE_RETURN:case SDL_SCANCODE_KP_ENTER:edit=K::Confirm;break;
    case SDL_SCANCODE_ESCAPE:edit=K::Cancel;break;
    case SDL_SCANCODE_A:if(event.key.mod&SDL_KMOD_CTRL)edit=K::SelectAll;break;
    case SDL_SCANCODE_V:if((event.key.mod&SDL_KMOD_CTRL) && clipboard)send({K::Insert,clipboard()});break;
    default:break;
    }
    if(edit)send({*edit,{}});
}
}
