#include "../src/bot/chatgpt/terrain_model.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iostream>

namespace TetrTests::bot_chatgpt_terrain_test {
namespace B = Bot_chatgpt;

m_tetris::TetrisMap rows(std::initializer_list<unsigned> values) {
    m_tetris::TetrisMap map(10, 40);
    int y = 0;
    for (unsigned row : values) {
        map.row[y] = row;
        for (int x = 0; x < 10; ++x) if (row & (1u << x)) {
            map.top[x] = y + 1;
            ++map.count;
            map.roof = y + 1;
        }
        ++y;
    }
    return map;
}

m_tetris::TetrisMap skyline(std::initializer_list<int> heights) {
    m_tetris::TetrisMap map(10, 40);
    int x = 0;
    for (int height : heights) {
        assert(x < 10 && height >= 0 && height <= 40);
        map.top[x] = height;
        map.roof = std::max(map.roof, height);
        map.count += height;
        for (int y = 0; y < height; ++y) map.row[y] |= 1u << x;
        ++x;
    }
    assert(x == 10);
    return map;
}

m_tetris::TetrisMap mirror(const m_tetris::TetrisMap& source) {
    m_tetris::TetrisMap map(10, 40);
    map.roof = source.roof;
    map.count = source.count;
    for (int x = 0; x < 10; ++x) {
        map.top[9 - x] = source.top[x];
        for (int y = 0; y < 40; ++y)
            if (source.row[y] & (1u << x)) map.row[y] |= 1u << (9 - x);
    }
    return map;
}

int run(int argc,char** argv) {
    // Same one-cell ceiling and hole count. A cell under a ledge opening into
    // empty space can be repaired from the side; one surrounded by solid
    // neighbours first needs its covering row removed. The second fixture
    // seals the opening without changing the column containing the cavity.
    const auto ledge = rows({0b0000000111, 0b0000000001, 0b0000000010});
    const auto sealed = rows({0b0000000111, 0b0000000101, 0b0000000010});
    const auto openFeatures = B::terrain_features(ledge);
    const auto sealedFeatures = B::terrain_features(sealed);
    assert(openFeatures.shallowCavities > 0);
    assert(sealedFeatures.buriedCavities > openFeatures.buriedCavities);
    assert(B::terrain_value(ledge) > B::terrain_value(sealed));

    // Burying an already sealed cavity further cannot make it cheap again.
    const auto deeplySealed = rows({0b0000000111, 0b0000000101,
        0b0000000010, 0b0000000010, 0b0000000010});
    assert(B::terrain_features(deeplySealed).extraCover > sealedFeatures.extraCover);
    assert(B::terrain_value(sealed) > B::terrain_value(deeplySealed));

    // One clean four-row I channel vs two separate four-row trenches. Even
    // with four fewer cells, two trenches are worse: there is no four-line exit.
    const auto oneWell = skyline({4, 4, 4, 4, 4, 4, 4, 4, 4, 0});
    const auto twoWells = skyline({4, 4, 0, 4, 4, 4, 4, 4, 0, 4});
    const auto single = B::terrain_features(oneWell);
    const auto doubleWell = B::terrain_features(twoWells);
    assert(single.primaryWellDepth == 4 && single.readyWellRows == 4);
    assert(single.secondaryWells == 0);
    assert(doubleWell.secondaryWells > 0 && doubleWell.readyWellRows == 0);
    assert(B::terrain_value(oneWell) > B::terrain_value(twoWells));

    // Reward is bounded: an eight-row channel cannot claim two I pieces of
    // speculative damage, and tall open terrain still has a safety cost.
    const auto deepWell = skyline({8, 8, 8, 8, 8, 8, 8, 8, 8, 0});
    assert(B::terrain_features(deepWell).readyWellRows == single.readyWellRows);
    assert(B::terrain_value(oneWell) > B::terrain_value(deepWell));
    const auto empty = skyline({0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    assert(std::isfinite(B::terrain_value(empty)));
    assert(B::terrain_value(empty) > B::terrain_value(oneWell));
    const auto high = skyline({40, 40, 40, 40, 40, 40, 40, 40, 40, 0});
    assert(std::isfinite(B::terrain_value(high)));
    assert(B::terrain_value(deepWell) > B::terrain_value(high));

    // Geometry is symmetric even if equal channels choose the first column
    // for diagnostics. No side gets an implicit strategic advantage.
    for (const auto& map : {empty, oneWell, twoWells, deepWell, high, ledge, sealed, deeplySealed}) {
        assert(B::terrain_value(map) == B::terrain_value(mirror(map)));
        const auto features = B::terrain_features(map);
        assert(B::terrain_value(features) == B::terrain_value(map));
    }
    std::cout << "terrain geometry: shallow repair, sealed cover, single channel, symmetry and height passed\n";
    return 0;
}
} // namespace TetrTests::bot_chatgpt_terrain_test
