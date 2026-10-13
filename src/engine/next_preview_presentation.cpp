#include "next_preview_presentation.h"

#include "tetr/engine/piece_queue.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/board_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"

namespace Engine {

void prepare_next_preview(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    auto& pieces = player.pieces;
    if (!pieces.bag.empty() && pieces.bag[0].startsNewBag) add_bag(context, player);

    const auto previous=pieces.nextPreview;

    pieces.nextPreview.clear();
    pieces.nextPreview.reserve(context.config.field.nextCount);
    for (int j = 0; j < context.config.field.nextCount && static_cast<std::size_t>(j+1)<pieces.bag.size(); ++j) {
        if (context.config.quickPlay.fool == Runtime::FoolMode::ReverseAprilFools2026) {
            pieces.nextPreview.emplace_back(Piece::fool_display_type(pieces.bag[j + 1].type));
        } else {
            pieces.nextPreview.emplace_back(pieces.bag[j + 1].type);
        }
    }

    Render::draw_next_preview_transition(context,player,previous);
    player.presentation.nextSpawnType=pieces.nextPreview.empty() ?
        std::optional<Piece::Type>{} : pieces.bag[1].type;
    player.presentation.nextSpawnPose.reset();
    Render::refresh_spawn_marker(context,player);
}

} // namespace Engine
