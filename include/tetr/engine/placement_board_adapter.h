#ifndef TETR_ENGINE_PLACEMENT_BOARD_ADAPTER_H
#define TETR_ENGINE_PLACEMENT_BOARD_ADAPTER_H
#include "tetr/engine/placement_application.h"
namespace Runtime { class GameContext; struct PlayerState; }
namespace Engine {
PlacementApplicationState capture_placement_application_state(const Runtime::PlayerState& player,const Runtime::GameContext& context);
PlacementApplicationConfig capture_placement_application_config(const Runtime::GameContext& context,int blitzLevel);
void store_pre_offset_application_state(Runtime::PlayerState& player,const PlacementApplicationState& state);
void store_post_offset_application_state(Runtime::PlayerState& player,const PlacementApplicationState& state,Runtime::GameContext& context);
}
#endif
