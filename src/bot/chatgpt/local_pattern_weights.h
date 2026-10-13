#ifndef TETR_BOT_CHATGPT_LOCAL_PATTERN_WEIGHTS_H
#define TETR_BOT_CHATGPT_LOCAL_PATTERN_WEIGHTS_H

#include "local_pattern_model.h"

namespace Bot_chatgpt {

// Exact frozen export: runlogs/chatgpt_moch_20260926/pattern_pilot_fit/model.json
// SHA256: 67a7c9f4daf4d947e4d8d4f87e6cd9efe4f14497f5bfc7540a719d9015a06241
// Empty-centre 3x3 bits: bottom 0,1,2 / middle 3,.,4 / top 5,6,7.
// Class order is ascending horizontal-mirror canonical code. Raw table scores
// already include the model's 560 multiplier; there is no normalization,
// extra intercept, or thirteen-feature correction here.
inline constexpr char local_pattern_model_sha256[] = "67a7c9f4daf4d947e4d8d4f87e6cd9efe4f14497f5bfc7540a719d9015a06241";
inline constexpr LocalPatternWeights fitted_local_pattern_weights{{0.0, -35.87497547670945, -80.0, 80.0, -0.0, 80.0, 80.0, -70.57308508913435, -0.0, -0.0, 0.0, -35.93869363399765, -0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, 0.0, 80.0, 11.40672438216136, -0.0, 0.0, 0.0, 41.64205726960696, 0.0, 18.77111262236432, 0.0, -0.0, -0.0, -36.3415118466208, -0.0, -0.479372198836799, -0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -80.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, 17.495561382342146, 0.0, -0.0, 0.0, -0.0, -0.0, 0.0, 0.0, 1.9637653182748178, 80.0, 0.0, 80.0, -0.0, 0.0, -0.0, 67.63214560602061, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -61.880995173929264, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -80.0}};
inline constexpr std::array<std::uint8_t, 144> fitted_local_pattern_codes{{0, 1, 2, 3, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 29, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 69, 71, 72, 73, 74, 75, 76, 77, 78, 79, 88, 89, 90, 91, 93, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 160, 161, 162, 163, 165, 167, 168, 169, 170, 171, 172, 173, 174, 175, 184, 185, 186, 187, 189, 191, 224, 225, 226, 227, 229, 231, 232, 233, 234, 235, 236, 237, 238, 239, 248, 249, 250, 251, 253, 255}};
constexpr bool fitted_local_pattern_encoding_matches() {
    for (std::size_t i = 0; i < fitted_local_pattern_codes.size(); ++i)
        if (fitted_local_pattern_codes[i] != local_pattern_encoding.canonicalCodes[i]) return false;
    return true;
}
static_assert(fitted_local_pattern_encoding_matches(), "Fitted local pattern class order changed");
static_assert(fitted_local_pattern_weights[0] == 0.0, "Empty-neighbour class must stay zero");

inline double local_pattern_correction(const m_tetris::TetrisMap& map) {
    return local_pattern_value(map, fitted_local_pattern_weights);
}

} // namespace Bot_chatgpt
#endif
