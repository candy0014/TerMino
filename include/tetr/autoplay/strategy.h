#ifndef TETR_AUTOPLAY_STRATEGY_H
#define TETR_AUTOPLAY_STRATEGY_H

#include "tetr/runtime/piece.h"
#include "tetr/runtime/playfield.h"

#include <string>
#include <vector>
#include <any>
#include <memory>


namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Autoplay {

struct HoldInput {
    bool enabled = true;
    Piece::Type piece = Piece::Type::none();
};
struct PlacementCandidate {
    Piece::Type type;
    Piece::Pose pose;
    Runtime::RotationRecord rotation;
    double value=0;
    double movementSeconds=0;
};

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual bool initialize(Runtime::GameContext& context) = 0;
    virtual std::any capture_state() const { return {}; }
    virtual void restore_state(const std::any&) {}
    // A search clone owns its worker state; it never reads live input/board
    // state or mutates the strategy stored in an undo snapshot.
    virtual std::unique_ptr<Strategy> clone_for_search() const { return {}; }
    virtual std::vector<PlacementCandidate> placement_candidates() const { return {}; }

    virtual Piece::Type choose_position(
        Runtime::GameContext& context,
        Runtime::PlayerSessionState& player,
        Runtime::Playfield field,
        const std::vector<Piece::Type>& next,
        const std::vector<Piece::Type>& unseenBagPieces,
        Piece::Pose start,
        HoldInput hold,
        Piece::Pose& answer,
        Runtime::RotationRecord& answerRotation) = 0;

    virtual std::string movement_script(
        Runtime::GameContext& context,
        Runtime::Playfield field,
        Piece::Definition piece,
        Piece::Pose start,
        Piece::Pose answer,
        Runtime::RotationRecord answerRotation) = 0;


};

} // namespace Autoplay

#endif
