#ifndef TETR_BOT_CHATGPT_TERRAIN_MODEL_H
#define TETR_BOT_CHATGPT_TERRAIN_MODEL_H

#include "tetris_core.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace Bot_chatgpt {

// Experimental terminal geometry only. Attack, chains, garbage timing and
// legality still belong to Evaluation. These weights have not been fitted to
// an opponent, match seed or replay and are not a claim of playing strength.
struct TerrainWeights {
    double aggregateHeight = 16;
    double maximumHeight = 72;
    double roughness = 48;
    double steepness = 16;
    double shallowCavity = 140;
    double buriedCavity = 620;
    double extraCover = 80;
    double secondaryWell = 80;
    double excessiveWell = 45;
    double readyWellRow = 70;
};

struct TerrainFeatures {
    int aggregateHeight = 0;
    int maximumHeight = 0;
    // A covered cell is shallow only when its ceiling is <=3 rows away,
    // <=2 occupied cells cover it, and empty space connects it to the sky by
    // downward/horizontal steps. This is a repair-cost hint, NOT proof that
    // a tetromino can enter, rotate, or score a Spin there.
    int shallowCavities = 0;
    int buriedCavities = 0;
    int extraCover = 0; // Occupied cells above holes, beyond the first ceiling.
    int primaryWellColumn = -1;
    int primaryWellDepth = 0;
    int roughness = 0;
    int steepness = 0;
    int secondaryWells = 0;
    int excessiveWell = 0;
    // Consecutive rows, starting at the open well's floor, missing only its
    // column. Capped at four: this is a bounded terminal option, never attack.
    int readyWellRows = 0;
};

inline double terrain_surface_value(const TerrainFeatures& f,
                                    const TerrainWeights& weights) {
    return -weights.roughness * f.roughness
        - weights.steepness * f.steepness
        - weights.secondaryWell * f.secondaryWells
        - weights.excessiveWell * f.excessiveWell
        + weights.readyWellRow * f.readyWellRows;
}

inline TerrainFeatures terrain_features(const m_tetris::TetrisMap& map,
                                        const TerrainWeights& weights = {}) {
    TerrainFeatures base;
    const int width = std::clamp(map.width, 1, 32);
    const int roof = std::clamp(map.roof, 0, m_tetris::max_height);
    const std::uint32_t full = width == 32 ? ~std::uint32_t{0}
        : (std::uint32_t{1} << width) - 1;
    base.maximumHeight = roof;
    for (int x = 0; x < width; ++x) base.aggregateHeight += map.top[x];

    std::array<int, 32> covers{};
    std::uint32_t cover = 0, reachable = full;
    for (int y = roof - 1; y >= 0; --y) {
        const std::uint32_t occupied = map.row[y] & full;
        const std::uint32_t empty = full ^ occupied;
        reachable &= empty;
        // Row expansion is bounded by board width and needs no allocation.
        // Upward moves are intentionally absent: enclosed or downward-only
        // inaccessible pockets cannot receive the shallow repair discount.
        for (;;) {
            const auto expanded = (reachable | (reachable << 1) | (reachable >> 1)) & empty;
            if (expanded == reachable) break;
            reachable = expanded;
        }
        const std::uint32_t holes = cover & empty;
        for (int x = 0; x < width; ++x) {
            const std::uint32_t bit = std::uint32_t{1} << x;
            if (holes & bit) {
                const bool shallow = (reachable & bit) && covers[x] <= 2 && map.top[x] - y <= 3;
                if (shallow) ++base.shallowCavities;
                else ++base.buriedCavities;
                base.extraCover += std::max(0, covers[x] - 1);
            }
            if (occupied & bit) ++covers[x];
        }
        cover |= occupied;
    }

    std::array<int, 32> depths{};
    for (int x = 0; x < width; ++x) {
        const int left = x ? map.top[x - 1] : roof;
        const int right = x + 1 < width ? map.top[x + 1] : roof;
        depths[x] = std::max(0, std::min(left, right) - map.top[x]);
    }

    // Compare a normal surface with every possible single attack/digging
    // channel. Only one column can be exempt from ordinary skyline penalties;
    // a second deep well still requires another scarce I or an awkward repair.
    // Selecting by score also avoids an arbitrary left/right tie bias in value.
    TerrainFeatures best;
    double bestSurface = 0;
    for (int well = -1; well < width; ++well) {
        if (well >= 0 && depths[well] == 0) continue;
        TerrainFeatures candidate = base;
        candidate.primaryWellColumn = well;
        for (int x = 0; x < width; ++x) {
            if (x != well)
                candidate.secondaryWells += depths[x] * (depths[x] + 1) / 2;
            if (x + 1 < width && x != well && x + 1 != well) {
                const int difference = std::abs(map.top[x] - map.top[x + 1]);
                candidate.roughness += difference;
                const int cliff = std::max(0, difference - 2);
                candidate.steepness += cliff * cliff;
            }
        }
        if (well >= 0) {
            candidate.primaryWellDepth = depths[well];
            const int excess = std::max(0, depths[well] - 4);
            candidate.excessiveWell = excess * excess;
            const auto target = full ^ (std::uint32_t{1} << well);
            for (int y = map.top[well]; y < roof && candidate.readyWellRows < 4; ++y) {
                if ((map.row[y] & full) != target) break;
                ++candidate.readyWellRows;
            }
        }
        const double surface = terrain_surface_value(candidate, weights);
        if (well == -1 || surface > bestSurface) {
            best = candidate;
            bestSurface = surface;
        }
    }
    return best;
}

// A feature record includes the best channel for the weights passed to
// terrain_features. When changing weights, extract the features again so the
// channel competes under those same weights.
inline double terrain_value(const TerrainFeatures& f, const TerrainWeights& weights = {}) {
    return -16000.0
        - weights.aggregateHeight * f.aggregateHeight
        - weights.maximumHeight * f.maximumHeight
        - weights.shallowCavity * f.shallowCavities
        - weights.buriedCavity * f.buriedCavities
        - weights.extraCover * f.extraCover
        + terrain_surface_value(f, weights);
}

inline double terrain_value(const m_tetris::TetrisMap& map, const TerrainWeights& weights = {}) {
    return terrain_value(terrain_features(map, weights), weights);
}

} // namespace Bot_chatgpt

#endif
