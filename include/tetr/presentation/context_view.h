#ifndef TETR_PRESENTATION_CONTEXT_VIEW_H
#define TETR_PRESENTATION_CONTEXT_VIEW_H

#include "tetr/presentation/board_view.h"

namespace Runtime { class GameContext; }
namespace Presentation {
Runtime::Duration board_elapsed(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player);
BoardView capture_board_view(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,int hiddenRows=kBoardViewHiddenRows);
}
#endif
