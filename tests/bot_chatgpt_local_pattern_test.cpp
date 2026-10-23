#include "../src/bot/chatgpt/local_pattern_model.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

namespace TetrTests::bot_chatgpt_local_pattern_test {
namespace B = Bot_chatgpt;

constexpr std::array<int, 8> dx{{-1, 0, 1, -1, 1, -1, 0, 1}};
constexpr std::array<int, 8> dy{{-1, -1, -1, 0, 0, 1, 1, 1}};

static unsigned slow_mirror(unsigned code) {
    unsigned result = 0;
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 8; ++j)
        if (dx[i] == -dx[j] && dy[i] == dy[j] && (code & (1u << i))) result |= 1u << j;
    return result;
}

static std::vector<unsigned> canonical_codes() {
    std::vector<unsigned> codes;
    for (unsigned code = 0; code < 256; ++code)
        if (code <= slow_mirror(code)) codes.push_back(code);
    return codes;
}

static B::LocalPatternCounts reference(const m_tetris::TetrisMap& map) {
    const auto codes = canonical_codes();
    B::LocalPatternCounts result{};
    const auto occupied = [&](int x, int y) {
        return x < 0 || x >= map.width || y < 0 || y >= map.height ||
            bool(map.row[y] & (1u << x));
    };
    for (int x = 0; x < map.width; ++x)
        for (int y = 0; y < map.height; ++y) {
            if (y < map.top[x] - 3 || y > map.top[x] + 1 || occupied(x, y)) continue;
            unsigned code = 0;
            for (int i = 0; i < 8; ++i)
                if (occupied(x + dx[i], y + dy[i])) code |= 1u << i;
            code = std::min(code, slow_mirror(code));
            if (!code) continue;
            const auto found = std::find(codes.begin(), codes.end(), code);
            assert(found != codes.end());
            ++result[std::distance(codes.begin(), found)];
        }
    return result;
}

static void refresh(m_tetris::TetrisMap& map) {
    std::fill(std::begin(map.top), std::end(map.top), 0);
    map.roof = map.count = 0;
    for (int y = 0; y < map.height; ++y) for (int x = 0; x < map.width; ++x)
        if (map.row[y] & (1u << x)) {
            map.top[x] = y + 1;
            map.roof = y + 1;
            ++map.count;
        }
}

static m_tetris::TetrisMap rows(std::initializer_list<unsigned> values, int height = 40) {
    m_tetris::TetrisMap map(10, height);
    std::copy(values.begin(), values.end(), map.row);
    refresh(map);
    return map;
}

static void check(const m_tetris::TetrisMap& map) {
    const auto fast = B::local_pattern_counts(map);
    assert(fast == reference(map) && fast[0] == 0);
    const int samples = std::accumulate(fast.begin(), fast.end(), 0);
    assert(samples <= map.width * 5);
    B::LocalPatternWeights weights{};
    for (std::size_t i = 0; i < weights.size(); ++i) weights[i] = double(int(i % 17) - 8) * 0.125;
    weights[0] = 1e9; // The full-empty class must be ignored unconditionally.
    double expected = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) expected += weights[i] * fast[i];
    assert(B::local_pattern_value(map, weights) == expected);
    m_tetris::TetrisMap mirror(map.width, map.height);
    for (int x = 0; x < map.width; ++x) for (int y = 0; y < map.height; ++y)
        if (map.row[y] & (1u << x)) mirror.row[y] |= 1u << (map.width - 1 - x);
    refresh(mirror);
    assert(B::local_pattern_counts(mirror) == fast);
    assert(B::local_pattern_value(mirror, weights) == expected);
}

int run(int argc,char** argv) {
    const auto codes = canonical_codes();
    assert(codes.size() == 144);
    for (unsigned code = 0; code < 256; ++code) {
        assert(B::local_pattern_mirror(code) == slow_mirror(code));
        const auto id = B::local_pattern_encoding.classOf[code];
        assert(id < codes.size());
        assert(codes[id] == std::min(code, slow_mirror(code)));
        assert(B::local_pattern_encoding.canonicalCodes[id] == codes[id]);
    }
    const auto empty = rows({});
    check(empty);
    // Empty boards still have wall/floor patterns; only all-empty windows vanish.
    const auto emptyCounts = B::local_pattern_counts(empty);
    assert(std::accumulate(emptyCounts.begin(), emptyCounts.end(), 0) > 0);
    check(rows({}, 1)); // Both top and bottom neighbours are out of bounds.
    check(rows({511, 511, 511, 511})); // One open I channel.
    check(rows({7, 1, 2})); // A shallow overhang.
    check(rows({511, 5, 7}));
    check(rows({511, 511, 0, 3}, 4)); // Empty centres directly below the solid top boundary.
    const auto solid = rows({1023, 1023, 1023, 1023}, 4);
    assert(B::local_pattern_counts(solid) == B::LocalPatternCounts{});

    // Fifty deterministic legal hard drops with row clearing. These validate
    // occupied geometry and extraction, not SRS movement or Spin reachability.
    constexpr std::array<std::array<unsigned, 4>, 8> shapes{{
        {15, 0, 0, 0}, {1, 1, 1, 1}, {3, 3, 0, 0}, {7, 2, 0, 0},
        {7, 1, 0, 0}, {7, 4, 0, 0}, {3, 6, 0, 0}, {6, 3, 0, 0}}};
    auto map = empty;
    unsigned random = 0x20260926u;
    for (int trial = 0; trial < 50; ++trial) {
        random = random * 1664525u + 1013904223u;
        const auto& shape = shapes[(random >> 16) % shapes.size()];
        unsigned columns = 0;
        int height = 0, width = 0;
        for (int y = 0; y < 4; ++y) if (shape[y]) { height = y + 1; columns |= shape[y]; }
        for (; columns; columns >>= 1) ++width;
        const int x = random % (11 - width);
        const auto fits = [&](int bottom) {
            for (int y = 0; y < height; ++y)
                if (map.row[bottom + y] & (shape[y] << x)) return false;
            return true;
        };
        int bottom = 40 - height;
        if (!fits(bottom)) map = empty;
        while (bottom && fits(bottom - 1)) --bottom;
        for (int y = 0; y < height; ++y) map.row[bottom + y] |= shape[y] << x;
        int target = 0;
        for (int y = 0; y < 40; ++y) if (map.row[y] != 1023) map.row[target++] = map.row[y];
        while (target < 40) map.row[target++] = 0;
        refresh(map);
        check(map);
    }
    std::cout << "144-class local patterns: encoding, reference, boundaries, symmetry and lookup passed\n";
    return 0;
}
} // namespace TetrTests::bot_chatgpt_local_pattern_test
