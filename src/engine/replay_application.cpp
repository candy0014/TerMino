#include "tetr/engine/replay_application.h"
#include "tetr/replay/movement_event.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/board_renderer.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/frame_board_adapter.h"
#include "tetr/rules/frame_update.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/input/services.h"
#include "garbage_rendering.h"

namespace Engine {

Rules::PlayResult wait_for_replay_start(
    Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    // Legacy simultaneous replay retains its existing spawn path.
    if (!Replay::session(context).turnPlayback) return Rules::PlayResult::ContinueFrame;
    auto& events = Replay::session(context).operations;
    auto piece = Piece::get_definition(player.pieces.bag.front().type);
    for (;;) {
        update_replay_playback_controls(context, player);
        if (events.empty()) return Rules::PlayResult::Dead;
        if (Replay::event_due(events.front(), Replay::playback_elapsed(context, context.now()))) {
            if (events.front().kind == Replay::OperationKind::StartPlace) {
                if (Replay::can_start_turn(context)) return Rules::PlayResult::ContinueFrame;
            } else {
                const auto operation = std::move(events.front());
                events.pop();
                Replay::mark_operation_consumed(context, operation);
                if (operation.kind == Replay::OperationKind::EndAll) return Rules::PlayResult::Dead;
                apply_replay_gameplay_event(context, operation, player.gameplay.playfield.field,
                    player, piece);
            }
        }
        if (context.real_now() - player.timing.framePoll > Runtime::Duration::from_seconds(0.05)) {
            run_frame(context, player.gameplay.playfield.field, player);
            GarbageService::update_buffer(context, player);
        }
        for (const auto& [key, action] : context.input_services().bindings.entries()) {
            if (context.input_services().key_down(key)) {
                if (action == Input::Action::Retry) return Rules::PlayResult::Retry;
                if (action == Input::Action::OpenSettings) return Rules::PlayResult::OpenSettings;
            }
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}

void apply_replay_gameplay_event(
    Runtime::GameContext& context,
    const Replay::Operation& operation,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    Piece::Definition& piece) {
    if (context.config.mode.replayMode && context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        auto& playback=Replay::session(context).playback;
        const auto elapsed=operation.gameTimestamp.value_or(operation.timestamp);
        const auto delta=elapsed-playback.lastFrameRuleUpdate;
        // Decode-time updates use game time, independently of wall-clock UI
        // repaint cadence and playback speed. The original rule polling is 50ms.
        if (delta>=Runtime::Duration::from_seconds(.05)) {
            auto state=capture_frame_rule_state(player,context);
            const auto config=make_frame_rule_config(context,elapsed);
            Rules::advance_frame_before_presentation(state,config,delta,context.services.random);
            Rules::advance_frame_after_presentation(state,config,delta);
            store_frame_rule_state(player,state,context);
            playback.lastFrameRuleUpdate=elapsed;
        }
    }
    using Replay::OperationKind;
    switch (operation.kind) {
        case OperationKind::BattleRank:player.gameplay.scoring.battleRank=operation.payload_as<Replay::BattleRankPayload>().remaining;break;
        case OperationKind::PutErase: {
            // Older full-speed BOT recordings can erase an unpainted planned
            // landing. Only remove the overlay that is actually on screen.
            if (player.presentation.paintedActivePose && player.presentation.paintedActivePieceType) {
                auto paintedPiece=Piece::get_definition(*player.presentation.paintedActivePieceType);
                const auto before=GarbageRendering::capture_field(context,player,field,
                    &paintedPiece,player.presentation.paintedActivePose);
                player.presentation.paintedActivePose.reset();
                player.presentation.paintedActivePieceType.reset();
                ++player.presentation.activePiecePaintRevision;
                GarbageRendering::render_field_changes(context,player,field,before);
                Render::flush();
            }
            break;
        }
        case OperationKind::PutDraw:
        case OperationKind::Move: {
            const Piece::Pose pose = Replay::parse_movement_event(operation).pose;
            if (player.presentation.paintedActivePose && player.presentation.paintedActivePieceType==piece.type())
                Render::draw_piece_transition(context,piece,player,*player.presentation.paintedActivePose,pose,field);
            else Render::draw_piece(context,piece,player,pose,field);
            break;
        }
        case OperationKind::Hold:
            if (player.gameplay.hold.piece.valid()) Render::draw_board_hold(context,player,Render::PieceDrawMode::Erase);
            if (context.config.quickPlay.fool == Runtime::FoolMode::ReverseAprilFools2026) {
                auto holdPiece=Piece::get_definition(Piece::fool_display_type(piece.type()));
                Render::draw_hold(context,holdPiece,player,Render::hold_draw_mode(context,player,1));
            } else {
                Render::draw_hold(context,piece,player,Render::hold_draw_mode(context,player,1));
            }
            break;
        case OperationKind::AddGarbage: {
            const auto& payload = operation.payload_as<Replay::AddGarbagePayload>();
            auto& qp=context.session.quickPlay;
            const auto savedFloor=qp.floor,savedProtection=qp.progress.receiveProtection;
            if (payload.quickPlayPolicy) {
                qp.floor=payload.quickPlayPolicy->floor;
                qp.progress.receiveProtection=payload.quickPlayPolicy->receiveProtection;
            }
            GarbageService::add_garbage(
                context, player, field, payload.height, payload.preserveHole, payload.cell,true,
                payload.quickPlayPolicy ? std::optional<double>{payload.quickPlayPolicy->elapsedSeconds} : std::nullopt);
            qp.floor=savedFloor;qp.progress.receiveProtection=savedProtection;
            break;
        }
        case OperationKind::AddSolidGarbage: {
            const auto& payload = operation.payload_as<Replay::AddSolidGarbagePayload>();
            GarbageService::add_solid_garbage(context, player, field, payload.height, payload.cell);
            break;
        }
        case OperationKind::AddBuffer: {
            const auto& payload = operation.payload_as<Replay::AddBufferPayload>();
            const auto before=player.garbage.buffer.size();
            GarbageService::add_buffer(context, player, field, payload.attack, payload.instant, payload.resolvedLines,
                payload.deferDisplay,payload.receivedAttack);
            if (player.garbage.buffer.size()>before) {
                // A seek may apply many events at one scheduled time. Preserve
                // each reception's recorded age instead of restarting its wait.
                auto& packet=player.garbage.buffer.back();
                auto arrival=Runtime::TimePoint::from_seconds(operation.gameTimestamp.value_or(operation.timestamp).seconds());
                if (payload.instant) arrival=arrival-(Runtime::current_incoming_garbage_delay(context)+Runtime::Duration::from_seconds(1));
                packet=Runtime::GarbagePacket(packet.line_count(),arrival,packet.preserve_hole());
                GarbageService::update_buffer(context,player);
            }
            if (payload.incomingFraction) player.garbage.incomingFraction=*payload.incomingFraction;
            break;
        }
        case OperationKind::OffsetBuffer: {
            const auto& payload = operation.payload_as<Replay::OffsetBufferPayload>();
            GarbageService::offset_buffer(context, player, field, payload.attack, payload.clearedLines,0,payload.perfectClearAttack);
            break;
        }
        case OperationKind::RaiseBuffer:
            GarbageService::consume_recorded_buffer(context,player,
                operation.payload_as<Replay::RaiseBufferPayload>().lines);
            break;
        case OperationKind::QuickPlayWindup: {
            const auto warning=operation.payload_as<Replay::QuickPlayWindupPayload>().warning;
            auto& p=context.session.quickPlay.progress;
            p.windupWarnings.push_back(warning);
            p.windupUntil=std::max(p.windupUntil,warning.end);
            Render::print_windup(context,player);
            break;
        }
        default:
            break;
    }
}

} // namespace Engine
