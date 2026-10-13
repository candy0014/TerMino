#ifndef TETR_BOT_CHATGPT_FLAT_ATTACK_EVALUATION_H
#define TETR_BOT_CHATGPT_FLAT_ATTACK_EVALUATION_H
#include "evaluation.h"
namespace Bot_chatgpt {
// Isolate the preference for cashing attack earlier. Only the real attack
// component becomes independent of depth; other rewards and risks retain
// their original timing. This does not add attack or a burst bonus.
class FlatAttackEvaluation : public Evaluation {
    double attackWeight_=560;
public:
    void set_policy(const PolicyWeights&policy){Evaluation::set_policy(policy);attackWeight_=policy.attack;}
    Status get(Landing&landing,const Result&evaluation,std::size_t depth,
               const Status&previous,const m_tetris::TetrisContext::Env&env)const{
        if(previous.dead)return previous;
        auto result=Evaluation::get(landing,evaluation,depth,previous,env);
        const double correction=(1-std::pow(.96,static_cast<double>(depth)))*attackWeight_*(result.attack-previous.attack);
        result.reward+=correction;result.value+=correction;
        return result;
    }
};
}
#endif
