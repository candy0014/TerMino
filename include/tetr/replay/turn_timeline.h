#ifndef TETR_REPLAY_TURN_TIMELINE_H
#define TETR_REPLAY_TURN_TIMELINE_H

#include "tetr/replay/operation.h"
namespace Runtime { class GameContext; }
namespace Replay {
void configure_turn_playback(Runtime::GameContext& first, Runtime::GameContext& second);
bool can_start_turn(const Runtime::GameContext& context);
void mark_operation_consumed(Runtime::GameContext& context, const Operation& operation);
void finish_turn_playback(Runtime::GameContext& context);
} // namespace Replay
#endif
