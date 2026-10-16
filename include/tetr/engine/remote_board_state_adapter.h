#ifndef TETR_ENGINE_REMOTE_BOARD_STATE_ADAPTER_H
#define TETR_ENGINE_REMOTE_BOARD_STATE_ADAPTER_H

namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Network { struct BoardStateSnapshot; struct PieceMotionFrame; struct RemoteClockSample; }

namespace Engine {
void apply_remote_clock_sample(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
    const Network::RemoteClockSample& sample);

// Returns false when the authoritative motion cannot fit the known board;
// request a fresh checkpoint rather than mutating the board speculatively.
bool apply_remote_piece_motion(Runtime::GameContext& context, Runtime::PlayerSessionState& player,
    const Network::PieceMotionFrame& motion,bool render=true);

// Apply a decoded multiplayer snapshot to the opponent state and issue the
// corresponding presentation updates. Network decoding stays in Network;
// rendering primitives stay in Render; this orchestration belongs to Engine.
void apply_remote_board_state(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    const Network::BoardStateSnapshot& snapshot,bool render=true);

} // namespace Engine

#endif
