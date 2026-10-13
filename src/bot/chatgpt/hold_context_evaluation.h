#ifndef TETR_BOT_CHATGPT_HOLD_CONTEXT_EVALUATION_H
#define TETR_BOT_CHATGPT_HOLD_CONTEXT_EVALUATION_H

#include "evaluation.h"

namespace Bot_chatgpt {

// P3-A: adapt only the native evaluator's preview after an EMPTY hold.
// The native tree advances its own queue correctly when expanding this child,
// but builds Env from the parent's next iterator before that extra consumption.
// Keep the correction private: do not change the shared opponent engine.
class HoldContextEvaluation : public Evaluation {
public:
    struct Status : Evaluation::Status {
        bool holdEmpty = false;
    };

    static m_tetris::TetrisContext::Env corrected_environment(
        m_tetris::TetrisContext::Env env, bool holdEmpty) {
        if (holdEmpty && env.is_hold && env.length) {
            --env.length;
            env.next = env.length ? env.next + 1 : nullptr;
        }
        return env;
    }

    Status get(Landing& landing, const Result& evaluated, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        if (previous.dead) return previous;
        Status result;
        static_cast<Evaluation::Status&>(result) = Evaluation::get(
            landing, evaluated, depth, previous, corrected_environment(env, previous.holdEmpty));
        result.holdEmpty = previous.holdEmpty && !env.is_hold;
        return result;
    }
};

} // namespace Bot_chatgpt
#endif
