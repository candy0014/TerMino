#ifndef TETR_BOT_CHATGPT_SETTLED_RISK_EVALUATION_H
#define TETR_BOT_CHATGPT_SETTLED_RISK_EVALUATION_H
#include "evaluation.h"
namespace Bot_chatgpt {
// Experimental separation of intermediate exposure and terminal reserve.
// Still-buffered lines remain in the original terminal danger estimate; the
// path integral counts only the visible board and explicitly projected rises.
// All attack, cancellation, forecast, spawn/death and terrain terms are native.
class SettledRiskEvaluation : public Evaluation {
public:
    Status get(Landing&landing,const Result&evaluation,std::size_t depth,
               const Status&previous,const m_tetris::TetrisContext::Env&env)const {
        auto result=Evaluation::get(landing,evaluation,depth,previous,env);
        if(previous.dead)return result;
        const int height=evaluation.shape.map->roof+result.risen;
        const double anticipated=std::max(0,height+std::min(8,result.incoming)-12);
        const double physical=std::max(0,height-12);
        const double refund=std::pow(0.96,static_cast<double>(depth))*35.0*
            (anticipated*anticipated-physical*physical);
        result.pathRisk-=refund;result.value+=refund;
        return result;
    }
};
}
#endif
