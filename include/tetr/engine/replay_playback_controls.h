#ifndef TETR_ENGINE_REPLAY_PLAYBACK_CONTROLS_H
#define TETR_ENGINE_REPLAY_PLAYBACK_CONTROLS_H

#include "tetr/render/terminal_renderer.h"

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Engine {

// Reinitializing from the original replay is required for backward seeking;
// this lightweight signal unwinds whichever replay wait loop is active.
struct ReplaySeekRequested {};

// Extent of the visible playback controls, sharing their actual text/layout.
Render::TerminalSize replay_playback_terminal_size(Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool compact=false, bool maximum=true);

// Draws the initial 0:00 timeline without polling playback controls. App
// calls this before an optional start countdown; playback starts afterwards.
void render_replay_playback_progress(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player);

// Polls the replay-only Space/-/+/Left/Right controls and refreshes the
// timeline below the supplied board. It advances normally while processing is
// caught up and follows applied event time under backlog. Calls are cheap
// and internally rate-limit redraws. A seek
// throws ReplaySeekRequested so App can rebuild.
void update_replay_playback_controls(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    bool forceRender = false);

// A mode goal can end the game loop before the remaining due EndAll record is
// consumed. Finalize a silent seek in that terminal state so output cannot
// remain suppressed on the last frame.
void complete_replay_rebuild_at_game_end(Runtime::GameContext& context);

} // namespace Engine

#endif
