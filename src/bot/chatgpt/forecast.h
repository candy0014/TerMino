#ifndef TETR_BOT_CHATGPT_FORECAST_H
#define TETR_BOT_CHATGPT_FORECAST_H

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

namespace Bot_chatgpt {

struct VisiblePacket { int lines; double secondsUntilReady; };

// An immutable schedule for the root's already visible FIFO. Branches carry
// only remaining line counts; cancellation and rises both consume its prefix.
// No opponent forecast, future packet, hole location, or test identifier.
class GarbageForecast {
public:
    void reset(const std::vector<VisiblePacket>& packets, double secondsPerPiece) {
        readyPrefixes_.clear();
        total_ = 0;
        step_ = std::max(0.0, secondsPerPiece);
        for (const auto& packet : packets) {
            if (packet.lines <= 0) continue;
            total_ += std::min(packet.lines, std::numeric_limits<int>::max() - total_);
            readyPrefixes_.emplace_back(packet.secondsUntilReady, total_);
        }
        active_ = true;
    }

    int ready(std::size_t depth, int remaining, int fallback) const {
        return ready_at(static_cast<double>(depth)*step_,remaining,fallback);
    }
    double step_seconds() const { return step_; }
    int ready_at(double elapsed,int remaining,int fallback) const {
        if (!active_) return fallback; // Direct evaluator tests without a root.
        const int consumed = total_ - remaining;
        auto it = std::upper_bound(readyPrefixes_.begin(), readyPrefixes_.end(), consumed,
            [](int count, const auto& prefix) { return count < prefix.second; });
        int readyLines = 0;
        // Cancelling an unready front can expose an older ready packet behind
        // it. A cached maximum deadline over the ORIGINAL prefix is incorrect.
        for (; it != readyPrefixes_.end() && it->first <= elapsed; ++it)
            readyLines = it->second - consumed;
        return std::clamp(readyLines, 0, remaining);
    }

private:
    std::vector<std::pair<double, int>> readyPrefixes_;
    int total_ = 0;
    double step_ = 0;
    bool active_ = false;
};

} // namespace Bot_chatgpt
#endif
