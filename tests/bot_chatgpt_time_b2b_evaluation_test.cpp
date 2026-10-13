#include "../src/bot/chatgpt/time_b2b_evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <tuple>

namespace B = Bot_chatgpt;

static m_tetris::TetrisMap rows(std::initializer_list<unsigned> values) {
    m_tetris::TetrisMap map(10, 40);
    int y = 0;
    for (unsigned bits : values) {
        map.row[y] = bits;
        for (int x = 0; x < 10; ++x) if (bits & (1u << x)) {
            map.top[x] = y + 1;
            map.roof = y + 1;
            ++map.count;
        }
        ++y;
    }
    return map;
}

static auto state(const B::Evaluation::Status& s) {
    return std::make_tuple(s.combo, s.b2b, s.garbageHeight, s.incoming,
        s.readyIncoming, s.risen, s.dead, s.previousCleared,
        s.openerDoubleCancelEnabled, s.placedPieces, s.attack, s.reward, s.pathRisk);
}

static double maximumFormulaError = 0;

static void near(double a, double b) {
    // Residual correction and a grouped formula may differ by rounding only.
    const double scale = std::max({1.0, std::abs(a), std::abs(b)});
    maximumFormulaError = std::max(maximumFormulaError, std::abs(a - b));
    assert(std::abs(a - b) <= 16 * std::numeric_limits<double>::epsilon() * scale);
}

