#ifndef TETR_ENGINE_MOVEMENT_PRESENTATION_H
#define TETR_ENGINE_MOVEMENT_PRESENTATION_H
#include "tetr/runtime/movement_restrictions.h"

#include "movement_controller.h"
#include "tetr/runtime/time.h"

#include <cstddef>
#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace Engine {

enum class MovementScriptCommand {
    Invalid,
    HardDrop,
    Rotate180,
    RotateCW,
    RotateCCW,
    SoftDropStep,
    SoftDropToFloor,
    LeftStep,
    LeftToWall,
    RightStep,
    RightToWall,
    CwLeft, CwRight, CcwLeft, CcwRight, HalfLeft, HalfRight,
    CwLeftToWall, CwRightToWall, CcwLeftToWall, CcwRightToWall, HalfLeftToWall, HalfRightToWall,
    GravityStep,
    WaitForLock // Natural lock at the floor, without a hard-drop input.
};

MovementScriptCommand decode_movement_script_command(char command);
char movement_script_command_char(MovementScriptCommand command);
std::array<MovementScriptCommand,2> movement_script_components(MovementScriptCommand command);
inline constexpr std::string_view kMovementSearchCommands = "sSVlrLRXAZBCDEFGbcdefg";
bool movement_script_command_allowed(MovementScriptCommand command,
    const Runtime::MovementRestrictions& restrictions, bool wallOnlyHorizontal,
    bool gravityEnabled, bool instantGravity, bool instantSoftDrop);
using MovementScriptRecorder = std::function<void(char,int,int)>;

struct MovementScriptEffect {
    bool changed = false;
    bool terminate = false;
};

// Applies one command emitted by Bot::getop. This deliberately does not apply
// EPLD reset accounting: the old low-PPS bot animation was presentation of an
// already chosen placement, not ordinary player input.
// WaitForLock reports grounded completion without a key event; the route
// planner/caller checks that gravity is enabled and waits for the lock delay.
MovementScriptEffect apply_movement_script_command(
    MovementState& state,
    MovementScriptCommand command,
    const MovementController::CanPlace& canPlace,
    const MovementController::TryRotate& tryRotate,
    bool zeroDas = false,
    bool zeroArr = false,
    const Runtime::MovementRestrictions& restrictions = {},
    bool instantGravity = false,
    bool instantSoftDrop = false,
    const MovementScriptRecorder& record = {});

// Standalone gravity settlement for callers without a script frame.
bool apply_presentation_instant_gravity(
    MovementState& state,
    const MovementController::CanPlace& canPlace);

class MovementScriptPlayback {
public:
    MovementScriptPlayback(std::string script, Runtime::TimePoint startTime, Runtime::Duration stepInterval);

    bool finished() const;
    bool commandDue(Runtime::TimePoint now) const;
    MovementScriptCommand consume();

private:
    std::string script_;
    std::size_t index_ = 0;
    Runtime::TimePoint startTime_{};
    Runtime::Duration stepInterval_{};
};

} // namespace Engine

#endif
