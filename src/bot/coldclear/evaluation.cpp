// C++ translation of bot/src/evaluation/standard.rs, MinusKelvin/cold-clear.
// SPDX-License-Identifier: MPL-2.0
#include "core.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace ColdClear {
void Value::improve(Value other) {
    value = std::max(value, other.value); spike = std::max(spike, other.spike);
}
std::int64_t Value::weight(Value minimum, std::size_t rank) const {
    const auto e = static_cast<std::int64_t>(value) - minimum.value + 10;
    return e * e / (static_cast<std::int64_t>(rank) * rank + 1);
}

namespace {
struct Slot { int x, y, rotation; };
using Cells = std::array<Piece::Offset, 4>;
Cells slot_cells(Slot slot) {
    Cells cells{{{-1,0},{0,0},{1,0},{0,1}}};
    for (auto& p : cells) {
        for (int r = 0; r < slot.rotation; ++r) { const int x = p[0]; p[0] = p[1]; p[1] = -x; }
        p[0] += slot.x; p[1] += slot.y;
    }
    return cells;
}
bool pattern(const Board& b, int x, int y, std::initializer_list<std::string_view> rows) {
    for (auto row : rows) {
        for (int dx = 0; dx < static_cast<int>(row.size()); ++dx)
            if ((row[dx] == '#' && !b.occupied(x + dx, y)) ||
                (row[dx] == '_' && b.occupied(x + dx, y))) return false;
        --y;
    }
    return true;
}
std::optional<Slot> sky_left(const Board& b, const std::array<int, Width>& h) {
    for (int x = 0; x <= 7; ++x) if (h[x+1] <= h[x]-1 &&
        pattern(b,x,h[x]+1,{"??#","??_","??#"})) return Slot{x+1,h[x],2};
    return {};
}
std::optional<Slot> sky_right(const Board& b, const std::array<int, Width>& h) {
    for (int x = 0; x <= 7; ++x) if (h[x+1] <= h[x+2]-1 &&
        pattern(b,x,h[x+2]+1,{"#??","_??","#??"})) return Slot{x+1,h[x+2],2};
    return {};
}
std::optional<Slot> twist_left(const Board& b, const std::array<int, Width>& h) {
    for (int x = 0; x <= 7; ++x) if (h[x] <= h[x+1] &&
        b.occupied(x-1,h[x+1]) == b.occupied(x-1,h[x+1]+1) &&
        pattern(b,x,h[x+1]+1,{"??#","??_","??_","?__","??_"})) return Slot{x+2,h[x+1]-2,3};
    return {};
}
std::optional<Slot> twist_right(const Board& b, const std::array<int, Width>& h) {
    for (int x = 0; x <= 7; ++x) if (h[x+2] <= h[x+1] &&
        b.occupied(x+3,h[x+1]) == b.occupied(x+3,h[x+1]+1) &&
        pattern(b,x,h[x+1]+1,{"#??","_??","_??","__?","_??"})) return Slot{x,h[x+1]-2,1};
    return {};
}
std::optional<Slot> fin_left(const Board& b, const std::array<int, Width>& h) {
    for (int x = 0; x <= 6; ++x) if (h[x] <= h[x+1]+1 &&
        pattern(b,x,h[x+1]+2,{"??##?","??__?","??__#","??__?","??#_#"})) return Slot{x+3,h[x+1]-1,3};
    return {};
}
std::optional<Slot> fin_right(const Board& b, const std::array<int, Width>& h) {
    for (int x = 0; x <= 6; ++x) if (h[x+3] <= h[x+2]+1 && b.occupied(x-1,h[x+2]) && b.occupied(x-1,h[x+2]-2) &&
        pattern(b,x,h[x+2]+2,{"##??","__??","__??","__??","_#??"})) return Slot{x,h[x+2]-1,1};
    return {};
}
Slot drop_slot(const Board& b, Slot slot) {
    const auto h = b.heights();
    int amount = Rows;
    for (const auto& p : slot_cells(slot)) {
        if (p[0] < 0 || p[0] >= Width) return slot;
        amount = std::min(amount, p[1] - h[p[0]]);
    }
    if (amount > 0) slot.y -= amount;
    else if (amount < 0) {
        for (;;) {
            auto next = slot; --next.y;
            const auto cells = slot_cells(next);
            if (std::any_of(cells.begin(), cells.end(), [&](auto p) { return b.occupied(p[0],p[1]); })) break;
            slot = next;
        }
    }
    return slot;
}
std::optional<Slot> cave(const Board& b, Slot slot) {
    slot = drop_slot(b, slot);
    const int x = slot.x, y = slot.y;
    if (slot.rotation == 1) {
        if (!b.occupied(x-1,y) && b.occupied(x-1,y-1) && b.occupied(x+1,y-1) && b.occupied(x-1,y+1))
            return Slot{x,y,2};
        if (!b.occupied(x+1,y-1) && !b.occupied(x+2,y-1) && !b.occupied(x+1,y-2) &&
            b.occupied(x-1,y) && b.occupied(x+2,y) && b.occupied(x,y-2) && b.occupied(x+2,y-2))
            return Slot{x+1,y-1,2};
    } else if (slot.rotation == 3) {
        if (!b.occupied(x+1,y) && b.occupied(x+1,y+1) && b.occupied(x+1,y-1) && b.occupied(x-1,y-1))
            return Slot{x,y,2};
        if (!b.occupied(x-1,y-1) && !b.occupied(x-2,y-1) && !b.occupied(x-1,y-2) &&
            b.occupied(x+1,y) && b.occupied(x-2,y) && b.occupied(x-2,y-2) && b.occupied(x,y-2))
            return Slot{x-1,y-1,2};
    }
    return {};
}
std::optional<Slot> detect_slot(const Board& b) {
    const auto h = b.heights();
    if (auto slot = sky_left(b,h)) return slot;
    if (auto slot = sky_right(b,h)) return slot;
    auto twist = twist_left(b,h);
    if (!twist) twist = twist_right(b,h);
    if (twist) {
        if (auto slot = cave(b,*twist)) return slot;
        const int x = twist->x, y = twist->y;
        const int corners = b.occupied(x-1,y-1) + b.occupied(x+1,y-1) + b.occupied(x-1,y+1) + b.occupied(x+1,y+1);
        auto lower = *twist; --lower.y;
        const auto cells = slot_cells(lower);
        if (corners >= 3 && std::any_of(cells.begin(),cells.end(),[&](auto p) { return b.occupied(p[0],p[1]); })) return twist;
    }
    if (auto slot = fin_left(b,h)) return slot;
    return fin_right(b,h);
}
int popcount(unsigned x) {
    int count = 0;
    while (x) { x &= x - 1; ++count; }
    return count;
}
}