int main() {
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::Search>;
    Engine geometry;
    assert(geometry.prepare(10, 40));
    const auto* context = geometry.context().get();
    B::Evaluation baseline;
    B::TimeB2BEvaluation timed;
    baseline.init(context);
    timed.init(context);
    B::Search search;
    B::Search::Config config;
    search.init(context, &config);
    // Placed-cell provenance must survive compaction independently of occupancy.
    const auto halfBefore = rows({1023u ^ (3u << 3),1023u ^ (3u << 3),1u});
    bool sawHalf=false;
    for (auto landing:*search.search(halfBefore,context->generate('O'),0)) {
        auto after=halfBefore;
        if (landing->attach(context,after)!=2) continue;
        const auto evaluated=baseline.eval(landing,after,halfBefore,2);
        B::Evaluation::Status previous;
        previous.b2b=10;
        previous.garbageHeight=3;
        previous.attackRules.halfClearEnabled=true;
        previous.attackRules.halfClearAttack=7;
        previous.attackRules.halfClearCharge=3;
        const auto future=baseline.get(landing,evaluated,0,previous,{nullptr,0,'O',' ',false});
        assert(future.b2b==13 && future.attack==9); // Double + B2B + half clear.
        assert(std::none_of(future.pieceRows.begin(),future.pieceRows.end(),[](auto row){return row!=0;}));
        // Changing a surviving garbage cell to a piece suppresses HALF CLEAR.
        previous.pieceRows[2]=1u;
        const auto withPiece=baseline.get(landing,evaluated,0,previous,{nullptr,0,'O',' ',false});
        assert(withPiece.b2b==0 && withPiece.attack==10); // Normal Double + released B2B 9.
        previous.pieceRows[2]=0;
        previous.attackRules.b2bChargeReduction=-2;
        previous.pieceRows[2]=1u;
        assert(baseline.get(landing,evaluated,0,previous,{nullptr,0,'O',' ',false}).attack==12);
        sawHalf=true; break;
    }
    assert(sawHalf);
    const auto empty = rows({});
    const auto pc = rows({1023u ^ (15u << 3)});
    const auto spin = rows({1023u ^ (1u << 4), 1023u ^ (7u << 3), 1u << 3});
    const auto garbage = rows({1022, 1022, 1021, 1021, 1019, 7});
    auto high = rows({});
    for (int y = 0; y < 18; ++y) { high.row[y] = 63; high.count += 6; }
    high.roof = 18;
    for (int x = 0; x < 6; ++x) high.top[x] = 18;

    int checks = 0, pcChecks = 0, spinGarbage = 0, rises = 0;
    int deaths = 0, releases = 0, absorbed = 0, changed = 0;
    for (const auto& before : {empty, pc, spin, garbage, high}) {
        for (char type : std::string("IJLOSTZ")) {
            const auto moves = *search.search(before, context->generate(type), 0);
            for (auto move : moves) {
                assert(move->check(before));
                assert(!move->move_down || !move->move_down->check(before));
                auto after = before;
                const int clear = move->attach(context, after);
                const auto oldShape = baseline.eval(move, after, before, clear);
                const auto newShape = timed.eval(move, after, before, clear);
                assert(oldShape.shape.value == newShape.shape.value);
                assert(oldShape.shape.count == newShape.shape.count);
                assert(oldShape.shape.clear == newShape.shape.clear);
                assert(oldShape.shape.map == newShape.shape.map);
                assert(oldShape.before == newShape.before && oldShape.clearedRows == newShape.clearedRows);
                for (int chain : {0, 4, 5, 11, 20}) for (std::size_t depth : {0u, 1u, 3u, 6u, 40u, 41u}) {
                    for (int scenario = 0; scenario < 5; ++scenario) {
                        B::Evaluation::Status previous;
                        previous.b2b = chain;
                        previous.combo = 3;
                        previous.garbageHeight = std::min(before.roof, 4);
                        previous.incoming = 12;
                        previous.readyIncoming = 8;
                        previous.placedPieces = scenario == 0 ? 0 : 30;
                        previous.openerDoubleCancelEnabled = true;
                        previous.risen = scenario == 1 ? 8 : 0;
                        previous.previousCleared = scenario == 2;
                        previous.dead = scenario == 3;
                        previous.attack = 19;
                        previous.reward = 1234.125;
                        previous.pathRisk = 315.25;
                        previous.value = previous.dead ? -1000000345.125 : 999.5;
                        const std::vector<B::VisiblePacket> packets{{8, -1}, {4, 1.5}};
                        baseline.set_garbage_forecast(packets, 1);
                        timed.set_garbage_forecast(packets, 1);
                        const m_tetris::TetrisContext::Env env{
                            scenario == 4 ? nullptr : "IOT", scenario == 4 ? 0u : 3u,
                            type, scenario == 4 ? 'O' : 'T', scenario == 2};
                        auto oldMove = move, newMove = move;
                        const auto a = baseline.get(oldMove, oldShape, depth, previous, env);
                        const auto b = timed.get(newMove, newShape, depth, previous, env);
                        assert(state(a) == state(b) && oldMove == newMove);
                        if (previous.dead) {
                            assert(state(b) == state(previous));
                            assert(a.value == previous.value && b.value == previous.value);
                            ++absorbed;
                        } else {
                            const double discount = std::pow(0.96, double(depth));
                            const double reserve = 480.0 * a.b2b + (a.b2b ? 300.0 : 0.0);
                            near(b.value, a.value + (discount - 1.0) * reserve);
                            const double danger = std::max(0, after.roof + b.risen + std::min(8, b.incoming) - 12);
                            double expected = oldShape.shape.value + b.reward + discount * reserve
                                - 95.0 * danger * danger - b.pathRisk;
                            if (env.hold == 'T' || env.hold == 'I') expected += 80;
                            if (b.dead) expected -= 1e9;
                            near(b.value, expected);
                            if (depth == 0 || a.b2b == 0) near(b.value, a.value);
                            if (!a.dead && depth > 0 && a.b2b > 0) {
                                assert(b.value < a.value);
                                ++changed;
                            }
                            pcChecks += clear > 0 && after.count == 0 && previous.risen == 0;
                            spinGarbage += move.spin && clear > 0 && a.garbageHeight < previous.garbageHeight;
                            rises += a.risen > previous.risen;
                            releases += chain >= 5 && a.b2b == 0 && clear > 0;
                            deaths += a.dead;
                            if (b.dead) {
                                // Different input depths must not discount a dead value again.
                                auto ignoredMove = move;
                                const auto again = timed.get(ignoredMove, newShape, depth + 4, b, env);
                                assert(state(again) == state(b) && again.value == b.value);
                            }
                        }
                        ++checks;
                    }
                }
            }
        }
    }
    assert(checks > 1000 && pcChecks > 0 && spinGarbage > 0 && rises > 0);
    assert(deaths > 0 && releases > 0 && absorbed > 0 && changed > 0);
    // Every cached depth and the fallback path use the same pow value. Also
    // verify forwarding/saving a non-default policy without changing it later.
    B::PolicyWeights policy;
    policy.attack = 521;
    policy.b2b = 271.25;
    baseline.set_policy(policy);
    timed.set_policy(policy);
    baseline.set_garbage_forecast({}, 1);
    timed.set_garbage_forecast({}, 1);
    bool checkedPolicy = false;
    for (auto move : *search.search(pc, context->generate('I'), 0)) {
        auto after = pc;
        const int clear = move->attach(context, after);
        if (after.count != 0) continue;
        const auto evaluated = baseline.eval(move, after, pc, clear);
        B::Evaluation::Status previous;
        previous.b2b = 11;
        previous.combo = 3;
        const m_tetris::TetrisContext::Env env{"I", 1, 'I', 'O', false};
        for (std::size_t depth = 0; depth <= 48; ++depth) {
            auto oldMove = move, newMove = move;
            const auto a = baseline.get(oldMove, evaluated, depth, previous, env);
            const auto b = timed.get(newMove, evaluated, depth, previous, env);
            assert(state(a) == state(b) && oldMove == newMove);
            const double expected = a.value + (std::pow(0.96, double(depth)) - 1.0) *
                (policy.b2b * a.b2b + (a.b2b ? 300.0 : 0.0));
            assert(b.value == expected);
            if (depth == 0) assert(a.value == b.value);
        }
        checkedPolicy = true;
        break;
    }
    assert(checkedPolicy);
    std::cout << "time B2B transitions=" << checks << " pc=" << pcChecks
              << " spin_garbage=" << spinGarbage << " rise=" << rises
              << " death=" << deaths << " release=" << releases
              << " absorbed=" << absorbed << " discounted=" << changed
              << " max_formula_rounding=" << maximumFormulaError << '\n';
}
