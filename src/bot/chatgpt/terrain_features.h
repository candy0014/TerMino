#ifndef TETR_BOT_CHATGPT_TERRAIN_FEATURES_H
#define TETR_BOT_CHATGPT_TERRAIN_FEATURES_H
#include "tetris_core.h"
#include "tetr/util/bit_ops.h"
#include <algorithm>

namespace Bot_chatgpt {
// Exact native terrain, with sparse covered-empty runs and skyline wells.
// The historical HoleCount field never increments; preserve that behavior.
inline double fast_native_terrain(const m_tetris::TetrisMap&map) {
    if(map.roof==0)return -16000.0;
    const unsigned full=map.width==32?~0u:(1u<<map.width)-1;
    const unsigned columns=full&~1u;
    int colTrans=2*(map.height-map.roof),rowTrans=map.roof==map.height?0:map.width;
    for(int y=0;y<map.roof;++y) {
        const auto row=map.row[y];
        colTrans+=!(row&1u);colTrans+=!(row&(1u<<(map.width-1)));
        colTrans+=Util::popcount32((row^(row<<1))&columns);
        if(y)rowTrans+=Util::popcount32(map.row[y-1]^row);
    }
    rowTrans+=Util::popcount32(full&~map.row[0]);
    rowTrans+=Util::popcount32(map.roof==map.height?full&~map.row[map.roof-1]:map.row[map.roof-1]);
    int wellDepth=0;
    for(int x=0;x<map.width;++x) {
        const int left=x?map.top[x-1]:map.height;
        const int right=x+1<map.width?map.top[x+1]:map.height;
        const int count=std::max(0,std::min(left,right)-int(map.top[x]));
        wellDepth+=count*(count+1)/2;
    }
    int holeDepth=0,holeLines=0,holeRun[32]{},clearWidth[40]{};
    unsigned cover=0,previousHoles=0;
    for(int y=map.roof-1;y>=0;--y) {
        cover|=map.row[y];const unsigned holes=cover^map.row[y];
        if(holes) {
            int covered=0;
            for(int higher=y+1;higher<map.roof;++higher) {
                const unsigned bits=holes&map.row[higher];if(!bits)break;
                covered+=(higher+1)*Util::popcount32(bits);
            }
            clearWidth[holeLines++]=covered;
            for(unsigned remaining=holes;remaining;) {
                const unsigned bit=remaining&(~remaining+1u);
                int x=0;
#if defined(__GNUC__) || defined(__clang__)
                x=Util::trailing_zeros32(bit);
#else
                while((1u<<x)!=bit)++x;
#endif
                holeRun[x]=(previousHoles&bit)?holeRun[x]+1:1;
                holeDepth+=holeRun[x];remaining^=bit;
            }
        }
        previousHoles=holes;
    }
    double value=0.0-map.roof*128-colTrans*160-rowTrans*160-holeLines*380-wellDepth*100-holeDepth*40;
    double rate=32;
    for(int i=0;i<holeLines;++i,rate*=.25)value-=clearWidth[i]*rate;
    return value;
}
// Actual empty cells below occupied cells in the same column. Separate from
// hole-bearing row count and consecutive vertical hole depth; no mode inputs.
inline int covered_empty_cells(const m_tetris::TetrisMap& map) {
    unsigned cover = 0;
    int holes = 0;
    for (int y = map.roof - 1; y >= 0; --y) {
        cover |= map.row[y];
        unsigned empty = cover & ~map.row[y];
        while (empty) { empty &= empty - 1; ++holes; }
    }
    return holes;
}
}
#endif
