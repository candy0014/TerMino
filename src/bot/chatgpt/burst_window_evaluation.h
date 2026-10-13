#ifndef TETR_BOT_CHATGPT_BURST_WINDOW_EVALUATION_H
#define TETR_BOT_CHATGPT_BURST_WINDOW_EVALUATION_H
#include "evaluation.h"
namespace Bot_chatgpt {
// A bounded estimate for concentrated net pressure. The one-rise capacity is
// eight; only pressure between one and two such capacities gets extra value.
// This is a three-placement envelope, not the game's one-second spike rule.
class BurstWindowEvaluation : public Evaluation {
    double attackWeight_=560;
public:
    struct Status:Evaluation::Status {
        int outgoingLast=0,outgoingBeforeLast=0,outgoingPeak=0;
        Status&operator=(const Evaluation::Status&s){static_cast<Evaluation::Status&>(*this)=s;outgoingLast=outgoingBeforeLast=outgoingPeak=0;return *this;}
    };
    void set_policy(const PolicyWeights&p){Evaluation::set_policy(p);attackWeight_=p.attack;}
    Status get(Landing&landing,const Result&evaluation,std::size_t depth,
               const Status&previous,const m_tetris::TetrisContext::Env&env)const{
        if(previous.dead)return previous;
        Status result;result=Evaluation::get(landing,evaluation,depth,previous,env);
        // Even opener double cancellation cannot emit attack while pending
        // exceeds it. FIFO timing affects rise, not this net outgoing amount.
        const int outgoing=std::clamp(result.attack-previous.attack-previous.incoming,0,16);
        result.outgoingBeforeLast=previous.outgoingLast;result.outgoingLast=outgoing;
        result.outgoingPeak=std::min(16,std::max(previous.outgoingPeak,outgoing+previous.outgoingLast+previous.outgoingBeforeLast));
        if(!result.dead)result.value+=attackWeight_*std::max(0,result.outgoingPeak-8);
        return result;
    }
};
}
#endif
