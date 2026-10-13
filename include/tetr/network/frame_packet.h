#ifndef TETR_NETWORK_FRAME_PACKET_H
#define TETR_NETWORK_FRAME_PACKET_H

#include "tetr/network/board_state_codec.h"
#include "tetr/network/hybrid_protocol.h"
#include "tetr/runtime/player_session_state.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace Network {

enum class FramePacketType {
    Win,
    OpponentLeft,
    Attack,
    OpponentState,
    PieceTurn,
    OpponentMotion,
    SnapshotRequest
};

struct FramePacket {
    FramePacketType type = FramePacketType::Attack;
    Runtime::MultiplayerSequenceId receiveId{};
    Runtime::MultiplayerSequenceId sendId{};
    int attack = 0;
    std::optional<BoardStateSnapshot> opponentState;
    std::uint32_t turnEpoch = 0;
    std::uint64_t completedTurns = 0;
    std::optional<BoardFrameStamp> checkpoint;
    std::optional<PieceMotionFrame> motion;
};

// Gameplay records that share the ! prefix with connection/room controls.
// This classifies wire families; decode_frame_packet validates their payloads.
bool is_gameplay_extension(std::string_view text) noexcept;

// Decode one historical multiplayer wire record. Protocol text is consumed at
// this boundary: callers receive a typed packet or nullopt for malformed data.
std::optional<FramePacket> decode_frame_packet(
    std::string_view text,
    int boardWidth,
    int visibleHeight);

struct OutgoingAttackFrame {
    Runtime::MultiplayerSequenceId sendId{};
    Runtime::MultiplayerSequenceId receiveId{};
    int attack = 0;
};

// Historical wire order is sender-sequence; acknowledged-sequence; attack.
// The receiver intentionally maps those first two fields to receiveId/sendId.
std::string encode_attack_frame(const OutgoingAttackFrame& frame);
std::string encode_piece_turn(std::uint32_t epoch, std::uint64_t completed);

enum class OutgoingRoundSignal {
    LocalLoss,
    LeaveRound
};

// LocalLoss is encoded as end1 because the server forwards it to the opponent,
// who interprets it as a win. LeaveRound is forwarded as opponent-left.
std::string encode_round_signal(OutgoingRoundSignal signal);
std::optional<OutgoingRoundSignal> decode_round_signal(std::string_view wire) noexcept;
bool is_round_acknowledgement(std::string_view wire) noexcept;
std::string_view encode_round_acknowledgement() noexcept;

enum class MultiplayerFrameSignal {
    Continue,
    Won,
    OpponentLeft
};

struct MultiplayerFrameStateRef {
    std::deque<Runtime::PendingAttack>& pendingSentAttacks;
    Runtime::MultiplayerSequenceId& receiveId;
    int& winCount;
};

struct AppliedFramePacket {
    MultiplayerFrameSignal signal = MultiplayerFrameSignal::Continue;
    int incomingAttack = 0;
    const BoardStateSnapshot* opponentState = nullptr;
    const PieceMotionFrame* opponentMotion = nullptr;
};

AppliedFramePacket apply_frame_packet(
    const FramePacket& packet,
    MultiplayerFrameStateRef state);

} // namespace Network

#endif
