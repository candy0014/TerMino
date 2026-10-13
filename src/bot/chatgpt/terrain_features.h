#ifndef TETR_BOT_CHATGPT_TERRAIN_FEATURES_H
#define TETR_BOT_CHATGPT_TERRAIN_FEATURES_H
#include "tetris_core.h"

namespace Bot_chatgpt {
// Actual empty cells below occupied cells in the same column. Separate from
// hole-bearing row count and consecutive vertical hole depth; no mode inputs.
inline int covered_empty_cells(const m_tetris::TetrisMap& map) {
    unsigned cover = 0;
    int holes = 0;
    for (int y = map.roof - 1; y >= 0; --y) {
        cover |= map.row[y];
        unsigned empty = cover & ~map.row[y];
        while (empty) { empty &= empty - 1; ++holes; }
    }
    return holes;
}
}
#endif
