#include "tetr/compat/legacy_encoding.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/rules/attack.h"
#include "tetr/engine/garbage_field.h"
#include "tetr/rules/opener_phase.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <deque>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {

std::vector<int> legacy_cells(const std::vector<Runtime::BoardCell>& cells) {
    std::vector<int> out;
    out.reserve(cells.size());
    for (const auto& cell : cells) out.push_back(Compat::to_legacy(cell));
    return out;
}
using Rules::GarbagePacket;

bool same_queue(const std::deque<GarbagePacket>& a, const std::deque<GarbagePacket>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].line_count() != b[i].line_count() ||
            a[i].arrival_time() != b[i].arrival_time() ||
            a[i].preserve_hole() != b[i].preserve_hole()) return false;
    }
    return true;
}

struct RefCancelResult {
    int outgoing = 0;
    int cancelled = 0;
    bool opener = false;
};

int pending(const std::deque<GarbagePacket>& q) {
    int ans = 0;
    for (auto p : q) ans += p.line_count();
    return ans;
}

RefCancelResult ref_cancel(
    std::deque<GarbagePacket>& q,
    int attack,
    int placedPieces,
    bool multiplayer) {
    const int before = pending(q);
    const bool opener = Rules::opener_phase_double_cancel(
        placedPieces, attack, before, multiplayer);
    int budget = Rules::opener_phase_cancel_budget(attack, opener);
    while (q.size() && budget) {
        if (q.front().line_count() <= budget) {
            budget -= q.front().line_count();
            q.pop_front();
        } else {
            q.front().consume_lines(budget);
            budget = 0;
        }
    }
    return RefCancelResult{opener ? 0 : budget, before - pending(q), opener};
}

Rules::GarbageRisePlan ref_rise(
    std::deque<GarbagePacket>& q,
    double now,
    double delay,
    int maximumRise) {
    Rules::GarbageRisePlan result;
    int tmp = maximumRise;
    while (q.size() && now - q.front().arrival_time().seconds() >= delay && tmp) {
        if (q.front().line_count() <= tmp) {
            auto p = q.front();
            result.chunks.push_back({p.line_count(), p.preserve_hole()});
            tmp -= p.line_count();
            q.pop_front();
        } else {
            auto p = q.front();
            result.chunks.push_back({tmp, p.preserve_hole()});
            q.pop_front();
            q.push_front(GarbagePacket{p.line_count() - tmp, p.arrival_time(), true});
            break;
        }
    }
    return result;
}

std::vector<int> ref_display(
    const std::deque<GarbagePacket>& q,
    double now,
    double multiplier,
    double delay,
    Rules::GarbageBufferDisplayMode mode, int displaySize) {
    std::vector<int> out(static_cast<std::size_t>(displaySize), 0);
    int x = 0;
    for (auto y : q) {
        for (int i = x + 1; i <= x + y.line_count() && i < displaySize; ++i) {
            const double age = (now - y.arrival_time().seconds()) * multiplier;
            if (mode == Rules::GarbageBufferDisplayMode::TwoStage) {
                if (age < delay) out[i] = 1;
                else out[i] = 2;
            }
            if (mode == Rules::GarbageBufferDisplayMode::ThreeStage) {
                if (delay <= 0.3) out[i] = age < delay ? 2 : 3;
                else if (age < delay / 2) out[i] = 1;
                else if (age < delay) out[i] = 2;
                else out[i] = 3;
            }
        }
        x += y.line_count();
    }
    return out;
}

