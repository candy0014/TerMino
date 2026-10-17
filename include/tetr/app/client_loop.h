#ifndef TETR_APP_CLIENT_LOOP_H
#define TETR_APP_CLIENT_LOOP_H
namespace Runtime {class GameContext;}
namespace App {
// Shared startup/settings/game/room recovery loop. Platform lifecycle and
// input initialization belong to the caller.
int run_client(Runtime::GameContext& context,bool skipStart=false);
}
#endif
