#include "tetr/network/multiplayer_channel.h"

#include "tetr/network/transport.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Network {

namespace {
BoardFrameStamp next_stamp(HybridBoardSync& sync,Runtime::TimePoint now) {
    if (!sync.origin) sync.origin=now;
    const double frame=std::max(0.0,std::floor((now-*sync.origin).seconds()*kMotionFramesPerSecond));
    return {sync.epoch,++sync.sentSequence,static_cast<std::uint64_t>(std::min(frame,
        static_cast<double>(std::numeric_limits<std::uint32_t>::max()))),sync.revision,sync.fingerprint};
}
Piece::Type current_wire_piece(const Runtime::PlayerSessionState& player) {
    return player.pieces.bag.empty() ? Piece::Type::none() : player.pieces.bag.front().type;
}
Piece::Pose wire_pose(const Runtime::PlayerSessionState& player,ActivePieceState active) {
    return active.kind==ActivePieceStateKind::Active ? active.pose :
        Piece::Pose{{-2,(player.gameplay.playfield.width-1)/2},Runtime::Rotation{}};
}
void emit_motion(Runtime::GameContext& context,const Runtime::PlayerSessionState& player,Piece::Pose pose) {
    auto& network=network_services(context); auto& sync=network.boardSync;
    const auto piece=current_wire_piece(player);
    if (!sync.pose || piece!=sync.piece || *sync.pose==pose) {sync.pendingMotion.reset();return;}
    const auto now=context.real_now();
    if(network.multiplayerRoom && network.webSocket && !network.webSocket->uses_event_stream()) {
        // A distant WSS route cannot sustain one small TCP frame per local
        // input event. Keep the newest absolute pose instead of building a
        // queue of seconds-old movements. Snapshots and combat stay immediate.
        const auto rtt=network.countdownBestRtt;
        const double fps=rtt>std::chrono::milliseconds(500)?20.:
            rtt>std::chrono::milliseconds(200)?30.:
            rtt>std::chrono::milliseconds(50)?60.:kMotionFramesPerSecond;
        if(sync.lastMotion!=Runtime::TimePoint{} &&
           now-sync.lastMotion<Runtime::Duration::from_seconds(1./fps)) {
            sync.pendingMotion=pose;return;
        }
    }
    const int rotations=pose.rotation.index()-sync.pose->rotation.index();
    PieceMotionFrame motion{next_stamp(sync,now),piece,
        rotations ? PieceMotionKind::Rotate : PieceMotionKind::Translate,
        pose.position.row-sync.pose->position.row,pose.position.col-sync.pose->position.col,rotations,pose};
    motion.clock=capture_player_clock(context,player);
    const auto wire=encode_piece_motion(motion);
    send(network,wire); sync.pose=pose;sync.lastMotion=now;sync.pendingMotion.reset();
    ++sync.motionsSent; sync.motionBytes+=wire.size();
}
}
void flush_piece_motion_frame(Runtime::GameContext& context) {
    auto& network=network_services(context);
    if(!context.session.roomScene.selfAlive ||
       (network.boardBudget && !network.boardBudget->full)) {
        network.boardSync.pendingMotion.reset();return;
    }
    if(network.boardSync.enabled && network.boardSync.pendingMotion &&
       context.real_now()-network.boardSync.lastMotion>=Runtime::Duration::from_seconds(1./kMotionFramesPerSecond))
        emit_motion(context,context.session.player,*network.boardSync.pendingMotion);
}
void configure_board_sync(Runtime::GameContext& context,bool peerSupportsHybrid,std::uint32_t epoch) {
    auto& network=network_services(context); network.boardSync={};
    auto& sync=network.boardSync; sync.enabled=peerSupportsHybrid && !uses_local_duplex(network);
    sync.epoch=epoch; sync.incoming.reset(epoch);
}
bool hybrid_board_sync_enabled(const Runtime::GameContext& context) noexcept {
    const auto* network=context.network_component();
    return network && network->boardSync.enabled && !uses_local_duplex(*network);
}
Runtime::Duration multiplayer_frame_poll_interval(const Runtime::GameContext& context) noexcept {
    return Runtime::Duration::from_seconds(hybrid_board_sync_enabled(context) ? 1.0/kMotionFramesPerSecond : 0.05);
}
void request_board_snapshot(Runtime::GameContext& context) {
    auto& sync=network_services(context).boardSync;
    if (!sync.enabled) return;
    sync.incoming.invalidate_anchor();
    const auto now=context.real_now();
    if (!sync.requested || now-sync.lastRequest>=Runtime::Duration::from_seconds(0.1)) {
        send(network_services(context),encode_snapshot_request(sync.epoch));
        sync.requested=true; sync.lastRequest=now;
    }
}
void send_piece_motion_frame(Runtime::GameContext& context,const Runtime::PlayerSessionState& player,Piece::Pose pose,
    bool coalesceWithinDisplayFrame) {
    if (!hybrid_board_sync_enabled(context) || context.config.mode.replayMode) return;
    auto& network=network_services(context);
    // A thumbnail has no falling piece. Avoid serializing and hashing the
    // whole field for every local key when nobody requests this full board.
    if(network.multiplayerRoom && network.boardBudget && !network.boardBudget->full) {
        send_board_state_frame(context,player,ActivePieceState::active(pose));return;
    }
    const auto state=encode_board_state(context,player,ActivePieceState::active(pose));
    const auto hash=board_state_fingerprint(state);
    auto& sync=network_services(context).boardSync;
    if (!sync.pose || current_wire_piece(player)!=sync.piece || hash!=sync.fingerprint)
        send_board_state_frame(context,player,ActivePieceState::active(pose),std::nullopt,true);
    else if(coalesceWithinDisplayFrame && context.real_now()-sync.lastMotion<Runtime::Duration::from_seconds(1./kMotionFramesPerSecond))
        sync.pendingMotion=pose;
    else emit_motion(context,player,pose);
}

