#ifndef TETR_BOT_CHATGPT_MOCH_MULTI_STEP_EVALUATION_H
#define TETR_BOT_CHATGPT_MOCH_MULTI_STEP_EVALUATION_H

#include "evaluation.h"

namespace Bot_chatgpt {

inline constexpr char moch_multi_step_model_sha256[] =
    "039b8a30552ef927cd3ffff6db98223be35e430ccf810bde1c33b58ffc4a7932";
inline constexpr char moch_multi_step_registration_sha256[] =
    "f30d84b734e4503d7b300bb15e6cd143ace68d9581bf7a7b85500dccbc6c0c92";
inline constexpr double moch_multi_step_terrain_scale = 0.5;

// Frozen five-step Moch fit: every one of the seven existing Botris terrain
// penalties has multiplier 0.5. Their common multiplier needs only this one
// operation after the original scan, including its empty-board value (-8000).
// Rule transitions, rewards, pressure, path risk, and death absorption remain
// the inherited Evaluation::get. This is a default-OFF experimental candidate,
// not evidence of match strength or a generic configurable terrain model.
class MochMultiStepEvaluation : public Evaluation {
public:
    std::string ai_name() const { return "bot_chatgpt v0.4-moch-multistep-rc1"; }

    Result eval(const Landing& landing, const m_tetris::TetrisMap& map,
                const m_tetris::TetrisMap& before, std::size_t clear) const {
        auto result = Evaluation::eval(landing, map, before, clear);
        result.shape.value *= moch_multi_step_terrain_scale;
        return result;
    }
};

} // namespace Bot_chatgpt
#endif
