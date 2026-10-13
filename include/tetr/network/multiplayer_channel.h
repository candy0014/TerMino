#ifndef TETR_NETWORK_MULTIPLAYER_CHANNEL_H
#define TETR_NETWORK_MULTIPLAYER_CHANNEL_H

#include "tetr/network/board_state_codec.h"
#include "tetr/network/frame_packet.h"

#include <optional>

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Network {

void configure_board_sync(Runtime::GameContext& context, bool peerSupportsHybrid, std::uint32_t epoch);
bool hybrid_board_sync_enabled(const Runtime::GameContext& context) noexcept;
Runtime::Duration multiplayer_frame_poll_interval(const Runtime::GameContext& context) noexcept;
void send_piece_motion_frame(Runtime::GameContext& context, const Runtime::PlayerSessionState& player, Piece::Pose pose);
void request_board_snapshot(Runtime::GameContext& context);

void send_attack_frame(Runtime::GameContext& context, const OutgoingAttackFrame& frame);
void send_round_signal(Runtime::GameContext& context, OutgoingRoundSignal signal);
void configure_piece_turns(Runtime::GameContext& context, double peerTurnPieces,
                           int playerIndex, std::uint32_t epoch);
void resend_piece_turn(Runtime::GameContext& context, bool force = false);

void send_board_state_frame(
    Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    ActivePieceState activePiece = ActivePieceState::spawn(),
    std::optional<RemotePlacementSnapshot> placement = std::nullopt,
    bool forceCheckpoint = false);

// Consume malformed datagrams inside the network boundary and return the next
// valid typed frame, or nullopt when the non-blocking socket has no more data.
std::optional<FramePacket> try_receive_frame(
    Runtime::GameContext& context,
    int boardWidth,
    int visibleHeight);

// End-of-round acknowledgement remains a wire-level "end" token, hidden here.
void acknowledge_round_complete(Runtime::GameContext& context);

} // namespace Network

#endif
