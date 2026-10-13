#ifndef TETR_APP_INIT_PLAYER_SETUP_H
#define TETR_APP_INIT_PLAYER_SETUP_H

namespace Runtime {
class GameContext;
struct PlayerSessionState;
struct RuntimeConfig;
}

namespace App::InitDetail {

void reset_player_board(Runtime::PlayerSessionState& player, const Runtime::RuntimeConfig& config);
void draw_initial_board(Runtime::GameContext& context, Runtime::PlayerSessionState& player);
void initialize_player_field(Runtime::GameContext& context, Runtime::PlayerSessionState& player);

} // namespace App::InitDetail

#endif
