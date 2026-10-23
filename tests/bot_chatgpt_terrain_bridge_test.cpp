#include "../src/bot/chatgpt/terrain_bridge_model.h"
#include <cassert>
#include <array>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <random>
#include <string_view>
#include <vector>

namespace TetrTests::bot_chatgpt_terrain_bridge_test {
namespace B = Bot_chatgpt;

// Fixtures read top to bottom, matching the verified replay report.
m_tetris::TetrisMap board(std::initializer_list<std::string_view> lines) {
    m_tetris::TetrisMap map(10, 40);
    int y = static_cast<int>(lines.size());
    for (const auto line : lines) {
        --y;
        assert(line.size() == 10);
        for (int x = 0; x < 10; ++x) if (line[x] != '.') {
            map.row[y] |= 1u << x;
            map.top[x] = std::max(map.top[x], y + 1);
            map.roof = std::max(map.roof, y + 1);
            ++map.count;
        }
    }
    return map;
}

// Deliberately slow independent oracle: enumerate each hole and its exact
// ceiling rows. Use a directed cell BFS, not the optimized row-mask flood.
int reference_bridge_cavities(const m_tetris::TetrisMap& map) {
    std::array<std::array<bool, 10>, 41> reached{};
    std::vector<std::array<int, 2>> queue;
    for (int x = 0; x < 10; ++x) {
        reached[map.roof][x] = true;
        queue.push_back({x, map.roof});
    }
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const auto [x, y] = queue[i];
        for (const auto [nx, ny] : {std::array<int, 2>{x - 1, y},
                                   std::array<int, 2>{x + 1, y},
                                   std::array<int, 2>{x, y - 1}}) {
            if (nx < 0 || nx >= 10 || ny < 0 || reached[ny][nx]) continue;
            if (ny < map.roof && (map.row[ny] & (1u << nx))) continue;
            reached[ny][nx] = true;
            queue.push_back({nx, ny});
        }
    }
    int count = 0;
    for (int y = 0; y < map.roof; ++y) for (int x = 0; x < 10; ++x) {
        if (map.row[y] & (1u << x)) continue;
        std::vector<int> ceilings;
        for (int above = y + 1; above < map.roof; ++above)
            if (map.row[above] & (1u << x)) ceilings.push_back(above);
        if (ceilings.empty() || ceilings.size() > 2) continue;
        if (reached[y][x] && map.top[x] - y <= 3) continue; // Already shallow in V1.
        bool removable = true;
        for (const int ceiling : ceilings) {
            std::vector<int> gaps;
            for (int gx = 0; gx < 10; ++gx)
                if (!(map.row[ceiling] & (1u << gx))) gaps.push_back(gx);
            if (gaps.empty() || gaps.size() > 2 ||
                gaps.back() - gaps.front() + 1 != static_cast<int>(gaps.size())) {
                removable = false;
                break;
            }
            for (const int gap : gaps) for (int above = ceiling + 1; above < map.roof; ++above) {
                // Higher ceiling rows are the only rows allowed to disappear.
                if (std::find(ceilings.begin(), ceilings.end(), above) != ceilings.end()) continue;
                if (map.row[above] & (1u << gap)) removable = false;
            }
        }
        count += removable;
    }
    return count;
}

void refresh(m_tetris::TetrisMap& map) {
    std::fill(std::begin(map.top), std::end(map.top), 0);
    map.count = map.roof = 0;
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 10; ++x)
        if (map.row[y] & (1u << x)) {
            ++map.count;
            map.roof = y + 1;
            map.top[x] = y + 1;
        }
}

m_tetris::TetrisMap mirrored(const m_tetris::TetrisMap& map) {
    m_tetris::TetrisMap result(10, 40);
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 10; ++x)
        if (map.row[y] & (1u << x)) result.row[y] |= 1u << (9 - x);
    refresh(result);
    return result;
}

