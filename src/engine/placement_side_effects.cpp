#include "placement_side_effects.h"

#include "tetr/engine/garbage_service.h"
#include "tetr/engine/placement_replay_sync.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/replay/recorder.h"
#include "tetr/rules/placement_resolution.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <algorithm>
#include <limits>

namespace Engine {

void apply_pre_offset_placement_garbage(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    const PlacementPreOffsetEffects& effects) {
    if (context.config.mode.replayMode) return;
    if (effects.directGarbageLines) {
        int lines=effects.directGarbageLines;
        if (!context.config.garbage.legacyIncomingRounding) {
            lines=GarbageService::resolve_received_attack(context,player,lines);
            // Direct-rise attacks have no queued packet. Record a zero-line
            // reception event so replay also restores their fractional carry.
        }
        player.gameplay.scoring.totalReceivedAttack+=static_cast<std::uint64_t>(std::max(0,lines));
        Replay::record_add_buffer(context,effects.directGarbageLines,false,0,player.garbage.incomingFraction,false,lines);
        if (lines) GarbageService::add_garbage(context, player, field, lines);
    }
    if (effects.preOffsetBufferLines) {
        GarbageService::add_buffer(context, player, field, effects.preOffsetBufferLines);
    }
}

PlacementOffsetResolution resolve_placement_offset(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const PlacementPreOffsetEffects& effects, int perfectClearAttack) {
    if (!effects.needsOffset) return {};

    if (!context.config.mode.replayMode) {
        const int offsetResult = GarbageService::offset_buffer(
            context,
            player,
            field,
            effects.offsetAttack,
            effects.offsetClearTrigger,
            static_cast<int>(std::clamp(effects.b2bReleaseCharge * context.config.garbage.sendMultiplier,
                0.0, static_cast<double>(std::numeric_limits<int>::max()))),
            context.config.garbage.legacyOpenerPhase || context.config.garbage.mode == Runtime::GarbageMode::QuickPlay ? 0 : perfectClearAttack);
        Replay::add_replay(context, Replay::OperationKind::OffsetData, offsetResult);
        return {Rules::PlayResult::ContinueFrame, offsetResult};
    }

    const auto replayOffset = wait_for_replay_offset_data(context, field, piece, player);
    return {replayOffset.signal, replayOffset.offsetResult};
}

void apply_post_offset_placement_garbage(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    const PlacementPostOffsetEffects& effects) {
    if (!context.config.mode.replayMode && effects.postOffsetBufferLines) {
        GarbageService::add_buffer(context, player, field, effects.postOffsetBufferLines);
    }
}

Rules::PlayResult complete_locked_piece_side_effects(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement,
    const PlacementPostOffsetEffects& effects) {
    const int clearedLayerLines = placement.clear.clearedLayerLines;
    const bool multiplayer = context.config.garbage.mode == Runtime::GarbageMode::Multiplayer;

    if (context.config.garbage.mode == Runtime::GarbageMode::CheeseLayer &&
        !context.config.mode.replayMode && context.config.garbage.cheeseModel > 0) {
        GarbageService::add_garbage(context, player, field, clearedLayerLines);
    }

    if (!context.config.mode.replayMode && multiplayer) {
        Network::send_board_state_frame(
            context,
            player,
            Network::ActivePieceState::locked(),
            Network::RemotePlacementSnapshot{
                piece.type(),
                placement.spin.spin,
                placement.attack.effectiveMini,
                placement.clear.clearedLines,
                player.gameplay.scoring.b2b,
                player.gameplay.scoring.combo,
                placement.clear.perfectClear,
                placement.clear.halfClear});
    }

    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        !context.config.mode.replayMode && Runtime::reverse(context.config.quickPlay.doubleHole)) {
        GarbageService::add_garbage(context, player, field, clearedLayerLines);
    }

    if (!context.config.mode.replayMode && effects.solidGarbageLines) {
        GarbageService::add_solid_garbage(
            context,
            player,
            field,
            effects.solidGarbageLines,
            Runtime::BoardCell::timed_solid());
    }

    if (context.config.mode.replayMode) {
        return wait_for_replay_lock_completion(context, field, piece, player);
    }

    if (context.config.garbage.entryMode != Runtime::GarbageEntryMode::AfterNonClear &&
        Runtime::uses_garbage_buffer(context.config.garbage.mode))
        GarbageService::raise_ready_buffer(context,player,field);

    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) {
        Replay::record_quick_play_state(context,player);
    }
    Replay::add_replay(context, Replay::OperationKind::EndPlace);
    return Rules::PlayResult::Locked;
}

} // namespace Engine
