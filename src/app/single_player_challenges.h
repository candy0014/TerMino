#ifndef TETR_APP_SINGLE_PLAYER_CHALLENGES_H
#define TETR_APP_SINGLE_PLAYER_CHALLENGES_H
#include "tetr/app/settings_model.h"
#include "tetr/challenges/challenges.h"
namespace Runtime {class GameContext;struct PlayerSessionState;}
namespace App {
std::string challenge_name(const Challenges::Definition& definition,bool includeCategory=false);
std::string challenge_score(const Challenges::Definition& definition,Challenges::Score score);
Settings::Model::SettingNode make_challenge_menu(UserConfig::Settings& settings,const UserConfig::Settings* opponent=nullptr);
UserConfig::Settings* challenge_partner_settings(Runtime::GameContext& context);
void begin_single_player_challenges(Runtime::GameContext& context);
void finish_single_player_challenges(Runtime::GameContext& context,Runtime::PlayerSessionState& player,bool won=false);
bool attach_challenge_replay(Runtime::GameContext& context,const std::filesystem::path& path);
void restore_challenge_replay(Runtime::GameContext& context);
}
#endif
