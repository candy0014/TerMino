#ifndef TETR_ENGINE_GAME_LOOP_ADAPTER_H
#define TETR_ENGINE_GAME_LOOP_ADAPTER_H
#include "tetr/runtime/piece.h"
#include "tetr/rules/game_result.h"
#include <functional>
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Engine {
struct PieceLoopState;
enum class GameLoopExit { GoalReached, Dead, OpenSettings, OpponentLeft, Won, Retry };
using PlayPiece = std::function<Rules::PlayResult(Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState&,bool)>;
GameLoopExit run_piece_loop(Runtime::GameContext& context,Runtime::PlayerSessionState& player,const PlayPiece& playPiece,
    PieceLoopState* loopState=nullptr);
void close_multiplayer_round(Runtime::GameContext& context);
}
#endif