void test_short_delay_buffer_colors() {
    const std::deque<GarbagePacket> queue{
        GarbagePacket{1, Runtime::TimePoint::from_seconds(0.0), false}};
    const auto state_at = [&](double age, double delay, Rules::GarbageBufferDisplayMode mode) {
        return Rules::compute_garbage_buffer_display(
            queue,
            Runtime::TimePoint::from_seconds(age),
            1.0,
            Runtime::Duration::from_seconds(delay),
            mode,
            3)[1];
    };

    using Mode=Rules::GarbageBufferDisplayMode;
    assert(state_at(0.0, 0.2, Mode::ThreeStage)==2);
    assert(state_at(0.199, 0.2, Mode::ThreeStage)==2);
    assert(state_at(0.2, 0.2, Mode::ThreeStage)==3);
    assert(state_at(0.0, 0.3, Mode::ThreeStage)==2);
    assert(state_at(0.3, 0.3, Mode::ThreeStage)==3);
    assert(state_at(0.0, 0.4, Mode::ThreeStage)==1);
    assert(state_at(0.199, 0.4, Mode::ThreeStage)==1);
    assert(state_at(0.2, 0.4, Mode::ThreeStage)==2);
    assert(state_at(0.399, 0.4, Mode::ThreeStage)==2);
    assert(state_at(0.4, 0.4, Mode::ThreeStage)==3);
    assert(state_at(0.0, 0.0, Mode::ThreeStage)==3);
    assert(state_at(0.3, 0.4, Mode::TwoStage)==1);
    assert(state_at(0.4, 0.4, Mode::TwoStage)==2);
}

void ref_shuffle(std::vector<int>& a, std::mt19937& rd) {
    for (int i = 0; i < static_cast<int>(a.size()); ++i) {
        std::swap(a[i], a[rd() % (i + 1)]);
    }
}

bool ref_chance(std::mt19937& rd, double probability) {
    return rd() % 1000000 < 1000000 * probability;
}

void ref_prepare(
    const Rules::GarbageGenerationConfig& c,
    Rules::GarbageGenerationState& s,
    std::mt19937& rd,
    bool preserveHole) {
    const int garbageMode = Compat::to_legacy(c.garbageMode);
    if (garbageMode == 1 || garbageMode == 2 || garbageMode == 5 ||
        garbageMode == 11 || garbageMode == 10) {
        if (!preserveHole) ref_shuffle(s.holeOrder, rd);
    }
}

std::vector<int> ref_garbage_row(
    const Rules::GarbageGenerationConfig& c,
    Rules::GarbageGenerationState& s,
    std::mt19937& rd) {
    const int garbageMode = Compat::to_legacy(c.garbageMode);
    const int doubleHoleMode = Compat::to_legacy(c.doubleHoleMode);
    if ((garbageMode != 3 && garbageMode != 4) || std::abs(c.cheeseModel) == 1) {
        if (garbageMode == 3 || garbageMode == 4) {
            if (ref_chance(rd, c.cheeseMessiness)) ref_shuffle(s.holeOrder, rd);
        }
        if (ref_chance(rd, c.singleMessiness)) ref_shuffle(s.holeOrder, rd);
        if (doubleHoleMode == 2) ref_shuffle(s.holeOrder, rd);

        std::vector<int> row(c.width, Compat::to_legacy(c.cell));
        if (doubleHoleMode == 2) {
            const int minimum=static_cast<int>(std::floor(c.width*0.3));
            const int maximum=std::min(c.width-1,static_cast<int>(std::ceil(c.width*0.4)));
            const int gray=minimum+static_cast<int>(rd()%(maximum-minimum+1));
            s.holePattern = Runtime::GarbageHolePattern::scattered(c.width-gray);
        }
        const int legacyHolePattern = Compat::to_legacy(s.holePattern);
        if (legacyHolePattern >= 0) {
            for (int j = 0; j < std::min(c.width, std::max(1, legacyHolePattern)); ++j) {
                row[s.holeOrder[j]] = -1;
            }
        } else {
            const int runLength = std::min(c.width, -legacyHolePattern);
            for (int j = 0; j < c.width; ++j) {
                if (s.holeOrder[j] + runLength <= c.width) {
                    for (int k = s.holeOrder[j]; k < s.holeOrder[j] + runLength; ++k) {
                        row[k] = -1;
                    }
                    break;
                }
            }
        }
        if (doubleHoleMode == 1) {
            if (rd() % 2) {
                int x;
                while ((x = rd() % c.width) == s.holeOrder[0]);
                row[x] = -1;
            }
        }
        return row;
    }

    s.chessboardParity = !s.chessboardParity;
    std::vector<int> vis(c.width, 0);
    int flag = 1;
    for (int j = 0; j < c.width; ++j) {
        if (std::abs(c.cheeseModel) == 2) {
            if (ref_chance(rd, c.cheeseMessiness)) vis[j] = 1;
        }
        if (std::abs(c.cheeseModel) == 3) vis[j] = (static_cast<int>(s.chessboardParity) ^ j) & 1;
        if (!vis[j]) flag = 0;
    }
    if (flag) vis[rd() % c.width] = 0;
    std::vector<int> row(c.width);
    for (int j = 0; j < c.width; ++j) row[j] = vis[j] == 0 ? -1 : 7;
    return row;
}

