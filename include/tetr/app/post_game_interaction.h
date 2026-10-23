#ifndef TETR_APP_POST_GAME_INTERACTION_H
#define TETR_APP_POST_GAME_INTERACTION_H
#include <string>

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace App {
class PostGameStatisticsView;

// Capture at the round's exit, before post-game interaction or a partner wait.
void freeze_post_game_times(Runtime::GameContext& context, Runtime::PlayerSessionState& player);
bool save_finished_online_replay(Runtime::GameContext& context,PostGameStatisticsView* statistics=nullptr);
bool save_replay_copy(Runtime::GameContext& context,const std::string& recording,
    bool updateRecent=false,PostGameStatisticsView* statistics=nullptr,bool incomplete=false);
void remember_finished_replay(Runtime::GameContext& context,const std::string& recording={});

void run_post_game_interaction(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player);

void run_local_versus_post_game_interaction(
    Runtime::GameContext& first,
    Runtime::GameContext& second);

} // namespace App

#endif
