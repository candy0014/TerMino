#ifndef TETR_OPENGL_SCROLL_GESTURE_H
#define TETR_OPENGL_SCROLL_GESTURE_H
#include "interaction.h"
#include <cmath>

namespace Graphical {
// Capture the displayed metrics once. Deltas stay fractional and are queued to
// the render worker, so input never waits for a repaint or reuses stale offsets.
class ScrollGesture {
public:
    ScrollGesture()=default;
    ScrollGesture(const Interaction& interaction,const Frontend::UiMetadata& metadata,float x,float y)
        :area_(interaction.menu_drag(x,y,metadata,false)),horizontal_(interaction.menu_drag(x,y,metadata,true)),
         page_(metadata.page),startX_(x),startY_(y),lastX_(x),lastY_(y) {
        if(horizontal_ && (contains(horizontal_->track,x,y) || !area_)) {
            area_=horizontal_;horizontal_.reset();
        }
        thumb_=area_ && contains(area_->track,x,y);
    }
    bool scrollbar() const {return thumb_;}
    bool dragged() const {return dragged_;}
    std::optional<MenuScroll> move(float x,float y) {
        if(!area_)return {};
        if(!dragged_ && std::hypot(x-startX_,y-startY_)<6)return {};
        if(!dragged_ && !thumb_ && horizontal_ && std::abs(x-startX_)>std::abs(y-startY_))area_=horizontal_;
        dragged_=true;
        const float delta=area_->horizontal?x-lastX_:y-lastY_;lastX_=x;lastY_=y;
        const float travel=area_->horizontal?area_->track.w-area_->thumb.w:area_->track.h-area_->thumb.h;
        const float rows=thumb_?delta*area_->maxOffset/std::max(1.f,travel):-delta/area_->rowHeight;
        return MenuScroll{page_,area_->key,rows};
    }
private:
    std::optional<MenuScrollMetrics> area_,horizontal_;
    std::uint64_t page_=0;
    float startX_=0,startY_=0,lastX_=0,lastY_=0;
    bool thumb_=false,dragged_=false;
};
}
#endif
