#ifndef TETR_LEGACY_BOT_STRATEGY_H
#define TETR_LEGACY_BOT_STRATEGY_H

#include "tetr/autoplay/strategy.h"

#include <memory>

namespace Legacy {

class BotStrategy final : public Autoplay::Strategy {
public:
    struct InstanceState;

    BotStrategy();
    ~BotStrategy() override;

    BotStrategy(const BotStrategy&) = delete;
    BotStrategy& operator=(const BotStrategy&) = delete;

    bool initialize(Runtime::GameContext& context) override;
    std::any capture_state() const override;
    void restore_state(const std::any& state) override;
    std::unique_ptr<Autoplay::Strategy> clone_for_search() const override;
    std::vector<Autoplay::PlacementCandidate> placement_candidates() const override;
    Piece::Type choose_position(
        Runtime::GameContext& context,
        Runtime::PlayerSessionState& player,
        Runtime::Playfield field,
        const std::vector<Piece::Type>& next,
        const std::vector<Piece::Type>& unseenBagPieces,
        Piece::Pose start,
        Autoplay::HoldInput hold,
        Piece::Pose& answer,
        Runtime::RotationRecord& answerRotation) override;
    std::string movement_script(
        Runtime::GameContext& context,
        Runtime::Playfield field,
        Piece::Definition piece,
        Piece::Pose start,
        Piece::Pose answer,
        Runtime::RotationRecord answerRotation) override;

private:
    std::unique_ptr<InstanceState> state_;
    std::vector<Autoplay::PlacementCandidate> candidates_;
};

} // namespace Legacy

#endif