int terrain(Board board, int b2b, int tCount) {
    int result = b2b > 0 ? 52 : 0;
    auto h = board.heights();
    const int initialHeight = *std::max_element(h.begin(), h.end());
    result -= 511 * std::max(0, initialHeight-15) + 150 * std::max(0, initialHeight-10);
    constexpr int tslot[]{8,148,192,407};
    for (int i = 0; i < tCount; ++i) {
        const auto slot = detect_slot(board);
        if (!slot) break;
        // Upstream cutout_tslot is an evaluation projection, not a legal move.
        Board projected = board;
        for (auto p : slot_cells(*slot)) if (p[0] >= 0 && p[0] < Width && p[1] >= 0 && p[1] < Rows)
            projected.rows[p[1]] |= 1u << p[0];
        const int lines = static_cast<int>(std::count(projected.rows.begin(),projected.rows.end(),Full));
        result += tslot[std::min(lines,3)];
        if (lines < 2) break;
        board = projected;
        board.clear_lines();
    }
    h = board.heights();
    const int highest = *std::max_element(h.begin(), h.end());
    result -= 39 * highest;
    int well = 0;
    for (int x = 1; x < Width; ++x) if (h[x] <= h[well]) well = x;
    int depth = 0;
    for (int y = h[well]; y < Height; ++y) {
        bool filled = true;
        for (int x = 0; x < Width; ++x) if (x != well && !board.occupied(x,y)) filled = false;
        if (!filled) break;
        ++depth;
    }
    depth = std::min(depth,17);
    constexpr int wellColumn[]{20,23,20,50,59,21,59,10,-10,24};
    result += 57 * depth + (depth ? wellColumn[well] : 0);
    for (int y = 0; y < std::max(40,highest); ++y)
        result -= 5 * popcount((board.rows[y] | (1u<<Width)) ^ (1u | (board.rows[y]<<1)));
    int bump = -1, bumpSq = -1, previous = well == 0 ? 1 : 0;
    for (int x = 1; x < Width; ++x) {
        if (x == well) continue;
        const int difference = std::abs(h[previous] - h[x]);
        bump += difference; bumpSq += difference*difference; previous = x;
    }
    result -= 24 * std::abs(bump) + 7 * std::abs(bumpSq);
    int cavities = 0, overhangs = 0, covered = 0, coveredSq = 0;
    for (int y = 0; y < highest; ++y) for (int x = 0; x < Width; ++x) {
        if (board.occupied(x,y) || y >= h[x]) continue;
        if ((x > 1 && h[x-1] <= y-1 && h[x-2] <= y) || (x < 8 && h[x+1] <= y-1 && h[x+2] <= y)) ++overhangs;
        else ++cavities;
    }
    for (int x = 0; x < Width; ++x) for (int y = h[x]-3; y >= 0; --y) if (!board.occupied(x,y)) {
        const int cells = std::min(6,h[x]-y-1); covered += cells; coveredSq += cells*cells;
    }
    result -= 173*cavities + 3*cavities*cavities + 34*overhangs + overhangs*overhangs + 17*covered + coveredSq;
    return result;
}

