#ifndef TETR_ENGINE_AUTOPLAY_ADAPTER_H
#define TETR_ENGINE_AUTOPLAY_ADAPTER_H
#include "tetr/runtime/piece.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/rules/game_result.h"
#include "tetr/runtime/gravity.h"
#include <functional>
#include <memory>
namespace Runtime { class Playfield; class GameContext; struct PlayerSessionState; }
namespace Engine {
class MovementInputState;
bool initialize_autoplay(Runtime::GameContext& context);
struct AutoplayCarryState {
    Piece::Pose pose{};
    Runtime::RotationRecord lastRotation{};
    Piece::Type selectedPieceType=Piece::Type::none();
};
struct AutoplayStepResult { Rules::PlayResult signal=Rules::PlayResult::ContinueFrame; bool hardDrop=false; bool locked=false; };
AutoplayStepResult run_handling_autoplay_step(Runtime::GameContext& context,Runtime::Playfield& field,
    Piece::Definition& piece,Runtime::PlayerSessionState& player,MovementState& movement,
    MovementInputState& input,const std::function<Rules::PlayResult()>& pumpFrame,
    AutoplayCarryState* carry=nullptr,bool heldPieceContinuation=false);
// Plans and presents immediately, then waits only for the remaining wall-time
// PPS period. A Hold continuation shares that same placement deadline.
AutoplayStepResult run_autoplay_step(Runtime::GameContext& context,Runtime::Playfield& field,Piece::Definition& piece,
    Runtime::PlayerSessionState& player,MovementState& movementState,AutoplayCarryState& carry,
    bool heldPieceContinuation,double pps,bool holdEnabled,Runtime::FoolMode foolMode,Runtime::GravityInterval gravityInterval,
    const MovementController::CanPlace& canPlace,const MovementController::TryRotate& tryRotate,
    const std::function<Rules::PlayResult()>& pumpFrame);
}
#endif
