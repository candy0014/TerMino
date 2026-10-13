#include "rule_tetr_srs_plus.h"

#include "piece_catalog_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void verify_kicks(const m_tetris::TetrisWallKickOpertion& actual,
                  const std::vector<std::array<int, 2>>& expected) {
    require(!expected.empty(), "parent kick list is empty");
    require(actual.length + 1 == expected.size(), "kick count differs from tetr SRS+");
    require(expected.front()[0] == 0 && expected.front()[1] == 0,
            "parent base kick is not zero");
    for (std::size_t index = 1; index < expected.size(); ++index) {
        require(actual.data[index - 1].x == expected[index][0],
                "kick x/order differs from tetr SRS+");
        require(actual.data[index - 1].y == expected[index][1],
                "kick y/order differs from tetr SRS+");
    }
}

} // namespace

int main() {
    constexpr std::array<char, 7> pieces{{'I', 'J', 'L', 'O', 'S', 'T', 'Z'}};
    constexpr std::array<int, 4> clockwise{{0, 2, 4, 6}};
    constexpr std::array<int, 4> counterclockwise{{7, 1, 3, 5}};
    constexpr std::array<int, 4> opposite{{8, 10, 9, 11}};

    const auto operations = rule_tetr_srs_plus::TetrisRule::get_opertion();
    const auto generators = rule_tetr_srs_plus::TetrisRule::get_generate();
    const auto& parentKicks = PieceCatalogData::kick_tables();
    require(operations.size() == 28, "SRS+ must expose four states for seven pieces");
    require(generators.size() == 7, "SRS+ must expose seven spawn generators");

    for (const char piece : pieces) {
        const auto generator = generators.find(piece);
        require(generator != generators.end(), "piece generator missing");
        const auto spawn = generator->second(nullptr);
        require(spawn.t == piece && spawn.x == 3 && spawn.y == 22 && spawn.r == 0,
                "spawn does not map to tetr pose {-2,4,0}");

        const int kickTable = piece == 'I' ? 1 : 0;
        for (int rotation = 0; rotation < 4; ++rotation) {
            const auto found = operations.find({piece, static_cast<std::uint8_t>(rotation)});
            require(found != operations.end(), "piece rotation state missing");
            const auto& operation = found->second;
            require(operation.rotate_clockwise != nullptr, "clockwise rotation missing");
            require(operation.rotate_counterclockwise != nullptr,
                    "counterclockwise rotation missing");
            require(operation.rotate_opposite != nullptr, "180-degree rotation missing");
            verify_kicks(operation.wall_kick_clockwise,
                         parentKicks[kickTable][clockwise[rotation]]);
            verify_kicks(operation.wall_kick_counterclockwise,
                         parentKicks[kickTable][counterclockwise[rotation]]);
            verify_kicks(operation.wall_kick_opposite,
                         parentKicks[kickTable][opposite[rotation]]);
        }
    }

    // Parent tetr's O rotations change only the rotation index. Confirm the
    // four upstream nodes also retain identical occupied geometry.
    const auto baseOperation = operations.at({'O', 0});
    const auto base = baseOperation.create(10, 40, baseOperation);
    for (int rotation = 1; rotation < 4; ++rotation) {
        const auto operation = operations.at({'O', static_cast<std::uint8_t>(rotation)});
        const auto node = operation.create(10, 40, operation);
        require(node.row == base.row && node.height == base.height &&
                    node.col == base.col && node.width == base.width,
                "O bounding geometry differs by rotation");
        for (int row = 0; row < 4; ++row) {
            require(node.data[row] == base.data[row], "O occupied cells differ by rotation");
        }
    }

    std::cout << "matched all 28 tetr SRS+ states and 84 kick transitions\n";
    return 0;
}
