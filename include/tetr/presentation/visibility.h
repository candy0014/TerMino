#ifndef TETR_PRESENTATION_VISIBILITY_H
#define TETR_PRESENTATION_VISIBILITY_H

#include "tetr/runtime/board_cell.h"
#include "tetr/runtime/time.h"
#include "tetr/runtime/field_visibility.h"

namespace Runtime { struct RuntimeConfig; struct PlayerSessionState; }

namespace Presentation {

enum class CellAppearance { Empty, Visible, Dimmed };
Runtime::FieldVisibility field_visibility(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player);
float board_cell_opacity(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player,int row,int column,
    Runtime::BoardCell cell,Runtime::Duration elapsed);

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
