#include "../src/bot/chatgpt/hold_context_evaluation.h"
#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace B = Bot_chatgpt;
namespace A = Bot_chatgpt::Analysis;
using FixedEngine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,
    B::HoldContextEvaluation, B::Search>;

// An independent visible-queue cursor checks the environment of actual native
// tree callbacks, including empty HOLD delayed until a later search depth.
class Inspect : public B::HoldContextEvaluation {
public:
    struct Status : B::HoldContextEvaluation::Status { int cursor = 0; };
    const B::RootSnapshot* root = nullptr;
    mutable int checked = 0, legacyMismatch = 0, delayedEmpty = 0;
    Status get(B::Landing& landing, const Result& value, std::size_t depth,
               const Status& previous, const m_tetris::TetrisContext::Env& env) const {
        Status result;
        static_cast<B::HoldContextEvaluation::Status&>(result) =
            B::HoldContextEvaluation::get(landing, value, depth, previous, env);
        const bool consumesExtra = previous.holdEmpty && env.is_hold;
        result.cursor = previous.cursor + 1 + int(consumesExtra);
        // Native known-HOLD tail with an unknown active piece is a separate,
        // retained limitation, not falsely certified by this test.
        if (previous.cursor < int(root->next.size())) {
            const auto corrected = corrected_environment(env, previous.holdEmpty);
            const int remaining = std::max(0, int(root->next.size()) - result.cursor);
            assert(corrected.length == std::size_t(remaining));
            if (remaining) assert(B::piece_index(*corrected.next) == root->next[result.cursor]);
            else assert(corrected.next == nullptr);
            legacyMismatch += env.length != corrected.length ||
                (remaining && *env.next != *corrected.next);
            delayedEmpty += consumesExtra && depth > 0;
            ++checked;
        }
        return result;
    }
};

Runtime::Playfield blocker(const m_tetris::TetrisContext* context, char blocked, char safe) {
    const auto* spawn = context->generate(blocked);
    for (int y = 0; y < spawn->height; ++y) for (int x = 0; x < 10; ++x) {
        if (!(spawn->data[y] & (1u << x))) continue;
        Runtime::Playfield field; field.initialize(10, 20); field.clear();
        field.set_cell(19 - spawn->row - y, x, Runtime::BoardCell::garbage());
        const auto map = BotNativeHost::make_map(field);
        if (!B::spawn_node(context, map, blocked, false) && B::spawn_node(context, map, safe, false))
            return field;
    }
    assert(false && "Missing distinct spawn fixture");
    return {};
}

