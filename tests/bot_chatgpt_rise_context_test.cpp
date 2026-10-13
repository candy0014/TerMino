#include "../src/bot/chatgpt/analysis/native_trace.h"
#include "../src/bot/chatgpt/analysis/spin_paths.h"
#include "../src/bot/chatgpt/rise_context_evaluation.h"
#include "tetr/engine/garbage_field.h"
#include <cassert>
#include <iostream>

namespace B = Bot_chatgpt;
namespace A = Bot_chatgpt::Analysis;

Runtime::Playfield raised(Runtime::Playfield field, int rise, int hole) {
    Engine::shift_field_up_for_garbage(10, -20, 20, rise,
        [&](int row, int col) { return field.cell(row, col); },
        [&](int to, int from) { field.copy_row(to, from); });
    std::vector<Runtime::BoardCell> row(10, Runtime::BoardCell::garbage());
    row[hole] = Runtime::BoardCell{};
    for (int y = 20 - rise; y < 20; ++y)
        Engine::write_generated_row(y, row,
            [&](int r, int c) { return field.cell(r, c); },
            [&](int r, int c, Runtime::BoardCell v) { field.set_cell(r, c, v); });
    return field;
}

int main() {
    A::PlainEngine geometry; assert(geometry.prepare(10, 40));
    auto* context = geometry.context().get();
    Runtime::Playfield field; field.initialize(10, 20); field.clear();
    // The actual O spawn is left of a tall divider after a rise. The old
    // unshifted virtual spawn sits above it and can incorrectly cross it.
    for (int row = 0; row < 20; ++row) field.set_cell(row, 6, Runtime::BoardCell::garbage());
    // A supported lip above the actual spawn also stops the old skyline drop
    // at the roof, so this route is present in the production enumerator.
    field.set_cell(0, 4, Runtime::BoardCell::garbage());
    field.set_cell(0, 5, Runtime::BoardCell::garbage());
    // A known one-hole bottom row seals the chamber for O even when the NEW
    // unknown garbage below it is optimistically empty.
    for (int col = 1; col < 10; ++col) field.set_cell(19, col, Runtime::BoardCell::garbage());
    auto before = BotNativeHost::make_map(field);
    B::RiseContextEvaluation fixed; fixed.init(context);
    B::Search search; B::Search::Config config; search.init(context, &config);
    const auto moves = *search.search(before, context->generate('O'), 1);
    B::Landing right, left;
    for (auto landing : moves) {
        if (landing->col == 7 && landing->row == 1) right = landing;
        if (landing->col == 0 && landing->row == 1) left = landing;
    }
    if (!right.node || !left.node) {
        for (const auto& landing : moves)
            std::cerr << "landing col=" << landing->col << " row=" << landing->row
                      << " low=" << landing->low << '\n';
    }
    assert(right.node && left.node);
    int counterexamples = 0;
    for (int rise : {4, 8, 12}) {
        auto after = before;
        auto oldLanding = right;
        const int lines = oldLanding->attach(context, after);
        auto evaluated = geometry.ai()->eval(oldLanding, after, before, lines);
        B::Evaluation::Status state; state.risen = rise;
        const m_tetris::TetrisContext::Env env{"O", 1, 'O', ' ', false};
        const auto predicted = geometry.ai()->get(oldLanding, evaluated, 1, state, env);
        assert(!predicted.dead && predicted.placedPieces == 1);
        const auto rejected = fixed.get(oldLanding, evaluated, 1, state, env);
        assert(rejected.dead && rejected.value == state.value - 1e9);
        assert(rejected.placedPieces == state.placedPieces && rejected.attack == state.attack);
        assert(rejected.b2b == state.b2b && rejected.reward == state.reward);
        assert(fixed.get(oldLanding, evaluated, 2, rejected, env).value == rejected.value);
        assert(fixed.continuation_possible(left, before, rise, false));
        assert(!fixed.continuation_possible(right, before, rise, true)); // Already-free spawn cannot lift.
        const auto floods = fixed.statistics().floods;
        for (int i = 0; i < 10; ++i) assert(!fixed.continuation_possible(right, before, rise, true));
        assert(fixed.statistics().floods == floods);
        for (int hole = 0; hole < 10; ++hole) {
            const auto actual = raised(field, rise, hole);
            const auto* bad = right.node;
            const auto* good = left.node;
            for (int y = 0; y < rise; ++y) { bad = bad->move_up; good = good->move_up; }
            assert(bad && good);
            const auto piece = Piece::get_definition(Piece::Type::from_index(B::piece_index('O')));
            const Piece::Pose spawn{{-2, 4}, Runtime::Rotation{}};
            assert(piece.can_place(spawn, actual));
            Piece::Pose answer; int kick = 0;
            assert(!BotNativeHost::reachable(bad, piece, actual, spawn, false, false, answer, kick));
            assert(BotNativeHost::reachable(good, piece, actual, spawn, false, false, answer, kick));
            ++counterexamples;
        }
    }
    // Same-address parent reuse with changed contents must not reuse old marks.
    assert(!fixed.continuation_possible(right, before, 8, false));
    auto open = field;
    open.set_cell(18, 6, Runtime::BoardCell{});
    open.set_cell(17, 6, Runtime::BoardCell{});
    before = BotNativeHost::make_map(open);
    assert(fixed.continuation_possible(right, before, 8, false));
    before = BotNativeHost::make_map(field);
    assert(!fixed.continuation_possible(right, before, 8, false));
    // Without the known floor, an optimistic route through the unknown area
    // exists. Do not label that uncertainty as proven unreachable.
    auto unsealed = field;
    for (int col = 0; col < 10; ++col) if (col != 6)
        unsealed.set_cell(19, col, Runtime::BoardCell{});
    auto unsealedMap = BotNativeHost::make_map(unsealed);
    auto uncertain = right; uncertain.node = right->move_down;
    assert(uncertain.node && fixed.continuation_possible(uncertain, unsealedMap, 8, false));
    fixed.set_garbage_forecast({}, 1);
    assert(fixed.statistics().checks == 0 && fixed.statistics().floods == 0);

    // Real-board landings must survive the permissive filter. Actual holes are
    // used only by this independent test oracle, never supplied to the guard.
    A::Moves full(context);
    int realLandings = 0;
    for (int rise : {4, 8}) for (int hole : {0, 4, 9}) for (bool clutch : {false, true}) {
        const auto actual = raised(field, rise, hole);
        const auto actualMap = BotNativeHost::make_map(actual);
        for (char type : std::string("IJLOSTZ")) {
            const auto* start = B::spawn_node(context, actualMap, type, clutch);
            for (auto landing : full.enumerate(actualMap, start)) {
                auto pose = B::host_pose(*landing.node); pose.position.row += rise;
                const auto* relative = B::native_pose(context, type, pose);
                if (!relative || !relative->check(before)) continue;
                landing.node = relative;
                assert(fixed.continuation_possible(landing, before, rise, clutch));
                ++realLandings;
            }
        }
    }
    assert(realLandings > 100);

    using Candidate = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::RiseContextEvaluation, B::Search>;
    for (int held : {-2, -1, 4}) {
        B::RootSnapshot root;
        root.field.initialize(10, 20); root.field.clear();
        root.next = {2, 4, 0, 3, 5, 1}; root.hold = held; root.step = 1;
        root.pose = B::host_pose(*context->generate('L'));
        A::PlainEngine baseline(geometry.context()); Candidate candidate(geometry.context());
        for (int revision = 0; revision < 2; ++revision) {
            if (revision) { baseline.update(); candidate.update(); root.status.b2b = 5; }
            A::configure_native(baseline, root); A::configure_native(candidate, root);
            for (int i = 0; i < 80; ++i) {
                const auto a = A::native_pass(baseline, root);
                const auto b = A::native_pass(candidate, root);
                assert(a.target == b.target && a.change_hold == b.change_hold);
                assert(a.status.value == b.status.value && a.status.attack == b.status.attack);
                assert(a.status.placedPieces == b.status.placedPieces);
            }
            assert(candidate.ai()->statistics().checks == 0);
        }
    }
    std::cout << "host_geometry_counterexamples=" << counterexamples
              << " old_model_accepts_unreachable_continuation=1 candidate_rejects=1"
              << " real_board_landings_preserved=" << realLandings << '\n';
}