Evaluation evaluate(const Input& input, const State& state, const Move& move, const Rules::PlacementResult& lock) {
    const int lines = lock.clear.clearedLines;
    const bool fullSpin = lock.spin.spin && !lock.attack.effectiveMini;
    const bool b2bClear = lock.chain.b2bTransition == Rules::B2BTransition::Increment && lock.chain.previousB2B > 0;
    constexpr int comboTable[]{0,0,1,1,2,2,3,3,4,4,4,5};
    const int combo = lines ? comboTable[std::clamp(state.combo-1,0,11)] : 0;
    int reward = 0, upstreamAttack = 0;
    if (lock.clear.perfectClear) { reward = 999; upstreamAttack = 10; }
    else {
        constexpr int clears[]{0,-143,-100,-58,390};
        constexpr int spins[]{0,121,410,602,800};
        constexpr int minis[]{0,-158,-93,-58,390};
        constexpr int base[]{0,0,1,2,4};
        const int count = std::clamp(lines,0,4);
        reward = (fullSpin ? spins[count] : lock.spin.spin ? minis[count] : clears[count]) +
            (b2bClear ? 104 : 0) + 150 * combo;
        upstreamAttack = (fullSpin ? 2*count : base[count]) + (b2bClear ? 1 : 0) + combo;
    }
    // Preserve Standard's clear-efficiency preferences, but replace PPT damage
    // with the host's actual All-spin/PC/combo/B2B-charge/garbage-bonus attack.
    reward += 150 * (lock.attack.attack - upstreamAttack);
    if (move.piece == 5 && !fullSpin) reward -= 152;
    const int time = move.time + (lines ? 40 : 0);
    reward -= 3 * time;
    const auto h = state.board.heights();
    const int highest = *std::max_element(h.begin(),h.end());
    reward -= 11 * std::max(0,highest-10) * time / 10;
    // Bag information is the public remainder at the preview endpoint. Never
    // consult the host's private shuffled queue to value T-slot availability.
    unsigned nextBag = state.bag;
    const int queueEnd = static_cast<int>(input.next.size()+state.tail.size());
    for (int i = queueEnd-1; i >= state.cursor; --i) {
        const int piece = i < static_cast<int>(input.next.size()) ? input.next[i] : state.tail[i-input.next.size()];
        if (nextBag == 127) nextBag = 0;
        nextBag |= 1u << piece;
    }
    int tCount = input.environment.randomPieces ? 1 :
        int((nextBag & (1u<<5)) != 0) + int(popcount(nextBag) <= 3);
    tCount += state.hold == 5;
    return {{terrain(state.board,state.b2b,tCount),0}, {reward, lines ? lock.attack.attack : -1}};
}
}
