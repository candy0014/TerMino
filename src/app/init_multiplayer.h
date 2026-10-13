#ifndef TETR_APP_INIT_MULTIPLAYER_H
#define TETR_APP_INIT_MULTIPLAYER_H

#include <cstdint>

namespace Runtime {
class GameContext;
struct PlayerSessionState;
}

namespace App::InitDetail {

enum class LobbyWaitResult {
    Ready,
    Reconfigure
};

LobbyWaitResult wait_for_multiplayer_lobby(Runtime::GameContext& context);
std::uint32_t multiplayer_seed(Runtime::GameContext& context);
void exchange_multiplayer_player_setup(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& local,
    Runtime::PlayerSessionState& remote);

} // namespace App::InitDetail

#endif
