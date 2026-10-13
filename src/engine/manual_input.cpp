#include "tetr/engine/player_input.h"
#include <algorithm>

namespace Engine {

std::vector<int> collect_action_keys(
    const Input::BindingSet& bindings,
    const std::vector<Input::Action>& actions) {
    std::vector<int> result;
    for (const auto& [key, action] : bindings.entries()) {
        if (std::find(actions.begin(), actions.end(), action) != actions.end()) result.push_back(key);
    }
    return result;
}

PlayerInputResult process_player_input_frame(
    MovementInputState& input,
    MovementController& movement,
    const Input::BindingSet& bindings,
    const std::vector<int>& rotateKeys,
    const std::vector<int>& moveKeys,
    const PlayerInputConfig& config,
    const std::function<bool(int)>& keyDown,
    const std::function<Runtime::TimePoint()>& now) {
    PlayerInputResult result;
    const bool zeroDas = config.das == Runtime::Duration{};
    const bool zeroArr = config.arr == Runtime::Duration{};
    HorizontalDropMode instantDrop = HorizontalDropMode::None;
    if (!config.autoplay) {
        if (config.gravityInterval.is_instant()) instantDrop=HorizontalDropMode::Gravity;
        if (!config.movement.disableSoftDrop &&
            soft_drop_interval(config.gravityInterval,config.softDropFactor,config.softDropFixed)==Runtime::Duration{}) {
            for (const auto& [key,action]:bindings.entries()) {
                if (action==Input::Action::SoftDrop && keyDown(key) && input.phase(key)!=KeyPhase::AwaitRelease) {
                    instantDrop=HorizontalDropMode::SoftDrop;
                    break;
                }
            }
        }
    }
    const auto horizontalDrop=instantDrop;
    struct PendingShift { int direction; Runtime::TimePoint time; bool toWall; int count; };
    std::vector<PendingShift> pendingShifts;
    const auto finishHorizontalMoves = [&] {
        for (const auto& shift:pendingShifts) {
            if (shift.toWall) {
                result.scoreDelta += movement.moveHorizontalToWall(shift.direction,shift.time,
                    config.lockResetLimit,horizontalDrop).scoreDelta;
                continue;
            }
            for (int i=0;i<shift.count;++i) {
                const auto effect=movement.moveHorizontal(shift.direction,shift.time,config.lockResetLimit,horizontalDrop);
                result.scoreDelta += effect.scoreDelta;
                if (!effect.changed) break;
            }
        }
        pendingShifts.clear();
    };
    const auto horizontalStep = [&](int direction, Runtime::TimePoint inputNow) {
        if (instantDrop!=HorizontalDropMode::None) {
            pendingShifts.push_back({direction,inputNow,false,1});
            return MovementEffect{true,0};
        }
        return movement.moveHorizontal(direction,inputNow,config.lockResetLimit);
    };
    const auto repeatHorizontal = [&](int direction, Runtime::TimePoint inputNow) {
        if (!zeroArr) return horizontalStep(direction,inputNow);
        if (horizontalDrop!=HorizontalDropMode::None) {
            // Give this frame's rotations a chance before the ARR=0 sweep
            // starts settling the piece after each cell.
            pendingShifts.push_back({direction,inputNow,true,1});
            return MovementEffect{true,0};
        }
        const auto effect=movement.moveHorizontalToWall(direction,inputNow,config.lockResetLimit,horizontalDrop);
        result.scoreDelta += effect.scoreDelta;
        return effect;
    };

    for (const auto& [key, action] : bindings.entries()) {
        const auto& restricted = config.movement;
        if ((Input::is_horizontal(action) && !restricted.allows_horizontal(Input::horizontal_delta(action))) ||
            (Input::is_rotation(action) && !restricted.allows_rotation(Input::rotation_delta(action))) ||
            (action == Input::Action::SoftDrop && restricted.disableSoftDrop) ||
            (action == Input::Action::HardDrop && restricted.disableHardDrop) ||
            (action == Input::Action::Undo && config.disableUndo)) {
            input.setPhase(key, KeyPhase::Released);
            continue;
        }
        if (config.autoplay && action != Input::Action::Retry && action != Input::Action::OpenSettings &&
            action != Input::Action::Undo) continue;

        const bool down = keyDown(key);
        if (input.consumeReleaseDebounce(key, down)) continue;
        if (input.phase(key) == KeyPhase::Released && !down) continue;

        if (down && action == Input::Action::HardDrop) {
            finishHorizontalMoves();
            const auto effect = movement.dropToFloor(2, false, now());
            input.cancelHorizontalRepeats(moveKeys,now(),config.das,config.arr,config.dcd);
            result.scoreDelta += effect.scoreDelta;
            result.command = PlayerInputCommand::HardDrop;
            return result;
        }
        if (down && action == Input::Action::Retry) {
            finishHorizontalMoves();
            result.command = PlayerInputCommand::Retry;
            return result;
        }
        if (down && action == Input::Action::OpenSettings) {
            finishHorizontalMoves();
            result.command = PlayerInputCommand::OpenSettings;
            return result;
        }
        if (input.consumeRelease(key, down)) continue;

        const Runtime::TimePoint inputNow = now();
        if (input.beginPress(key, down, inputNow)) {
            if (action == Input::Action::Undo) {
                finishHorizontalMoves();
                result.command = PlayerInputCommand::Undo;
                return result;
            }
            if (action == Input::Action::Hold) {
                if (config.holdAlreadyUsed || !config.holdEnabled) continue;
                finishHorizontalMoves();
                result.command = PlayerInputCommand::Hold;
                return result;
            }
            if (Input::is_rotation(action) && input.mostRecentActive(rotateKeys) == key) {
                if (movement.rotate(Input::rotation_delta(action), inputNow, config.lockResetLimit).changed)
                    input.cancelHorizontalRepeats(moveKeys,inputNow,config.das,config.arr,config.dcd);
            }
            if (action == Input::Action::SoftDrop) {
                if (soft_drop_interval(config.gravityInterval,config.softDropFactor,config.softDropFixed)!=Runtime::Duration{})
                    result.scoreDelta += movement.softDropStep(inputNow).scoreDelta;
            }
            if (Input::is_horizontal(action) && input.mostRecentActive(moveKeys) == key) {
                const int direction = Input::horizontal_delta(action);
                if (zeroDas && zeroArr) {
                    repeatHorizontal(direction,inputNow);
                } else {
                    horizontalStep(direction,inputNow);
                    // Guideline: the first automatic shift happens at DAS,
                    // rather than at DAS+ARR. Zero DAS finishes in this frame.
                    if (zeroDas) repeatHorizontal(direction, inputNow);
                }
                if (zeroDas) input.setPhase(key, KeyPhase::Repeating);
            }
        }
        else if (input.phase(key) != KeyPhase::Released && down) {
            if (Input::is_rotation(action)) continue;
            if (action == Input::Action::SoftDrop) {
                if (soft_drop_interval(config.gravityInterval,config.softDropFactor,config.softDropFixed)!=Runtime::Duration{}) {
                    const int repeats = input.softDropRepeatsDue(key, inputNow, config.gravityInterval, config.softDropFactor,config.softDropFixed);
                    for (int i = 0; i < repeats; ++i) {
                        const auto effect = movement.softDropStep(inputNow);
                        result.scoreDelta += effect.scoreDelta;
                        if (!effect.changed) break;
                    }
                }
            }
            if (Input::is_horizontal(action) && input.mostRecentActive(moveKeys) == key) {
                const int repeats = input.horizontalRepeatsDue(key, inputNow, config.das, config.arr);
                if (instantDrop!=HorizontalDropMode::None && !zeroArr) {
                    if (repeats) pendingShifts.push_back({Input::horizontal_delta(action),inputNow,false,repeats});
                } else {
                    for (int i = 0; i < repeats; ++i)
                        if (!repeatHorizontal(Input::horizontal_delta(action), inputNow).changed) break;
                }
            }
        }
    }
    finishHorizontalMoves();
    if (instantDrop!=HorizontalDropMode::None)
        result.scoreDelta += movement.dropToFloor(instantDrop==HorizontalDropMode::SoftDrop ? 1 : 0,true,now()).scoreDelta;
    return result;
}

} // namespace Engine
