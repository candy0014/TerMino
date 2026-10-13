#ifndef TETR_INPUT_FOCUS_STATE_H
#define TETR_INPUT_FOCUS_STATE_H

#include "tetr/input/key_events.h"

#include <atomic>
#include <set>

namespace Input {

// Tracks physical releases even while input is disabled. A key held across a
// focus change must be released before it can become a new game action.
class FocusState {
public:
    void set_focused(bool focused) {
        std::lock_guard<std::mutex> lock(mutex_);
        focused_=focused;
        reconcile();
    }
    void set_continue_unfocused(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        continueUnfocused_=enabled;
        reconcile();
    }
    bool accepting_input() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepting_;
    }
    bool focused() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return focused_;
    }
    bool key_down(int key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepted_.count(key)!=0;
    }
    void update(int key,bool down,
        std::chrono::steady_clock::time_point time=std::chrono::steady_clock::now()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!down) {
            physical_.erase(key);
            suppressed_.erase(key);
            accepted_.erase(key);
            events_.update(key,false,time);
        } else if (physical_.insert(key).second) {
            if (accepting_ && !suppressed_.count(key)) {
                accepted_.insert(key);
                events_.update(key,true,time);
            } else suppressed_.insert(key);
        }
    }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        physical_.clear(); suppressed_.clear(); accepted_.clear();
        events_.clear(); ++generation_;
    }
    std::uint64_t generation() const { return generation_.load(); }
    KeyEventBuffer& events() { return events_; }

private:
    void reconcile() {
        const bool accepting=focused_ || continueUnfocused_;
        if (accepting_ && !accepting) {
            suppressed_.insert(physical_.begin(),physical_.end());
            accepted_.clear();
            // Keep releases for active captures, but discard pending presses.
            events_.discard_presses();
            ++generation_;
        }
        accepting_=accepting;
    }
    mutable std::mutex mutex_;
    bool focused_=true,continueUnfocused_=false,accepting_=true;
    std::set<int> physical_,suppressed_,accepted_;
    KeyEventBuffer events_;
    std::atomic<std::uint64_t> generation_{0};
};

} // namespace Input

#endif
