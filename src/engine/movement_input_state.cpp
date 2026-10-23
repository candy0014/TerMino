#include "tetr/engine/movement_input_state.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine {

Runtime::Duration soft_drop_interval(Runtime::GravityInterval gravity, double sdf, bool fixedSeconds) {
    if (sdf <= 0.0) return {};
    const auto base = gravity.is_disabled() ? Runtime::Duration::from_seconds(1) : gravity.interval();
    return fixedSeconds ? (gravity.is_disabled() ? Runtime::Duration::from_seconds(sdf)
        : std::min(Runtime::Duration::from_seconds(sdf),base)) : base / sdf;
}

MovementInputState::KeyState& MovementInputState::state_for(int key) {
    return states_[key];
}

const MovementInputState::KeyState* MovementInputState::find_state(int key) const {
    const auto it = states_.find(key);
    return it == states_.end() ? nullptr : &it->second;
}

void MovementInputState::clear() {
    states_.clear();
    pressOrder_ = 0;
    preparedAt_ = {};
}

void MovementInputState::rebase_timers(Runtime::Duration offset) {
    for (auto& [key,state]:states_) { state.timestamp+=offset; state.cancelledUntil+=offset; }
    preparedAt_ += offset;
}

void MovementInputState::copy_horizontal_precharge(const Input::BindingSet& bindings,
    const MovementInputState& source, Runtime::Duration offset) {
    for (const auto& [key,action]:bindings.entries()) {
        if (!Input::is_horizontal(action)) continue;
        if (const auto* from=source.find_state(key)) {
            auto& state=state_for(key);
            state=*from;
            state.timestamp+=offset;
            state.cancelledUntil+=offset;
        }
    }
    pressOrder_=std::max(pressOrder_,source.pressOrder_);
}

void MovementInputState::preparePiece(
    const Input::BindingSet& bindings,
    const std::function<bool(int)>& keyDown,
    Runtime::TimePoint now) {
    preparedAt_ = now;
    for (const auto& [key, action] : bindings.entries()) {
        if (!validKey(key)) continue;
        // These controls belong to the whole game, rather than to a piece.
        // Preserve their release debounce across spawns so a press between
        // locking one piece and spawning the next is still handled.
        if ((action == Input::Action::Retry || action == Input::Action::OpenSettings) && find_state(key))
            continue;
        auto& state = state_for(key);
        if (!Input::is_horizontal(action)) {
            state.phase = KeyPhase::Released;
            state.timestamp = now;
        }
        if (Input::is_rotation(action) || action == Input::Action::HardDrop ||
            action == Input::Action::Retry || action == Input::Action::OpenSettings ||
            action == Input::Action::Hold || action == Input::Action::Undo) {
            state.phase = keyDown(key) ? KeyPhase::AwaitRelease : KeyPhase::Released;
        }
    }
}

KeyPhase MovementInputState::phase(int key) const {
    if (!validKey(key)) return KeyPhase::Released;
    const auto* state = find_state(key);
    return state ? state->phase : KeyPhase::Released;
}

Runtime::TimePoint MovementInputState::timestamp(int key) const {
    if (!validKey(key)) return {};
    const auto* state = find_state(key);
    return state ? state->timestamp : Runtime::TimePoint{};
}

void MovementInputState::setPhase(int key, KeyPhase value) {
    if (validKey(key)) state_for(key).phase = value;
}

void MovementInputState::setTimestamp(int key, Runtime::TimePoint value) {
    if (validKey(key)) state_for(key).timestamp = value;
}

bool MovementInputState::consumeReleaseDebounce(int key, bool down) {
    if (!validKey(key)) return false;
    auto& state = state_for(key);
    if (state.phase != KeyPhase::AwaitRelease) return false;
    if (!down) state.phase = KeyPhase::Released;
    return true;
}

bool MovementInputState::consumeRelease(int key, bool down) {
    if (!validKey(key)) return false;
    auto& state = state_for(key);
    if (state.phase == KeyPhase::Released || down) return false;
    state.phase = KeyPhase::Released;
    return true;
}

bool MovementInputState::beginPress(int key, bool down, Runtime::TimePoint now) {
    if (!validKey(key)) return false;
    auto& state = state_for(key);
    if (state.phase != KeyPhase::Released || !down) return false;
    state.timestamp = now;
    state.phase = KeyPhase::Pressed;
    state.pressOrder = ++pressOrder_;
    state.cancelledUntil = {};
    return true;
}

int MovementInputState::mostRecentActive(const std::vector<int>& keys) const {
    const KeyState* latest = nullptr;
    int result = 0;
    for (int key : keys) {
        if (!validKey(key)) continue;
        const auto* state = find_state(key);
        if (!state || state->phase == KeyPhase::Released) continue;
        if (!latest || latest->pressOrder < state->pressOrder ||
            (latest->pressOrder == state->pressOrder && latest->timestamp < state->timestamp)) {
            latest = state;
            result = key;
        }
    }
    return result;
}

bool MovementInputState::horizontalRepeatDue(
    int key,
    Runtime::TimePoint now,
    Runtime::Duration das,
    Runtime::Duration arr) {
    return horizontalRepeatsDue(key, now, das, arr) != 0;
}

