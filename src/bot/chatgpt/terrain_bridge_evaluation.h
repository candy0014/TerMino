#ifndef TETR_BOT_CHATGPT_TERRAIN_BRIDGE_EVALUATION_H
#define TETR_BOT_CHATGPT_TERRAIN_BRIDGE_EVALUATION_H

#include "terrain_evaluation.h"
#include "terrain_bridge_model.h"

namespace Bot_chatgpt {

class TerrainBridgeEvaluation : public TerrainEvaluation {
public:
    std::string ai_name() const { return "bot_chatgpt v0.4-terrain-rc2"; }

    Result eval(const Landing& landing, const m_tetris::TetrisMap& map,
                const m_tetris::TetrisMap& before, std::size_t clear) const {
        return terrain_result(landing, map, before, clear, bridge_terrain_value(map));
    }
};

} // namespace Bot_chatgpt
#endif
