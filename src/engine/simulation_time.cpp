#include "tetr/engine/simulation_time.h"

#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/input/services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/replay/recorder.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Engine {

Rules::PlayResult advance_piece_time(Runtime::GameContext& context,
                                    Runtime::PlayerSessionState& player) {
    if (!context.uses_piece_time()) return Rules::PlayResult::ContinueFrame;
    auto& session = context.session;
    ++session.completedSimulationPieces;
    // Derive the endpoint from the count: fractional PPS must not accumulate
    // floating point drift over a long run.
    const auto target = Runtime::TimePoint::from_seconds(static_cast<double>(
        static_cast<long double>(session.completedSimulationPieces) / context.config.simulationPps));
    auto& field = player.gameplay.playfield.field;
    const bool cheese = context.config.garbage.mode == Runtime::GarbageMode::CheeseTimer;
    const bool quickPlay = context.config.garbage.mode == Runtime::GarbageMode::QuickPlay;
    const auto interval = context.config.garbage.generationInterval;
    const auto second = Runtime::Duration::from_seconds(1.0);
    auto result = Rules::PlayResult::ContinueFrame;
    while (session.simulationTime < target) {
        auto next = std::min(target, session.simulationTime + Runtime::Duration::from_seconds(0.05));
        if (cheese && interval.seconds() > 0.0)
            next = std::min(next, player.timing.timedGarbage + interval);
        if (quickPlay) next = std::min(next, player.timing.qpGravity + second);
        if (player.garbage.rising.remainingLines>0 && player.garbage.rising.nextRow &&
            *player.garbage.rising.nextRow>session.simulationTime)
            next=std::min(next,*player.garbage.rising.nextRow);
        session.simulationTime = next;
        // Once a piece is complete its entire tick is atomic, even if the
        // opponent leaves while the due events are being settled.
        const auto signal = run_frame(context, field, player,
            result == Rules::PlayResult::ContinueFrame);
        if (signal != Rules::PlayResult::ContinueFrame) result = signal;
        if (cheese && interval.seconds() > 0.0 &&
            next >= player.timing.timedGarbage + interval) {
            GarbageService::add_garbage(context, player, field, 1);
            player.timing.timedGarbage += interval;
        }
        if (quickPlay && next >= player.timing.qpGravity + second) {
            Runtime::set_current_gravity_interval(context,
                advance_qp_gravity_speed(Runtime::current_gravity_interval(context), true));
            player.timing.qpGravity += second;
        }
    }
    // A zero interval means one row per completed piece, without an endless
    // zero-duration catch-up loop.
    if (cheese && interval.seconds() <= 0.0)
        GarbageService::add_garbage(context, player, field, 1);
    if (Runtime::uses_garbage_buffer(context.config.garbage.mode))
        GarbageService::update_buffer(context, player);
    return result;
}

Rules::PlayResult wait_for_piece_turn(Runtime::GameContext& context,
                                     Runtime::PlayerSessionState& player) {
    if (context.config.mode.replayMode || !context.network_component())
        return Rules::PlayResult::ContinueFrame;
    auto& turns = Network::network_services(context).pieceTurns;
    if (!turns.enabled) return Rules::PlayResult::ContinueFrame;
    const auto previous = turns.completed;
    turns.complete_pieces(static_cast<std::uint64_t>(player.gameplay.scoring.placedBlocks));
    if (turns.completed != previous) Network::resend_piece_turn(context, true);
    struct WaitClock {
        Runtime::GameContext& context;
        bool paused;
        WaitClock(Runtime::GameContext& c, bool waiting)
            : context(c), paused(waiting && !c.uses_piece_time()) {
            if (!paused) return;
            context.session.turnWaitStarted = context.real_now();
            if (context.replay_component()) Replay::add_replay(context, Replay::OperationKind::PauseGameTime);
        }
        ~WaitClock() {
            if (!paused) return;
            context.session.turnWaitDuration += context.real_now() - *context.session.turnWaitStarted;
            context.session.turnWaitStarted.reset();
            if (context.replay_component()) Replay::add_replay(context, Replay::OperationKind::ResumeGameTime);
        }
    } waitClock(context, !turns.can_play());
    bool waited = false;
    std::unordered_map<int, bool> heldControls;
    for (const auto& [key, action] : context.input_services().bindings.entries()) {
        if (action == Input::Action::Retry || action == Input::Action::OpenSettings)
            heldControls[key] = context.input_services().key_down(key);
    }
    for (;;) {
        // Continue receiving attacks and round-control signals while this
        // player's simulation and spawning are suspended.
        Network::resend_piece_turn(context);
        const auto signal = run_frame(context, player.gameplay.playfield.field, player);
        if (signal != Rules::PlayResult::ContinueFrame) return signal;
        if (turns.can_play()) {
            if (waited) player.timing.autoplay = context.real_now();
            return Rules::PlayResult::ContinueFrame;
        }
        for (const auto& [key, action] : context.input_services().bindings.entries()) {
            if (action == Input::Action::Retry || action == Input::Action::OpenSettings) {
                const bool down = context.input_services().key_down(key);
                const bool pressed = down && !heldControls[key];
                heldControls[key] = down;
                if (!pressed) continue;
                Network::send_round_signal(context, Network::OutgoingRoundSignal::LeaveRound);
                return action == Input::Action::Retry ? Rules::PlayResult::Retry
                                                     : Rules::PlayResult::OpenSettings;
            }
        }
        waited = true;
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.01));
    }
}

} // namespace Engine
