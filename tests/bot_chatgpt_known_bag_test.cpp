#include "../src/bot/chatgpt/known_bag_preview.h"
#include "tetr/bot/bot_chatgpt.h"
#include "tetr/runtime/piece.h"

#include <cassert>
#include <limits>
#include <string>
#include <vector>

namespace {

#ifdef TETR_CHATGPT_KNOWN_BAG
constexpr bool candidateEnabled = true;
#else
constexpr bool candidateEnabled = false;
#endif

void check_tail_deduction() {
    const std::string visible = "JLOSZ";
    for (const auto& unseen : {std::vector<int>{}, {0, 5}, {0, 0},
                              {-1}, {7}, {std::numeric_limits<int>::max()}}) {
        auto preview = visible;
        assert(Bot_chatgpt::append_known_bag_preview(preview, unseen, true) == -1);
        assert(preview == visible);
    }
    for (int inferred : {0, 5}) {
        const std::vector<int> unseen{inferred};
        const auto originalUnseen = unseen;
        auto preview = visible;
        assert(Bot_chatgpt::append_known_bag_preview(preview, unseen, true) == inferred);
        assert(preview == visible + (inferred == 0 ? "I" : "T"));
        assert(preview.size() == visible.size() + 1);
        assert(unseen == originalUnseen);

        preview = visible;
        assert(Bot_chatgpt::append_known_bag_preview(preview, unseen, false) == -1);
        assert(preview == visible); // With-replacement randomizer has no deduction.
    }
    // Once the preview ends at a bag boundary the complement is empty. No
    // order may be invented for the new bag or carried over from a prior root.
    auto newBagPreview = visible;
    assert(Bot_chatgpt::append_known_bag_preview(newBagPreview, {}, true) == -1);
    assert(newBagPreview == visible);
}

void check_bot_boundary() {
    Bot::Environment environment;
    environment.gravityIntervalSeconds = 0.6;
    environment.autoplaySearchWidth = 3;
    assert(Bot_chatgpt::init(environment));
    Runtime::PlayerSessionState player;
    Runtime::Playfield field;
    field.initialize(10, 20);
    field.clear();

    auto choose = [&](const std::vector<int>& visible, const std::vector<int>& unseen,
                      int hold, int inferred) {
        const auto visibleBefore = visible;
        const auto unseenBefore = unseen;
        const auto hiddenBefore = player.pieces.bag;
        const auto randomBefore = player.pieces.random;
        int row = 0, col = 0, rotation = 0, kick = 0;
        const int selected = Bot_chatgpt::getpos(environment, player, field, visible, unseen,
            -2, 4, 0, hold, row, col, rotation, kick);
        // Even when the inferred family would be attractive, HOLD must select
        // its existing piece or the FIRST visible next, never the inferred tail.
        assert(selected == visible.front() ||
               (hold == -1 && selected == visible[1]) ||
               (hold >= 0 && selected == hold));
        const auto piece = Piece::get_definition(Piece::Type::from_index(selected));
        const Piece::Pose pose{{row, col}, Runtime::Rotation::from_index(rotation)};
        assert(piece.can_place(pose, field) && !piece.can_place(pose.translated(1, 0), field));
        const auto diagnostic = Bot_chatgpt::last_diagnostics();
        assert(diagnostic.rootNext == visibleBefore);
        assert(diagnostic.rootUnseen == unseenBefore);
        assert(diagnostic.rootHold == hold);
        assert(diagnostic.inferredNextPiece == (candidateEnabled ? inferred : -1));
        assert(visible == visibleBefore && unseen == unseenBefore);
        assert(player.pieces.bag == hiddenBefore && player.pieces.random == randomBefore);
        return selected;
    };

    for (int inferred : {0, 5}) {
        // Current and immediate next deliberately differ from the deduced tail.
        const std::vector<int> visible{inferred == 0 ? 5 : 0, 1, 2, 3, 4, 6};
        for (int hold : {-2, -1, 4}) {
            assert(choose(visible, {inferred}, hold, inferred) != inferred);
        }
        // The hidden queue and RNG deliberately disagree with the supplied
        // singleton. Neither is a source of additional search information.
        player.pieces.bag = {{Piece::Type::from_index(6), true},
                             {Piece::Type::from_index(6), false}};
        player.pieces.nextPreview = {Piece::Type::from_index(6)};
        player.pieces.allowedPieces = {Piece::Type::from_index(6)};
        player.pieces.random.seed(42 + inferred);
        assert(choose(visible, {inferred}, -1, inferred) != inferred);
        choose(visible, {}, -1, -1); // New bag: reset diagnostic, do not append.
        choose(visible, {0, 5}, -1, -1);
        choose(visible, {-1}, -1, -1);
        choose(visible, {7}, -1, -1);
    }

    // Existing unsupported environments remain rejected before inference.
    for (bool randomPieces : {false, true}) {
        environment.randomPieces = randomPieces;
        environment.quickPlayFool = randomPieces ? 0 : 1;
        int row = 0, col = 0, rotation = 0, kick = 0;
        assert(Bot_chatgpt::getpos(environment, player, field, {5, 1, 2, 3, 4, 6}, {0},
            -2, 4, 0, -1, row, col, rotation, kick) == -1);
        assert(Bot_chatgpt::last_diagnostics().inferredNextPiece == -1);
    }
}

} // namespace

int main() {
    check_tail_deduction();
    check_bot_boundary();
}
