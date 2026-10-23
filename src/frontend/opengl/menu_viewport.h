#ifndef TETR_OPENGL_MENU_VIEWPORT_H
#define TETR_OPENGL_MENU_VIEWPORT_H
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Graphical {
// Scrolling changes only the viewport. A subsequent keyboard selection or
// opening an editor reveals the focused row again.
class MenuViewport {
public:
    float show(std::size_t selected,std::size_t count,std::size_t rows,std::uint64_t page,float movement=0) {
        if(!initialized_ || selected!=selected_ || page!=page_ || rows!=rows_ || count!=count_) {
            if(selected<offset_)offset_=static_cast<float>(selected);
            else if(selected+1>offset_+rows)offset_=static_cast<float>(selected+1-rows);
        }
        selected_=selected;page_=page;rows_=rows;count_=count;initialized_=true;
        offset_=std::clamp(offset_+movement,0.f,static_cast<float>(count>rows?count-rows:0));
        return offset_;
    }
private:
    float offset_=0;
    std::size_t selected_=0;
    std::size_t rows_=0,count_=0;
    std::uint64_t page_=0;
    bool initialized_=false;
};
}
#endif
