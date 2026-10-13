#include "tetr/legacy/bot_strategy.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"

#include <cassert>
#include <vector>

namespace {

struct BotFixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};
    Legacy::BotStrategy strategy;

    BotFixture() {
        context.config.autoplay.mode = Runtime::AutoplayMode::Mech;
        context.config.handling.gravityInterval = Runtime::GravityInterval::periodic(
            Runtime::Duration::from_seconds(0.6));
        context.config.field.width = 10;
        context.config.field.height = 20;
        context.config.field.nextCount = 5;
        context.config.field.holdEnabled = true;
        context.config.field.antiGravity = false;
        context.config.rotationSystem = Runtime::RotationSystem::SrsPlus;

        auto& playfield = session.player.gameplay.playfield;
        playfield.width = 10;
        playfield.visibleHeight = 20;
        playfield.activeHeight = 20;
        playfield.nextCount = 5;
        playfield.holdEnabled = true;
        playfield.field.initialize(10, 20);
        playfield.field.clear();
    }
};

struct Decision {
    Piece::Type selected;
    Piece::Pose pose;
    Runtime::RotationRecord rotation;
};

Decision choose(BotFixture& fixture, const std::vector<Piece::Type>& sequence) {
    const Piece::Pose start{{-2, 4}, Runtime::Rotation{}};
    Decision decision{Piece::Type::none(), start, Runtime::RotationRecord::none()};
    decision.selected = fixture.strategy.choose_position(
        fixture.context,
        fixture.session.player,
        fixture.session.player.gameplay.playfield.field,
        sequence,
        {},
        start,
        Autoplay::HoldInput{true, Piece::Type::none()},
        decision.pose,
        decision.rotation);
    return decision;
}

void assert_same(const Decision& first, const Decision& second) {
    assert(first.selected == second.selected);
    assert(first.pose == second.pose);
    assert(first.rotation == second.rotation);
}

} // namespace

int main() {
    BotFixture first;
    BotFixture second;
    assert(first.strategy.initialize(first.context));
    assert(second.strategy.initialize(second.context));

    // O appears before I, selecting the same opening branch in each Mech
    // instance. Alternating calls must advance each player's own plan once,
    // rather than advancing one process-global plan twice.
    const std::vector<Piece::Type> sequence{
        Piece::Type::from_index(3),
        Piece::Type::from_index(0),
        Piece::Type::from_index(1),
        Piece::Type::from_index(2),
        Piece::Type::from_index(4),
        Piece::Type::from_index(5),
        Piece::Type::from_index(6)};

    const Decision firstOpening = choose(first, sequence);
    const Decision secondOpening = choose(second, sequence);
    assert_same(firstOpening, secondOpening);

    const Decision firstFollowup = choose(first, sequence);
    const Decision secondFollowup = choose(second, sequence);
    assert_same(firstFollowup, secondFollowup);
}
