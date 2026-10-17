#include "tetr/presentation/context_view.h"
#include "tetr/runtime/game_context.h"
#include "tetr/replay/playback_clock.h"
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
    return capture_board_view(context.config, player, player.gameplay.playfield.field, board_elapsed(context, player));
}
}
