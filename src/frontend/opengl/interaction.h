#ifndef TETR_OPENGL_INTERACTION_H
#define TETR_OPENGL_INTERACTION_H
#include "tetr/frontend/ui_surface.h"
#include <optional>
#include <vector>

namespace Graphical {
struct Rect {float x,y,w,h;};
struct UiHit {
    Rect bounds;std::size_t index=0;bool back=false;std::optional<std::size_t> ancestor;
    Frontend::UiCommandKind kind=Frontend::UiCommandKind::Activate;
    int row=0,column=0;
};
struct EditorFieldHit {Rect bounds;int firstRow=0,firstColumn=0,rows=0,columns=0;};
struct MenuScroll {std::uint64_t page=0;std::string key;float rows=0;};
inline bool contains(Rect r,float x,float y) {return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;}
struct MenuScrollMetrics {
    Rect bounds;std::string key;float rowHeight=1,offset=0,maxOffset=0;
    Rect track{},thumb{};
    bool horizontal=false;
};
// A value snapshot of the displayed layout. Input never calls into a renderer
// or holds its locks while glyphs, OpenGL or presentation are busy.
struct Interaction {
    std::optional<Frontend::UiMetadata> metadata;
    std::vector<UiHit> hits;
    std::optional<Rect> textRect;
    std::optional<EditorFieldHit> editorField;
    std::vector<std::pair<Rect,std::size_t>> scrollAreas;
    std::vector<std::pair<Rect,std::string>> menuScrollAreas;
    std::vector<MenuScrollMetrics> menuScrollMetrics;
    std::optional<MenuScrollMetrics> menu_drag(float x,float y,const Frontend::UiMetadata& current,
        std::optional<bool> horizontal={}) const {
        if(!metadata || metadata->page!=current.page || !Frontend::same_ui_page(*metadata,current))return {};
        for(const auto& area:menuScrollMetrics)if(area.maxOffset>0 && (!horizontal || area.horizontal==*horizontal) && contains(area.bounds,x,y))return area;
        return {};
    }
    std::optional<UiHit> hit(float x,float y,const Frontend::UiMetadata& current) const {
        if(!metadata)return {};
        const auto& shown=*metadata;
        if(shown.page!=current.page || !Frontend::same_ui_page(shown,current))return {};
        for(const auto& hit:hits)if(x>=hit.bounds.x && y>=hit.bounds.y && x<hit.bounds.x+hit.bounds.w && y<hit.bounds.y+hit.bounds.h)return hit;
        if(editorField) {
            const auto& f=*editorField;const auto b=f.bounds;
            if(x>=b.x && y>=b.y && x<b.x+b.w && y<b.y+b.h) {
                UiHit result{b};result.kind=Frontend::UiCommandKind::MapPaint;
                result.row=f.firstRow+std::min(f.rows-1,int((y-b.y)*f.rows/b.h));
                result.column=f.firstColumn+std::min(f.columns-1,int((x-b.x)*f.columns/b.w));
                return result;
            }
        }
        return {};
    }
    std::optional<std::size_t> scroll(float x,float y,const Frontend::UiMetadata& current) const {
        if(!metadata || metadata->page!=current.page || !Frontend::same_ui_page(*metadata,current))return {};
        for(const auto& area:scrollAreas) {
            const auto b=area.first;if(x>=b.x && y>=b.y && x<b.x+b.w && y<b.y+b.h)return area.second;
        }
        return {};
    }
    std::optional<std::string> menu_scroll(float x,float y,const Frontend::UiMetadata& current) const {
        if(!metadata || metadata->page!=current.page || !Frontend::same_ui_page(*metadata,current))return {};
        for(const auto& area:menuScrollAreas) {
            const auto b=area.first;if(x>=b.x && y>=b.y && x<b.x+b.w && y<b.y+b.h)return area.second;
        }
        return {};
    }
};
}
#endif
