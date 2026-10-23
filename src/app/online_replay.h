#ifndef TETR_APP_ONLINE_REPLAY_H
#define TETR_APP_ONLINE_REPLAY_H
#include <cstdint>
#include <optional>
#include <string>
namespace Runtime {class GameContext;}
namespace App {
class PostGameStatisticsView;
void finish_online_replay(Runtime::GameContext&);
std::optional<std::string> download_room_replay(Runtime::GameContext&,std::uint64_t room,std::uint64_t epoch);
bool save_online_room_replay(Runtime::GameContext&,std::uint64_t room,std::uint64_t epoch,PostGameStatisticsView* statistics=nullptr);
void run_room_replay_menu(Runtime::GameContext&);
enum class RoomReplayExit { Back, Retry, Settings };
RoomReplayExit view_multiplayer_replay(Runtime::GameContext&,const std::string& recording);
void run_multiplayer_replay(Runtime::GameContext&);
}
#endif
