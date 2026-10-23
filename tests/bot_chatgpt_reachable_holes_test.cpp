#include "tetr/util/bit_ops.h"
#include "../src/bot/chatgpt/key_piece_evaluation.h"
#include "../src/bot/chatgpt/analysis/spin_paths.h"
#include "../src/bot/chatgpt/analysis/original_terrain_features.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <tuple>

namespace TetrTests::bot_chatgpt_reachable_holes_test {
namespace B = Bot_chatgpt;
using Map = m_tetris::TetrisMap;
static Map board(const std::vector<unsigned>& rows) {
    Map map(10, 40);
    for (int y = 0; y < int(rows.size()); ++y) {
        map.row[y] = rows[y];
        for (int x = 0; x < 10; ++x) if (rows[y] & (1u << x)) {
            map.top[x] = y + 1; map.roof = y + 1; ++map.count;
        }
    }
    return map;
}
static auto physical(const B::Evaluation::Status& s) {
    return std::make_tuple(s.combo, s.b2b, s.garbageHeight, s.incoming, s.readyIncoming, s.risen,
        s.dead, s.previousCleared, s.openerDoubleCancelEnabled, s.placedPieces, s.attack, s.reward, s.pathRisk);
}
int run(int argc,char** argv) {
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::Search>;
    Engine owner; assert(owner.prepare(10, 40)); auto* context = owner.context().get();
    B::KeyPieceGeometry paths; paths.init(context);
    B::ReachableHoles holes; holes.init();
    const auto sidePocket = board({999, 1015, 1015, 1015});
    const auto sealed = board({1007, 1015, 1015, 1015});
    const auto tsd = board({1007, 967, 8});
    const auto mini = board({1007,1007,1007,1007,1007,1007,895,991,799,831,543,903});
    const auto pocket = holes.analyze(sidePocket, false, paths);
    assert(pocket.cells.size() == 1);
    assert(pocket.cells[0].x == 4 && pocket.cells[0].y == 0 && pocket.cells[0].covers == 3);
    assert(pocket.cells[0].connected && pocket.cells[0].families == 0);
    // At least one final piece footprint fits this side pocket. Only a real
    // spawn-to-lock route can reject it, not collision checks at the endpoint.
    int fitting = 0;
    for (char t : std::string("IJLOSTZ")) for (int r = 0; r < 4; ++r)
        for (int x = -4; x <= 10; ++x) for (int y = 0; y < 5; ++y) {
            auto* node = context->get(t, x, y, r);
            if (node && node->row == 0 && (node->data[0] & 16) && node->check(sidePocket) &&
                (!node->move_down || !node->move_down->check(sidePocket))) ++fitting;
        }
    assert(fitting > 0);
    const auto shut = holes.analyze(sealed, false, paths);
    assert(shut.cells.size() == 1 && !shut.cells[0].connected && !shut.cells[0].families);
    const auto a = holes.evaluate(sidePocket, false, "IJLOSTZ", ' ', true, paths);
    const auto b = holes.evaluate(sealed, false, "IJLOSTZ", ' ', true, paths);
    assert(a.penalty == 640 && a.penalty == b.penalty);
    assert(a.connectedBlocked == 1 && b.disconnected == 1);
    // Coverage counts actual solid cells, not absolute elevation or only the
    // nearest lid. Elevating the same closed pocket cannot increase this term.
    assert(holes.evaluate(board({1023,1007,1015,1015,1015}), false, "T", ' ', false, paths).penalty == b.penalty);
    assert(holes.evaluate(board({1007,1015,1015,1015,16}), false, "T", ' ', false, paths).penalty == b.penalty + 80);

    B::Analysis::Moves reference(context);
    std::mt19937 random(20260928);
    int checked = 0, reachable = 0, multiple = 0, witnesses = 0;
    for (int sample = 0; sample < 80; ++sample) {
        Map map = sample % 3 == 0 ? tsd : sample % 3 == 1 ? mini : sidePocket;
        std::vector<unsigned> rows(map.row, map.row + 40);
        if (sample >= 3) for (int i = 0; i < 4; ++i) {
            const int y = random() % std::max(1, map.roof), x = random() % 10;
            rows[y] ^= 1u << x;
            if (rows[y] == 1023) rows[y] ^= 1u << x;
        }
        if (sample >= 70) {
            // High boards exercise normal spawn versus clear-enabled Clutch.
            rows.resize(14); rows.insert(rows.begin(), 16, 1022u); rows.resize(40);
        }
        map = board(rows);
        const bool clutch = sample % 2;
        const auto data = holes.analyze(map, clutch, paths);
        const auto features = B::Analysis::original_terrain_features(map);
        assert(data.legacyPenalty == 380 * features[3] + 40 * features[5] + features[6]);
        std::array<std::array<unsigned, 10>, 40> fills{};
        for (int t = 0; t < 7; ++t) {
            auto* spawn = B::spawn_node(context, map, "IJLOSTZ"[t], clutch);
            for (auto move : reference.enumerate(map, spawn)) {
                for (int y = 0; y < move->height; ++y) for (int x = 0; x < 10; ++x)
                    if (move->data[y] & (1u << x)) fills[move->row + y][x] |= 1u << t;
                if (sample < 3 && move.spin) {
                    Runtime::Playfield field; field.initialize(10,20); field.clear();
                    for (int y = 0; y < 40; ++y) for (int x = 0; x < 10; ++x)
                        if (map.full(x,y)) field.set_cell(19-y,x,Runtime::BoardCell::garbage());
                    Piece::Pose answer; int kick;
                    assert(BotNativeHost::reachable(move.node, Piece::get_definition(Piece::Type::from_index(t)),
                        field, B::host_pose(*spawn), move.spin, move.mini, answer, kick));
                    ++witnesses;
                }
            }
        }
        for (const auto& cell : data.cells) {
            assert(cell.families == (cell.connected ? fills[cell.y][cell.x] : 0));
            ++checked; reachable += cell.families != 0; multiple += Util::popcount32(cell.families) > 1;
            if (!cell.families) continue;
            const char t = "IJLOSTZ"[Util::trailing_zeros32(cell.families)];
            const char other = t == 'O' ? 'I' : 'O';
            const auto immediate = B::ReachableHoles::distances(std::string(1,t), ' ', false);
            const auto later = B::ReachableHoles::distances(std::string(4,other) + t, ' ', false);
            auto unknown = immediate; unknown.fill(-1);
            auto oneType = cell; oneType.families &= -oneType.families;
            assert(B::ReachableHoles::cell_penalty(oneType, immediate) < B::ReachableHoles::cell_penalty(oneType, later));
            assert(B::ReachableHoles::cell_penalty(oneType, later) < B::ReachableHoles::cell_penalty(oneType, unknown));
            assert(B::ReachableHoles::cell_penalty(cell, unknown) <= B::ReachableHoles::cell_penalty(oneType, unknown));
            assert(B::ReachableHoles::cell_penalty(cell, unknown) < B::ReachableHoles::blockedBase);
        }
        const auto score = holes.evaluate(map, clutch, "ZLTOIJ", 'S', true, paths);
        const auto hits = holes.statistics().boardHits;
        assert(holes.evaluate(map, clutch, "ZLTOIJ", 'S', true, paths).penalty == score.penalty);
        assert(holes.statistics().boardHits > hits);
        holes.set_cache_enabled(false); paths.set_cache_enabled(false);
        const auto cold = holes.evaluate(map, clutch, "ZLTOIJ", 'S', true, paths);
        assert(cold.penalty == score.penalty && cold.legacyPenalty == score.legacyPenalty);
        holes.set_cache_enabled(true); paths.set_cache_enabled(true);
    }
    assert(reachable > 0 && multiple > 0 && witnesses > 0);
    assert(B::ReachableHoles::distances("OT", ' ', true)[B::piece_index('T')] == 0);
    assert(B::ReachableHoles::distances("OT", ' ', false)[B::piece_index('T')] == 1);
    assert(B::ReachableHoles::distances("OO", 'T', true)[B::piece_index('T')] == 0);
    assert(B::ReachableHoles::distances("OO", 'T', false)[B::piece_index('T')] == -1);
    assert(B::ReachableHoles::distances("", 'T', true)[B::piece_index('T')] == -1);

    B::KeyPieceEvaluation candidate; candidate.init(context); candidate.set_key_state(false);
    candidate.set_feature_weights(1, 1); // Full-weight reference for exact residual-hole fixtures.
    B::Evaluation baseline; baseline.init(context);
    candidate.set_garbage_forecast({}, 1); baseline.set_garbage_forecast({}, 1);
    B::Evaluation::Status previous; previous.b2b = 5; previous.placedPieces = 30;
    // No root keys/preparations exist in the bank. These opportunities must
    // be found on the current branch, including a new T Spin below.
    candidate.begin_search(board({}), "OIT", ' ', false);
    assert(candidate.reserved_count() == 0);
    for (bool residualLid : {false, true}) {
        std::vector<unsigned> rows{991,1022,1022,1022,1022};
        if (residualLid) rows.push_back(32);
        const auto map = board(rows);
        assert(candidate.hole_score(map, false, "I", ' ').disconnected == 1);
        auto move = B::Landing(context->generate('O'));
        auto ev = baseline.eval(move, map, map, 0); ev.before = nullptr;
        const m_tetris::TetrisContext::Env env{"I",1,'O',' ',false};
        const auto plain = baseline.get(move, ev, 3, previous, env);
        const auto actual = candidate.get(move, ev, 3, previous, env);
        const auto projection = candidate.last_projection();
        assert(physical(actual) == physical(plain));
        assert(projection.key && projection.key->status.t == 'I' && projection.lines == 4);
        assert(projection.discoveredDuringSearch);
        auto after = map; auto key = projection.key; const int cleared = key->attach(context, after);
        const auto remaining = candidate.hole_score(after, true, "", ' ');
        assert(remaining.penalty == (residualLid ? 480 : 0));
        const auto keyEv = baseline.eval(key, after, map, cleared);
        const m_tetris::TetrisContext::Env exhausted{nullptr,0,'I',' ',false};
        const auto expected = baseline.get(key, keyEv, 4, plain, exhausted);
        assert(std::abs(actual.value - (expected.value + remaining.adjustment())) < 1e-8);
        // An already-risen unknown substrate only affects the known geometry's
        // spawn/access checks, not the old absolute-elevation cover refund.
        assert(candidate.hole_score(map, false, "I", ' ', 2).penalty == candidate.hole_score(map, false, "I", ' ').penalty);
    }
    // New defaults blend the residual hole correction, then independently
    // discount the realizable key gain. Physical attack must remain untouched.
    {
        B::KeyPieceEvaluation weighted; weighted.init(context); weighted.set_key_state(false);
        weighted.set_garbage_forecast({}, 1); weighted.begin_search(board({}), "OI", ' ', false);
        const auto map = board({991,1022,1022,1022,1022,32});
        auto placed = B::Landing(context->generate('O'));
        auto evaluated = baseline.eval(placed, map, map, 0); evaluated.before = nullptr;
        const m_tetris::TetrisContext::Env upcoming{"I",1,'O',' ',false};
        const auto normal = baseline.get(placed,evaluated,3,previous,upcoming);
        const auto half = weighted.get(placed,evaluated,3,previous,upcoming);
        weighted.set_feature_weights(0.5,0);
        const auto holesOnly = weighted.get(placed,evaluated,3,previous,upcoming);
        assert(std::abs(holesOnly.value-normal.value-0.5*weighted.hole_score(map,false,"I",' ').adjustment())<1e-8);
        assert(!weighted.last_projection().key);
        weighted.set_feature_weights(0.5,1);
        const auto undiscounted = weighted.get(placed,evaluated,3,previous,upcoming);
        assert(undiscounted.value > holesOnly.value);
        assert(std::abs(half.value-(holesOnly.value+0.5*(undiscounted.value-holesOnly.value)))<1e-8);
        assert(physical(half)==physical(normal) && physical(undiscounted)==physical(normal));
        weighted.set_feature_weights(0,0);
        assert(weighted.get(placed,evaluated,3,previous,upcoming).value==normal.value);
    }
    auto move = B::Landing(context->generate('O'));
    auto ev = baseline.eval(move, tsd, tsd, 0); ev.before = nullptr;
    const m_tetris::TetrisContext::Env nextT{"T",1,'O',' ',false};
    const auto spun = candidate.get(move, ev, 3, previous, nextT);
    assert(candidate.last_projection().key && candidate.last_projection().key->status.t == 'T');
    assert(candidate.last_projection().key.spin && candidate.last_projection().lines > 0);
    assert(candidate.last_projection().discoveredDuringSearch);
    assert(physical(spun) == physical(baseline.get(move, ev, 3, previous, nextT)));
    // Clearing and blocking a formerly usable route must invalidate warm data.
    const auto warm = candidate.hole_score(tsd, false, "T", ' ');
    candidate.hole_score(sidePocket, false, "T", ' ');
    assert(candidate.hole_score(tsd, false, "T", ' ').penalty == warm.penalty);
    // The native empty-HOLD callback still names the just-used T in NEXT.
    // The hole supply must consume it too, even when no preview remains.
    B::KeyPieceEvaluation::Status emptyHold = previous; emptyHold.holdEmpty = true;
    const m_tetris::TetrisContext::Env usedT{"T",1,'T','O',true};
    const auto physicalHold = baseline.get(move, ev, 3, emptyHold, usedT);
    const auto corrected = candidate.get(move, ev, 3, emptyHold, usedT);
    assert(!candidate.last_projection().key);
    assert(std::abs(corrected.value - physicalHold.value - candidate.hole_score(tsd, false, "", 'O').adjustment()) < 1e-8);
    // A later real clear exposes a NEW weak quad. The root bank is empty and
    // the branch is past the old shallow preparation window.
    const auto hiddenWeak = board({1022,1022,1022,1010,1023u ^ (15u << 5)});
    bool newWeakChecked = false;
    candidate.begin_search(board({}), "JOIT", ' ', false);
    for (auto unlock : reference.enumerate(hiddenWeak, B::spawn_node(context, hiddenWeak, 'I', false))) {
        auto after = hiddenWeak;
        const int clear = unlock->attach(context, after);
        if (clear != 1 || after.roof != 4 || after.row[3] != 1010) continue;
        const auto unlocked = baseline.eval(unlock, after, hiddenWeak, clear);
        const m_tetris::TetrisContext::Env upcoming{"OIT",3,'I',' ',false};
        candidate.set_dynamic_weak_enabled(false);
        const auto old = candidate.get(unlock, unlocked, 4, previous, upcoming);
        assert(!candidate.last_projection().key);
        candidate.set_dynamic_weak_enabled(true);
        const auto found = candidate.get(unlock, unlocked, 4, previous, upcoming);
        const auto projection = candidate.last_projection();
        assert(projection.weak && projection.discoveredDuringSearch && projection.lines == 4);
        assert(found.value > old.value && physical(found) == physical(old));
        candidate.set_cache_enabled(false);
        assert(candidate.get(unlock, unlocked, 4, previous, upcoming).value == found.value);
        candidate.set_cache_enabled(true);
        newWeakChecked = true; break;
    }
    assert(newWeakChecked);
    std::cout << "hole cells=" << checked << " fillable=" << reachable << " multi-family=" << multiple
              << " host spin witnesses=" << witnesses << "; side pocket, supply, cache, new keys and residual lids verified\n";
    return 0;
}
} // namespace TetrTests::bot_chatgpt_reachable_holes_test
