#ifndef TETR_BOT_CHATGPT_LEARNED_TERRAIN_EVALUATION_H
#define TETR_BOT_CHATGPT_LEARNED_TERRAIN_EVALUATION_H

#include "evaluation.h"
#include "learned_terrain_model.h"

namespace Bot_chatgpt {

class LearnedTerrainEvaluation : public Evaluation {
public:
    std::string ai_name() const { return "bot_chatgpt v0.4-learned-terrain-rc1"; }

    Result eval(const Landing& landing, const m_tetris::TetrisMap& map,
                const m_tetris::TetrisMap& before, std::size_t clear) const {
        auto result = Evaluation::eval(landing, map, before, clear);
        result.shape.value += learned_terrain_correction(map);
        return result;
    }
};

} // namespace Bot_chatgpt
#endif