std::vector<int> ref_solid_row(
    int width,
    int type,
    int doubleHoleMode,
    int timerValue,
    Rules::GarbageGenerationState& s,
    std::mt19937& rd) {
    std::vector<int> row(width, type);
    if (type != 9) return row;
    if (doubleHoleMode == 2) {
        ref_shuffle(s.holeOrder, rd);
        const int minimum=static_cast<int>(std::floor(width*0.3));
        const int maximum=std::min(width-1,static_cast<int>(std::ceil(width*0.4)));
        s.holePattern = Runtime::GarbageHolePattern::scattered(
            width-maximum+static_cast<int>(rd()%(maximum-minimum+1)));
        for (int j = 0; j < std::min(width, std::max(1, s.holePattern.extent())); ++j) {
            row[s.holeOrder[j]] = timerValue;
        }
    } else {
        int x = rd() % width, y;
        row[x] = timerValue;
        if (doubleHoleMode == 1) {
            if (rd() % 2) {
                while ((y = rd() % width) == x);
                row[y] = timerValue;
            }
        }
    }
    return row;
}

void test_queue_rules() {
    test_short_delay_buffer_colors();
    std::mt19937 rng(0x53474152u);
    for (int tc = 0; tc < 100000; ++tc) {
        std::deque<GarbagePacket> q;
        const int n = rng() % 12;
        for (int i = 0; i < n; ++i) {
            q.push_back(GarbagePacket{
                static_cast<int>(rng() % 13),
                Runtime::TimePoint::from_seconds(
                    static_cast<double>(static_cast<int>(rng() % 4000) - 2000) / 100.0),
                static_cast<bool>(rng() % 2)
            });
        }

        auto a = q, b = q;
        const int attack = rng() % 24;
        const int pieces = 1 + rng() % 30;
        const bool multiplayer = rng() & 1;
        const auto expected = ref_cancel(a, attack, pieces, multiplayer);
        const auto actual = Rules::cancel_incoming_garbage(
            b, Rules::GarbageCancelInput{attack, pieces, multiplayer});
        assert(expected.outgoing == actual.outgoingAttack);
        assert(same_queue(a, b));

        a = q; b = q;
        const double now = static_cast<double>(rng() % 10000) / 100.0;
        const double delay = static_cast<double>(rng() % 1000) / 100.0;
        const int maxRise = rng() % 17;
        const auto expectedRise = ref_rise(a, now, delay, maxRise);
        const auto actualRise = Rules::consume_ready_garbage(b, Runtime::TimePoint::from_seconds(now), Runtime::Duration::from_seconds(delay), maxRise);
        assert(expectedRise.chunks.size() == actualRise.chunks.size());
        for (std::size_t i = 0; i < expectedRise.chunks.size(); ++i) {
            assert(expectedRise.chunks[i].lines == actualRise.chunks[i].lines);
            assert(expectedRise.chunks[i].preserveHole == actualRise.chunks[i].preserveHole);
        }
        assert(same_queue(a, b));

        const auto mode = static_cast<Rules::GarbageBufferDisplayMode>(rng() % 3);
        const double multiplier = static_cast<double>(rng() % 500) / 100.0;
        assert(ref_display(q, now, multiplier, delay, mode, 100) ==
               Rules::compute_garbage_buffer_display(q, Runtime::TimePoint::from_seconds(now), multiplier, Runtime::Duration::from_seconds(delay), mode, 100));
    }

    for (int attack = 1; attack <= 20; ++attack) {
        const auto p = Rules::make_garbage_packet(attack, 1.75, Runtime::TimePoint::from_seconds(12.5), true, Runtime::Duration::from_seconds(5.0));
        assert(p.line_count() == static_cast<int>(attack * 1.75));
        assert(p.arrival_time().seconds() == 6.5);
        assert(!p.preserve_hole());
    }
}

