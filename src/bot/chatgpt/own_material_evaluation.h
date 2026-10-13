#ifndef TETR_BOT_CHATGPT_OWN_MATERIAL_EVALUATION_H
#define TETR_BOT_CHATGPT_OWN_MATERIAL_EVALUATION_H

#include "evaluation.h"

namespace Bot_chatgpt {
// Follow-up to the total-occupancy reserve experiment. Standard garbage adds
// nine cells per row; those cells are not paid for by our own piece supply.
// Value our remaining material only, so clearing a garbage row does not lose
// the same reserve as clearing ten cells we assembled ourselves. The known
// garbageHeight excludes abstract future rises, as does the stored map.
class OwnMaterialEvaluation : public Evaluation {
public:
    static double material_reserve(int occupied, int garbageHeight = 0) {
        return 50.0 * std::clamp(occupied - 9 * garbageHeight, 0, 60);
    }
    Status get(Landing& landing, const Result& evaluation, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        if (previous.dead) return previous;
        auto result = Evaluation::get(landing, evaluation, depth, previous, env);
        if (!result.dead)
            result.value += material_reserve(evaluation.shape.count, result.garbageHeight);
        return result;
    }
};
}
#endif
