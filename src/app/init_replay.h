#ifndef TETR_APP_INIT_REPLAY_H
#define TETR_APP_INIT_REPLAY_H

namespace Runtime {
enum class GameMode;
class GameContext;
}

namespace App::InitDetail {

bool configure_replay_or_local_game(
    Runtime::GameContext& context,
    Runtime::GameMode& selectedMode,
    Runtime::GameContext* localPlayerTwoContext = nullptr);

} // namespace App::InitDetail

#endif
