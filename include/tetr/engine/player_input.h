#ifndef TETR_ENGINE_PLAYER_INPUT_H
#define TETR_ENGINE_PLAYER_INPUT_H

#include "tetr/engine/movement_controller.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/input/action.h"
#include "tetr/runtime/movement_restrictions.h"

#include <functional>
#include <chrono>
#include <optional>
#include <vector>

namespace Input { struct Services; }

namespace Engine {

enum class PlayerInputCommand {
    None,
    HardDrop,
    Hold,
    Retry,
    OpenSettings,
    Undo
};

struct PlayerInputConfig {
    bool autoplay = false;
    bool holdAlreadyUsed = false;
    bool holdEnabled = true;
    double softDropFactor = 1.0;
    Runtime::GravityInterval gravityInterval = Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(1.0));
    Runtime::Duration das{};
    Runtime::Duration arr{};
    int lockResetLimit = 0;
    Runtime::MovementRestrictions movement;
    Runtime::Duration dcd{};
    bool softDropFixed = false;
    bool disableUndo = false;
    bool disableRetry = false;
};

struct PlayerInputResult {
    PlayerInputCommand command = PlayerInputCommand::None;
    int scoreDelta = 0;
};

std::vector<int> collect_action_keys(
    const Input::BindingSet& bindings,
    const std::vector<Input::Action>& actions);

PlayerInputResult process_player_input_frame(
    MovementInputState& input,
    MovementController& movement,
    const Input::BindingSet& bindings,
    const std::vector<int>& rotateKeys,
    const std::vector<int>& moveKeys,
    const PlayerInputConfig& config,
    const std::function<bool(int)>& keyDown,
    const std::function<Runtime::TimePoint()>& now,
    // Effects cannot predate the piece, but held time must never be promoted
    // to its spawn time when the physical input sample is older.
    std::optional<Runtime::TimePoint> repeatThrough = std::nullopt);

PlayerInputResult process_buffered_player_input_frame(
    MovementInputState& input, MovementController& movement,
    Input::Services& services, const std::vector<int>& rotateKeys,
    const std::vector<int>& moveKeys, const PlayerInputConfig& config,
    Runtime::TimePoint now,
    const std::function<Runtime::TimePoint(std::chrono::steady_clock::time_point)>& eventTime,
    const std::function<void()>& afterStep = {});

} // namespace Engine

#endif
