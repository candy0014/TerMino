#include "tetr/network/frame_packet.h"

#include <algorithm>
#include "tetr/util/text_codec.h"

#include <string>

namespace Network {
namespace {

std::optional<FramePacket> decode_attack_packet(std::string_view text) {
    const auto fields = Util::Text::split_exact<3>(text, ';');
    if (!fields) return std::nullopt;

    FramePacket packet;
    packet.type = FramePacketType::Attack;
    const auto receiveId = Util::Text::parse_integer<int>((*fields)[0]);
    const auto sendId = Util::Text::parse_integer<int>((*fields)[1]);
    const auto attack = Util::Text::parse_integer<int>((*fields)[2]);
    if (!receiveId || !sendId || !attack || *receiveId < 0 || *sendId < 0 || *attack < 0) {
        return std::nullopt;
    }
    packet.receiveId = Runtime::MultiplayerSequenceId::from_wire(*receiveId);
    packet.sendId = Runtime::MultiplayerSequenceId::from_wire(*sendId);
    packet.attack = *attack;
    return packet;
}

} // namespace

bool is_gameplay_extension(std::string_view text) noexcept {
    for (const auto prefix : {std::string_view("!piece-turn;"),std::string_view("!motion-v1;"),
                              std::string_view("!snapshot-v1;"),std::string_view("!snapshot-request;")})
        if (text.substr(0,prefix.size())==prefix) return true;
    if (text.size() < 2 || text.front() != '!') return false;
    // Historical board snapshots begin with their first cell, rather than a
    // named command. Empty, ordinary and timed cells use -, 0-9 and A-Z.
    const char cell = text[1];
    return cell == '-' || (cell >= '0' && cell <= '9') || (cell >= 'A' && cell <= 'Z');
}

std::string encode_attack_frame(const OutgoingAttackFrame& frame) {
    return std::to_string(frame.sendId.value()) + ";" +
        std::to_string(frame.receiveId.value()) + ";" +
        std::to_string(frame.attack);
}

std::string encode_piece_turn(std::uint32_t epoch, std::uint64_t completed) {
    return "!piece-turn;" + std::to_string(epoch) + ";" + std::to_string(completed);
}

std::string encode_round_signal(OutgoingRoundSignal signal) {
    return signal == OutgoingRoundSignal::LocalLoss ? "end1" : "end2";
}

std::optional<OutgoingRoundSignal> decode_round_signal(std::string_view wire) noexcept {
    if (wire == "end1") return OutgoingRoundSignal::LocalLoss;
    if (wire == "end2") return OutgoingRoundSignal::LeaveRound;
    return std::nullopt;
}

bool is_round_acknowledgement(std::string_view wire) noexcept {
    return wire == "end";
}

std::string_view encode_round_acknowledgement() noexcept {
    return "end";
}

std::optional<FramePacket> decode_frame_packet(
    std::string_view text,
    int boardWidth,
    int visibleHeight) {
    if (text.substr(0,10)=="!motion-v1") {
        auto motion=decode_piece_motion(text);
        if (!motion) return std::nullopt;
        FramePacket packet; packet.type=FramePacketType::OpponentMotion; packet.motion=std::move(*motion); return packet;
    }
    if (text.substr(0,12)=="!snapshot-v1") {
        const auto checkpoint=decode_board_checkpoint(text);
        if (!checkpoint || board_state_fingerprint(checkpoint->state)!=checkpoint->stamp.fingerprint) return std::nullopt;
        auto snapshot=decode_board_state(checkpoint->state,boardWidth,visibleHeight);
        if (!snapshot) return std::nullopt;
        FramePacket packet; packet.type=FramePacketType::OpponentState;
        packet.opponentState=std::move(*snapshot); packet.checkpoint=checkpoint->stamp; return packet;
    }
    if (text.substr(0,17)=="!snapshot-request") {
        const auto epoch=decode_snapshot_request(text);
        if (!epoch) return std::nullopt;
        FramePacket packet; packet.type=FramePacketType::SnapshotRequest; packet.turnEpoch=*epoch; return packet;
    }
    if (text.substr(0, 12) == "!piece-turn;") {
        const auto fields = Util::Text::split_exact<3>(text, ';');
        if (!fields) return std::nullopt;
        const auto epoch = Util::Text::parse_integer<std::uint32_t>((*fields)[1]);
        const auto completed = Util::Text::parse_integer<std::uint64_t>((*fields)[2]);
        if (!epoch || !completed) return std::nullopt;
        FramePacket packet;
        packet.type = FramePacketType::PieceTurn;
        packet.turnEpoch = *epoch;
        packet.completedTurns = *completed;
        return packet;
    }
    if (const auto roundSignal = decode_round_signal(text)) {
        FramePacket packet;
        packet.type = *roundSignal == OutgoingRoundSignal::LocalLoss
            ? FramePacketType::Win
            : FramePacketType::OpponentLeft;
        return packet;
    }
    if (!text.empty() && text.front() == '!') {
        auto snapshot = decode_board_state(text, boardWidth, visibleHeight);
        if (!snapshot) return std::nullopt;
        FramePacket packet;
        packet.type = FramePacketType::OpponentState;
        packet.opponentState = std::move(*snapshot);
        return packet;
    }
    return decode_attack_packet(text);
}

AppliedFramePacket apply_frame_packet(
    const FramePacket& packet,
    MultiplayerFrameStateRef state) {
    AppliedFramePacket result;
    if (packet.type==FramePacketType::PieceTurn || packet.type==FramePacketType::SnapshotRequest) return result;
    if (packet.type==FramePacketType::OpponentMotion) {
        if (packet.motion) result.opponentMotion=&*packet.motion;
        return result;
    }

    if (packet.type == FramePacketType::Win ||
        packet.type == FramePacketType::OpponentLeft) {
        ++state.winCount;
        result.signal = packet.type == FramePacketType::OpponentLeft
            ? MultiplayerFrameSignal::OpponentLeft
            : MultiplayerFrameSignal::Won;
        return result;
    }

    if (packet.type == FramePacketType::OpponentState) {
        if (packet.opponentState) result.opponentState = &*packet.opponentState;
        return result;
    }

    int attack = packet.attack;
    while (!state.pendingSentAttacks.empty() &&
           state.pendingSentAttacks.front().sequenceId <= packet.sendId) {
        state.pendingSentAttacks.pop_front();
    }
    while (!state.pendingSentAttacks.empty() && attack) {
        const int cancelled = std::min(
            state.pendingSentAttacks.front().attack,
            attack);
        attack -= cancelled;
        state.pendingSentAttacks.front().attack -= cancelled;
        if (!state.pendingSentAttacks.front().attack) {
            state.pendingSentAttacks.pop_front();
        }
    }
    state.receiveId = packet.receiveId;
    result.incomingAttack = attack;
    return result;
}

} // namespace Network
