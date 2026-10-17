#include "tetr/presentation/board_view.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_config.h"
#include <algorithm>
#include <stdexcept>

namespace Presentation {

const CellView& BoardView::at(int row, int column) const {
    if (row < firstRow || row >= bottom || column < 0 || column >= width)
        throw std::out_of_range("board view cell outside captured bounds");
    return cells.at(static_cast<std::size_t>(row - firstRow) * width + column);
}

BoardView capture_board_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    Runtime::Duration elapsed) {
    BoardView view;
    view.width = field.width();
    view.firstRow = -Runtime::kDisplayHiddenRows;
    view.bottom = std::clamp(player.gameplay.playfield.visibleHeight, 0, field.height());
    view.elapsed = elapsed;
    view.obscuredColors = config.display.obscuredColors;
    view.cells.reserve(static_cast<std::size_t>(view.bottom - view.firstRow) * view.width);
    for (int row = view.firstRow; row < view.bottom; ++row) {
        for (int column = 0; column < view.width; ++column) {
            const auto value = field.cell(row, column);
            const auto appearance = board_cell_appearance(config, player, row, column, value, elapsed);
            view.cells.push_back({appearance == CellAppearance::Empty ? Runtime::BoardCell::empty() : value, appearance});
        }
    }
    const auto type = player.presentation.activePieceType;
    const auto pose = player.presentation.activePose;
    const auto appearance = active_piece_appearance(config, player, elapsed);
    if (type && type->valid() && pose && appearance != CellAppearance::Empty) {
        view.active = PieceView{*type, *pose, appearance};
        if (config.field.ghost) {
            const auto piece = Piece::get_definition(*type);
            auto landing = *pose;
            while (piece.can_place(landing.translated(1, 0), field)) ++landing.position.row;
            view.ghost = PieceView{*type, landing, CellAppearance::Dimmed};
        }
    }
    const auto& hold = player.gameplay.hold;
    view.hold = hold.piece;
    view.holdEnabled = player.gameplay.playfield.holdEnabled;
    view.holdAvailable = hold.remoteAvailable.value_or(Runtime::hold_available(config.field, hold.used));
    view.next = player.pieces.nextPreview;
    view.username = player.presentation.username;
    const auto& score = player.gameplay.scoring;
    view.score = score.score; view.lines = score.totalLines; view.attack = score.totalAttack;
    view.pieces = score.placedBlocks; view.combo = score.combo; view.b2b = score.b2b;
    return view;
}

} // namespace Presentation
