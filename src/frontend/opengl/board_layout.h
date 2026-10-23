#ifndef TETR_OPENGL_BOARD_LAYOUT_H
#define TETR_OPENGL_BOARD_LAYOUT_H
#include "tetr/presentation/board_view.h"
#include "tetr/runtime/piece.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace Graphical {
// Reserve at least five spawn rows. Short wells can use more of the space above.
inline constexpr int aboveFieldRows=5;
inline int reserved_above_rows(int height) {return aboveFieldRows+std::max(0,15-height)/4;}
// Only configured field dimensions determine geometry, independently of text,
// occupancy, spawn poses or the requested network sampling resolution.
struct FieldSize {int width=10,height=20;};
// Raise the complete layout by one standard 10x20 cell, preserving cell sizes.
inline constexpr float fieldVerticalOffset=23.1f;
// A dedicated rail for gameplay/replay/end-of-round keyboard help.
inline constexpr float controlsLeft=32,controlsTop=130,controlsWidth=132,controlsBottom=742,
    gameplayLeft=196;
inline constexpr float fieldTop=252-fieldVerticalOffset,fieldFloor=714-fieldVerticalOffset,fieldCeiling=112-fieldVerticalOffset,
    maxFieldWidth=360,maxCellSize=72,railTop=164-fieldVerticalOffset,railHeight=500,
    topPreviewCeiling=86,topPreviewGap=8;
