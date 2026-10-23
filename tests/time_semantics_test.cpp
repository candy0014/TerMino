#include "tetr/engine/movement_presentation.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/replay/movement_event.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/runtime/time.h"

#include <cassert>
#include <cmath>
#include <type_traits>
#include <vector>

namespace TetrTests::time_semantics_test {
namespace {

constexpr Runtime::TimePoint tp(double value) {
    return Runtime::TimePoint::from_seconds(value);
}

constexpr Runtime::Duration dur(double value) {
    return Runtime::Duration::from_seconds(value);
}

static_assert(!std::is_convertible_v<double, Runtime::TimePoint>);
static_assert(!std::is_convertible_v<double, Runtime::Duration>);
static_assert(std::is_same_v<decltype(tp(2.0) - tp(1.0)), Runtime::Duration>);
static_assert(std::is_same_v<decltype(tp(1.0) + dur(2.0)), Runtime::TimePoint>);
static_assert(std::is_same_v<decltype(dur(3.0) + dur(2.0)), Runtime::Duration>);

void arithmetic_is_semantic() {
    constexpr auto start = tp(10.0);
    constexpr auto elapsed = dur(0.25);
    constexpr auto end = start + elapsed;
    static_assert(end.seconds() == 10.25);
    static_assert((end - start).seconds() == 0.25);
    static_assert((end - elapsed) == start);
    static_assert((dur(0.5) * 2.0).seconds() == 1.0);
    static_assert((dur(1.0) / 4.0).seconds() == 0.25);
    static_assert(dur(1.0) / dur(0.25) == 4.0);
}

void periodic_thresholds_keep_legacy_behavior() {
    const auto last = tp(5.0);
    assert(!Engine::periodic_event_due(tp(5.10), last, dur(0.10)));
    assert(Engine::periodic_event_due(tp(5.100001), last, dur(0.10)));

    assert(!Engine::blitz_time_expired(Runtime::GameMode::Racing, dur(20.0), dur(10.0)));
    assert(!Engine::blitz_time_expired(Runtime::GameMode::Blitz, dur(10.0), dur(10.0)));
    assert(Engine::blitz_time_expired(Runtime::GameMode::Blitz, dur(10.001), dur(10.0)));
}

void replay_timestamp_is_elapsed_duration() {
    const auto operation=Replay::decode_operation_fields({"move","1.25","3","4","2"});
    const auto event=Replay::parse_movement_event(operation);
    assert(std::fabs(event.timestamp.seconds()-1.25)<1e-12);
    assert(!Replay::event_due(operation,dur(1.249)));
    assert(Replay::event_due(operation,dur(1.25)));
}

void playback_uses_absolute_start_plus_duration() {
    Engine::MovementScriptPlayback playback("LR",tp(100.0),dur(0.05));
    assert(playback.commandDue(tp(100.0)));
    assert(playback.consume()==Engine::MovementScriptCommand::LeftStep);
    assert(!playback.commandDue(tp(100.049)));
    assert(playback.commandDue(tp(100.050001)));
    assert(playback.consume()==Engine::MovementScriptCommand::RightStep);
    assert(playback.finished());
}

} // namespace

int run(int argc,char** argv) {
    arithmetic_is_semantic();
    periodic_thresholds_keep_legacy_behavior();
    replay_timestamp_is_elapsed_duration();
    playback_uses_absolute_start_plus_duration();
    return 0;
}
} // namespace TetrTests::time_semantics_test
