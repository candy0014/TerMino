#ifndef TETR_BOT_CHATGPT_TERRAIN_BRIDGE_MODEL_H
#define TETR_BOT_CHATGPT_TERRAIN_BRIDGE_MODEL_H

#include "terrain_model.h"

namespace Bot_chatgpt {

// Isolated V2 experiment: all V1 weights and surface/channel treatment stay
// unchanged. Only some buried cells receive the existing shallow-cavity cost.
struct BridgeTerrainFeatures {
    TerrainFeatures terrain;
    int removableBridgeCavities = 0;
};

inline bool bridge_row_has_small_gap(std::uint32_t gap) {
    if (!gap) return false;
    const auto withoutFirst = gap & (gap - 1);
    if (!withoutFirst) return true;
    // Exactly two missing cells must be adjacent, not independent columns.
    return !(withoutFirst & (withoutFirst - 1)) && (gap & (gap << 1));
}

inline BridgeTerrainFeatures bridge_terrain_features(const m_tetris::TetrisMap& map,
                                                      const TerrainWeights& weights = {}) {
    BridgeTerrainFeatures result{terrain_features(map, weights), 0};
    if (result.terrain.buriedCavities == 0) return result;
    const int width = std::clamp(map.width, 1, 32);
    const int roof = std::clamp(map.roof, 0, m_tetris::max_height);
    const std::uint32_t full = width == 32 ? ~std::uint32_t{0}
        : (std::uint32_t{1} << width) - 1;
    std::array<int, 32> covers{};
    // Per-column union of higher rows that are NOT its covering rows. When
    // considering the second ceiling, the first ceiling is allowed to have
    // cleared, but all unrelated blockers remain in the sky-access check.
    std::array<std::uint32_t, 32> otherBlockers{};
    std::array<bool, 32> removable{};
    removable.fill(true);
    std::uint32_t reachable = full;
    for (int y = roof - 1; y >= 0; --y) {
        const std::uint32_t occupied = map.row[y] & full;
        const std::uint32_t gap = full ^ occupied;
        reachable &= gap;
        for (;;) {
            const auto expanded = (reachable | (reachable << 1) | (reachable >> 1)) & gap;
            if (expanded == reachable) break;
            reachable = expanded;
        }
        const bool smallGap = bridge_row_has_small_gap(gap);
        for (int x = 0; x < width; ++x) {
            const auto bit = std::uint32_t{1} << x;
            if (occupied & bit) {
                ++covers[x];
                removable[x] = removable[x] && covers[x] <= 2 && smallGap &&
                    !(gap & otherBlockers[x]);
            } else {
                const bool v1Shallow = (reachable & bit) && covers[x] <= 2 && map.top[x] - y <= 3;
                if (covers[x] && removable[x] && !v1Shallow)
                    ++result.removableBridgeCavities;
                otherBlockers[x] |= occupied;
            }
        }
        // A rejected covering column can never become removable again when
        // scanning downward, so no later hole can receive the discount.
        if (std::none_of(removable.begin(), removable.begin() + width,
                         [](bool value) { return value; })) break;
    }
    result.terrain.buriedCavities -= result.removableBridgeCavities;
    result.terrain.shallowCavities += result.removableBridgeCavities;
    return result;
}

// This is an optimistic repair-cost hint, not a tetromino reachability test:
// each of at most two covering rows has one contiguous gap of <=2 cells with
// an open vertical approach after the upper cover is removed. It neither
// promises an available piece nor invents a clear, Spin, B2B, or attack.
// Missing one of those conditions preserves V1's full buried-cavity penalty.
inline double bridge_terrain_value(const m_tetris::TetrisMap& map,
                                  const TerrainWeights& weights = {}) {
    return terrain_value(bridge_terrain_features(map, weights).terrain, weights);
}

} // namespace Bot_chatgpt

#endif
