#include "../src/bot/chatgpt/local_pattern_evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <tuple>

namespace B = Bot_chatgpt;

static m_tetris::TetrisMap rows(std::initializer_list<unsigned> values) {
    m_tetris::TetrisMap map(10, 40);
    int y = 0;
    for (unsigned row : values) {
        map.row[y] = row;
        for (int x = 0; x < 10; ++x) if (row & (1u << x)) {
            map.top[x] = y + 1;
            map.roof = y + 1;
            ++map.count;
        }
        ++y;
    }
    return map;
}

static auto transition(const B::Evaluation::Status& s) {
    return std::make_tuple(s.combo, s.b2b, s.garbageHeight, s.incoming,
        s.readyIncoming, s.risen, s.dead, s.previousCleared,
        s.openerDoubleCancelEnabled, s.placedPieces, s.attack, s.reward, s.pathRisk);
}

static void check_formula(const m_tetris::TetrisMap& map) {
    // Compare the raw table export against the separately exported theta and
    // explicit 560 multiplier from frozen model 67a7c9f4...06241.
    constexpr B::LocalPatternWeights theta{{0.0, -0.06406245620840974, -0.14285714285714285, 0.14285714285714285, -0.0, 0.14285714285714285, 0.14285714285714285, -0.12602336623059707, -0.0, -0.0, 0.0, -0.06417623863213866, -0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, 0.0, 0.14285714285714285, 0.020369150682431003, -0.0, 0.0, 0.0, 0.07436081655286957, 0.0, 0.03351984396850771, 0.0, -0.0, -0.0, -0.06489555686896571, -0.0, -0.0008560217836371411, -0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.14285714285714285, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, 0.03124207389703955, 0.0, -0.0, 0.0, -0.0, -0.0, 0.0, 0.0, 0.003506723782633603, 0.14285714285714285, 0.0, 0.14285714285714285, -0.0, 0.0, -0.0, 0.12077168858217967, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.11050177709630225, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, -0.0, -0.0, -0.0, 0.0, -0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, 0.0, 0.0, -0.0, 0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.0, -0.14285714285714285}};
    const auto counts = B::local_pattern_counts(map);
    assert(counts[0] == 0);
    double full = 0, tableDot = 0;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        full += theta[i] * counts[i];
        tableDot += B::fitted_local_pattern_weights[i] * counts[i];
    }
    full *= 560;
    const double optimized = B::local_pattern_correction(map);
    assert(std::abs(full - optimized) < 1e-9);
    assert(std::abs(tableDot - optimized) < 1e-9);
    assert(std::isfinite(optimized));
}

static void check_mirror(const m_tetris::TetrisMap& map) {
    m_tetris::TetrisMap mirrored(10, 40);
    mirrored.count = map.count;
    mirrored.roof = map.roof;
    for (int x = 0; x < 10; ++x) {
        mirrored.top[9 - x] = map.top[x];
        for (int y = 0; y < 40; ++y)
            if (map.row[y] & (1u << x)) mirrored.row[y] |= 1u << (9 - x);
    }
    check_formula(mirrored);
    assert(B::local_pattern_counts(map) == B::local_pattern_counts(mirrored));
    assert(std::abs(B::local_pattern_correction(map) - B::local_pattern_correction(mirrored)) < 1e-9);
}

int main() {
    double l1 = 0;
    assert(B::fitted_local_pattern_weights[0] == 0);
    for (const auto weight : B::fitted_local_pattern_weights) {
        assert(std::abs(weight) <= 80 + 1e-12);
        l1 += std::abs(weight);
    }
    assert(l1 <= 1120 + 1e-9);
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::Search>;
    Engine geometry;
    assert(geometry.prepare(10, 40));
    const auto* context = geometry.context().get();
    B::Evaluation baseline;
    B::LocalPatternEvaluation learned;
    baseline.init(context);
    learned.init(context);
    B::Search search;
    B::Search::Config config;
    search.init(context, &config);
    const auto empty = rows({});
    const auto perfectClear = rows({1023u ^ (15u << 3)});
    const auto spinCavity = rows({1023u ^ (1u << 4), 1023u ^ (7u << 3), 1u << 3});
    const auto garbage = rows({1022, 1022, 1021, 1021, 1019, 7});
    const auto bridge = rows({1022, 1022, 1023u ^ (1u << 8)});
    for (const auto& map : {empty, perfectClear, spinCavity, garbage, bridge}) {
        check_formula(map);
        check_mirror(map);
    }
    int checks = 0, pcs = 0, spins = 0, rises = 0;
    for (const auto& before : {empty, perfectClear, spinCavity, garbage, bridge}) {
        for (char type : std::string("IJLOSTZ")) {
            const auto moves = *search.search(before, context->generate(type), 0);
            for (auto move : moves) {
                auto after = before;
                const int clear = move->attach(context, after);
                check_formula(after);
                const auto oldResult = baseline.eval(move, after, before, clear);
                const auto newResult = learned.eval(move, after, before, clear);
                assert(oldResult.before == newResult.before && oldResult.clearedRows == newResult.clearedRows);
                assert(oldResult.shape.map == newResult.shape.map && oldResult.shape.clear == newResult.shape.clear);
                assert(oldResult.shape.count == newResult.shape.count);
                const double correction = B::local_pattern_correction(after);
                assert(std::abs(newResult.shape.value - oldResult.shape.value - correction) < 1e-9);
                for (int scenario = 0; scenario < 4; ++scenario) {
                    B::Evaluation::Status state;
                    state.b2b = 7;
                    state.combo = 3;
                    state.garbageHeight = std::min(before.roof, 4);
                    state.incoming = 12;
                    state.readyIncoming = 8;
                    state.placedPieces = 30;
                    state.risen = scenario == 1 ? 8 : 0;
                    state.previousCleared = scenario == 2;
                    state.dead = scenario == 3;
                    state.value = state.dead ? -1e9 : 0;
                    const std::vector<B::VisiblePacket> packets{{8, -1}, {4, 1.5}};
                    baseline.set_garbage_forecast(packets, 1);
                    learned.set_garbage_forecast(packets, 1);
                    const m_tetris::TetrisContext::Env env{"IOT", 3, type, 'T', scenario == 2};
                    auto oldMove = move, newMove = move;
                    const auto a = baseline.get(oldMove, oldResult, 1, state, env);
                    const auto b = learned.get(newMove, newResult, 1, state, env);
                    assert(transition(a) == transition(b) && oldMove == newMove);
                    assert(std::abs((b.value - a.value) - (state.dead ? 0 : correction)) < 1e-5);
                    if (state.dead) assert(b.value == state.value);
                    pcs += clear && after.count == 0 && !state.risen && !state.dead;
                    spins += move.spin && clear;
                    rises += b.risen > state.risen;
                    ++checks;
                }
            }
        }
    }
    assert(checks > 100 && pcs > 0 && spins > 0 && rises > 0);
    std::cout << "local pattern model formula, direct-table correction, mirror fixtures and "
              << checks << " unchanged rule transitions passed\n";
}
