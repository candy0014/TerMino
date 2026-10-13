#ifndef ZZZTOJ_STANDALONE_RULE_TETR_SRS_PLUS_H
#define ZZZTOJ_STANDALONE_RULE_TETR_SRS_PLUS_H

#include "tetris_core.h"

#include <map>
#include <utility>

namespace rule_tetr_srs_plus {

// Search-reachability rule matching the parent tetr project's standard
// tetromino shapes and SRS+ kick order. Bot evaluation/search code is not
// changed; only the TetrisContext pointer graph is built with these moves.
struct TetrisRule {
    static bool init(int width, int height);
    static std::map<std::pair<char, std::uint8_t>, m_tetris::TetrisOpertion>
        get_opertion();
    static std::map<char, m_tetris::TetrisBlockStatus(*)(
        const m_tetris::TetrisContext*)> get_generate();
};

} // namespace rule_tetr_srs_plus

#endif
