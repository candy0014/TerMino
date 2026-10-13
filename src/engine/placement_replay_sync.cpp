#include "tetr/engine/placement_replay_sync.h"
#include "tetr/replay/session_state.h"

#include "tetr/engine/replay_application.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/replay/movement_event.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"

#include <utility>

namespace Engine {
namespace {

bool next_due_operation(Runtime::GameContext& context, Replay::Operation& operation) {
    auto& operations = Replay::session(context).operations;
    if (operations.empty()) {
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
        return false;
    }
    const Runtime::Duration elapsed = Replay::playback_elapsed(context,context.now());
    if (!Replay::event_due(operations.front(), elapsed)) {
        // Keep waits interruptible so pause and speed controls remain live.
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
        return false;
    }
    operation = std::move(operations.front());
    operations.pop();
    Replay::mark_operation_consumed(context, operation);
    return true;
}

} // namespace

ReplayOffsetSyncResult wait_for_replay_offset_data(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player) {
    for (;;) {
        update_replay_playback_controls(context,player);
        Replay::Operation operation;
        if (!next_due_operation(context, operation)) continue;
        if (operation.kind == Replay::OperationKind::EndAll) {
            update_replay_playback_controls(context,player,true);
            return {Rules::PlayResult::Dead, 0};
        }
        apply_replay_gameplay_event(context, operation, field, player, piece);
        if (operation.kind == Replay::OperationKind::OffsetData) {
            return {Rules::PlayResult::ContinueFrame,
                operation.payload_as<Replay::OffsetDataPayload>().result};
        }
    }
}

Rules::PlayResult wait_for_replay_lock_completion(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player) {
    for (;;) {
        update_replay_playback_controls(context,player);
        Replay::Operation operation;
        if (!next_due_operation(context, operation)) continue;
        if (operation.kind == Replay::OperationKind::EndAll) {
            update_replay_playback_controls(context,player,true);
            return Rules::PlayResult::Dead;
        }
        apply_replay_gameplay_event(context, operation, field, player, piece);
        if (operation.kind == Replay::OperationKind::EndPlaceData) {
            const auto& payload = operation.payload_as<Replay::QuickPlayPlacementPayload>();
            context.session.quickPlay.speed = payload.speed;
            context.session.quickPlay.altitude = payload.altitude;
            context.session.quickPlay.floor = payload.floor;
            context.session.quickPlay.garbageMultiple = payload.garbageMultiple;
            context.session.quickPlay.garbageInTime = payload.garbageInTime;
            player.pieces.qpBagBreather = payload.bagBreather;
            if (payload.progress) {
                auto& progress = context.session.quickPlay.progress;
                const auto quadColumns = progress.quadColumns;
                const auto szColumns = progress.szColumns;
                const auto jlColumns = progress.jlColumns;
                const auto szPieces = progress.szPieces;
                const auto extraI5Issued = progress.extraI5Issued;
                const auto windupWarnings=progress.windupWarnings;
                progress = *payload.progress;
                progress.quadColumns = quadColumns;
                progress.szColumns = szColumns;
                progress.jlColumns = jlColumns;
                progress.szPieces = szPieces;
                progress.extraI5Issued = extraI5Issued;
                progress.windupWarnings=windupWarnings;
            }
        }
        if (operation.kind == Replay::OperationKind::EndPlace) {
            return Rules::PlayResult::Locked;
        }
    }
}

} // namespace Engine
