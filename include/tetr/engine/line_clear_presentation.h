#ifndef TETR_ENGINE_LINE_CLEAR_PRESENTATION_H
#define TETR_ENGINE_LINE_CLEAR_PRESENTATION_H

#include "tetr/rules/game_result.h"
#include "tetr/runtime/time.h"

#include <functional>
#include <vector>

namespace Engine {

std::vector<int> build_line_clear_column_order(int mapWidth);


enum class LineClearPresentationMarker {
    ClearLine,
    EndClearLine
};

struct LineClearReplayPumpOutcome {
    Rules::PlayResult result = Rules::PlayResult::ContinueFrame;
    bool markerReached = false;
};

struct LineClearPresentationCallbacks {
    std::function<Runtime::TimePoint()> now;
    std::function<Runtime::TimePoint()> animationStart;
    std::function<void(int)> clearColumn;
    std::function<Rules::PlayResult()> pumpFrame;
    std::function<LineClearReplayPumpOutcome(LineClearPresentationMarker, bool)> pumpReplay;
    std::function<void(LineClearPresentationMarker)> recordReplayMarker;
    std::function<void()> flush;
    std::function<void()> idleWait;
};

// Coordinates the legacy clear_line -> animation -> end_clear_line phase while
// delegating terminal drawing, frame updates and replay side effects to the
// caller. This keeps presentation timing outside App::run_piece without pulling
// legacy UI globals into the engine translation unit.
Rules::PlayResult run_line_clear_presentation(
    int mapWidth,
    int clearedLines,
    Runtime::Duration lineClearDelay,
    bool replayMode,
    const LineClearPresentationCallbacks& callbacks);

enum class LineClearAnimationAction {
    Waiting,
    ClearColumn,
    DelayComplete,
    Finished
};

struct LineClearAnimationStep {
    LineClearAnimationAction action = LineClearAnimationAction::Waiting;
    int column = -1;
};

// Pure timing/state-machine representation of the legacy centre-out clear
// animation. poll() intentionally releases at most one action per call so a
// caller keeps the same frame/replay interleaving as the old placement loop.
class LineClearAnimation {
public:
    LineClearAnimation(int mapWidth, Runtime::Duration lineClearDelay, Runtime::TimePoint startTime);

    LineClearAnimationStep poll(Runtime::TimePoint now);
    bool finished() const;
    Runtime::Duration columnInterval() const;
    Runtime::TimePoint endTime() const;
    const std::vector<int>& columnOrder() const;

private:
    std::vector<int> columnOrder_;
    std::vector<Runtime::TimePoint> actionTimes_;
    std::size_t nextAction_ = 0;
    Runtime::Duration columnInterval_{};
    Runtime::TimePoint endTime_{};
};

} // namespace Engine

#endif
