#ifndef TETR_ENGINE_MOVEMENT_INPUT_STATE_H
#define TETR_ENGINE_MOVEMENT_INPUT_STATE_H

#include "tetr/runtime/gravity.h"
#include "tetr/input/action.h"
#include "tetr/runtime/time.h"

#include <functional>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <optional>

namespace Engine {

Runtime::Duration soft_drop_interval(Runtime::GravityInterval gravity, double sdf, bool fixedSeconds = false);

enum class KeyPhase : int {
    AwaitRelease = -1,
    Released = 0,
    Pressed = 1,
    Repeating = 2
};

class MovementInputState {
public:
    void clear();
    void rebase_timers(Runtime::Duration offset);
    void copy_horizontal_precharge(const Input::BindingSet& bindings,
        const MovementInputState& source, Runtime::Duration offset);
    void preparePiece(
        const Input::BindingSet& bindings,
        const std::function<bool(int)>& keyDown,
        Runtime::TimePoint now);
    Runtime::TimePoint prepared_at() const { return preparedAt_; }
    KeyPhase phase(int key) const;
    Runtime::TimePoint timestamp(int key) const;
    void setPhase(int key, KeyPhase value);
    void setTimestamp(int key, Runtime::TimePoint value);
    bool consumeReleaseDebounce(int key, bool down);
    bool consumeRelease(int key, bool down);
    bool beginPress(int key, bool down, Runtime::TimePoint now);
    int mostRecentActive(const std::vector<int>& keys) const;
    bool horizontalRepeatDue(
        int key,
        Runtime::TimePoint now,
        Runtime::Duration das,
        Runtime::Duration arr);
    bool softDropRepeatDue(
        int key,
        Runtime::TimePoint now,
        Runtime::GravityInterval gravityInterval,
        double sdf);
    int horizontalRepeatsDue(int key, Runtime::TimePoint now,
        Runtime::Duration das, Runtime::Duration arr);
    int softDropRepeatsDue(int key, Runtime::TimePoint now,
        Runtime::GravityInterval gravityInterval, double sdf, bool fixedSeconds = false);
    bool horizontalCharged(int key, Runtime::TimePoint now, Runtime::Duration das) const;
    void prechargeHorizontal(int key, Runtime::TimePoint now, Runtime::Duration das, Runtime::Duration arr,
        bool enabled = true);
    void cancelHorizontalRepeats(const std::vector<int>& keys, Runtime::TimePoint now,
        Runtime::Duration das, Runtime::Duration arr, Runtime::Duration dcd);
    void consumeInitialShift(int key, Runtime::TimePoint now);
    std::optional<Runtime::TimePoint> nextHorizontalRepeat(int key,
        Runtime::Duration das, Runtime::Duration arr, Runtime::TimePoint now = {}) const;

private:
    struct KeyState {
        KeyPhase phase = KeyPhase::Released;
        Runtime::TimePoint timestamp{};
        std::uint64_t pressOrder = 0;
        Runtime::TimePoint cancelledUntil{};
    };

    static bool validKey(int key) noexcept { return key >= 0; }
    KeyState& state_for(int key);
    const KeyState* find_state(int key) const;

    std::unordered_map<int, KeyState> states_;
    std::uint64_t pressOrder_ = 0;
    Runtime::TimePoint preparedAt_{};
};

} // namespace Engine
#endif
