#ifndef TETR_BOT_CHATGPT_LEARNED_TERRAIN_MODEL_H
#define TETR_BOT_CHATGPT_LEARNED_TERRAIN_MODEL_H

#include "terrain_bridge_model.h"
#include <array>

namespace Bot_chatgpt {

// Frozen export: runlogs/chatgpt_moch_20260926/imitation_pilot_fit/model.json
// SHA256: 2c3f79e0e36f8e6c8d43158cca2778bbaf69ca677245be2a805770abb11b38c8
// Conditional selected-family imitation; these parameters are not a win-rate claim.
using LearnedTerrainVector = std::array<double, 13>;
inline constexpr char learned_terrain_model_sha256[] = "2c3f79e0e36f8e6c8d43158cca2778bbaf69ca677245be2a805770abb11b38c8";
inline constexpr double learned_terrain_score_unit = 560.0;
inline constexpr LearnedTerrainVector learned_terrain_mean{{66.61547747705872, 9.125775049200604, 1.6434642575067473, 4.740048686677956, 11.330848613531607, 6.832283433903717, 2.846718943962737, 0.8719124341219399, 0.7711444532116905, 0.9846627830962432, 2.0427948289699276, 60.23196453287391, 0.32997848692639736}};
inline constexpr LearnedTerrainVector learned_terrain_std{{34.80492143227305, 3.697035617170313, 1.5339118113816426, 3.94120088967862, 14.157543549542527, 3.0017847483625806, 5.112496181465433, 1.5273263253727625, 4.8015986115756, 1.3928324080620607, 1.8270973361471625, 31.568657735874837, 1.2275290489094752}};
inline constexpr LearnedTerrainVector learned_terrain_theta{{0.08453418869838922, 0.07014393234377667, 0.5, -0.10437414790267853, -0.0, -0.0021903311603271736, 0.04634164344329239, -0.19241575645153186, 0.0, 0.0, -0.0, 0.0, 0.0}};

// Explicitly freeze feature extraction independently of learned coefficients.
inline constexpr TerrainWeights learned_terrain_selector_weights{
    16, 72, 48, 16, 140, 620, 80, 80, 45, 70};

inline LearnedTerrainVector learned_terrain_pack(const TerrainFeatures& f,
                                                int occupied, int bridge = 0) {
    return {{double(f.aggregateHeight), double(f.maximumHeight),
        double(f.shallowCavities), double(f.buriedCavities), double(f.extraCover),
        double(f.roughness), double(f.steepness), double(f.secondaryWells),
        double(f.excessiveWell), double(f.readyWellRows), double(f.primaryWellDepth),
        double(occupied), double(bridge)}};
}

// Complete exported 13-vector for independent model checking. The first 12
// entries are V1 features, not bridge-reclassified cavity counts.
inline LearnedTerrainVector learned_terrain_features(const m_tetris::TetrisMap& map) {
    const auto f = terrain_features(map, learned_terrain_selector_weights);
    const int bridge = bridge_terrain_features(map, learned_terrain_selector_weights)
        .removableBridgeCavities;
    return learned_terrain_pack(f, map.count, bridge);
}

inline double learned_terrain_correction(const LearnedTerrainVector& features) {
    double score = 0;
    for (std::size_t i = 0; i < features.size(); ++i)
        score += learned_terrain_theta[i] *
            ((features[i] - learned_terrain_mean[i]) / learned_terrain_std[i]);
    return learned_terrain_score_unit * score;
}

inline double learned_terrain_correction(const m_tetris::TetrisMap& map) {
    // A zero bridge coefficient makes its value (and normalization constant)
    // irrelevant. Avoid the second scan without changing the 13-term formula.
    static_assert(learned_terrain_theta[12] == 0.0, "Bridge scan may only be omitted for zero weight");
    return learned_terrain_correction(learned_terrain_pack(
        terrain_features(map, learned_terrain_selector_weights), map.count));
}

} // namespace Bot_chatgpt
#endif
