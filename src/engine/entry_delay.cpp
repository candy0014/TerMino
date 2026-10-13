#include "tetr/engine/entry_delay.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/input/services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/render/terminal_renderer.h"

namespace Engine {
void schedule_entry_delay(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    player.timing.entryDelayEnd.reset();
    const bool simulated=Runtime::autoplay_enabled(context.config.autoplay.mode) && context.config.autoplay.handlingSimulation;
    auto deadline=context.real_now()+context.config.field.entryDelay;
    if(simulated && context.config.autoplay.pps>0)
        deadline=std::max(deadline,player.timing.autoplay);
    if (!context.config.mode.replayMode && deadline>context.real_now()) {
        player.timing.entryDelayEnd = deadline;
        player.presentation.activePose.reset();
        player.presentation.activePieceType.reset();
        player.presentation.paintedActivePose.reset();
        player.presentation.paintedActivePieceType.reset();
    }
}
Rules::PlayResult wait_for_entry_delay(Runtime::GameContext& context, Runtime::PlayerSessionState& player,
    const std::function<Rules::PlayResult()>& pumpFrame, MovementInputState* movementInput) {
    // Replay already records the gap between placements; do not wait twice.
    if (context.config.mode.replayMode) return Rules::PlayResult::ContinueFrame;
    bool firstFrame = true;
    while (player.timing.entryDelayEnd && context.real_now() < *player.timing.entryDelayEnd) {
        if (GarbageService::advance_garbage_rise(context,player,player.gameplay.playfield.field)>0) {
            Render::OutputGuard output(context);
            Render::flush();
        }
        if (movementInput) precharge_movement_input(context,*movementInput);
        if (context.input_component()) {
            for (const auto& [key,action] : context.input_services().bindings.entries()) {
                if ((action == Input::Action::Retry || action == Input::Action::OpenSettings) &&
                    context.input_services().key_down(key)) {
                    if (context.config.garbage.mode == Runtime::GarbageMode::Multiplayer)
                        Network::send_round_signal(context,Network::OutgoingRoundSignal::LeaveRound);
                    player.timing.entryDelayEnd.reset();
                    return action == Input::Action::Retry ? Rules::PlayResult::Retry : Rules::PlayResult::OpenSettings;
                }
            }
        }
        if (firstFrame || context.real_now()-player.timing.framePoll >= Runtime::Duration::from_seconds(.05)) {
            firstFrame = false;
            const auto result = pumpFrame();
            if (result != Rules::PlayResult::ContinueFrame) {
                player.timing.entryDelayEnd.reset();
                return result;
            }
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
    player.timing.entryDelayEnd.reset();
    if (movementInput) precharge_movement_input(context,*movementInput);
    return Rules::PlayResult::ContinueFrame;
}
}
