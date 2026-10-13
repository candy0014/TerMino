#ifndef TETR_BOT_CHATGPT_RELATIVE_COVER_EVALUATION_H
#define TETR_BOT_CHATGPT_RELATIVE_COVER_EVALUATION_H

#include "evaluation.h"
#include "integer_utils.h"

namespace Bot_chatgpt {
// Experimental local cover depth. Native cover cost multiplies each covering
// cell by its absolute board row (higher+1). Replace only that factor with
// (higher-holeRow); pressure and board height keep their original terms.
// This is a geometry heuristic, not proof that a cavity is legally repairable.
inline double relative_cover_refund(const m_tetris::TetrisMap&map){
    unsigned cover=0;double rate=32,refund=0;
    for(int y=map.roof-1;y>=0;--y){
        cover|=map.row[y];const unsigned holes=cover^map.row[y];
        if(!holes)continue;
        int coveringCells=0;
        for(int higher=y+1;higher<map.roof;++higher){
            const unsigned bits=holes&map.row[higher];if(!bits)break;
            coveringCells+=ZZZ_BitCount(bits);
        }
        refund+=rate*(y+1)*coveringCells;rate*=.25;
    }
    return refund;
}
class RelativeCoverEvaluation : public Evaluation {
public:
    Result eval(const Landing&landing,const m_tetris::TetrisMap&map,
                const m_tetris::TetrisMap&before,std::size_t clear)const{
        auto result=Evaluation::eval(landing,map,before,clear);
        result.shape.value+=relative_cover_refund(map);
        return result;
    }
};
}
#endif
