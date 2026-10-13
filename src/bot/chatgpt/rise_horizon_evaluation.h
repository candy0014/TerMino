#ifndef TETR_BOT_CHATGPT_RISE_HORIZON_EVALUATION_H
#define TETR_BOT_CHATGPT_RISE_HORIZON_EVALUATION_H

#include "evaluation.h"

namespace Bot_chatgpt {

// Experimental planning boundary. A rise consumes visible FIFO packets but
// their holes are unknown. Score that first rise with the original evaluator,
// then retain that terminal estimate instead of scoring later placements on
// the unshifted relative board. This does not declare the game dead or certify
// future geometry. Different branches can stop at different planning depths.
class RiseHorizonEvaluation : public Evaluation {
public:
    std::string ai_name() const { return "bot_chatgpt rise-horizon candidate"; }

    Status get(Landing& landing, const Result& evaluation, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        if (previous.risen > 0) return previous;
        return Evaluation::get(landing, evaluation, depth, previous, env);
    }
};

} // namespace Bot_chatgpt
#endif
