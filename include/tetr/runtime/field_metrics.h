#ifndef TETR_RUNTIME_FIELD_METRICS_H
#define TETR_RUNTIME_FIELD_METRICS_H
#include "tetr/runtime/playfield.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/constants.h"
#include <algorithm>
#include <array>
namespace Runtime {
struct FieldMetrics {int height=0,secretGrade=0,checkerboardRows=0;bool symmetric=true;};
inline FieldMetrics field_metrics(const Playfield& field,RotationSystem system) {
    FieldMetrics m;std::array<int,128> families;families.fill(-1);
    for(const auto& p:Piece::catalog())if(p.metadata.rotationSystem==system && p.metadata.colorId>=0 && p.metadata.colorId<128)
        families[p.metadata.colorId]=p.metadata.familyIndex;
    constexpr std::array<int,7> mirror{{0,2,1,3,6,5,4}};
    for(int y=-kHiddenRowCapacity;y<field.height();++y)for(int x=0;x<field.width();++x) {
        const auto a=field.cell(y,x);if(!a.occupied())continue;
        m.height=std::max(m.height,field.height()-y);
        const auto b=field.cell(y,field.width()-1-x);if(!b.occupied())continue;
        const int af=a.is_piece() && a.color_id()>=0 && a.color_id()<128?families[a.color_id()]:-1;
        const int bf=b.is_piece() && b.color_id()>=0 && b.color_id()<128?families[b.color_id()]:-1;
        if(af<0 || af>=7 || bf!=mirror[af])m.symmetric=false;
    }
    const int w=field.width(),h=field.height();
    // One hole per row, tracing a >; each hole must be capped from above.
    for(int layer=0;layer<std::min(h-1,2*w-1);++layer) {
        const int y=h-1-layer,hole=layer<w?layer:2*w-2-layer;
        bool matches=hole>=0 && hole<w && field.cell(y-1,hole).occupied();
        for(int x=0;x<w && matches;++x)matches=field.cell(y,x).occupied()!=(x==hole);
        if(!matches)break;++m.secretGrade;
    }
    for(int layer=0;layer<h;++layer) {
        const int y=h-1-layer;bool matches=true;
        for(int x=0;x<w;++x)if(field.cell(y,x).occupied()!=((x+layer)%2==0)){matches=false;break;}
        if(!matches)break;++m.checkerboardRows;
    }
    return m;
}
}
#endif
