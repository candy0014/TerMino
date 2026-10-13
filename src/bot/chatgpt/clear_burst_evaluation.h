#ifndef TETR_BOT_CHATGPT_CLEAR_BURST_EVALUATION_H
#define TETR_BOT_CHATGPT_CLEAR_BURST_EVALUATION_H

#include "evaluation.h"

namespace Bot_chatgpt {
// A sequence of actual clears stalls garbage on every step. Charge its peak
// physical exposure once, instead of repeatedly charging buffered rows that
// never entered the board. A non-clear ends the burst and retains the original
// risk charge. Terminal pressure, attack, terrain and spawn checks are intact.
class ClearBurstEvaluation : public Evaluation {
public:
    struct Status : Evaluation::Status {
        double clearExposure = 0;
        Status& operator=(const Evaluation::Status& source) {
            static_cast<Evaluation::Status&>(*this) = source;
            clearExposure = 0;
            return *this;
        }
    };

    Status get(Landing& landing, const Result& evaluation, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        if (previous.dead) return previous;
        Status result;
        result = Evaluation::get(landing, evaluation, depth, previous, env);
        if (evaluation.shape.clear > 0) {
            const double physical = std::max(0, evaluation.shape.map->roof + result.risen - 12);
            const double exposure = std::pow(0.96, static_cast<double>(depth)) * 35 * physical * physical;
            const double oldPeak = previous.previousCleared ? previous.clearExposure : 0;
            result.clearExposure = std::max(oldPeak, exposure);
            const double correctedRisk = previous.pathRisk + result.clearExposure - oldPeak;
            result.value += result.pathRisk - correctedRisk;
            result.pathRisk = correctedRisk;
        }
        return result;
    }
};
}
#endif
