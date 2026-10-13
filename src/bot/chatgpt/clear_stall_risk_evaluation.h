#ifndef TETR_BOT_CHATGPT_CLEAR_STALL_RISK_EVALUATION_H
#define TETR_BOT_CHATGPT_CLEAR_STALL_RISK_EVALUATION_H
#include "settled_risk_evaluation.h"
namespace Bot_chatgpt {
// A narrower experimental refund: only a clearing placement stalls garbage.
// Non-clears keep the original buffer exposure, and every terminal reserve,
// attack, cancellation, spawn and death calculation remains unchanged.
class ClearStallRiskEvaluation : public SettledRiskEvaluation {
public:
    Status get(Landing&landing,const Result&evaluation,std::size_t depth,
               const Status&previous,const m_tetris::TetrisContext::Env&env)const {
        if(evaluation.shape.clear<=0)
            return Evaluation::get(landing,evaluation,depth,previous,env);
        return SettledRiskEvaluation::get(landing,evaluation,depth,previous,env);
    }
};
}
#endif
