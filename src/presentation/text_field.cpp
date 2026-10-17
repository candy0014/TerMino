#include "tetr/frontend/text_input.h"
namespace Frontend {
void TextField::apply(const TextEditEvent& event) {
    const auto previous=[&](std::size_t at) {
        if(at)--at;
        while(at && (static_cast<unsigned char>(value[at])&0xc0)==0x80)--at;
        return at;
    };
    const auto next=[&](std::size_t at) {
        if(at<value.size())++at;
        while(at<value.size() && (static_cast<unsigned char>(value[at])&0xc0)==0x80)++at;
        return at;
    };
    switch(event.kind) {
    case TextEditKind::Confirm:done=true;return;
    case TextEditKind::Cancel:done=cancelled=true;return;
    case TextEditKind::SelectAll:replace=true;return;
    case TextEditKind::Left:caret=previous(caret);replace=false;return;
    case TextEditKind::Right:caret=next(caret);replace=false;return;
    case TextEditKind::Home:caret=0;replace=false;return;
    case TextEditKind::End:caret=value.size();replace=false;return;
    case TextEditKind::Backspace:
        if(replace) {value.clear();caret=0;replace=false;return;}
        {const auto at=previous(caret);value.erase(at,caret-at);caret=at;}return;
    case TextEditKind::Delete:
        if(replace) {value.clear();caret=0;replace=false;return;}
        value.erase(caret,next(caret)-caret);return;
    case TextEditKind::Insert:
        if(event.text.size()>16384 || value.size()+event.text.size()>16384)return;
        if(replace) {value.clear();caret=0;replace=false;}
        value.insert(caret,event.text);caret+=event.text.size();return;
    }
}
}