void configure_piece_turns(Runtime::GameContext& context, double peerTurnPieces,
                           int playerIndex, std::uint32_t epoch) {
    auto& state = network_services(context).pieceTurns;
    state = {};
    state.enabled = !context.config.mode.replayMode && context.config.turnPieces > 0.0 && peerTurnPieces > 0.0 &&
                    (playerIndex == 0 || playerIndex == 1);
    state.playerIndex = playerIndex;
    state.epoch = epoch;
    state.piecesPerTurn = context.config.turnPieces;
}

void resend_piece_turn(Runtime::GameContext& context, bool force) {
    auto& network = network_services(context);
    auto& state = network.pieceTurns;
    if (!state.enabled || context.config.mode.replayMode) return;
    const auto now = context.real_now();
    if (!force && now - state.lastSent < Runtime::Duration::from_seconds(0.1)) return;
    send(network, encode_piece_turn(state.epoch, state.completed));
    state.lastSent = now;
}

void send_attack_frame(Runtime::GameContext& context, const OutgoingAttackFrame& frame) {
    auto& network=network_services(context);
    const auto wire=encode_attack_frame(frame);
    if(network.multiplayerRoom) {
        network.outgoingAttacks[frame.sendId.value()]=wire;
        for(auto& pending:context.session.player.multiplayer.pendingSends) if(pending.sequenceId==frame.sendId) pending.targetSlot=network.targetSlot;
    }
    send(network,wire);
}

void send_round_signal(Runtime::GameContext& context, OutgoingRoundSignal signal) {
    if(network_services(context).multiplayerRoom && signal==OutgoingRoundSignal::LocalLoss) {
        send(network_services(context),"!loss-v1;"+std::to_string(context.session.player.multiplayer.lastDamageSource));return;
    }
    send(network_services(context), encode_round_signal(signal));
}

