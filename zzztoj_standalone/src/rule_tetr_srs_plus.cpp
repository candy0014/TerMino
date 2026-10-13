#include "rule_tetr_srs_plus.h"

#include "rule_srs.h"

#include <array>
#include <cstddef>

namespace rule_tetr_srs_plus {
namespace {

using Kick = std::array<int, 2>;
using KickSet = std::array<Kick, 6>;
using KickTable = std::array<KickSet, 12>;

// Exact kick order from parent tetr's PieceCatalogData kick table 0.
constexpr KickTable kJlstzKicks{{
    {{{0,0},{-1,0},{-1,1},{0,-2},{-1,-2},{0,0}}},
    {{{0,0},{1,0},{1,-1},{0,2},{1,2},{0,0}}},
    {{{0,0},{1,0},{1,-1},{0,2},{1,2},{0,0}}},
    {{{0,0},{-1,0},{-1,1},{0,-2},{-1,-2},{0,0}}},
    {{{0,0},{1,0},{1,1},{0,-2},{1,-2},{0,0}}},
    {{{0,0},{-1,0},{-1,-1},{0,2},{-1,2},{0,0}}},
    {{{0,0},{-1,0},{-1,-1},{0,2},{-1,2},{0,0}}},
    {{{0,0},{1,0},{1,1},{0,-2},{1,-2},{0,0}}},
    {{{0,0},{0,1},{1,1},{-1,1},{1,0},{-1,0}}},
    {{{0,0},{0,-1},{-1,-1},{1,-1},{-1,0},{1,0}}},
    {{{0,0},{1,0},{1,2},{1,1},{0,2},{0,1}}},
    {{{0,0},{-1,0},{-1,2},{-1,1},{0,2},{0,1}}}
}};

// Exact kick order from parent tetr's PieceCatalogData kick table 1.
constexpr KickTable kIKicks{{
    {{{0,0},{1,0},{-2,0},{-2,-1},{1,2},{0,0}}},
    {{{0,0},{-1,0},{2,0},{-1,-2},{2,1},{0,0}}},
    {{{0,0},{-1,0},{2,0},{-1,2},{2,-1},{0,0}}},
    {{{0,0},{-2,0},{1,0},{-2,1},{1,-2},{0,0}}},
    {{{0,0},{2,0},{-1,0},{2,1},{-1,-2},{0,0}}},
    {{{0,0},{1,0},{-2,0},{1,2},{-2,-1},{0,0}}},
    {{{0,0},{1,0},{-2,0},{2,-1},{-2,1},{0,0}}},
    {{{0,0},{-1,0},{2,0},{2,-1},{-1,2},{0,0}}},
    {{{0,0},{0,1},{1,1},{-1,1},{1,0},{-1,0}}},
    {{{0,0},{0,-1},{-1,-1},{1,-1},{-1,0},{1,0}}},
    {{{0,0},{1,0},{1,2},{1,1},{0,2},{0,1}}},
    {{{0,0},{-1,0},{-1,2},{-1,1},{0,2},{0,1}}}
}};

constexpr std::array<int, 4> kClockwiseTransition{{0, 2, 4, 6}};
constexpr std::array<int, 4> kCounterclockwiseTransition{{7, 1, 3, 5}};
constexpr std::array<int, 4> kOppositeTransition{{8, 10, 9, 11}};

void assign_kicks(m_tetris::TetrisWallKickOpertion& destination,
                  const KickSet& source, bool opposite) {
    const std::size_t count = opposite ? 6 : 5;
    destination.length = static_cast<std::uint32_t>(count - 1);
    for (std::size_t index = 1; index < count; ++index) {
        destination.data[index - 1].x = static_cast<std::int8_t>(source[index][0]);
        destination.data[index - 1].y = static_cast<std::int8_t>(source[index][1]);
    }
}

void set_opposite_rotation(m_tetris::TetrisOpertion& operation, int rotation) {
    using namespace m_tetris_rule_tools;
    switch (rotation) {
        case 0: operation.rotate_opposite = rotate_template<2>; break;
        case 1: operation.rotate_opposite = rotate_template<3>; break;
        case 2: operation.rotate_opposite = rotate_template<0>; break;
        case 3: operation.rotate_opposite = rotate_template<1>; break;
        default: operation.rotate_opposite = nullptr; break;
    }
}

std::array<m_tetris::TetrisOpertion, 4> make_o_operations() {
    using namespace m_tetris_rule_tools;
    constexpr std::uint32_t empty = 0;
    constexpr std::uint32_t occupied = 0b0110;
    return {{
        {create_node<'O', 0, 0, 0, occupied, occupied, empty, empty>,
         rotate_template<1>, rotate_template<3>, rotate_template<2>, {}, {}, {}},
        {create_node<'O', 0, 0, 1, occupied, occupied, empty, empty>,
         rotate_template<2>, rotate_template<0>, rotate_template<3>, {}, {}, {}},
        {create_node<'O', 0, 0, 2, occupied, occupied, empty, empty>,
         rotate_template<3>, rotate_template<1>, rotate_template<0>, {}, {}, {}},
        {create_node<'O', 0, 0, 3, occupied, occupied, empty, empty>,
         rotate_template<0>, rotate_template<2>, rotate_template<1>, {}, {}, {}}
    }};
}

template<char Piece>
m_tetris::TetrisBlockStatus generate(const m_tetris::TetrisContext*) {
    // Parent tetr spawns at pose {row=-2,col=4}. With the standalone's
    // bottom-origin conversion that is status {x=3,y=22,r=0} for all seven
    // standard tetrominoes, including O's invariant geometry.
    return m_tetris::TetrisBlockStatus(Piece, 3, 22, 0);
}

} // namespace

bool TetrisRule::init(int width, int height) {
    return width == 10 && height == 40;
}

std::map<std::pair<char, std::uint8_t>, m_tetris::TetrisOpertion>
TetrisRule::get_opertion() {
    auto operations = rule_srs::TetrisRule::get_opertion();
    const auto oOperations = make_o_operations();
    for (int rotation = 0; rotation < 4; ++rotation) {
        operations[{'O', static_cast<std::uint8_t>(rotation)}] = oOperations[rotation];
    }
    for (auto& entry : operations) {
        const char piece = entry.first.first;
        const int rotation = entry.first.second;
        auto& operation = entry.second;

        const KickTable& kicks = piece == 'I' ? kIKicks : kJlstzKicks;
        assign_kicks(operation.wall_kick_clockwise,
                     kicks[kClockwiseTransition[rotation]], false);
        assign_kicks(operation.wall_kick_counterclockwise,
                     kicks[kCounterclockwiseTransition[rotation]], false);
        if (piece != 'O') set_opposite_rotation(operation, rotation);
        assign_kicks(operation.wall_kick_opposite,
                     kicks[kOppositeTransition[rotation]], true);
    }
    return operations;
}

std::map<char, m_tetris::TetrisBlockStatus(*)(const m_tetris::TetrisContext*)>
TetrisRule::get_generate() {
    return {
        {'O', &generate<'O'>},
        {'I', &generate<'I'>},
        {'S', &generate<'S'>},
        {'Z', &generate<'Z'>},
        {'L', &generate<'L'>},
        {'J', &generate<'J'>},
        {'T', &generate<'T'>}
    };
}

} // namespace rule_tetr_srs_plus
