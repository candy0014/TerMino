#ifndef TETR_APP_BOT_BATTLE_ROYALE_H
#define TETR_APP_BOT_BATTLE_ROYALE_H
#include <memory>
namespace Runtime {class GameContext;}
namespace App {
class BotBattleRoyale;
struct BotRoyaleStatus {int population=0,alive=0,placements=0;};
std::shared_ptr<BotBattleRoyale> start_bot_battle_royale(Runtime::GameContext& context);
BotRoyaleStatus bot_battle_status(const std::shared_ptr<BotBattleRoyale>& arena);
}
#endif