void send_board_state_frame(
    Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    ActivePieceState activePiece,
    std::optional<RemotePlacementSnapshot> placement,
    bool forceCheckpoint) {
    auto& network=network_services(context);
    if (uses_local_duplex(network)) return;
    const auto now=context.real_now();
    if(network.multiplayerRoom && network.boardBudget) {
        const auto& budget=*network.boardBudget;
        if(placement)network.boardSync.lastPlacement=placement;
        send_mini_board_frame(context,player);
        if(!budget.full)return;
    }
    const auto state=encode_board_state(context,player,activePiece,placement);
    auto& sync=network.boardSync;
    if (!sync.enabled) {send(network,state); return;}
    const auto hash=board_state_fingerprint(state);
    const bool changed=sync.revision==0 || hash!=sync.fingerprint;
    if (!changed && !placement && !forceCheckpoint &&
        now-sync.lastSnapshot<Runtime::Duration::from_seconds(0.5)) {
        if (activePiece.kind!=ActivePieceStateKind::Locked) emit_motion(context,player,wire_pose(player,activePiece));
        return;
    }
    if (changed) {++sync.revision; sync.fingerprint=hash;}
    const auto wire=encode_board_checkpoint(next_stamp(sync,now),state);
    const auto payload=network.multiplayerRoom ? "!board-v1;"+std::to_string(player.gameplay.playfield.width)+";"+
        std::to_string(player.gameplay.playfield.visibleHeight)+";"+std::to_string(player.gameplay.playfield.nextCount)+";"+wire : wire;
    send(network,payload); sync.lastSnapshot=now; sync.piece=current_wire_piece(player);
    sync.pendingMotion.reset();sync.lastMotion=now;
    sync.pose=activePiece.kind==ActivePieceStateKind::Locked ? std::optional<Piece::Pose>{} : wire_pose(player,activePiece);
    sync.lastPlacement=placement;
    ++sync.snapshotsSent; sync.snapshotBytes+=wire.size();
}

std::optional<FramePacket> try_receive_frame(
    Runtime::GameContext& context,
    int boardWidth,
    int visibleHeight) {
    auto& network=network_services(context); auto& sync=network.boardSync;
    if(network.multiplayerRoom) return try_receive_multiplayer_frame(context);
    while (!sync.terminal) {
        auto wire=try_receive(network); if (!wire) break;
        if (auto packet=decode_frame_packet(*wire,boardWidth,visibleHeight)) {
            if (packet->type==FramePacketType::PieceTurn) {
                auto& state=network.pieceTurns;
                if (state.enabled && packet->turnEpoch==state.epoch)
                    state.peerCompleted=std::max(state.peerCompleted,packet->completedTurns);
                continue;
            }
            if (packet->type==FramePacketType::SnapshotRequest) {
                if (sync.enabled && packet->turnEpoch==sync.epoch && (!sync.responded ||
                    context.real_now()-sync.lastResponse>=Runtime::Duration::from_seconds(0.1))) {
                    const auto& player=context.session.player;
                    const auto active=player.presentation.activePose
                        ? ActivePieceState::active(*player.presentation.activePose)
                        : sync.pose ? ActivePieceState::active(*sync.pose)
                        : sync.lastPlacement ? ActivePieceState::locked() : ActivePieceState::spawn();
                    const auto placement=active.kind==ActivePieceStateKind::Locked ? sync.lastPlacement : std::nullopt;
                    send_board_state_frame(context,player,active,placement,true);
                    sync.responded=true; sync.lastResponse=context.real_now();
                }
                continue;
            }
            if (packet->checkpoint || packet->motion) {
                if (sync.enabled) sync.incoming.insert(std::move(*packet),context.real_now());
                continue;
            }
            if (sync.enabled && packet->type==FramePacketType::OpponentState) continue;
            if (sync.enabled && (packet->type==FramePacketType::Win || packet->type==FramePacketType::OpponentLeft)) {
                sync.terminal=std::make_unique<FramePacket>(std::move(*packet)); break;
            }
            return packet;
        }
    }
    auto packet=sync.enabled ? sync.incoming.next(context.real_now()+
        Runtime::Duration::from_seconds(sync.terminal ? 0.3 : 0.0)) : std::nullopt;
    if (!packet && sync.terminal) {packet=std::move(*sync.terminal); sync.terminal.reset();}
    if (sync.enabled && !sync.terminal && sync.incoming.needs_snapshot() &&
        (!packet || (packet->type!=FramePacketType::Win && packet->type!=FramePacketType::OpponentLeft))) request_board_snapshot(context);
    return packet;
}

void acknowledge_round_complete(Runtime::GameContext& context) {
    if (uses_local_duplex(network_services(context))) {
        drain(network_services(context));
        return;
    }
    drain(network_services(context));
    send(network_services(context), std::string(encode_round_acknowledgement()));
}

} // namespace Network
