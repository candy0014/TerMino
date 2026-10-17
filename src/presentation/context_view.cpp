#include "tetr/presentation/context_view.h"
#include "tetr/runtime/game_context.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/network/board_state_codec.h"
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
    const Runtime::PlayerSessionState& player) {
    auto view=capture_board_view(context.config, player, player.gameplay.playfield.field, board_elapsed(context, player));
    for(const auto& packet:player.garbage.buffer)
        view.garbageReadyIn.push_back((packet.arrival_time()-context.now()).seconds());
    if(player.timing.remoteGameTime) {
        view.garbage.clear();view.garbageReadyIn.clear();
        for(int raw:player.presentation.garbageBufferDisplay) {
            const auto cell=static_cast<Network::RemoteGarbageCell>(raw);
            if(cell==Network::RemoteGarbageCell::Empty)continue;
            view.garbage.push_back(1);
            view.garbageReadyIn.push_back(cell==Network::RemoteGarbageCell::Ready?0:
                cell==Network::RemoteGarbageCell::EarlyBuffer?.1:1);
        }
        if(player.presentation.remotePlacementRevision) {
            view.spinPiece=player.presentation.remoteSpinPiece;view.mini=player.presentation.remoteMini;
            view.clearLines=player.presentation.remoteClearLines;
            view.perfectClear=player.presentation.remotePerfectClear;view.halfClear=player.presentation.remoteHalfClear;
        }
    }
    if(context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        view.quickPlay=true;const auto& qp=context.session.quickPlay;
        view.altitude=qp.altitude;view.booster=qp.speed;view.floor=qp.floor;view.experience=qp.progress.experience;
        for(const auto& warning:qp.progress.windupWarnings)
            if(warning.start<=view.elapsed.seconds() && view.elapsed.seconds()<warning.end) {view.windup=warning.segments;break;}
    }
    return view;
}
}
