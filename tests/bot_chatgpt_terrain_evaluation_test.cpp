#include "../src/bot/chatgpt/terrain_evaluation.h"
#include "../src/bot/chatgpt/terrain_bridge_evaluation.h"
#include "rule_tetr_srs_plus.h"

#include <cassert>
#include <cmath>
#include <initializer_list>
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
            ++map.count;
            map.roof = y + 1;
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

static void check_bridge_partition_and_symmetry(const m_tetris::TetrisMap& map) {
    const auto original = B::terrain_features(map);
    const auto bridge = B::bridge_terrain_features(map);
    assert(bridge.removableBridgeCavities >= 0);
    assert(bridge.removableBridgeCavities <= original.buriedCavities);
    assert(bridge.terrain.buriedCavities >= 0 && bridge.terrain.shallowCavities >= 0);
    assert(original.buriedCavities + original.shallowCavities ==
           bridge.terrain.buriedCavities + bridge.terrain.shallowCavities);
    assert(std::isfinite(B::bridge_terrain_value(map)));
    m_tetris::TetrisMap mirrored(map.width, map.height);
    mirrored.roof = map.roof;
    mirrored.count = map.count;
    for (int x = 0; x < map.width; ++x) {
        mirrored.top[map.width - x - 1] = map.top[x];
        for (int y = 0; y < map.height; ++y)
            if (map.row[y] & (1u << x)) mirrored.row[y] |= 1u << (map.width - x - 1);
    }
    const auto reflected = B::bridge_terrain_features(mirrored);
    assert(bridge.removableBridgeCavities == reflected.removableBridgeCavities);
    assert(B::bridge_terrain_value(map) == B::bridge_terrain_value(mirrored));
}

int main() {
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::Search>;
    Engine geometry;
    assert(geometry.prepare(10, 40));
    const auto* context = geometry.context().get();
    B::Evaluation baseline;
    B::TerrainEvaluation candidate;
    B::TerrainBridgeEvaluation bridgeCandidate;
    baseline.init(context);
    candidate.init(context);
    bridgeCandidate.init(context);
    B::Search search;
    B::Search::Config config;
    search.init(context, &config);
    const auto empty = rows({});
    // Horizontal I perfect clear; a T double cavity; staggered garbage; high
    // stack with a legal side channel. Every action comes from the enumerator.
    const auto perfectClear = rows({1023u ^ (15u << 3)});
    const auto spinCavity = rows({1023u ^ (1u << 4), 1023u ^ (7u << 3), 1u << 3});
    const auto garbage = rows({1023u ^ 1u, 1023u ^ 1u, 1023u ^ 2u,
                              1023u ^ 2u, 1023u ^ 4u, 7u});
    // One removable ceiling closes two cells of an otherwise open channel.
    const auto removableBridge = rows({1023u ^ 1u, 1023u ^ 1u, 1023u ^ (1u << 8)});
    auto high = rows({});
    for (int y = 0; y < 18; ++y) {
        high.row[y] = 63;
        high.count += 6;
    }
    high.roof = 18;
    for (int x = 0; x < 6; ++x) high.top[x] = 18;

    int checks = 0, pcChecks = 0, spinGarbageChecks = 0, riseChecks = 0;
    int deathChecks = 0, valueChanges = 0, bridgeValueChanges = 0, holdChecks = 0;
    for (const auto& before : {empty, perfectClear, spinCavity, garbage, removableBridge, high}) {
        check_bridge_partition_and_symmetry(before);
        for (char type : std::string("IJLOSTZ")) {
            const auto moves = *search.search(before, context->generate(type), 0);
            for (auto move : moves) {
                assert(move->check(before));
                assert(!move->move_down || !move->move_down->check(before));
                auto after = before;
                const int clear = move->attach(context, after);
                const auto a = baseline.eval(move, after, before, clear);
                const auto b = candidate.eval(move, after, before, clear);
                const auto c = bridgeCandidate.eval(move, after, before, clear);
                assert(a.shape.map == b.shape.map && a.shape.clear == b.shape.clear);
                assert(a.before == b.before && a.clearedRows == b.clearedRows);
                assert(a.shape.map == c.shape.map && a.shape.clear == c.shape.clear);
                assert(a.before == c.before && a.clearedRows == c.clearedRows);
                check_bridge_partition_and_symmetry(after);
                if (a.shape.value != b.shape.value) ++valueChanges;
                if (b.shape.value != c.shape.value) ++bridgeValueChanges;
                for (int scenario = 0; scenario < 5; ++scenario) {
                    B::Evaluation::Status state;
                    state.b2b = 7;
                    state.combo = 3;
                    state.garbageHeight = std::min(before.roof, 4);
                    state.incoming = 12;
                    state.readyIncoming = 8;
                    state.placedPieces = scenario == 0 ? 0 : 30;
                    state.openerDoubleCancelEnabled = true;
                    state.previousCleared = scenario == 3;
                    state.risen = scenario == 2 ? 8 : 0;
                    state.dead = scenario == 4;
                    state.value = state.dead ? -1e9 : 0;
                    const std::vector<B::VisiblePacket> packets{{8, -1}, {4, 1.5}};
                    baseline.set_garbage_forecast(packets, 1);
                    candidate.set_garbage_forecast(packets, 1);
                    bridgeCandidate.set_garbage_forecast(packets, 1);
                    const m_tetris::TetrisContext::Env env{"IOT", 3, type, 'T', scenario == 3};
                    auto oldMove = move, newMove = move, bridgeMove = move;
                    const auto original = baseline.get(oldMove, a, scenario % 3, state, env);
                    const auto changed = candidate.get(newMove, b, scenario % 3, state, env);
                    const auto bridged = bridgeCandidate.get(bridgeMove, c, scenario % 3, state, env);
                    assert(transition(original) == transition(changed));
                    assert(transition(original) == transition(bridged));
                    assert(oldMove == newMove);
                    assert(oldMove == bridgeMove);
                    const double expectedDifference = state.dead ? 0 : b.shape.value - a.shape.value;
                    const double expectedBridgeDifference = state.dead ? 0 : c.shape.value - a.shape.value;
                    assert(std::abs((changed.value - original.value) - expectedDifference) < 1e-5);
                    assert(std::abs((bridged.value - original.value) - expectedBridgeDifference) < 1e-5);
                    if (state.dead) {
                        assert(changed.value == state.value);
                        assert(transition(changed) == transition(state));
                        assert(bridged.value == state.value);
                        assert(transition(bridged) == transition(state));
                    }
                    ++checks;
                    pcChecks += clear > 0 && after.count == 0 && state.risen == 0 && !state.dead;
                    spinGarbageChecks += move.spin && clear > 0 && changed.garbageHeight < state.garbageHeight;
                    riseChecks += changed.risen > state.risen;
                    deathChecks += changed.dead && !state.dead;
                    holdChecks += env.is_hold;
                }
            }
        }
    }
    assert(checks > 1000 && valueChanges > 100 && bridgeValueChanges > 0);
    assert(pcChecks > 0 && spinGarbageChecks > 0 && riseChecks > 0);
    assert(deathChecks > 0 && holdChecks > 0);
    std::cout << "terrain V1/V2 adapter transitions=" << checks << " pc=" << pcChecks
              << " spin_garbage=" << spinGarbageChecks << " rise=" << riseChecks
              << " death=" << deathChecks << " hold=" << holdChecks
              << " bridge_value_changes=" << bridgeValueChanges << '\n';
}
