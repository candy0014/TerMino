#include "tetr/engine/spike_presentation.h"

#include "tetr/render/board_renderer.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/session_state.h"
#include "tetr/rules/spike.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <algorithm>

namespace Engine {
namespace {

Runtime::TimePoint spike_attack_clock_now(const Runtime::GameContext& context) {
    if (!context.config.mode.replayMode) return context.now();
    if (Replay::session(context).separateGameTime)
        return Runtime::TimePoint::from_seconds(Replay::playback_game_elapsed(context,context.now()).seconds());
    return Runtime::TimePoint::from_seconds(
        Replay::playback_presented_elapsed(context).seconds());
}

Runtime::TimePoint spike_expiration_clock_now(const Runtime::GameContext& context) {
    if (!context.config.mode.replayMode) return context.now();
    if (Replay::session(context).separateGameTime)
        return Runtime::TimePoint::from_seconds(Replay::playback_game_elapsed(context,context.now()).seconds());

    const Runtime::Duration scheduled=
        Replay::playback_elapsed(context,context.now());
    const auto& replay=Replay::session(context);
    const bool processingBacklog=Replay::playback_rebuilding(context)||
        (!replay.operations.empty()&&
         replay.operations.front().timestamp<=scheduled);
    const Runtime::Duration elapsed=processingBacklog
        ? Replay::playback_presented_elapsed(context)
        : scheduled;
    return Runtime::TimePoint::from_seconds(elapsed.seconds());
}

} // namespace

void present_spike_attack(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int attack) {
    if (attack <= 0) return;
    Rules::add_spike_attack(
        player.presentation.spike,attack,spike_attack_clock_now(context));
    player.gameplay.scoring.maxSpike=std::max(player.gameplay.scoring.maxSpike,player.presentation.spike.attack);
    Render::print_spike(context, player);
}

void refresh_spike_display(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player) {
    if (Rules::expire_spike(
            player.presentation.spike,spike_expiration_clock_now(context))) {
        Render::print_spike(context, player);
    }
}

} // namespace Engine
