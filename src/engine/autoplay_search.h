#ifndef TETR_ENGINE_AUTOPLAY_SEARCH_H
#define TETR_ENGINE_AUTOPLAY_SEARCH_H
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/autoplay/strategy.h"
#include <any>
#include <atomic>
#include <future>
namespace Engine {
struct AutoplayDecision {
    Piece::Type selected;
    Piece::Pose pose;
    Runtime::RotationRecord rotation;
    std::optional<AutoplayPath> path;
    std::string script;
    std::any strategyState;
    int fieldShift=0;
};
// Worker owns an immutable board/context and a private strategy. Only the
// session owner accepts the result; a rise never cancels a strategic search.
std::optional<std::future<AutoplayDecision>> launch_autoplay_search(
    const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const Piece::Definition& piece,MovementState movement,
    Autoplay::HoldInput hold,const AutoplayCarryState* continuation,
    Runtime::GravityInterval gravity,bool buildPath,bool buildScript,
    const std::shared_ptr<std::atomic<bool>>& cancelled);
int autoplay_field_shift(const Runtime::PlayerSessionState& player);
}
#endif
