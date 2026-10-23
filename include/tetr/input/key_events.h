#ifndef TETR_INPUT_KEY_EVENTS_H
#define TETR_INPUT_KEY_EVENTS_H

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace Input {

struct KeyEvent {
    int key = 0;
    bool down = false;
    std::chrono::steady_clock::time_point time;
    std::uint64_t sequence = 0;
};

// One physical event history, with independent cursors for local players.
// Autorepeat is deliberately ignored: DAS/ARR belongs to the game engine.
class KeyEventBuffer {
public:
    using SampleTime = std::optional<std::chrono::steady_clock::time_point>;

    void update(int key, bool down,
        std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (down_[key] == down) return;
        down_[key] = down;
        if (down) pressedAt_[key] = time;
        else pressedAt_.erase(key);
        events_.push_back({key, down, time, ++sequence_});
        // Bound history even while menus or an inactive player are open.
        if (events_.size() > 16384) events_.pop_front();
        changed_.notify_all();
    }

    // A polled backend publishes a complete batch before the consumer may
    // advance held-key timers through this time. Native asynchronous backends
    // leave this unset and retain their existing wall-clock behavior.
    void complete_sample(std::chrono::steady_clock::time_point time) {
        std::lock_guard<std::mutex> lock(mutex_);
        sampledThrough_ = sampledThrough_ ? std::max(*sampledThrough_, time) : time;
        sampledDown_ = down_;
        sampledPressedAt_ = pressedAt_;
        sampledSequence_ = ++sequence_;
        changed_.notify_all(); // Held keys need progress even without transitions.
    }

    std::map<int, bool> snapshot(std::uint64_t& cursor,
        std::map<int, std::chrono::steady_clock::time_point>* pressedAt = nullptr,
        SampleTime* sampledThrough = nullptr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        cursor = visible_sequence();
        if (sampledThrough) *sampledThrough = sampledThrough_;
        if (pressedAt) *pressedAt = sampledThrough_ ? sampledPressedAt_ : pressedAt_;
        return sampledThrough_ ? sampledDown_ : down_;
    }

    std::vector<KeyEvent> read(std::uint64_t& cursor, SampleTime* sampledThrough = nullptr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto visible = visible_sequence();
        if (sampledThrough) *sampledThrough = sampledThrough_;
        std::vector<KeyEvent> result;
        for (auto it = events_.rbegin(); it != events_.rend() && it->sequence > cursor; ++it)
            if (it->sequence <= visible) result.push_back(*it);
        std::reverse(result.begin(), result.end());
        cursor = visible;
        return result;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        down_.clear();
        pressedAt_.clear();
        sampledDown_.clear();
        sampledPressedAt_.clear();
        sampledSequence_ = sequence_;
        events_.clear();
        // Cursors must remain monotonic across backend restarts.
    }

    void discard_presses() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
        pressedAt_.clear();
        for (auto& [key,down]:down_) {
            if (!down) continue;
            down=false;
            events_.push_back({key,false,std::chrono::steady_clock::now(),++sequence_});
        }
        changed_.notify_all();
    }

    void wait(std::uint64_t cursor, std::chrono::microseconds timeout) const {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock,timeout,[&] { return visible_sequence() != cursor; });
    }

private:
    std::uint64_t visible_sequence() const { return sampledThrough_ ? sampledSequence_ : sequence_; }
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::map<int, bool> down_;
    std::map<int, std::chrono::steady_clock::time_point> pressedAt_;
    std::deque<KeyEvent> events_;
    std::uint64_t sequence_ = 0;
    SampleTime sampledThrough_;
    std::uint64_t sampledSequence_ = 0;
    std::map<int, bool> sampledDown_;
    std::map<int, std::chrono::steady_clock::time_point> sampledPressedAt_;
};

} // namespace Input
#endif
