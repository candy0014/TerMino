#ifndef TETR_BOT_CHATGPT_CHARGE_RESERVE_EVALUATION_H
#define TETR_BOT_CHATGPT_CHARGE_RESERVE_EVALUATION_H
#include "evaluation.h"
#include "clear_burst_evaluation.h"
namespace Bot_chatgpt {
// Keep the existing chain premium, but price each already releasable charge
// unit using the same undiscounted unit weight as real attack. The host starts
// releasing (b2b-1) only at b2b>=5. This is terminal asset valuation, not attack.
template<class Base> class ChargeReserve : public Base {
    double spread_=80;
public:
    using Status=typename Base::Status;
    using Result=typename Base::Result;
    void set_policy(const PolicyWeights&policy){Base::set_policy(policy);spread_=policy.attack-policy.b2b;}
    Status get(Landing&landing,const Result&evaluation,std::size_t depth,
               const Status&previous,const m_tetris::TetrisContext::Env&env)const {
        if(previous.dead)return previous;
        auto result=Base::get(landing,evaluation,depth,previous,env);
        if(!result.dead&&result.b2b>=5)result.value+=spread_*(result.b2b-1);
        return result;
    }
};
using ChargeReserveEvaluation=ChargeReserve<Evaluation>;
using ChargeBurstEvaluation=ChargeReserve<ClearBurstEvaluation>;
}
#endif
