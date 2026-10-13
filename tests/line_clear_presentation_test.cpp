#include "tetr/engine/line_clear_presentation.h"
#include "tetr/replay/line_clear_event.h"
#include "tetr/replay/operation_codec.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <queue>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr Runtime::TimePoint tpnt(double value){ return Runtime::TimePoint::from_seconds(value); }
constexpr Runtime::Duration duration(double value){ return Runtime::Duration::from_seconds(value); }

std::vector<int> legacy_order(int width) {
    std::vector<int> out;
    for (int i=(width-1)/2,j=width/2;i>=0;i--,j++) {
        out.push_back(i);
        if (i!=j) out.push_back(j);
    }
    return out;
}

void test_column_order() {
    for (int width=1;width<=100;++width) {
        assert(Engine::build_line_clear_column_order(width)==legacy_order(width));
    }
    assert(Engine::build_line_clear_column_order(0).empty());
}

void test_random_timeline_equivalence() {
    std::mt19937_64 rng(20260831);
    for (int tc=0;tc<100000;++tc) {
        const int width=1+static_cast<int>(rng()%100);
        const double delay=(rng()%500000)/100000.0 + 1e-6;
        const double start=(rng()%100000)/1000.0;
        const double tp=std::min(0.2,delay/width*0.6);
        const auto order=legacy_order(width);
        Engine::LineClearAnimation animation(width,duration(delay),tpnt(start));
        assert(animation.columnOrder()==order);
        assert(std::fabs(animation.columnInterval().seconds()-tp)<1e-12);
        assert(std::fabs(animation.endTime().seconds()-(start+delay))<1e-12);

        std::size_t legacyP=0;
        for (int step=0;step<1000 && !animation.finished();++step) {
            // Include times on both sides of boundaries and large jumps.
            double now=start+(rng()%700000)/100000.0;
            if (step<=width) {
                const double boundary=(step<width)?start+step*tp:start+delay;
                if (step%3==0) now=boundary;
                if (step%3==1) now=boundary-1e-13;
            }

            bool legacyReady=false;
            int legacyColumn=-1;
            if (legacyP<=static_cast<std::size_t>(width)) {
                const double due=legacyP<static_cast<std::size_t>(width)
                    ? start+legacyP*tp : start+delay;
                if (now>=due) {
                    legacyReady=true;
                    if (legacyP<static_cast<std::size_t>(width)) legacyColumn=order[legacyP];
                    ++legacyP;
                }
            }

            const auto got=animation.poll(tpnt(now));
            if (!legacyReady) {
                assert(got.action==Engine::LineClearAnimationAction::Waiting);
            } else if (legacyColumn>=0) {
                assert(got.action==Engine::LineClearAnimationAction::ClearColumn);
                assert(got.column==legacyColumn);
            } else {
                assert(got.action==Engine::LineClearAnimationAction::DelayComplete);
            }
        }

        // Finish deterministically after random probes.
        while (!animation.finished()) {
            const auto got=animation.poll(tpnt(start+delay+100.0));
            assert(got.action==Engine::LineClearAnimationAction::ClearColumn ||
                   got.action==Engine::LineClearAnimationAction::DelayComplete);
        }
        assert(animation.poll(tpnt(start+delay+100.0)).action==Engine::LineClearAnimationAction::Finished);
    }
}

std::queue<Replay::Operation> make_queue() {
    std::queue<Replay::Operation> q;
    q.push(Replay::decode_operation_fields({"add_buffer","1.00","2","0"}));
    q.push(Replay::decode_operation_fields({"clear_line","1.10"}));
    q.push(Replay::decode_operation_fields({"add_garbage","1.20","1","0","0"}));
    q.push(Replay::decode_operation_fields({"end_clear_line","1.30"}));
    q.push(Replay::decode_operation_fields({"end_all","1.40"}));
    return q;
}

void test_replay_marker_pump() {
    using Replay::LineClearMarker;
    {
        auto q=make_queue();
        auto p=Replay::pump_due_until_line_clear_marker(q,duration(1.05),LineClearMarker::ClearLine,false);
        assert(!p.markerReached&&!p.endAll);
        assert(p.events.size()==1&&p.events[0].kind==Replay::OperationKind::AddBuffer);
        assert(q.front().kind==Replay::OperationKind::ClearLine);
        p=Replay::pump_due_until_line_clear_marker(q,duration(1.10),LineClearMarker::ClearLine,false);
        assert(p.markerReached&&!p.endAll);
        assert(p.events.size()==1&&p.events[0].kind==Replay::OperationKind::ClearLine);
        assert(q.front().kind==Replay::OperationKind::AddGarbage);
    }
    {
        auto q=make_queue();
        auto p=Replay::pump_due_until_line_clear_marker(q,duration(10.0),LineClearMarker::ClearLine,false);
        assert(p.markerReached);
        p=Replay::pump_due_until_line_clear_marker(q,duration(10.0),LineClearMarker::EndClearLine,true);
        assert(!p.markerReached&&!p.endAll);
        assert(p.events.size()==1&&p.events[0].kind==Replay::OperationKind::AddGarbage);
        assert(q.front().kind==Replay::OperationKind::EndClearLine); // deliberately reserved
        p=Replay::pump_due_until_line_clear_marker(q,duration(10.0),LineClearMarker::EndClearLine,false);
        assert(p.markerReached&&!p.endAll);
        assert(p.events.size()==1&&p.events[0].kind==Replay::OperationKind::EndClearLine);
        assert(q.front().kind==Replay::OperationKind::EndAll);
    }
    {
        std::queue<Replay::Operation> q;
        q.push(Replay::decode_operation_fields({"end_all","0.1"}));
        auto p=Replay::pump_due_until_line_clear_marker(q,duration(1.0),LineClearMarker::ClearLine,false);
        assert(p.endAll&&!p.markerReached&&p.events.empty());
    }
}

