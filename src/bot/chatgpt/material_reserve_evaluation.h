#ifndef TETR_BOT_CHATGPT_MATERIAL_RESERVE_EVALUATION_H
#define TETR_BOT_CHATGPT_MATERIAL_RESERVE_EVALUATION_H

#include "evaluation.h"

namespace Bot_chatgpt {
// Experimental value of retaining a working stack. A small ordinary clear can
// consume useful material for little attack; a Spin/Quad or a stronger combo
// can convert the same cells efficiently. This terminal reserve is bounded at
// six full rows, so it adds no incentive to grow an already large stack.
// Holes, pressure, death, garbage and real attacks retain their original score.
class MaterialReserveEvaluation : public Evaluation {
public:
    static double material_reserve(int occupied) {
        return 50.0 * std::clamp(occupied, 0, 60);
    }
    Status get(Landing& landing, const Result& evaluation, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        if(previous.dead)return previous;
        auto result=Evaluation::get(landing,evaluation,depth,previous,env);
        if(!result.dead)result.value+=material_reserve(evaluation.shape.count);
        return result;
    }
};
}
#endif