void compare_reference(const m_tetris::TetrisMap& map) {
    const int expected = reference_bridge_cavities(map);
    const auto v1 = B::terrain_features(map);
    const auto fast = B::bridge_terrain_features(map);
    const auto reflected = mirrored(map);
    assert(fast.removableBridgeCavities == expected);
    assert(fast.terrain.buriedCavities >= 0 && fast.terrain.shallowCavities >= 0);
    assert(fast.terrain.buriedCavities + fast.terrain.shallowCavities ==
           v1.buriedCavities + v1.shallowCavities);
    assert(B::bridge_terrain_features(reflected).removableBridgeCavities == expected);
    assert(reference_bridge_cavities(reflected) == expected);
    assert(B::bridge_terrain_value(map) == B::bridge_terrain_value(reflected));
    for (const auto weights : {B::TerrainWeights{}, B::TerrainWeights{16, 72, 48, 16, 23, 97}}) {
        const double reference = B::terrain_value(map, weights) + expected *
            (weights.buriedCavity - weights.shallowCavity);
        assert(std::abs(reference - B::bridge_terrain_value(map, weights)) < 1e-9);
    }
}

// Real non-overlapping hard drops from empty air, followed by full-row clears,
// generate internally consistent occupied fields rather than malformed maps.
void random_legal_boards() {
    constexpr std::array<std::array<unsigned, 4>, 19> shapes{{
        {15, 0, 0, 0}, {1, 1, 1, 1}, {3, 3, 0, 0},
        {7, 2, 0, 0}, {2, 3, 2, 0}, {2, 7, 0, 0}, {1, 3, 1, 0},
        {7, 1, 0, 0}, {3, 2, 2, 0}, {4, 7, 0, 0}, {1, 1, 3, 0},
        {7, 4, 0, 0}, {2, 2, 3, 0}, {1, 7, 0, 0}, {3, 1, 1, 0},
        {3, 6, 0, 0}, {2, 3, 1, 0}, {6, 3, 0, 0}, {1, 3, 2, 0}
    }};
    std::mt19937 random(0x20260926u);
    m_tetris::TetrisMap map(10, 40);
    for (int trial = 0; trial < 1000; ++trial) {
        const auto& shape = shapes[random() % shapes.size()];
        int height = 0, width = 0;
        unsigned columns = 0;
        for (int y = 0; y < 4; ++y) if (shape[y]) {
            height = y + 1;
            columns |= shape[y];
        }
        for (; columns; columns >>= 1) ++width;
        const int x = random() % (11 - width);
        const auto fits = [&](int bottom) {
            for (int y = 0; y < height; ++y)
                if (map.row[bottom + y] & (shape[y] << x)) return false;
            return true;
        };
        int bottom = 40 - height;
        if (!fits(bottom)) map = m_tetris::TetrisMap(10, 40);
        while (bottom > 0 && fits(bottom - 1)) --bottom;
        for (int y = 0; y < height; ++y) map.row[bottom + y] |= shape[y] << x;
        int destination = 0;
        for (int y = 0; y < 40; ++y)
            if (map.row[y] != 1023u) map.row[destination++] = map.row[y];
        while (destination < 40) map.row[destination++] = 0;
        refresh(map);
        compare_reference(map);
    }
}

