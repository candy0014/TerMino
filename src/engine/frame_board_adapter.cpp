#include "tetr/engine/frame_board_adapter.h"

#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

namespace Engine {

Rules::FrameRuleState capture_frame_rule_state(const Runtime::PlayerSessionState& player, const Runtime::GameContext& context) {
    Rules::FrameRuleState state;
    state.qpSpeed = context.session.quickPlay.speed;
    state.qpAltitude = context.session.quickPlay.altitude;
    state.qpFloor = context.session.quickPlay.floor;
    state.survivalApm = Runtime::current_survival_apm(context);
    state.garbageInTime = Runtime::current_incoming_garbage_delay(context);
    state.gravitySpeed = Runtime::current_gravity_interval(context);
    state.epld = Runtime::current_epld(context);
    state.qpBagBreather = player.pieces.qpBagBreather;
    state.qpProgress = context.session.quickPlay.progress;
    return state;
}

void store_frame_rule_state(Runtime::PlayerSessionState& player, const Rules::FrameRuleState& state, Runtime::GameContext& context) {
    context.session.quickPlay.speed = state.qpSpeed;
    context.session.quickPlay.altitude = state.qpAltitude;
    context.session.quickPlay.floor = state.qpFloor;
    context.session.quickPlay.progress = state.qpProgress;
    Runtime::set_current_survival_apm(context, state.survivalApm);
    Runtime::set_current_incoming_garbage_delay(context, state.garbageInTime);
    Runtime::set_current_gravity_interval(context, state.gravitySpeed);
    Runtime::set_current_epld(context, state.epld);
    player.pieces.qpBagBreather = state.qpBagBreather;
}


Rules::FrameRuleConfig make_frame_rule_config(const Runtime::GameContext& context, Runtime::Duration elapsedGameTime) {
    Rules::FrameRuleConfig config;
    config.garbageMode = context.config.garbage.mode;
    config.replayMode = context.config.mode.replayMode;
    config.qpModExpert = context.config.quickPlay.expert;
    config.qpModGravity = context.config.quickPlay.gravity;
    config.qpGarbageMultiple = context.session.quickPlay.garbageMultiple;
    config.qpGarbageInTime = context.session.quickPlay.garbageInTime;
    config.elapsedGameTime = elapsedGameTime;
    config.qpMods = context.config.quickPlay;
    const auto& field = context.session.player.gameplay.playfield;
    config.garbageRows = field.garbageHeight;
    for (int row = -Runtime::kDisplayHiddenRows; row < 4; ++row)
        for (int col = 0; col < field.width; ++col)
            config.inDanger = config.inDanger || field.field.cell(row, col).occupied();
    return config;
}


} // namespace Engine
