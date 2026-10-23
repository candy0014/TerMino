#ifndef TETR_ENGINE_AUTOPLAY_PATH_H
#define TETR_ENGINE_AUTOPLAY_PATH_H
#include "tetr/engine/movement_presentation.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/runtime_config.h"
#include <optional>
#include <vector>
namespace Engine {
// A geometric route with presentation delays. Playback advances no virtual
// keys, periodic gravity, DAS charge timers or lock timers during movement.
// A terminal WaitForLock only waits out the configured lock delay at the floor.
struct AutoplayTimedStep {
    MovementScriptCommand command=MovementScriptCommand::Invalid;
    MovementState movement;
    Runtime::Duration delay;
    int scoreDelta=0;
};
struct AutoplayPath {
    std::string script;
    std::vector<AutoplayTimedStep> steps;
    Runtime::Duration duration;
    MovementState finalMovement;
};
struct AutoplaySpinRules {
    Runtime::SpinBonuses bonuses=Runtime::SpinBonuses::AllMiniPlus;
    bool allSpinEnabled=false;
    bool noHoldReverse=false;
    bool suppressNonTSpins=false;
};
AutoplaySpinRules autoplay_spin_rules(const Runtime::RuntimeConfig& config);
// The actual kick record may differ from the BOT's record if the resulting
// non-spin / mini / full-spin classification at the same landing is unchanged.
bool autoplay_landing_matches(const Runtime::Playfield& field,const Piece::Definition& piece,
    const MovementState& actual,Piece::Pose target,Runtime::RotationRecord rotation,
    const AutoplaySpinRules& spinRules={});
std::optional<AutoplayPath> make_autoplay_path(const Runtime::Playfield& field,const Piece::Definition& piece,
    MovementState start,const Runtime::HandlingConfig& handling,const Runtime::MovementRestrictions& restrictions,
    Runtime::GravityInterval gravity,std::string_view script,bool handlingPresentation=true);
// Shared by Bot::getop and the handling presenter. Handling changes the route
// to an already selected landing, never BOT placement candidates or scores.
std::optional<AutoplayPath> search_autoplay_path(const Runtime::Playfield& field,const Piece::Definition& piece,
    MovementState start,Piece::Pose target,Runtime::RotationRecord rotation,
    const Runtime::HandlingConfig& handling,const Runtime::MovementRestrictions& restrictions,
    Runtime::GravityInterval gravity,bool handlingPresentation=false,
    const std::function<bool()>& cancelled={},const AutoplaySpinRules& spinRules={},int maxExpanded=0);
// Retain the selected column/orientation across vertical board motion, then
// settle into the CURRENT holes and prove a route from the CURRENT live pose.
std::optional<AutoplayPath> repair_autoplay_path(const Runtime::Playfield& field,
    const Piece::Definition& piece,MovementState start,Piece::Pose& target,
    Runtime::RotationRecord& rotation,int raised,const Runtime::HandlingConfig& handling,
    const Runtime::MovementRestrictions& restrictions,Runtime::GravityInterval gravity,
    bool timed,const AutoplaySpinRules& spinRules={},std::string_view preferredScript={});
}
#endif
