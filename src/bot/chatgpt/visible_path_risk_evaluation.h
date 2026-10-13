#ifndef TETR_BOT_CHATGPT_VISIBLE_PATH_RISK_EVALUATION_H
#define TETR_BOT_CHATGPT_VISIBLE_PATH_RISK_EVALUATION_H
#include "evaluation.h"
namespace Bot_chatgpt {
// Experimental valuation only. Retain all actual transitions, terminal
// pressure, and spawn-death checks. Refund cumulative exposure on a fully
// known board only after the visible next spawn has passed the base check.
// Revoke the refund at an unknown-rise boundary. Unseen enemy attacks are
// still unknown; this is not a guarantee against every future attack.
class VisiblePathRiskEvaluation : public Evaluation {
public:
    struct Status : Evaluation::Status {
        double visiblePathRefund=0;
        Status& operator=(const Evaluation::Status& base) {
            static_cast<Evaluation::Status&>(*this)=base;
            visiblePathRefund=0;
            return *this;
        }
    };
    Status get(Landing& landing,const Result& evaluated,std::size_t depth,
               const Status& previous,const m_tetris::TetrisContext::Env& env) const {
        if(previous.dead)return previous;
        Status result;
        static_cast<Evaluation::Status&>(result)=Evaluation::get(landing,evaluated,depth,previous,env);
        if(!result.dead && result.risen==0) {
            result.visiblePathRefund=previous.visiblePathRefund;
            if(env.length>0)
                result.visiblePathRefund+=result.pathRisk-previous.pathRisk;
            result.value+=result.visiblePathRefund;
        }
        return result;
    }
};
}
#endif