int run(int argc,char** argv) {
    // Official-engine-verified moch vs zzz replay, round 4 after piece 266.
    // The horizontal I seals the centre, but row "jiiiisszz." is missing
    // only its rightmost cell. The next L clears it before Z continues Spin.
    const auto replayBridge = board({"jj...szz..", "jiiiisszz.", "jss...s...",
        "ss...oolll", "ooj..ooloo", "#########."});
    const auto v1 = B::terrain_features(replayBridge);
    const auto v2 = B::bridge_terrain_features(replayBridge);
    compare_reference(replayBridge);
    assert(v2.removableBridgeCavities > 0);
    assert(v2.terrain.buriedCavities < v1.buriedCavities);
    assert(v2.terrain.buriedCavities + v2.terrain.shallowCavities ==
           v1.buriedCavities + v1.shallowCavities);
    assert(v2.terrain.extraCover == v1.extraCover);
    assert(v2.terrain.primaryWellColumn == v1.primaryWellColumn);
    assert(B::bridge_terrain_value(replayBridge) > B::terrain_value(replayBridge));

    // A second verified sequence: round 6 after piece 174. The near-full
    // "tiiiiss.sl" bridge has an unobstructed x=7 gap. T clears it next,
    // followed by an I quad. This is not tied to the first fixture's column.
    const auto secondReplayBridge = board({"........ll", ".........l", "tiiiiss.sl",
        "ttz.sszzss", "tzz.jjjzzs", "###.######", "###.######", "#########.",
        ".#########", "#####.####", "#####.####", "#####.####"});
    compare_reference(secondReplayBridge);
    assert(B::bridge_terrain_features(secondReplayBridge).removableBridgeCavities > 0);
    assert(B::bridge_terrain_value(secondReplayBridge) > B::terrain_value(secondReplayBridge));

    // A sealed cavity under one almost-complete ceiling can be exposed by
    // clearing that ceiling, without requiring a sideways entrance first.
    const auto thin = board({"#########.", "#.#.......", "###......."});
    assert(B::bridge_terrain_features(thin).removableBridgeCavities > 0);
    const auto twoLayers = board({"#########.", "#########.", "#.#.......", "###......."});
    const auto twoLayersFeatures = B::bridge_terrain_features(twoLayers);
    compare_reference(thin);
    compare_reference(twoLayers);
    assert(twoLayersFeatures.removableBridgeCavities > 0);
    assert(twoLayersFeatures.terrain.extraCover == B::terrain_features(twoLayers).extraCover);
    assert(B::bridge_terrain_value(thin) > B::bridge_terrain_value(twoLayers));

    // Three layers are not a cheap bridge, even when every row is near-full.
    const auto thick = board({"#########.", "#########.", "#########.",
        "#.#.......", "###......."});
    compare_reference(thick);
    assert(B::bridge_terrain_features(thick).removableBridgeCavities == 0);
    assert(B::bridge_terrain_value(thick) == B::terrain_value(thick));

    // A brick above the only missing column blocks the vertical approach.
    // This is not an open one-row exit and must not earn the bridge discount.
    const auto blocked = board({".........#", "#########.", "#.#.......", "###......."});
    compare_reference(blocked);
    assert(B::bridge_terrain_features(blocked).removableBridgeCavities == 0);
    assert(B::bridge_terrain_value(blocked) == B::terrain_value(blocked));

    // Sparse floating shelves and separated gaps are not cheap cover rows.
    for (const auto& invalid : {
            board({"#####.....", "#.#.......", "###......."}),
            board({"######.#.#", "#.#.......", "###......."})}) {
        compare_reference(invalid);
        assert(B::bridge_terrain_features(invalid).removableBridgeCavities == 0);
        assert(B::bridge_terrain_value(invalid) == B::terrain_value(invalid));
    }

    // Hole-free terrain and the empty board retain V1 exactly.
    for (const auto& unchanged : {board({".........."}),
            board({"#########.", "#########.", "#########."})}) {
        compare_reference(unchanged);
        assert(B::bridge_terrain_value(unchanged) == B::terrain_value(unchanged));
    }
    random_legal_boards();
    std::cout << "replay V1=" << B::terrain_value(replayBridge)
              << " V2=" << B::bridge_terrain_value(replayBridge)
              << " bridge_cells=" << v2.removableBridgeCavities
              << " buried=" << v1.buriedCavities << "->" << v2.terrain.buriedCavities
              << '\n';
    return 0;
}
} // namespace TetrTests::bot_chatgpt_terrain_bridge_test
