#ifndef TETR_NETWORK_HYBRID_PROTOCOL_H
#define TETR_NETWORK_HYBRID_PROTOCOL_H
#include "tetr/runtime/strong_types.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Network {
struct RemoteClockSample {
    double elapsedSeconds = 0.0;
    double rate = 1.0;
};
inline constexpr double kMotionFramesPerSecond = 120.0;
struct BoardFrameStamp {
    std::uint32_t epoch = 0;
    std::uint64_t sequence = 0, frame = 0, revision = 0, fingerprint = 0;
};
enum class PieceMotionKind { Translate, Rotate };
// Resolved movement includes kick/drop displacement, avoiding dependence on
// the receiver's DAS, gravity, rotation-system or mod configuration.
struct PieceMotionFrame {
    BoardFrameStamp stamp;
    Piece::Type piece = Piece::Type::none();
    PieceMotionKind kind = PieceMotionKind::Translate;
    int rows = 0, columns = 0, rotations = 0;
    Piece::Pose result{};
    std::optional<RemoteClockSample> clock;
};
struct BoardCheckpointWire {
    BoardFrameStamp stamp;
    std::string state;
};
std::string encode_piece_motion(const PieceMotionFrame& motion);
std::optional<PieceMotionFrame> decode_piece_motion(std::string_view wire);
std::string encode_board_checkpoint(const BoardFrameStamp& stamp, std::string_view state);
std::optional<BoardCheckpointWire> decode_board_checkpoint(std::string_view wire);
std::string encode_snapshot_request(std::uint32_t epoch);
std::optional<std::uint32_t> decode_snapshot_request(std::string_view wire);
} // namespace Network
#endif
