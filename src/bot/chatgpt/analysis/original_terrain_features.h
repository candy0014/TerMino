#include "tetr/util/bit_ops.h"
#ifndef TETR_BOT_CHATGPT_ANALYSIS_ORIGINAL_TERRAIN_FEATURES_H
#define TETR_BOT_CHATGPT_ANALYSIS_ORIGINAL_TERRAIN_FEATURES_H
// Offline decomposition of the seven effective terms in original Botris::eval.
#include "../evaluation.h"
#include <array>
namespace Bot_chatgpt::Analysis {
inline constexpr std::array<double,7> original_terrain_coefficients{{-128,-160,-160,-380,-100,-40,-1}};
inline std::array<double,7> original_terrain_features(const m_tetris::TetrisMap& map) {
    const int last=map.width-1;
    const std::uint32_t full=map.width==32?0xffffffffu:((1u<<map.width)-1);
    int col=2*(map.height-map.roof),row=map.roof==map.height?0:map.width;
    const auto bits=[](std::uint32_t x){return Util::popcount32(x);};
    for(int y=0;y<map.roof;++y) {
        col+=!map.full(0,y);col+=!map.full(last,y);
        col+=bits((map.row[y]^(map.row[y]<<1))&(full&~1u));
        if(y)row+=bits(map.row[y-1]^map.row[y]);
    }
    row+=bits(full&~map.row[0]);
    if(map.roof>0)row+=bits(map.roof==map.height?full&~map.row[map.roof-1]:map.row[map.roof-1]);
    std::array<int,32> holes{},wells{};int holeLines=0,holeDepth=0,wellDepth=0;
    std::uint32_t covered=0;double coverWeighted=0,rate=32;
    for(int y=map.roof-1;y>=0;--y) {
        covered|=map.row[y];const auto lineHole=covered^map.row[y];
        if(lineHole) {
            ++holeLines;int clearWidth=0;
            for(int hy=y+1;hy<map.roof;++hy) {
                const auto check=lineHole&map.row[hy];if(!check)break;
                clearWidth+=(hy+1)*bits(check);
            }
            coverWeighted+=clearWidth*rate;rate*=0.25;
        }
        for(int x=1;x<last;++x) {
            if((lineHole>>x)&1)holeDepth+=++holes[x];else holes[x]=0;
            // Original Botris does NOT reset a well counter on other rows.
            if(((covered>>(x-1))&7)==5)wellDepth+=++wells[x];
        }
        if(lineHole&1)holeDepth+=++holes[0];else holes[0]=0;
        if((covered&3)==2)wellDepth+=++wells[0];
        if((lineHole>>last)&1)holeDepth+=++holes[last];else holes[last]=0;
        if(((covered>>(last-1))&3)==1)wellDepth+=++wells[last];
    }
    return {{double(map.roof),double(col),double(row),double(holeLines),double(wellDepth),double(holeDepth),coverWeighted}};
}
inline double original_terrain_dot(const std::array<double,7>& features) {
    double value=0;for(std::size_t i=0;i<features.size();++i)value+=original_terrain_coefficients[i]*features[i];return value;
}
}
#endif
