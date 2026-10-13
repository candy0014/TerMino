#ifndef TETR_BOT_CHATGPT_LOCAL_PATTERN_MODEL_H
#define TETR_BOT_CHATGPT_LOCAL_PATTERN_MODEL_H

#include "tetris_core.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace Bot_chatgpt {

// Fixed geometry features, with no learned weights or production selection.
// Coordinates increase upward. An empty-centre 3x3 window is encoded as:
//   5 6 7
//   3 . 4
//   0 1 2
// Walls and cells below or above the map are occupied. These patterns do not
// establish a legal tetromino entry, rotation, clear, or Spin.
inline constexpr int local_pattern_class_count = 144;
using LocalPatternCounts = std::array<int, local_pattern_class_count>;
using LocalPatternWeights = std::array<double, local_pattern_class_count>;

constexpr unsigned local_pattern_mirror(unsigned code) {
    constexpr std::array<int, 8> target{{2, 1, 0, 4, 3, 7, 6, 5}};
    unsigned result = 0;
    for (int bit = 0; bit < 8; ++bit) result |= ((code >> bit) & 1u) << target[bit];
    return result;
}

struct LocalPatternEncoding {
    std::array<std::uint8_t, 256> classOf{};
    std::array<std::uint8_t, local_pattern_class_count> canonicalCodes{};
    int count = 0;
};

constexpr LocalPatternEncoding make_local_pattern_encoding() {
    LocalPatternEncoding result;
    for (unsigned code = 0; code < 256; ++code) {
        if (code <= local_pattern_mirror(code)) {
            result.classOf[code] = static_cast<std::uint8_t>(result.count);
            result.canonicalCodes[result.count++] = static_cast<std::uint8_t>(code);
        }
    }
    for (unsigned code = 0; code < 256; ++code)
        result.classOf[code] = result.classOf[std::min(code, local_pattern_mirror(code))];
    return result;
}

inline constexpr auto local_pattern_encoding = make_local_pattern_encoding();
static_assert(local_pattern_encoding.count == local_pattern_class_count);
static_assert(local_pattern_encoding.classOf[0] == 0);

template<class Consume>
inline void visit_local_patterns(const m_tetris::TetrisMap& map, Consume&& consume) {
    const int width = std::clamp(map.width, 1, 32);
    const int height = std::clamp(map.height, 1, m_tetris::max_height);
    const std::uint64_t full = (std::uint64_t{1} << (width + 2)) - 1;
    const std::uint64_t walls = 1 | (std::uint64_t{1} << (width + 1));
    const std::uint32_t mask = width == 32 ? ~std::uint32_t{0}
        : (std::uint32_t{1} << width) - 1;
    std::array<std::uint64_t, m_tetris::max_height + 2> padded{};
    padded[0] = padded[height + 1] = full;
    // Valid TetrisMap tops never exceed roof. The highest visited centre is
    // roof+1, so its neighbours cannot read rows above roof+2. Keep summation
    // order and the physical ceiling sentinel unchanged.
    const int lastPaddedRow = std::min(height - 1, map.roof + 2);
    for (int y = 0; y <= lastPaddedRow; ++y)
        padded[y + 1] = (std::uint64_t(map.row[y] & mask) << 1) | walls;
    for (int x = 0; x < width; ++x) {
        const int begin = std::max(0, map.top[x] - 3);
        const int end = std::min(height - 1, map.top[x] + 1);
        for (int y = begin; y <= end; ++y) {
            if (map.row[y] & (std::uint32_t{1} << x)) continue;
            const unsigned square = unsigned((padded[y] >> x) & 7) |
                (unsigned((padded[y + 1] >> x) & 7) << 3) |
                (unsigned((padded[y + 2] >> x) & 7) << 6);
            const unsigned code = (square & 15u) | ((square >> 1) & 240u);
            // All-empty code/class zero is fixed at zero, even if a caller
            // supplies a nonzero table entry. Counts also omit this class.
            if (code) consume(local_pattern_encoding.classOf[code]);
        }
    }
}

inline LocalPatternCounts local_pattern_counts(const m_tetris::TetrisMap& map) {
    LocalPatternCounts result{};
    visit_local_patterns(map, [&](unsigned id) { ++result[id]; });
    return result;
}

// Raw original-score units: one table read per eligible window, with no
// histogram construction, normalization, queue inputs, or search of moves.
inline double local_pattern_value(const m_tetris::TetrisMap& map,
                                  const LocalPatternWeights& weights) {
    double result = 0;
    visit_local_patterns(map, [&](unsigned id) { result += weights[id]; });
    return result;
}

} // namespace Bot_chatgpt
#endif
