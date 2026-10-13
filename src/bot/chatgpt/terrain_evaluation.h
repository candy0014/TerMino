#ifndef TETR_BOT_CHATGPT_TERRAIN_EVALUATION_H
#define TETR_BOT_CHATGPT_TERRAIN_EVALUATION_H

#include "evaluation.h"
#include "terrain_model.h"

namespace Bot_chatgpt {

// Isolate the stacking hypothesis: search, attack/chain rewards, visible
// garbage forecasting and spawn checks are inherited without modification.
class TerrainEvaluation : public Evaluation {
public:
    void init(const m_tetris::TetrisContext* context) {
        Evaluation::init(context);
        context_ = context;
    }

    std::string ai_name() const { return "bot_chatgpt v0.4-terrain-rc1"; }

    Result eval(const Landing& landing, const m_tetris::TetrisMap& map,
                const m_tetris::TetrisMap& before, std::size_t clear) const {
        return terrain_result(landing, map, before, clear, terrain_value(map));
    }

protected:
    Result terrain_result(const Landing& landing, const m_tetris::TetrisMap& map,
                          const m_tetris::TetrisMap& before, std::size_t clear,
                          double value) const {
        Result result;
        result.before = &before;
        result.shape = {value, static_cast<int>(clear), map.count, &map};
        for (int y = 0; y < landing->height; ++y) {
            const int row = landing->row + y;
            if ((before.row[row] | landing->data[y]) == context_->full())
                result.clearedRows |= std::uint64_t{1} << row;
        }
        return result;
    }

private:
    const m_tetris::TetrisContext* context_ = nullptr;
};

} // namespace Bot_chatgpt
#endif
