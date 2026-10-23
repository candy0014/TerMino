#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/autoplay_path.h"
#include "autoplay_search.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/engine/undo.h"
#include "piece_replay.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/input/services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include <algorithm>
#include <future>
#include <thread>
#include <condition_variable>
#include <deque>
#include "tetr/util/worker_thread.h"
#include <mutex>
namespace Engine {
namespace {
struct Snapshot {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Runtime::PlayerSessionState player;
    Snapshot(const Runtime::GameContext& live,const Runtime::PlayerSessionState& p)
        :session(live.session),services(live.services),player(p) {
        services.clock.waitInterrupt={};context.config=live.config;
        context.pause=live.pause;context.pause.prechargeMovement={};
    }
};
struct SearchWorker {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::function<void()>> jobs;
    SearchWorker() {
        Util::WorkerThread([this] {
            for(;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ready.wait(lock,[&]{return !jobs.empty();});
                    job=std::move(jobs.front());jobs.pop_front();
                }
                job();
            }
        }).detach();
    }
    void post(std::function<void()> job) {
        {std::lock_guard<std::mutex> lock(mutex);jobs.push_back(std::move(job));}
        ready.notify_one();
    }
};
SearchWorker& search_worker() {
    // The process-lifetime worker preserves the ordinary BOT's thread-local
    // search allocations between pieces. Jobs own snapshots; cancelled jobs
    // cannot publish into a later piece and leaving a game never joins work.
    static SearchWorker* workers[]={new SearchWorker,new SearchWorker};
    static std::atomic<unsigned> next{0};
    return *workers[next.fetch_add(1)%2];
}
template<class Work> std::future<AutoplayDecision> launch(Work work) {
    auto promise=std::make_shared<std::promise<AutoplayDecision>>();auto future=promise->get_future();
    search_worker().post([work=std::make_shared<Work>(std::move(work)),promise] {
        try {promise->set_value((*work)());} catch(...) {promise->set_exception(std::current_exception());}
    });return future;
}
}
int autoplay_field_shift(const Runtime::PlayerSessionState& p) {
    const auto& b=p.gameplay.playfield;
    return b.garbageHeight+b.visibleHeight-b.activeHeight;
}
std::optional<std::future<AutoplayDecision>> launch_autoplay_search(
    const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const Piece::Definition& piece,MovementState movement,
    Autoplay::HoldInput hold,const AutoplayCarryState* continuation,
    Runtime::GravityInterval gravity,bool buildPath,bool buildScript,
    const std::shared_ptr<std::atomic<bool>>& cancelled) {
    auto clone=context.autoplay_component()->clone_for_search();
    if(!clone)return {};
    clone->restore_state(context.autoplay_component()->capture_state());
    auto snapshot=std::make_shared<Snapshot>(context,player);
    snapshot->context.autoplaySearchCancelled=cancelled;
    auto sequence=player.pieces.nextPreview;sequence.insert(sequence.begin(),current_piece(player));
    const auto unseen=unseen_pieces_in_preview_endpoint_bag(context,player);
    const auto saved=continuation?*continuation:AutoplayCarryState{};
    return launch([snapshot,clone=std::move(clone),field,piece,movement,sequence,unseen,hold,saved,
        continuing=continuation!=nullptr,gravity,buildPath,buildScript]() mutable {
        AutoplayDecision answer;answer.pose=movement.pose;answer.rotation=movement.lastRotation;
        answer.fieldShift=autoplay_field_shift(snapshot->player);
        if(continuing){
            answer.selected=saved.selectedPieceType;answer.pose=saved.pose;answer.rotation=saved.lastRotation;
            const int raised=autoplay_field_shift(snapshot->player)-saved.fieldShift;
            if(raised && buildPath) {
                answer.path=repair_autoplay_path(field,piece,movement,answer.pose,answer.rotation,raised,
                    snapshot->context.config.handling,snapshot->context.config.movement,gravity,true,
                    autoplay_spin_rules(snapshot->context.config));
                if(answer.path)answer.script=answer.path->script;
                else answer.fieldShift=saved.fieldShift;
                answer.strategyState=clone->capture_state();return answer;
            }
        }
        else answer.selected=clone->choose_position(snapshot->context,snapshot->player,field,sequence,unseen,
            movement.pose,hold,answer.pose,answer.rotation);
        answer.strategyState=clone->capture_state();
        if(answer.selected!=piece.type())return answer;
        if(buildPath || buildScript)answer.script=clone->movement_script(snapshot->context,field,piece,movement.pose,answer.pose,answer.rotation);
        if(buildPath) {
            answer.path=make_autoplay_path(field,piece,movement,snapshot->context.config.handling,
                snapshot->context.config.movement,gravity,answer.script);
            const auto spinRules=autoplay_spin_rules(snapshot->context.config);
            if(!answer.path || !autoplay_landing_matches(field,piece,answer.path->finalMovement,
                answer.pose,answer.rotation,spinRules))
                answer.path=search_autoplay_path(field,piece,movement,answer.pose,answer.rotation,
                    snapshot->context.config.handling,snapshot->context.config.movement,gravity,true,
                    [&]{return snapshot->context.autoplaySearchCancelled->load();},spinRules);
        }
        return answer;
    });
}
AutoplayStepResult run_handling_autoplay_step(Runtime::GameContext& context,Runtime::Playfield& field,
    Piece::Definition& piece,Runtime::PlayerSessionState& player,MovementState& movement,
    MovementInputState&,const std::function<Rules::PlayResult()>& pumpFrame,
    AutoplayCarryState* carry,bool heldPieceContinuation) {
    AutoplayStepResult result;
    auto gravity=Runtime::current_gravity_interval(context);
    const auto present=[&](const MovementState& state,int score=0) {
        const auto previous=movement.pose;movement=state;player.gameplay.scoring.score+=score;
        if(previous!=movement.pose) {
            record_piece_pose(context,Replay::OperationKind::Move,movement.pose);
            if (player.presentation.paintedActivePose)
                Render::draw_piece_transition(context,piece,player,*player.presentation.paintedActivePose,movement.pose,field);
            else Render::draw_piece(context,piece,player,movement.pose,field);
        }
        player.presentation.activePose=movement.pose;
        // Local presentation commits first. Reuse the non-waiting gameplay
        // send path, including full-view subscriptions and stale-pose merging.
        if(previous!=movement.pose && context.config.garbage.mode==Runtime::GarbageMode::Multiplayer)
            Network::send_piece_motion_frame(context,player,movement.pose,true);
    };
    const bool continuation=carry && heldPieceContinuation && carry->selectedPieceType==piece.type();
    // Ordinary gravity is suspended while deciding and planning. 20G settles
    // once at birth; a known Hold target may buffer its first turn before fall.
    if(gravity.is_instant() && !continuation) {
        auto settled=movement;
        apply_presentation_instant_gravity(settled,[&](Piece::Pose pose){return piece.can_place(pose,field);});
        present(settled);
    }
    struct WorkLifetime {
        std::shared_ptr<std::atomic<bool>> cancelled=std::make_shared<std::atomic<bool>>(false);
        ~WorkLifetime(){cancelled->store(true);}
    } work;
    bool fieldChanged=false;
    const auto service=[&] {
        poll_undo_key(context);
        const int activeHeightBefore=player.gameplay.playfield.activeHeight;
        if(context.input_component()) for(const auto& [key,action]:context.input_services().bindings.entries()) {
            if((action!=Input::Action::Retry && action!=Input::Action::OpenSettings) || !context.input_services().key_down(key)) continue;
            if(!Network::handle_room_game_control(context,action==Input::Action::Retry)) continue;
            if(context.config.garbage.mode==Runtime::GarbageMode::Multiplayer && !context.session.roomScene.enabled)
                Network::send_round_signal(context,Network::OutgoingRoundSignal::LeaveRound);
            result.signal=action==Input::Action::Retry ? Rules::PlayResult::Retry : Rules::PlayResult::OpenSettings;return false;
        }
        if(periodic_event_due(context.real_now(),player.timing.framePoll,Network::multiplayer_frame_poll_interval(context))) {
            result.signal=pumpFrame();if(result.signal!=Rules::PlayResult::ContinueFrame) return false;
        }
        bool changed=false;
        if(const auto signal=apply_immediate_garbage_to_live_piece(context,field,piece,player,movement,player.presentation.paintedActivePose.has_value(),&changed)) {
            result.signal=*signal;return false;
        }
        if(changed || activeHeightBefore!=player.gameplay.playfield.activeHeight)fieldChanged=true;
        if(gravity!=Runtime::current_gravity_interval(context)) {if(carry)*carry={};return false;}
        return true;
    };
    if(!service()) return result;
    const Autoplay::HoldInput hold{Runtime::hold_available(context.config.field,player.gameplay.hold.used) && hold_replacement_available(context,player),player.gameplay.hold.piece};
    auto future=launch_autoplay_search(context,player,field,piece,movement,hold,continuation?carry:nullptr,
        gravity,true,true,work.cancelled);
    if(!future)throw std::logic_error("Handling presentation requires an independent strategy");
    auto& task=*future;
    while(task.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
        if(!service()) return result;
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
    }
    if(!service()) return result;
    auto decision=task.get();
    context.autoplay_component()->restore_state(decision.strategyState);
    if(decision.selected.valid() && decision.selected!=piece.type() && hold.enabled) {
        if(carry) {carry->selectedPieceType=decision.selected;carry->pose=decision.pose;carry->lastRotation=decision.rotation;carry->fieldShift=decision.fieldShift;}
        record_piece_pose(context,Replay::OperationKind::PutErase,movement.pose);
        if (player.presentation.paintedActivePose)
            Render::draw_piece(context,piece,player,*player.presentation.paintedActivePose,field,Render::PieceDrawMode::Erase);
        Replay::add_replay(context,Replay::OperationKind::Hold);
        if(auto* recorder=Replay::tetrio_recorder(context)) recorder->hold();
        if(player.gameplay.hold.piece.valid()) Render::draw_board_hold(context,player,Render::PieceDrawMode::Erase);
        Render::draw_hold(context,piece,player,Render::hold_draw_mode(context,player,1));
        result.signal=Rules::PlayResult::Hold;return result;
    }
    int targetShift=decision.fieldShift;
    const auto refresh=[&](bool force=false) {
        const int shift=autoplay_field_shift(player);
        if(!force && !fieldChanged && shift==targetShift)return;
        auto path=repair_autoplay_path(field,piece,movement,decision.pose,decision.rotation,
            shift-targetShift,context.config.handling,context.config.movement,gravity,true,
            autoplay_spin_rules(context.config),decision.script);
        if(!path) {
            // Keep making progress even if new garbage blocks the old tuck.
            path=make_autoplay_path(field,piece,movement,context.config.handling,context.config.movement,
                gravity,context.config.movement.disableHardDrop?"W":"H");
            if(path){decision.pose=path->finalMovement.pose;decision.rotation=path->finalMovement.lastRotation;}
        }
        decision.path=std::move(path);targetShift=shift;fieldChanged=false;
    };
    refresh(!decision.path);
    if(!decision.path)throw std::logic_error("BOT selected an unreachable landing");
    auto at=context.real_now();
    auto* recorder=Replay::tetrio_recorder(context);
    if(recorder)recorder->begin_script(decision.path->steps.size()+32);
    auto steps=decision.path->steps;
    unsigned repairs=0;
    for(std::size_t index=0;index<steps.size();++index) {
        const auto step=steps[index];at+=step.delay;
        while(context.real_now()<at) {
            if(!service())return result;
            context.services.clock.sleep_for(std::min(at-context.real_now(),Runtime::Duration::from_seconds(.001)));
        }
        if(!service())return result;
        const bool terminal=step.command==MovementScriptCommand::HardDrop || step.command==MovementScriptCommand::WaitForLock;
        if(terminal && (fieldChanged || autoplay_field_shift(player)!=targetShift)) {
            // Repair only the unexecuted tail. Earlier DAS/ARR/soft-drop time
            // has already been spent; neither search nor the cadence restarts.
            refresh();
            if(!decision.path)throw std::logic_error("BOT has no live landing");
            if(++repairs>2) {
                decision.path=make_autoplay_path(field,piece,movement,context.config.handling,
                    context.config.movement,gravity,context.config.movement.disableHardDrop?"W":"H");
                if(!decision.path)throw std::logic_error("BOT cannot finish live drop");
                decision.pose=decision.path->finalMovement.pose;decision.rotation=decision.path->finalMovement.lastRotation;
            }
            steps=decision.path->steps;
            if(!steps.empty() && steps.front().command==step.command)steps.front().delay={};
            index=std::size_t(-1);continue;
        }
        auto live=movement;
        if(step.command!=MovementScriptCommand::WaitForLock) {
            apply_movement_script_command(live,step.command,
                [&](auto pose){return piece.can_place(pose,field);},
                [&](auto& pose,auto turns,auto& kick){return piece.try_rotate(pose,turns,field,kick);},
                false,false,context.config.movement,gravity.is_instant(),
                soft_drop_interval(gravity,context.config.handling.softDropFactor,context.config.handling.softDropFixed)==Runtime::Duration{});
        }
        const int rows=std::max(0,live.pose.position.row-movement.pose.position.row);
        const int score=step.command==MovementScriptCommand::HardDrop?2*rows:
            step.command==MovementScriptCommand::SoftDropStep || step.command==MovementScriptCommand::SoftDropToFloor?rows:0;
        if(recorder && step.command!=MovementScriptCommand::WaitForLock)
            recorder->movement(movement_script_command_char(step.command),
                live.pose.position.row-movement.pose.position.row,live.pose.position.col-movement.pose.position.col);
        present(live,score);
    }
    if(!autoplay_landing_matches(field,piece,movement,decision.pose,decision.rotation,autoplay_spin_rules(context.config)))
        throw std::logic_error("BOT presentation missed its selected landing");
    if(carry) *carry={};
    const auto now=context.real_now();
    player.timing.autoplay=context.config.autoplay.pps>0 ?
        std::max(now,player.timing.autoplay+Runtime::Duration::from_seconds(1/context.config.autoplay.pps)) : now;
    result.hardDrop=!context.config.movement.disableHardDrop;result.locked=true;return result;
}
}
