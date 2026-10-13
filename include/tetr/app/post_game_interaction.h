#ifndef TETR_APP_POST_GAME_INTERACTION_H
#define TETR_APP_POST_GAME_INTERACTION_H

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace App {

// Capture at the round's exit, before post-game interaction or a partner wait.
void freeze_post_game_times(Runtime::GameContext& context, Runtime::PlayerSessionState& player);

void run_post_game_interaction(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player);

void run_local_versus_post_game_interaction(
    Runtime::GameContext& first,
    Runtime::GameContext& second);

} // namespace App

#endif
