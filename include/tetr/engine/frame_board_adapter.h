#ifndef TETR_ENGINE_FRAME_BOARD_ADAPTER_H
#define TETR_ENGINE_FRAME_BOARD_ADAPTER_H
#include "tetr/rules/frame_update.h"
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Engine {
Rules::FrameRuleState capture_frame_rule_state(const Runtime::PlayerSessionState& player,const Runtime::GameContext& context);
void store_frame_rule_state(Runtime::PlayerSessionState& player,const Rules::FrameRuleState& state,Runtime::GameContext& context);
Rules::FrameRuleConfig make_frame_rule_config(const Runtime::GameContext& context,Runtime::Duration elapsedGameTime);
}
#endif
