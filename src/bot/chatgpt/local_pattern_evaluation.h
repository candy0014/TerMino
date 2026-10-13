#ifndef TETR_BOT_CHATGPT_LOCAL_PATTERN_EVALUATION_H
#define TETR_BOT_CHATGPT_LOCAL_PATTERN_EVALUATION_H

#include "evaluation.h"
#include "local_pattern_weights.h"

namespace Bot_chatgpt {

class LocalPatternEvaluation : public Evaluation {
public:
    std::string ai_name() const { return "bot_chatgpt v0.4-local-pattern-rc1"; }

    Result eval(const Landing& landing, const m_tetris::TetrisMap& map,
                const m_tetris::TetrisMap& before, std::size_t clear) const {
        auto result = Evaluation::eval(landing, map, before, clear);
        result.shape.value += local_pattern_correction(map);
        return result;
    }
};

} // namespace Bot_chatgpt
#endif