struct BoardGeometry {
    float left=0,top=0,unit=1,width=0,height=0,rail=0,next=0,ceiling=78;
    float bottom() const {return top+height;}
};
inline int displayed_above_rows(const BoardGeometry& field) {
    if(field.unit<=0)return aboveFieldRows;
    return std::clamp(int(std::floor((field.top-field.ceiling)/field.unit+.001f)),0,Runtime::kHiddenRowCapacity);
}
inline int first_display_row(const Presentation::BoardView& board,const BoardGeometry& field) {
    return std::max(board.firstRow,-displayed_above_rows(field));
}
inline int top_preview_rows(int columns,float nextWidth,float nextTop) {
    if(columns<=0 || nextWidth<=0)return 0;
    const float available=std::max(0.f,nextTop-topPreviewGap-topPreviewCeiling);
    return std::clamp(int(std::floor(available/(nextWidth/columns))),0,Runtime::kHiddenRowCapacity);
}
struct TopPreviewGeometry {
    float left=0,top=0,width=0,height=0,unit=0;
    int rows=0;
};
inline TopPreviewGeometry top_preview_geometry(const BoardGeometry& field,int columns,float nextTop) {
    const float unit=columns>0?field.next/columns:0;
    const int rows=top_preview_rows(columns,field.next,nextTop);
    const float height=rows*unit;
    return {field.left+field.width+20,nextTop-topPreviewGap-height,field.next,height,unit,rows};
}
inline TopPreviewGeometry top_preview_geometry(const BoardGeometry& field,int columns) {
    return top_preview_geometry(field,columns,field.top);
}
inline std::size_t next_slot_count(const Presentation::BoardView& board) {
    return std::max(static_cast<std::size_t>(std::max(0,board.nextCount)),board.next.size());
}
inline float next_rail_top(const BoardGeometry& field,std::size_t nextCount,std::size_t cueCount) {
    // Follow the well downward while keeping each preview legible, both
    // objective panels and the booster/footer inside their existing bounds.
    const float minimumHeight=40+54*cueCount+24*nextCount;
    return std::min(field.top,std::max(railTop,railTop+railHeight-minimumHeight));
}
inline float fitted_field_unit(int width,int height,float availableWidth,float floor=fieldFloor) {
    return std::min({maxCellSize,availableWidth/std::max(1,width),
        (floor-fieldTop)/std::max(1,height),
        (floor-fieldCeiling)/std::max(1,height+reserved_above_rows(height))});
}
inline std::optional<float> target_line_y(const BoardGeometry& field,double remaining) {
    const float height=float(remaining*field.unit);
    if(!std::isfinite(remaining) || remaining<=0 || field.width<=0 || field.height<=0 || height>field.height)return {};
    return field.bottom()-height;
}
inline BoardGeometry fitted_field_geometry(int width,int height,float availableWidth) {
    const int rows=reserved_above_rows(height);
    float unit=fitted_field_unit(width,height,availableWidth);
    const float center=(fieldTop+fieldFloor)/2;
    if(height<12)unit=std::min(unit,(center-fieldCeiling)/(rows+height/2.f));
    const float top=std::max({fieldTop,fieldCeiling+rows*unit,
        height<12?center-height*unit/2:fieldTop});
    return {600-width*unit/2,top,unit,width*unit,height*unit,180,124,top-rows*unit};
}
inline BoardGeometry solo_geometry(int width,int height) {
    return fitted_field_geometry(width,height,maxFieldWidth);
}
struct SceneGeometry {std::vector<BoardGeometry> full,mini;};
inline SceneGeometry multiplayer_geometry(const std::vector<FieldSize>& full,const std::vector<FieldSize>& mini,
    float leftEdge=gameplayLeft,float rightEdge=1160) {
    SceneGeometry result;if(full.empty())return result;
    const int maxWidth=std::max(10,full.front().width),maxHeight=std::max(20,full.front().height);
    const int copies=std::max(2,int(full.size()));
    const float availableWidth=rightEdge-leftEdge;
    const float unit=std::min({25.f,500.f/maxHeight,(availableWidth-30*(copies-1))/(copies*(maxWidth+11.8f))});
    const float rail=6.4f*unit,next=4.f*unit,gap=.8f*unit,span=(maxWidth+11.8f)*unit;
    // Reserve two full footprints for a stable left-center main board.
    const float origin=leftEdge+(availableWidth-(copies*span+30*(copies-1)))/2;
    // Keep each horizontal footprint stable and reserve the spawn rows before
    // fitting the visible well. The main field owns the shared floor.
    const float availableFieldWidth=std::min(maxFieldWidth,maxWidth*unit);
    const auto main=fitted_field_geometry(full.front().width,full.front().height,availableFieldWidth);
    const float mainUnit=main.unit;
    const float bottom=std::max(fieldTop+300,main.bottom());
    for(std::size_t i=0;i<full.size();++i) {
        const float u=i==0?mainUnit:fitted_field_unit(full[i].width,full[i].height,availableFieldWidth,bottom);
        const float top=bottom-full[i].height*u;
        result.full.push_back({origin+i*(span+30)+rail+gap,top,
            u,full[i].width*u,full[i].height*u,rail,next,top-reserved_above_rows(full[i].height)*u});
    }
    if(mini.empty())return result;
    const float start=origin+full.size()*(span+30),available=rightEdge-start;
    const float areaTop=fieldCeiling,areaHeight=bottom-areaTop+24;
    if(available<=0)return result;
    int maxW=1,maxH=1;for(const auto& f:mini){maxW=std::max(maxW,f.width);maxH=std::max(maxH,f.height);}
    int columns=1,rows=int(mini.size());float best=0;
    for(int cols=1;cols<=int(mini.size());++cols) {
        const int countRows=(int(mini.size())+cols-1)/cols;
        const float u=std::min({unit,(available/cols-24)/maxW,(areaHeight/countRows-24)/(maxH+aboveFieldRows)});
        if(u>best+.001f || (std::abs(u-best)<=.001f && countRows<rows)) {best=u;columns=cols;rows=countRows;}
    }
    if(best<=0)return result;
    const float cellW=available/columns,cellH=areaHeight/rows;
    for(std::size_t i=0;i<mini.size();++i) {
        const auto& f=mini[i];
        const float u=std::min({unit,(cellW-24)/std::max(1,f.width),(cellH-24)/std::max(1,f.height+aboveFieldRows)});
        const float cellTop=areaTop+(i/columns)*cellH;
        const float floor=rows==1?bottom:cellTop+cellH-24;
        result.mini.push_back({start+(i%columns)*cellW+(cellW-f.width*u)/2,floor-f.height*u,
            u,f.width*u,f.height*u,0,0,std::max(cellTop,floor-(f.height+aboveFieldRows)*u)});
    }
    return result;
}

}
#endif
