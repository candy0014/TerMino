#ifndef TETR_BOT_CHATGPT_RECOVERED_PATH_RISK_EVALUATION_H
#define TETR_BOT_CHATGPT_RECOVERED_PATH_RISK_EVALUATION_H
#include "evaluation.h"
namespace Bot_chatgpt {
// Experimental recovery credit. A safe visible spawn alone does not erase
// exposure: the known plan must return below the baseline danger threshold.
// Unknown rises revoke all credit. No new attack, movement or rule is added.
class RecoveredPathRiskEvaluation : public Evaluation {
public:
    struct Status : Evaluation::Status {
        double knownExposure=0, recoveryRefund=0;
        Status& operator=(const Evaluation::Status& base) {
            static_cast<Evaluation::Status&>(*this)=base;
            knownExposure=recoveryRefund=0;
            return *this;
        }
    };
    Status get(Landing& landing,const Result& evaluated,std::size_t depth,
               const Status& previous,const m_tetris::TetrisContext::Env& env) const {
        if(previous.dead)return previous;
        Status result;
        static_cast<Evaluation::Status&>(result)=Evaluation::get(landing,evaluated,depth,previous,env);
        if(!result.dead && result.risen==0) {
            result.knownExposure=previous.knownExposure;
            result.recoveryRefund=previous.recoveryRefund;
            if(env.length>0) {
                result.knownExposure+=result.pathRisk-previous.pathRisk;
                if(evaluated.shape.map->roof+std::min(8,result.incoming)<=12)
                    result.recoveryRefund=result.knownExposure;
            }
            result.value+=result.recoveryRefund;
        }
        return result;
    }
};
}
#endif
