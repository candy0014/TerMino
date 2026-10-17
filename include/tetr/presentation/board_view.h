#ifndef TETR_PRESENTATION_BOARD_VIEW_H
#define TETR_PRESENTATION_BOARD_VIEW_H

#include "tetr/presentation/visibility.h"
#include "tetr/runtime/piece.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Runtime { class Playfield; }

namespace Presentation {

struct CellView {
    Runtime::BoardCell value;
    CellAppearance appearance = CellAppearance::Empty;
    bool hint=false;
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
    int squareStyle=0,spike=0,clutch=0,clearLines=0;
    Piece::Type spinPiece=Piece::Type::none();
    bool mini=false,perfectClear=false,halfClear=false,blighted=false;
    std::vector<int> garbage;
    std::vector<double> garbageReadyIn;
    std::vector<std::pair<int,int>> spinHistory;
    Runtime::GameGoal goal=Runtime::GameGoal::None;
    int goalTarget=0,goalProgress=0;
    bool quickPlay=false;
    double altitude=0,booster=0,experience=0;
    int floor=0,windup=0,rank=0,stages=0,population=0,wins=0;
    std::string headline,stageTask;

    const CellView& at(int row, int column) const;
};

// Copies semantic display values only. Hidden live cells are replaced with
// empty values. Capturing a view never consumes RNG, input, replay or time.
BoardView capture_board_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    Runtime::Duration elapsed);

} // namespace Presentation
#endif
