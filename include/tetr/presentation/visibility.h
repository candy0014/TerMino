#ifndef TETR_PRESENTATION_VISIBILITY_H
#define TETR_PRESENTATION_VISIBILITY_H

#include "tetr/runtime/board_cell.h"
#include "tetr/runtime/time.h"

namespace Runtime { struct RuntimeConfig; struct PlayerSessionState; }

namespace Presentation {

enum class CellAppearance { Empty, Visible, Dimmed };

bool board_cell_visible(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, int row, int column,
    Runtime::BoardCell cell, Runtime::Duration elapsed);
CellAppearance board_cell_appearance(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, int row, int column,
    Runtime::BoardCell cell, Runtime::Duration elapsed);
bool active_piece_visible(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, Runtime::Duration elapsed);
CellAppearance active_piece_appearance(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, Runtime::Duration elapsed);

} // namespace Presentation
#endif
