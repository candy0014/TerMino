#ifndef TETR_ENGINE_GAME_END_PRESENTATION_H
#define TETR_ENGINE_GAME_END_PRESENTATION_H


namespace Runtime { class GameContext; struct PlayerSessionState; class Duration; }

namespace Engine {

void render_final_mode_result(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Duration elapsedGameTime);

} // namespace Engine

#endif
