#include "../src/bot/chatgpt/analysis/spin_paths.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <random>

namespace A = Bot_chatgpt::Analysis;
int main() {
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Bot_chatgpt::Evaluation, Bot_chatgpt::Search>;
    Engine geometry; assert(geometry.prepare(10, 40));
    const auto* context = geometry.context().get();
    A::Moves moves(context);
    Runtime::Playfield tower; tower.initialize(10, 20); tower.clear();
    for (int y = 15; y < 20; ++y) tower.set_cell(y, 0, Runtime::BoardCell::garbage());
    const auto map = BotNativeHost::make_map(tower);
    const auto* target = context->generate('O');
    while (target->move_left && target->move_left->check(map)) target = target->move_left;
    target = target->drop(map);
    const auto& candidates = moves.enumerate(map, context->generate('O'));
    assert(std::any_of(candidates.begin(), candidates.end(), [&](const auto& l) {
        return l->index_filtered == target->index_filtered;
    }));
    // Full enumeration includes all constructive high translate/rotate/drop
    // placements, and every returned spin variant is reachable by host BFS.
    std::mt19937 random(20260926);
    for (int sample = 0; sample < 3; ++sample) {
        Runtime::Playfield field; field.initialize(10, 20); field.clear();
        for (int x = 0; x < 10; ++x)
            for (int y = 0, h = random() % 8; y < h; ++y)
                field.set_cell(19 - y, x, Runtime::BoardCell::garbage());
        const auto board = BotNativeHost::make_map(field);
        for (char type : std::string("IJLOSTZ")) {
            std::set<std::size_t> found;
            const auto* origin = context->generate(type);
            for (const auto& landing : moves.enumerate(board, origin)) {
                found.insert(landing->index_filtered);
                Piece::Pose answer; int kick;
                assert(BotNativeHost::reachable(landing.node,
                    Piece::get_definition(Piece::Type::from_index(Bot_chatgpt::piece_index(type))),
                    field, Bot_chatgpt::host_pose(*origin), landing.spin, landing.mini, answer, kick));
            }
            for (int rotation = 0; rotation < 4; ++rotation) {
                auto* air = context->get(type, 3, 22, rotation);
                while (air->move_left) air = air->move_left;
                for (; air; air = air->move_right)
                    if (air->check(board)) assert(found.count(air->drop(board)->index_filtered));
            }
        }
    }
    auto root = Bot_chatgpt::RootSnapshot::parse(
        "TETR_BOT_DIAGNOSTIC trace_version=1 root_hold=3 root_row=-2 root_col=4 root_rotation=0 "
        "root_b2b=5 root_combo=1 root_garbage_height=6 pieces=19 incoming=0 ready=0 root_cleared=1 "
        "opener_double=1 forecast_step=1 root_next=2,1,4,6,0,5 root_packets=- selected=2 row=13 col=2 rotation=3 "
        "board=1019,511,511,1015,1015,1019,931,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
    const A::LiveMove live{2, 13, 2, 3, 0, 0, 1, 4, 0};
    A::SpinPaths probe(context);
    auto forced = probe.run(root, live, 5, 32, A::Mode::LiveFirst);
    auto keep = probe.run(root, live, 5, 32, A::Mode::Preserve);
    assert(forced.liveMatched && forced.best && forced.best->broken);
    assert(keep.twoSpins && !keep.twoSpins->broken && keep.twoSpins->spins >= 2);
    probe.verify(root, *forced.best); probe.verify(root, *keep.twoSpins);
    const auto again = probe.run(root, live, 5, 32, A::Mode::Preserve);
    assert(again.twoSpins && again.twoSpins->status.value == keep.twoSpins->status.value);
    assert(again.work.evaluated == keep.work.evaluated);
    auto tampered = *keep.twoSpins;
    ++tampered.path[0].attack;
    bool rejected = false;
    try { probe.verify(root, tampered); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
    auto choiceRoot = root; choiceRoot.next = {5, 0, 2};
    auto c = A::choices(choiceRoot, 0, -1);
    assert(c.size() == 2 && c[1].cursor == 2 && c[1].piece == 0 && c[1].hold == 5);
    assert(A::choices(choiceRoot, 0, -2).size() == 1);
    assert(A::choices(choiceRoot, 0, 5).size() == 1);
    assert(A::choices(choiceRoot, 3, 0).empty()); // Never play hidden current via HOLD.
    // No-clear with ready garbage must censor, not continue through invented holes.
    root.status.incoming = root.status.readyIncoming = 100;
    root.packets = {{100, -1}};
    auto pressure = probe.run(root, live, 5, 16, A::Mode::Preserve);
    assert(pressure.work.unknownRiseCuts > 0);
    for (auto* result : {&pressure.best, &pressure.twoSpins, &pressure.prefix})
        if (*result) { assert((*result)->status.risen == 0); probe.verify(root, **result); }
    // Retrospective development fixture: 0402, 131 already placed pieces.
    // A worse-looking S setup admits two Spin clears with a lower peak/risk.
    // This is a finite-horizon diagnostic regression, NOT a win guarantee.
    auto setup = Bot_chatgpt::RootSnapshot::parse(
        "TETR_BOT_DIAGNOSTIC trace_version=1 root_hold=4 root_row=-2 root_col=4 root_rotation=0 "
        "root_b2b=1 root_combo=2 root_garbage_height=13 pieces=131 incoming=0 ready=0 root_cleared=1 "
        "opener_double=1 forecast_step=1 root_next=2,5,4,6,2,3 root_packets=- selected=4 row=6 col=6 rotation=0 "
        "board=1021,895,767,1007,959,895,895,895,895,895,895,895,767,911,256,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
    const A::LiveMove setupLive{4,6,6,0,0,0,0,0,1};
    auto setupForced = probe.run(setup, setupLive, 6, 256, A::Mode::LiveFirst);
    auto setupKeep = probe.run(setup, setupLive, 6, 256, A::Mode::Preserve);
    assert(setupForced.best && setupKeep.twoSpins);
    auto& original = *setupForced.best;
    auto& alternative = *setupKeep.twoSpins;
    probe.verify(setup, original); probe.verify(setup, alternative);
    assert(alternative.status.value > original.status.value);
    assert(alternative.status.attack >= original.status.attack && alternative.status.b2b >= original.status.b2b);
    assert(alternative.peakRoof < original.peakRoof && alternative.map.roof <= original.map.roof);
    assert(Bot_chatgpt::covered_empty_cells(alternative.map) <= Bot_chatgpt::covered_empty_cells(original.map));
    assert(alternative.status.pathRisk < original.status.pathRisk);
    assert(alternative.path.front().value < original.path.front().value);
    for (const auto& step : alternative.path) assert(step.productionGeometry && step.productionSpin);
}