void test_event_types() {
    using Replay::LineClearEventType;
    assert(Replay::line_clear_event_type("clear_line")==LineClearEventType::ClearLine);
    assert(Replay::line_clear_event_type("end_clear_line")==LineClearEventType::EndClearLine);
    assert(Replay::line_clear_event_type("end_all")==LineClearEventType::EndAll);
    assert(Replay::line_clear_event_type("move")==LineClearEventType::Other);
}


void test_coordinator_non_replay() {
    double now=5.0;
    std::vector<std::string> log;
    int framePumps=0;
    Engine::LineClearPresentationCallbacks cb;
    cb.now=[&]{return tpnt(now);};
    cb.clearColumn=[&](int c){log.push_back("C"+std::to_string(c));};
    cb.pumpFrame=[&]{++framePumps;now+=0.01;return Rules::PlayResult::ContinueFrame;};
    cb.pumpReplay=[&](Engine::LineClearPresentationMarker,bool){
        assert(false && "non-replay must not pump replay");
        return Engine::LineClearReplayPumpOutcome{};
    };
    cb.recordReplayMarker=[&](Engine::LineClearPresentationMarker m){
        log.push_back(m==Engine::LineClearPresentationMarker::ClearLine?"BEGIN":"END");
    };
    cb.flush=[&]{log.push_back("F");};
    const auto r=Engine::run_line_clear_presentation(4,2,duration(0.10),false,cb);
    assert(r==Rules::PlayResult::ContinueFrame);
    const std::vector<std::string> expected={
        "BEGIN","F","C1","F","C2","F","C0","F","C3","F","END"};
    assert(log==expected);
    assert(framePumps>0);
}

void test_coordinator_replay_marker_reservation_and_interrupt() {
    double now=0.0;
    int beginPumps=0,endAnimationPumps=0,endFinalPumps=0;
    bool inFinal=false;
    Engine::LineClearPresentationCallbacks cb;
    cb.now=[&]{return tpnt(now);};
    cb.clearColumn=[](int){};
    cb.pumpFrame=[&]{now+=0.02;return Rules::PlayResult::ContinueFrame;};
    cb.pumpReplay=[&](Engine::LineClearPresentationMarker marker,bool leave){
        if(marker==Engine::LineClearPresentationMarker::ClearLine){
            ++beginPumps;
            return Engine::LineClearReplayPumpOutcome{
                Rules::PlayResult::ContinueFrame,beginPumps>=3};
        }
        if(leave){
            ++endAnimationPumps;
            return Engine::LineClearReplayPumpOutcome{Rules::PlayResult::ContinueFrame,false};
        }
        inFinal=true;
        ++endFinalPumps;
        return Engine::LineClearReplayPumpOutcome{
            Rules::PlayResult::ContinueFrame,endFinalPumps>=2};
    };
    cb.recordReplayMarker=[](Engine::LineClearPresentationMarker){
        assert(false && "replay mode must not record markers");
    };
    cb.flush=[]{};
    auto r=Engine::run_line_clear_presentation(6,1,duration(0.08),true,cb);
    assert(r==Rules::PlayResult::ContinueFrame);
    assert(beginPumps==3);
    assert(endAnimationPumps>0);
    assert(inFinal&&endFinalPumps==2);

    // A frame result must escape immediately, before end_clear_line finalization.
    now=0.0;
    int frames=0;
    cb.pumpReplay=[&](Engine::LineClearPresentationMarker marker,bool){
        return Engine::LineClearReplayPumpOutcome{
            Rules::PlayResult::ContinueFrame,
            marker==Engine::LineClearPresentationMarker::ClearLine};
    };
    cb.pumpFrame=[&]{++frames;return Rules::PlayResult::Won;};
    r=Engine::run_line_clear_presentation(10,1,duration(0.5),true,cb);
    assert(r==Rules::PlayResult::Won);
    assert(frames==1);
}

void test_backlogged_replay_animation_at_end_of_recording() {
    Engine::LineClearPresentationCallbacks cb;
    cb.now=[] { return tpnt(1.1); }; // Playback has reached its fixed duration.
    cb.animationStart=[] { return tpnt(1.0); }; // Recorded clear_line timestamp.
    int columns=0, waits=0;
    cb.clearColumn=[&](int) { ++columns; };
    cb.pumpReplay=[](Engine::LineClearPresentationMarker,bool leave) {
        return Engine::LineClearReplayPumpOutcome{Rules::PlayResult::ContinueFrame,!leave};
    };
    cb.idleWait=[&] { assert(++waits<30); };
    assert(Engine::run_line_clear_presentation(10,1,duration(0.08),true,cb)
        ==Rules::PlayResult::ContinueFrame);
    assert(columns==10);
}

} // namespace

int main() {
    test_column_order();
    test_random_timeline_equivalence();
    test_replay_marker_pump();
    test_event_types();
    test_coordinator_non_replay();
    test_coordinator_replay_marker_reservation_and_interrupt();
    test_backlogged_replay_animation_at_end_of_recording();
    std::cout << "line clear presentation tests passed\n";
}
