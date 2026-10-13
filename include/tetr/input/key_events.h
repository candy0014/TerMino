#ifndef TETR_INPUT_KEY_EVENTS_H
#define TETR_INPUT_KEY_EVENTS_H

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
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

    std::map<int, bool> snapshot(std::uint64_t& cursor,
        std::map<int, std::chrono::steady_clock::time_point>* pressedAt = nullptr) const {
        std::lock_guard<std::mutex> lock(mutex_);
        cursor = sequence_;
        if (pressedAt) *pressedAt = pressedAt_;
        return down_;
    }

    std::vector<KeyEvent> read(std::uint64_t& cursor) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<KeyEvent> result;
        for (auto it = events_.rbegin(); it != events_.rend() && it->sequence > cursor; ++it)
            result.push_back(*it);
        std::reverse(result.begin(), result.end());
        cursor = sequence_;
        return result;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        down_.clear();
        pressedAt_.clear();
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
        changed_.wait_for(lock,timeout,[&] { return sequence_ != cursor; });
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::map<int, bool> down_;
    std::map<int, std::chrono::steady_clock::time_point> pressedAt_;
    std::deque<KeyEvent> events_;
    std::uint64_t sequence_ = 0;
};

} // namespace Input
#endif
