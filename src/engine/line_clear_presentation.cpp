#include "tetr/engine/line_clear_presentation.h"

#include <algorithm>

namespace Engine {

std::vector<int> build_line_clear_column_order(int mapWidth) {
    std::vector<int> order;
    if (mapWidth <= 0) return order;
    order.reserve(static_cast<std::size_t>(mapWidth));
    for (int left = (mapWidth - 1) / 2, right = mapWidth / 2;
         left >= 0;
         --left, ++right) {
        order.push_back(left);
        if (left != right) order.push_back(right);
    }
    return order;
}

LineClearAnimation::LineClearAnimation(
    int mapWidth,
    Runtime::Duration lineClearDelay,
    Runtime::TimePoint startTime)
    : columnOrder_(build_line_clear_column_order(mapWidth)) {
    if (mapWidth <= 0) {
        endTime_ = startTime;
        return;
    }

    columnInterval_ = Runtime::Duration::from_seconds(std::min(0.2, lineClearDelay.seconds() / mapWidth * 0.6));
    endTime_ = startTime + lineClearDelay;
    actionTimes_.reserve(static_cast<std::size_t>(mapWidth) + 1);
    for (int i = 0; i < mapWidth; ++i) {
        actionTimes_.push_back(startTime + columnInterval_ * static_cast<double>(i));
    }
    actionTimes_.push_back(endTime_);
}

LineClearAnimationStep LineClearAnimation::poll(Runtime::TimePoint now) {
    if (nextAction_ >= actionTimes_.size()) {
        return {LineClearAnimationAction::Finished, -1};
    }
    if (now < actionTimes_[nextAction_]) {
        return {LineClearAnimationAction::Waiting, -1};
    }
    if (nextAction_ < columnOrder_.size()) {
        const int column = columnOrder_[nextAction_++];
        return {LineClearAnimationAction::ClearColumn, column};
    }
    ++nextAction_;
    return {LineClearAnimationAction::DelayComplete, -1};
}

bool LineClearAnimation::finished() const {
    return nextAction_ >= actionTimes_.size();
}

Runtime::Duration LineClearAnimation::columnInterval() const {
    return columnInterval_;
}

Runtime::TimePoint LineClearAnimation::endTime() const {
    return endTime_;
}

const std::vector<int>& LineClearAnimation::columnOrder() const {
    return columnOrder_;
}


Rules::PlayResult run_line_clear_presentation(
    int mapWidth,
    int clearedLines,
    Runtime::Duration lineClearDelay,
    bool replayMode,
    const LineClearPresentationCallbacks& callbacks) {
    const auto recordOrWait = [&](LineClearPresentationMarker marker) -> Rules::PlayResult {
        if (!replayMode) {
            if (callbacks.recordReplayMarker) callbacks.recordReplayMarker(marker);
            return Rules::PlayResult::ContinueFrame;
        }
        while (true) {
            const auto pump = callbacks.pumpReplay(marker, false);
            if (pump.result != Rules::PlayResult::ContinueFrame) return pump.result;
            if (pump.markerReached) return Rules::PlayResult::ContinueFrame;
            if (callbacks.idleWait) callbacks.idleWait();
        }
    };

    auto result = recordOrWait(LineClearPresentationMarker::ClearLine);
    if (result != Rules::PlayResult::ContinueFrame) return result;
    if (callbacks.flush) callbacks.flush();

    if (lineClearDelay > Runtime::Duration::from_seconds(1e-7) && clearedLines) {
        LineClearAnimation animation(mapWidth, lineClearDelay,
            callbacks.animationStart ? callbacks.animationStart() : callbacks.now());
        while (!animation.finished()) {
            const auto step = animation.poll(callbacks.now());
            if (step.action == LineClearAnimationAction::ClearColumn) {
                callbacks.clearColumn(step.column);
                if (callbacks.flush) callbacks.flush();
            }

            if (callbacks.pumpFrame) {
                result = callbacks.pumpFrame();
                if (result != Rules::PlayResult::ContinueFrame) return result;
            }

            if (replayMode) {
                const auto pump = callbacks.pumpReplay(
                    LineClearPresentationMarker::EndClearLine, true);
                if (pump.result != Rules::PlayResult::ContinueFrame) return pump.result;
            }
            if (!animation.finished() && callbacks.idleWait) callbacks.idleWait();
        }
    }

    return recordOrWait(LineClearPresentationMarker::EndClearLine);
}

} // namespace Engine