int main() {
    A::PlainEngine geometry; assert(geometry.prepare(10, 40));
    B::Evaluation old; old.init(geometry.context().get());
    B::HoldContextEvaluation fixed; fixed.init(geometry.context().get());
    const char preview[] = "IT";
    m_tetris::TetrisContext::Env env{preview, 2, 'I', 'L', true};
    auto adjusted = B::HoldContextEvaluation::corrected_environment(env, true);
    assert(adjusted.length == 1 && *adjusted.next == 'T');
    assert(adjusted.hold == 'L' && adjusted.node == 'I' && adjusted.is_hold);
    assert(B::HoldContextEvaluation::corrected_environment(env, false).next == preview);
    env.is_hold = false;
    assert(B::HoldContextEvaluation::corrected_environment(env, true).next == preview);
    env.is_hold = true;
    env.length = 1;
    adjusted = B::HoldContextEvaluation::corrected_environment(env, true);
    assert(!adjusted.length && adjusted.next == nullptr);
    env.next = nullptr; env.length = 0;
    assert(!B::HoldContextEvaluation::corrected_environment(env, true).next);

    // Both errors are possible: rejecting a safe actual next piece or accepting
    // a blocked one. Confirm each fixture against the host's piece geometry.
    for (char blocked : {'I', 'T'}) {
        const char safe = blocked == 'I' ? 'T' : 'I';
        const auto field = blocker(geometry.context().get(), blocked, safe);
        const auto map = BotNativeHost::make_map(field);
        const auto hostFits = [&](char type) {
            return Piece::get_definition(Piece::Type::from_index(B::piece_index(type)))
                .can_place({{-2, 4}, Runtime::Rotation{}}, field);
        };
        assert(!hostFits(blocked) && hostFits(safe));
        B::Evaluation::Result evaluated;
        evaluated.shape = {0, 0, map.count, &map};
        B::Landing landing(geometry.context()->generate('I'));
        B::HoldContextEvaluation::Status state; state.holdEmpty = true;
        env = {preview, 2, 'I', 'L', true};
        const auto before = old.get(landing, evaluated, 0, state, env);
        const auto after = fixed.get(landing, evaluated, 0, state, env);
        assert(before.dead == !hostFits('I'));
        assert(after.dead == !hostFits('T'));
        assert(before.dead != after.dead && !after.holdEmpty);
        assert(before.attack == after.attack && before.reward == after.reward);
        assert(before.pathRisk == after.pathRisk && before.b2b == after.b2b);
        assert(std::abs((after.value - before.value) - (before.dead ? 1e9 : -1e9)) < 1e-6);
        // No extra preview consumption on nonempty HOLD or ordinary placements.
        state.holdEmpty = false;
        assert(fixed.get(landing, evaluated, 0, state, env).value == before.value);
        state.holdEmpty = true; env.is_hold = false;
        assert(fixed.get(landing, evaluated, 0, state, env).value == before.value);
        assert(fixed.get(landing, evaluated, 0, state, env).holdEmpty);
        // A terminal state cannot earn rewards or consume another preview.
        state.dead = true; state.value = -1e9; state.placedPieces = 42;
        const auto terminal = fixed.get(landing, evaluated, 2, state, env);
        assert(terminal.holdEmpty && terminal.placedPieces == 42 && terminal.value == -1e9);
    }

    int checked = 0, legacyMismatch = 0, delayed = 0;
    for (int held : {-2, -1, 4}) {
        B::RootSnapshot root;
        root.field.initialize(10, 20); root.field.clear();
        root.next = {2, 4, 0, 3, 5, 1}; root.hold = held; root.step = 1;
        root.pose = B::host_pose(*geometry.context()->generate('L'));
        using InspectedEngine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Inspect, B::Search>;
        InspectedEngine inspected(geometry.context());
        A::configure_native(inspected, root); inspected.status()->holdEmpty = held == -1;
        inspected.ai()->root = &root;
        for (int i = 0; i < 80; ++i) A::native_pass(inspected, root);
        checked += inspected.ai()->checked;
        legacyMismatch += inspected.ai()->legacyMismatch;
        delayed += inspected.ai()->delayedEmpty;
        if (held != -1) assert(inspected.ai()->legacyMismatch == 0);

        A::PlainEngine baseline(geometry.context()); FixedEngine candidate(geometry.context());
        for (int revision = 0; revision < 2; ++revision) {
            if (revision) {
                baseline.update(); candidate.update(); root.status.b2b = 5;
                root.status.incoming = root.status.readyIncoming = 4; root.packets = {{4, -1}};
            }
            A::configure_native(baseline, root); A::configure_native(candidate, root);
            candidate.status()->holdEmpty = root.hold == -1;
            for (int i = 0; i < 80; ++i) {
                const auto a = A::native_pass(baseline, root);
                const auto b = A::native_pass(candidate, root);
                assert(A::first_legal(geometry.context().get(), root, b));
                if (held != -1) {
                    assert(a.target == b.target && a.change_hold == b.change_hold);
                    assert(a.status.value == b.status.value && a.status.dead == b.status.dead);
                    assert(a.status.placedPieces == b.status.placedPieces);
                }
            }
        }
    }
    assert(checked > 1000 && legacyMismatch > 0 && delayed > 0);
    std::cout << "checked_native_contexts=" << checked << " legacy_empty_hold_mismatches="
              << legacyMismatch << " delayed_empty_holds=" << delayed << " corrected_mismatches=0\n";
}
