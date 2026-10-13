#ifndef TETR_ENGINE_NEXT_PREVIEW_PRESENTATION_H
#define TETR_ENGINE_NEXT_PREVIEW_PRESENTATION_H

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {

// Updates the typed NEXT preview and redraws it. Queue generation itself lives
// in piece_queue.cpp so the core queue model has no Render dependency.
void prepare_next_preview(Runtime::GameContext& context, Runtime::PlayerSessionState& player);

} // namespace Engine

#endif