namespace {
int elapsed_repeats(Runtime::TimePoint& timestamp, Runtime::TimePoint now, Runtime::Duration interval) {
    const double count = std::floor((now - timestamp) / interval + 1e-9);
    if (count < 1) return 0;
    if (!std::isfinite(count)) {
        timestamp = now;
        return std::numeric_limits<int>::max() - 1;
    }
    // Consume elapsed time in O(1), even for tiny ARR or a long suspension.
    timestamp += interval * count;
    return static_cast<int>(std::min(count, double(std::numeric_limits<int>::max() - 1)));
}
}

int MovementInputState::horizontalRepeatsDue(int key, Runtime::TimePoint now,
    Runtime::Duration das, Runtime::Duration arr) {
    if (!validKey(key)) return 0;
    auto& state = state_for(key);
    if (state.phase == KeyPhase::Released || state.phase == KeyPhase::AwaitRelease) return 0;
    if (now < state.cancelledUntil) return 0;
    int first = 0;
    if (state.phase == KeyPhase::Pressed) {
        if (now < state.timestamp + das) return 0;
        state.timestamp += das;
        state.phase = KeyPhase::Repeating;
        first = 1;
    }
    if (arr <= Runtime::Duration{}) {
        state.timestamp = now;
        return 1;
    }
    return first + elapsed_repeats(state.timestamp, now, arr);
}

bool MovementInputState::softDropRepeatDue(
    int key,
    Runtime::TimePoint now,
    Runtime::GravityInterval gravityInterval,
    double sdf) {
    return softDropRepeatsDue(key, now, gravityInterval, sdf) != 0;
}

int MovementInputState::softDropRepeatsDue(int key, Runtime::TimePoint now,
    Runtime::GravityInterval gravityInterval, double sdf, bool fixedSeconds) {
    if (!validKey(key) || sdf <= 0.0) return 0;
    auto& state = state_for(key);
    if (state.phase == KeyPhase::Released || state.phase == KeyPhase::AwaitRelease) return 0;
    const auto interval = soft_drop_interval(gravityInterval,sdf,fixedSeconds);
    if (interval <= Runtime::Duration{}) {
        if (now <= state.timestamp) return 0;
        state.timestamp = now;
        return 1;
    }
    return elapsed_repeats(state.timestamp, now, interval);
}

bool MovementInputState::horizontalCharged(int key, Runtime::TimePoint now, Runtime::Duration das) const {
    const auto* state=find_state(key);
    return state && now>=state->cancelledUntil &&
        (state->phase==KeyPhase::Repeating || (state->phase==KeyPhase::Pressed && now>=state->timestamp+das));
}

void MovementInputState::prechargeHorizontal(int key, Runtime::TimePoint now,
    Runtime::Duration das, Runtime::Duration arr, bool enabled) {
    auto* state=&state_for(key);
    if (!enabled) {
        // Track held direction/order during waits without carrying their
        // elapsed time into the next piece's DAS timer.
        if (state->phase==KeyPhase::Pressed || state->phase==KeyPhase::Repeating) {
            state->phase=KeyPhase::Pressed;
            // A stale sample can be earlier than a newly arrived press.
            state->timestamp=std::max(state->timestamp,now);
        }
        return;
    }
    if (now<state->cancelledUntil) return;
    if (state->phase==KeyPhase::Pressed) {
        if (now<state->timestamp+das) return;
        state->phase=KeyPhase::Repeating;
        state->timestamp=now-arr;
    } else if (state->phase==KeyPhase::Repeating && state->timestamp+arr<=now) {
        // With no active piece, charge DAS only. Waiting must not bank ARR
        // repeats, even if the initial pre-shift is blocked at spawn.
        state->timestamp=now-arr;
    }
}

void MovementInputState::cancelHorizontalRepeats(const std::vector<int>& keys, Runtime::TimePoint now,
    Runtime::Duration das, Runtime::Duration arr, Runtime::Duration dcd) {
    if (dcd<=Runtime::Duration{}) return;
    for (const int key:keys) {
        auto& state=state_for(key);
        if (state.phase!=KeyPhase::Repeating &&
            !(state.phase==KeyPhase::Pressed && now-state.timestamp>=das)) continue;
        const auto next = state.timestamp + (state.phase==KeyPhase::Pressed ? das : arr);
        state.phase=KeyPhase::Repeating;
        state.cancelledUntil=std::max(state.cancelledUntil,now+dcd);
        // Skip repeats inside the cut, while preserving an ARR deadline that
        // was already later. A short DCD must never accelerate a long ARR.
        state.timestamp=std::max(next,state.cancelledUntil)-arr;
    }
}

void MovementInputState::consumeInitialShift(int key, Runtime::TimePoint now) {
    auto& state=state_for(key);
    state.phase=KeyPhase::Repeating;
    state.timestamp=now;
}

std::optional<Runtime::TimePoint> MovementInputState::nextHorizontalRepeat(int key,
    Runtime::Duration das, Runtime::Duration arr, Runtime::TimePoint now) const {
    const auto* state=find_state(key);
    if (!state) return {};
    if (state->phase==KeyPhase::Pressed) return std::max(state->timestamp+das,state->cancelledUntil);
    if (state->phase==KeyPhase::Repeating && arr>Runtime::Duration{})
        return std::max(state->timestamp+arr,state->cancelledUntil);
    if (state->phase==KeyPhase::Repeating && state->cancelledUntil>now) return state->cancelledUntil;
    return {};
}

} // namespace Engine
