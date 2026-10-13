#include "tetr/rules/spin.h"

#include <algorithm>

namespace Rules {
namespace {
bool blocked(const SpinInput& in, const OccupiedCell& occupied, int row, int col) {
    return row >= in.mapHeight || col < 0 || col >= in.mapWidth || occupied(row, col);
}

SpinResult t_corners(const SpinInput& in, const OccupiedCell& occupied) {
    static constexpr int corners[4][2] = {{-1,-1},{-1,1},{1,1},{1,-1}};
    const bool ars = in.spinChecker == Piece::SpinCheck::ArsTCorner;
    const int r = in.pose.rotation.index();
    const int rowOffset = ars && r != 2 ? -1 : 0;
    const int front = ars ? (r + 2) % 4 : r;
    int count = 0, frontCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (blocked(in, occupied, in.pose.position.row + corners[i][0] + rowOffset,
                    in.pose.position.col + corners[i][1])) {
            ++count;
            if (i == front || i == (front + 1) % 4) ++frontCount;
        }
    }
    if (count < 3) return {};
    const bool full = !in.noHoldReverse &&
        (frontCount == 2 || (!ars && in.lastRotation.kick().index() == 4));
    return {true, !full};
}

// HANDHELD uses three occupied check points for J/L/S/Z as well as T.
// Coordinates are (row, col), in the canonical SRS orientation.
using Corners = std::array<Piece::Offset, 4>;
static constexpr std::array<Corners, 4> jCorners = {{
    {{{-1,0},{-1,1},{1,1},{1,-1}}}, {{{-1,-1},{0,1},{1,1},{1,-1}}},
    {{{-1,-1},{-1,1},{1,0},{1,-1}}}, {{{-1,-1},{-1,1},{1,1},{0,-1}}}
}};
static constexpr std::array<Corners, 4> lCorners = {{
    {{{-1,-1},{-1,0},{1,1},{1,-1}}}, {{{-1,-1},{-1,1},{0,1},{1,-1}}},
    {{{-1,-1},{-1,1},{1,1},{1,0}}}, {{{0,-1},{-1,1},{1,1},{1,-1}}}
}};
static constexpr std::array<Corners, 4> sCorners = {{
    {{{-1,-1},{-1,2},{0,1},{0,-2}}}, {{{-2,0},{-1,1},{2,1},{1,0}}},
    {{{0,-1},{0,2},{1,1},{1,-2}}}, {{{-2,-1},{-1,0},{1,-1},{2,0}}}
}};
static constexpr std::array<Corners, 4> zCorners = {{
    {{{-1,-2},{-1,1},{0,2},{0,-1}}}, {{{-1,0},{-2,1},{2,0},{1,1}}},
    {{{0,-2},{0,1},{1,2},{1,-1}}}, {{{-1,-1},{-2,0},{1,0},{2,-1}}}
}};

bool handheld_corners(const SpinInput& in, const OccupiedCell& occupied) {
    int family;
    const std::array<Corners,4>* checks;
    switch (in.spinChecker) {
        case Piece::SpinCheck::J: family = 1; checks = &jCorners; break;
        case Piece::SpinCheck::L: family = 2; checks = &lCorners; break;
        case Piece::SpinCheck::S: family = 4; checks = &sCorners; break;
        case Piece::SpinCheck::Z: family = 6; checks = &zCorners; break;
        default: return false;
    }
    int rotation = in.pose.rotation.index();
    Piece::Offset shift{0,0};
    if (in.rotationSystem == Runtime::RotationSystem::Ars && !in.canonicalGeometry) {
        // Align the actual ARS shape with SRS rather than assume identical
        // orientation numbers or origins across rotation systems.
        const auto& actual = Piece::catalog_entry(Piece::Type::from_index(14 + family)).shapes[rotation];
        const auto& canonical = Piece::catalog_entry(Piece::Type::from_index(family)).shapes;
        bool matched = false;
        for (int r = 0; r < 4 && !matched; ++r) {
            for (const auto& cell : canonical[r]) {
                const Piece::Offset delta{actual.front()[0] - cell[0], actual.front()[1] - cell[1]};
                if (std::all_of(canonical[r].begin(), canonical[r].end(), [&](const auto& p) {
                    return std::find(actual.begin(), actual.end(),
                        Piece::Offset{p[0] + delta[0], p[1] + delta[1]}) != actual.end();
                })) {
                    rotation = r;
                    shift = delta;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) return false;
    }
    int count = 0;
    for (const auto& p : (*checks)[rotation]) {
        if (blocked(in, occupied, in.pose.position.row + shift[0] + p[0],
                    in.pose.position.col + shift[1] + p[1])) ++count;
    }
    return count >= 3;
}
} // namespace

SpinResult detect_spin(const SpinInput& in, const OccupiedCell& occupied) {
    using Mode = Runtime::SpinBonuses;
    const Mode mode = in.allSpinEnabled ? Mode::AllSpinPlus : in.bonuses;
    if (mode == Mode::None || !in.grounded) return {};
    if (!in.lastRotation.rotated()) return {};
    if (mode == Mode::Stupid) return {true, false};

    const bool t = Piece::t_spin_check(in.spinChecker);
    if (t) {
        auto result = t_corners(in, occupied);
        if (result.spin) {
            if (mode == Mode::MiniOnly) result.mini = true;
            return result;
        }
        if (mode == Mode::TSpins || mode == Mode::AllSpin ||
            mode == Mode::AllMini || mode == Mode::Handheld) return {};
    } else {
        if (mode == Mode::TSpins || mode == Mode::TSpinsPlus) return {};
        if (mode == Mode::Handheld) return {handheld_corners(in, occupied), false};
    }
    if (!in.immobile) return {};
    const bool mini = t || mode == Mode::AllMini || mode == Mode::AllMiniPlus || mode == Mode::MiniOnly;
    return {true, mini};
}
} // namespace Rules
