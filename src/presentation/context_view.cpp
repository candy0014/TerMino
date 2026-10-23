#include "tetr/presentation/context_view.h"
#include "tetr/runtime/game_context.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/network_services.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/quick_play.h"
#include "tetr/runtime/dynamic_rules.h"
#include <algorithm>

namespace Presentation {
Runtime::Duration board_elapsed(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player) {
    const auto timeline = context.config.mode.replayMode
        ? Replay::playback_game_elapsed(context, context.now())
        : Runtime::elapsed_game_time(context.session, context.now()) * context.config.timeMultiplier;
    const auto elapsed = player.timing.completedGameTime.value_or(
        player.timing.remoteGameTime ? player.timing.remote_elapsed(context.services.clock.now()) : timeline);
    return std::max(Runtime::Duration{}, elapsed);
}

BoardView capture_board_view(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,int hiddenRows) {
    auto view=capture_board_view(context.config, player, player.gameplay.playfield.field, board_elapsed(context, player),hiddenRows);
    const auto delay=Runtime::current_incoming_garbage_delay(context);
    const auto stages=Rules::compute_garbage_buffer_display(player.garbage.buffer,context.now(),
        context.config.timeMultiplier,delay,Rules::GarbageBufferDisplayMode::ThreeStage,
        view.bottom+Runtime::kDisplayHiddenRows+1);
    std::size_t offset=1;
    for(const auto& packet:player.garbage.buffer) {
        view.garbageReadyIn.push_back((delay-(context.now()-packet.arrival_time())*context.config.timeMultiplier).seconds());
        const int stage=offset<stages.size()?stages[offset]:3;
        view.garbagePhases.push_back(stage==1?GarbagePhase::Early:stage==2?GarbagePhase::Incoming:GarbagePhase::Ready);
        offset+=std::max(0,packet.line_count());
    }
    if(player.timing.remoteGameTime) {
        view.garbage.clear();view.garbageReadyIn.clear();view.garbagePhases.clear();
        for(int raw:player.presentation.garbageBufferDisplay) {
            const auto cell=static_cast<Network::RemoteGarbageCell>(raw);
            if(cell==Network::RemoteGarbageCell::Empty)continue;
            const auto phase=cell==Network::RemoteGarbageCell::Ready?GarbagePhase::Ready:
                cell==Network::RemoteGarbageCell::EarlyBuffer?GarbagePhase::Early:GarbagePhase::Incoming;
            // The legacy wire contains per-line colors, not packet boundaries.
            // Group contiguous colors instead of inventing one packet per row.
            if(!view.garbagePhases.empty() && view.garbagePhases.back()==phase)++view.garbage.back();
            else {
                view.garbage.push_back(1);view.garbagePhases.push_back(phase);
                view.garbageReadyIn.push_back(phase==GarbagePhase::Ready?0:phase==GarbagePhase::Early?1:.1);
            }
        }
        if(player.presentation.remotePlacementRevision) {
            view.spinPiece=player.presentation.remoteSpinPiece;view.mini=player.presentation.remoteMini;
            view.clearLines=player.presentation.remoteClearLines;
            view.perfectClear=player.presentation.remotePerfectClear;view.halfClear=player.presentation.remoteHalfClear;
        }
    }
    const auto& scene=context.session.roomScene;
    if(scene.enabled) {
        int slot=&player==&context.session.player?scene.ownSlot:-1;
        for(const auto& [id,remote]:scene.players)if(&remote==&player){slot=id;break;}
        if(const auto member=scene.alive.find(slot);member!=scene.alive.end()) {
            if(scene.finished && !scene.aborted)view.result=slot==scene.winnerSlot?RoundResult::Win:RoundResult::Defeat;
            else if(!scene.aborted && !member->second)view.result=RoundResult::Defeat;
            if(scene.finished && scene.aborted)view.headline="STOPPED";
        } else if(scene.participants.empty()) {
            const auto& network=Network::network_services(context);
            for(const auto& member:network.roomMembers)if(member.slot==slot && member.participating) {
                if(network.matchPhase==Network::RoomMatchPhase::Finished)view.result=slot==network.winnerSlot?RoundResult::Win:RoundResult::Defeat;
                else if(network.matchPhase==Network::RoomMatchPhase::Playing && !member.alive)view.result=RoundResult::Defeat;
            }
        }
    } else if(context.config.mode.mode==Runtime::GameMode::LocalVersus || context.config.mode.mode==Runtime::GameMode::Multiplayer) {
        if(view.headline=="YOU WIN" || view.headline=="WIN")view.result=RoundResult::Win;
        else if(view.headline=="GAME OVER" || view.headline=="LOSE" || view.headline=="DEFEAT")view.result=RoundResult::Defeat;
        if(context.config.mode.mode==Runtime::GameMode::Multiplayer && &player==&context.session.opponent &&
            context.session.player.timing.completedGameTime) {
            const auto& own=context.session.player.presentation.roundEndHeadline;
            if(own=="WIN")view.result=RoundResult::Defeat;
            else if(own=="LOSE")view.result=RoundResult::Win;
            view.finished=view.result!=RoundResult::None;
        }
    }
    if(context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        view.quickPlay=true;const auto& qp=context.session.quickPlay;
        if(Runtime::enabled(context.config.quickPlay.snowball)) {
            const int remaining=Rules::qp_snowball_lines_until_growth(view.lines);
            if(remaining>0) {
                view.growth.nextSize=Rules::qp_snowball_size(std::max(0,view.lines)+remaining);
                view.growth.remaining=remaining;
                view.growth.progress=1.-double(remaining)/Rules::qp_snowball_lines_until_growth(0);
            }
        }
        view.altitude=qp.altitude;view.booster=qp.speed;view.floor=qp.floor;view.experience=qp.progress.experience;
        view.boosterProgress=std::clamp(qp.progress.experience/(4*std::max(1.,qp.speed)),0.,1.);
        // Windup is scheduled on the engine clock. The displayed elapsed time
        // may additionally be multiplied for a live game's statistics.
        const auto windupTime=context.config.mode.replayMode
            ? Replay::playback_game_elapsed(context,context.now())
            : Runtime::elapsed_game_time(context.session,context.now());
        for(const auto& warning:qp.progress.windupWarnings)
            if(warning.start<=windupTime.seconds() && windupTime.seconds()<warning.end) {
                view.windup=warning.segments;
                view.windupAge=windupTime-Runtime::Duration::from_seconds(warning.start);
                break;
            }
    }
    return view;
}
}