void test_b2b_release_packets() {
    const auto ten = Rules::split_b2b_release_garbage(10, 10);
    assert(ten.ordinaryAttack == 0);
    assert((ten.chargePackets == std::array<int, 3>{{3, 3, 4}}));

    const auto eleven = Rules::split_b2b_release_garbage(11, 11);
    assert(eleven.ordinaryAttack == 0);
    assert((eleven.chargePackets == std::array<int, 3>{{4, 4, 3}}));

    const auto withOrdinary = Rules::split_b2b_release_garbage(14, 10);
    assert(withOrdinary.ordinaryAttack == 4);
    assert((withOrdinary.chargePackets == std::array<int, 3>{{3, 3, 4}}));

    // Four lines were cancelled from a 4+10 attack.  Ordinary attack is
    // consumed first, leaving the complete stored release in three packets.
    const auto partiallyCancelled = Rules::split_b2b_release_garbage(10, 10);
    assert(partiallyCancelled.ordinaryAttack == 0);
    assert((partiallyCancelled.chargePackets == std::array<int, 3>{{3, 3, 4}}));

    const auto heavilyCancelled = Rules::split_b2b_release_garbage(5, 10);
    assert(heavilyCancelled.ordinaryAttack == 0);
    assert((heavilyCancelled.chargePackets == std::array<int, 3>{{2, 2, 1}}));
}

void test_generation_rules() {
    std::mt19937 configRng(0x484f4c45u);
    for (int tc = 0; tc < 50000; ++tc) {
        const int width = 2 + configRng() % 15;
        Rules::GarbageGenerationConfig c;
        c.legacyShapeRules=true; // Historical replay RNG and mode-specific shapes.
        c.width = width;
        const int models[] = {0,1,2,3,4,5,10,11};
        c.garbageMode = Compat::garbage_mode_from_legacy(models[configRng() % 8]);
        const int cheeses[] = {-3,-2,-1,1,2,3};
        c.cheeseModel = cheeses[configRng() % 6];
        c.cheeseMessiness = static_cast<double>(configRng() % 1001) / 1000.0;
        c.singleMessiness = static_cast<double>(configRng() % 1001) / 1000.0;
        c.doubleHoleMode = Compat::modifier_mode_from_legacy(configRng() % 3);
        c.cell = Compat::board_cell_from_legacy(7 + configRng() % 3);

        Rules::GarbageGenerationState s1, s2;
        s1.holeOrder.resize(width);
        for (int i = 0; i < width; ++i) s1.holeOrder[i] = i;
        std::mt19937 init1(configRng());
        ref_shuffle(s1.holeOrder, init1);
        s2 = s1;
        int initialHolePattern = static_cast<int>(configRng() % (2 * width + 1)) - width;
        if (initialHolePattern == 0) initialHolePattern = 1;
        s1.holePattern = s2.holePattern = Compat::hole_pattern_from_legacy(initialHolePattern);
        s1.chessboardParity = s2.chessboardParity = (configRng() & 1) != 0;

        const unsigned seed = configRng();
        std::mt19937 r1(seed), r2(seed);
        const bool preserve = configRng() & 1;
        ref_prepare(c, s1, r1, preserve);
        Rules::prepare_garbage_batch(c, s2, r2, preserve);
        assert(s1.holeOrder == s2.holeOrder);

        const int rows = 1 + configRng() % 8;
        for (int row = 0; row < rows; ++row) {
            const auto expected = ref_garbage_row(c, s1, r1);
            const auto actual = Rules::generate_garbage_row(c, s2, r2);
            assert(expected == legacy_cells(actual));
            assert(s1.holeOrder == s2.holeOrder);
            assert(s1.holePattern == s2.holePattern);
            assert(s1.chessboardParity == s2.chessboardParity);
        }
        assert(r1() == r2());

        // Solid garbage has a separate generator but must preserve the same RNG sequence.
        Rules::GarbageGenerationState t1 = s1, t2 = s1;
        const unsigned solidSeed = configRng();
        std::mt19937 sr1(solidSeed), sr2(solidSeed);
        const int type = (configRng() % 3 == 0) ? 9 : 8;
        const auto expectedSolid = ref_solid_row(
            width, type, Compat::to_legacy(c.doubleHoleMode), 109, t1, sr1);
        const auto actualSolid = Rules::generate_solid_garbage_row(
            width, Compat::board_cell_from_legacy(type), c.doubleHoleMode, 9, t2, sr2);
        assert(expectedSolid == legacy_cells(actualSolid));
        assert(t1.holeOrder == t2.holeOrder);
        assert(t1.holePattern == t2.holePattern);
        assert(sr1() == sr2());
    }
}

