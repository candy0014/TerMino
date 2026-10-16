#ifndef TETR_NETWORK_MULTIPLAYER_CHANNEL_H
#define TETR_NETWORK_MULTIPLAYER_CHANNEL_H

#include "tetr/network/board_state_codec.h"
#include "tetr/network/frame_packet.h"

#include <optional>

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Network {
// Flush the latest movement after a paced WebSocket send, including when
// the player has stopped pressing keys.
void flush_piece_motion_frame(Runtime::GameContext& context);

void configure_board_sync(Runtime::GameContext& context, bool peerSupportsHybrid, std::uint32_t epoch);
bool hybrid_board_sync_enabled(const Runtime::GameContext& context) noexcept;
Runtime::Duration multiplayer_frame_poll_interval(const Runtime::GameContext& context) noexcept;
// Bot scripts can perform several zero-delay steps within one display frame.
// Coalesce those poses; ordinary input keeps its existing immediate path.
void send_piece_motion_frame(Runtime::GameContext& context, const Runtime::PlayerSessionState& player, Piece::Pose pose,
    bool coalesceWithinDisplayFrame=false);
void request_board_snapshot(Runtime::GameContext& context);

void send_attack_frame(Runtime::GameContext& context, const OutgoingAttackFrame& frame);
void send_round_signal(Runtime::GameContext& context, OutgoingRoundSignal signal);
// Hosts abandon the current round and prepare a new one; guests cannot retry.
// Returns false for an ignored guest retry, and interrupts a host's game loop.
bool handle_room_game_control(Runtime::GameContext& context, bool retry);
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
