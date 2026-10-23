#ifndef TETR_NETWORK_BOARD_STATE_CODEC_H
#define TETR_NETWORK_BOARD_STATE_CODEC_H

#include "tetr/runtime/playfield.h"
#include "tetr/runtime/strong_types.h"
#include "tetr/network/hybrid_protocol.h"
#include "tetr/runtime/field_visibility.h"

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Network {

enum class ActivePieceStateKind { Spawn, Active, Locked };
struct ActivePieceState {
    ActivePieceStateKind kind = ActivePieceStateKind::Spawn;
    Piece::Pose pose{};
    static constexpr ActivePieceState spawn() noexcept { return {}; }
    static constexpr ActivePieceState active(Piece::Pose pose) noexcept { return {ActivePieceStateKind::Active, pose}; }
    static constexpr ActivePieceState locked() noexcept { return {ActivePieceStateKind::Locked, {}}; }
};

enum class RemoteGarbageCell : unsigned char {
    Empty = 0,
    Incoming = 1,
    Ready = 2,
    EarlyBuffer = 3
};

struct RemoteScoreSnapshot {
    int wins = 0;
    int placedBlocks = 0;
    int totalAttack = 0;
};

struct RemotePlacementSnapshot {
    Piece::Type piece = Piece::Type::none();
    bool spin = false;
    bool mini = false;
    int clearedLines = 0;
    int b2b = 0;
    int combo = 0;
    bool perfectClear = false;
    // The existing wire clear field uses 0=none, 1=perfect, 2=half.
    bool halfClear = false;
};

// Decoded form of the historical !board-state wire record.  The codec owns
// all knowledge of semicolon sections, comma-delimited piece ids and the
// legacy cell alphabet.  Render/Engine code should only consume this object.
struct BoardStateSnapshot {
    Runtime::Playfield field;
    std::vector<RemoteGarbageCell> garbage;
    Piece::Type currentPiece = Piece::Type::none();
    std::vector<Piece::Type> nextPreview;
    Piece::Type hold = Piece::Type::none();
    Piece::Pose activePose{};
    RemoteScoreSnapshot score;
    std::optional<RemotePlacementSnapshot> placement;
    std::optional<bool> holdAvailable;
    std::optional<Piece::Type> nextSpawnType;
    std::optional<Piece::Pose> nextSpawnPose;
    std::optional<RemoteClockSample> clock;
};
RemoteClockSample capture_player_clock(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player);
// Optional display control, separate from historical board/setup records.
std::string encode_field_visibility(const Runtime::FieldVisibility& visibility);
std::optional<Runtime::FieldVisibility> decode_field_visibility(std::string_view wire);
void send_field_visibility(Runtime::GameContext& context);

// Hash the authoritative field, queue, hold and HUD, excluding active pose and placement notifications.
std::uint64_t board_state_fingerprint(std::string_view state);

std::string encode_board_state(
    const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    ActivePieceState activePiece = ActivePieceState::spawn(),
    std::optional<RemotePlacementSnapshot> placement = std::nullopt);

// Returns nullopt for malformed or dimension-incompatible wire input.
std::optional<BoardStateSnapshot> decode_board_state(
    std::string_view state,
    int width,
    int visibleHeight);

} // namespace Network
#endif