void test_field_shift() {
    std::mt19937 rng(0x4649454cu);
    constexpr int offset = 40;
    constexpr int rows = 100;
    for (int tc = 0; tc < 20000; ++tc) {
        const int width = 1 + rng() % 14;
        const int first = -20 + rng() % 8;
        const int bottom = 10 + rng() % 20;
        const int height = rng() % (bottom - first + 1);
        std::vector<std::vector<int>> expected(rows, std::vector<int>(width));
        for (auto& row : expected) for (int& v : row) v = static_cast<int>(rng() % 14) - 1;
        auto actual = expected;
        auto silent = expected;

        std::vector<Engine::CellChange> expectedChanges;
        for (int row = first; row < bottom - height; ++row) {
            for (int col = 0; col < width; ++col) {
                const int next = expected[row + height + offset][col];
                if (expected[row + offset][col] != next) {
                    expectedChanges.push_back({row, col, Compat::board_cell_from_legacy(next)});
                }
            }
            expected[row + offset] = expected[row + height + offset];
        }

        const auto result = Engine::shift_field_up_for_garbage(
            width, first, bottom, height,
            [&](int row, int col){ return Compat::board_cell_from_legacy(actual[row + offset][col]); },
            [&](int destination, int source){ actual[destination + offset] = actual[source + offset]; });
        assert(expected == actual);
        const auto silentResult=Engine::shift_field_up_for_garbage(
            width,first,bottom,height,
            [&](int row,int col) { return Compat::board_cell_from_legacy(silent[row+offset][col]); },
            [&](int destination,int source) { silent[destination+offset]=silent[source+offset]; },false);
        assert(silent==expected && silentResult.changes.empty());
        assert(expectedChanges.size() == result.changes.size());
        for (std::size_t i = 0; i < expectedChanges.size(); ++i) {
            assert(expectedChanges[i].row == result.changes[i].row);
            assert(expectedChanges[i].col == result.changes[i].col);
            assert(expectedChanges[i].value == result.changes[i].value);
        }
    }
}

void test_oversized_buffer_display() {
    const auto now=Runtime::TimePoint::from_seconds(10);
    const auto delay=Runtime::Duration::from_seconds(1);
    const std::deque<GarbagePacket> queue{
        {1,now},
        {std::numeric_limits<int>::max(),Runtime::TimePoint{}},
        {std::numeric_limits<int>::max(),now}};
    assert(Rules::pending_garbage(queue)==std::numeric_limits<int>::max());
    assert((Rules::compute_garbage_buffer_display(queue,now,1,delay,
        Rules::GarbageBufferDisplayMode::ThreeStage,5)==std::vector<int>{0,1,3,3,3}));
    assert((Rules::compute_garbage_buffer_display(queue,now,1,delay,
        Rules::GarbageBufferDisplayMode::TwoStage,5)==std::vector<int>{0,1,2,2,2}));
    assert((Rules::compute_garbage_buffer_display(queue,now,1,delay,
        Rules::GarbageBufferDisplayMode::Hidden,5)==std::vector<int>(5,0)));
    for(int size:{-1,0,1})
        assert(Rules::compute_garbage_buffer_display(queue,now,1,delay,
            Rules::GarbageBufferDisplayMode::ThreeStage,size)==std::vector<int>(std::max(0,size),0));
}

} // namespace

int main() {
    test_oversized_buffer_display();
    assert(Rules::make_garbage_packet(std::numeric_limits<int>::max(),1000,
        Runtime::TimePoint{},false,Runtime::Duration{}).line_count()==std::numeric_limits<int>::max());
    assert(Rules::make_garbage_packet(-1,-1000,
        Runtime::TimePoint{},false,Runtime::Duration{}).line_count()==0);
    assert(Rules::make_garbage_packet(1,std::numeric_limits<double>::quiet_NaN(),
        Runtime::TimePoint{},false,Runtime::Duration{}).line_count()==0);
    test_queue_rules();
    test_b2b_release_packets();
    test_generation_rules();
    test_field_shift();
    std::cout << "garbage system tests passed\n";
    return 0;
}
