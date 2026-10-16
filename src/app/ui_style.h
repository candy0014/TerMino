#ifndef TETR_APP_UI_STYLE_H
#define TETR_APP_UI_STYLE_H
#include "terminal_canvas.h"
namespace App {
struct UiStyle {
    TerminalCanvas::Color text,background,heading,selectedText,selectedBackground,inactiveSelection,muted;
};
struct UiInsets {int left,right,top,bottom;};
inline UiInsets ui_insets(Render::TerminalSize size) {
    return {std::min(2,std::max(0,size.columns-1)),std::min(size.columns<32?1:size.columns<60?2:8,std::max(1,size.columns/4)),
        size.rows<12?0:1,size.rows<20?1:2};
}
inline UiStyle ui_style(bool obscured=false) {
    if(obscured)return {{{0,0,0}},{{12,12,12}},{{54,57,62}},{{54,57,62}},{{0,0,0}},{{54,57,62}},{{0,0,0}}};
    return {{{255,255,255}},{{12,12,12}},{{237,109,0}},{{255,188,92}},{{56,34,16}},{{193,168,65}},{{128,128,128}}};
}
}
#endif
