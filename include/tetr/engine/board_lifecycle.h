#ifndef TETR_ENGINE_BOARD_LIFECYCLE_H
#define TETR_ENGINE_BOARD_LIFECYCLE_H
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Engine {
void initialize_board(Runtime::GameContext& context,Runtime::PlayerSessionState& player,bool initializeQueue=true);
bool grow_snowball_board(Runtime::GameContext& context,Runtime::PlayerSessionState& player);
void initialize_board_timers(Runtime::GameContext& context,Runtime::PlayerSessionState& player);
void initialize_quick_play(Runtime::GameContext& context,Runtime::PlayerSessionState& player);
}
#endif
