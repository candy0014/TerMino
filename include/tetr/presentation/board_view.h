#ifndef TETR_PRESENTATION_BOARD_VIEW_H
#define TETR_PRESENTATION_BOARD_VIEW_H

#include "tetr/presentation/visibility.h"
#include "tetr/runtime/piece.h"
#include <optional>
#include <string>
#include <vector>

namespace Runtime { class Playfield; }

namespace Presentation {

struct CellView {
    Runtime::BoardCell value;
    CellAppearance appearance = CellAppearance::Empty;
};
struct PieceView {
    Piece::Type type;
    Piece::Pose pose;
    CellAppearance appearance = CellAppearance::Visible;
};
struct BoardView {
    int width = 0;
    int firstRow = 0;
    int bottom = 0;
    std::vector<CellView> cells;
    std::optional<PieceView> active;
    std::optional<PieceView> ghost;
    Piece::Type hold = Piece::Type::none();
    bool holdEnabled = false, holdAvailable = false;
    std::vector<Piece::Type> next;
    std::string username;
    Runtime::Duration elapsed;
    long long score = 0;
    int lines = 0, attack = 0, pieces = 0, combo = 0, b2b = 0;
    bool obscuredColors = false;

    const CellView& at(int row, int column) const;
};

// Copies semantic display values only. Hidden live cells are replaced with
// empty values. Capturing a view never consumes RNG, input, replay or time.
BoardView capture_board_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    Runtime::Duration elapsed);

} // namespace Presentation
#endif
