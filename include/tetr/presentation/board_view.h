#ifndef TETR_PRESENTATION_BOARD_VIEW_H
#define TETR_PRESENTATION_BOARD_VIEW_H

#include "tetr/presentation/visibility.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/visual_effects.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Runtime { class Playfield; }

namespace Presentation {

// Default snapshot coverage. Consumers can request a larger hidden region.
// The terminal/wire
// display extent remains independently defined by Runtime::kDisplayHiddenRows.
inline constexpr int kBoardViewHiddenRows=10;

struct CellView {
    Runtime::BoardCell value;
    CellAppearance appearance = CellAppearance::Empty;
    bool hint=false;
    float opacity=1;
};
struct PieceView {
    Piece::Type type;
    Piece::Pose pose;
    CellAppearance appearance = CellAppearance::Visible;
};
struct PlacementView {
    int sequence=0;
    PieceView piece;
    std::vector<int> clearedRows;
    std::optional<int> dropFromRow;
};
struct GoalView {
    Runtime::GameGoal type=Runtime::GameGoal::None;
    int target=0,progress=0,remaining=0,markerRows=0;
    std::string unit;

    // Clear counts and starting-cell counts remain in the side panels only.
    bool supports_field_marker() const {
        return type!=Runtime::GameGoal::None && type!=Runtime::GameGoal::AllClear &&
            type!=Runtime::GameGoal::InitialCells;
    }
};
struct FieldGrowthView {
    int nextSize=0,remaining=0;
    double progress=0;
};
enum class RoundResult { None, Win, Defeat };
enum class GarbagePhase { Early, Incoming, Ready };
// Shared near-finish cue, using game time (including replay/remote adapters).
GoalView capture_goal_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player,Runtime::Duration elapsed);
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
    int nextCount=0; // Configured slots, including empty slots in a finite sequence.
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
    std::vector<GarbagePhase> garbagePhases;
    bool finished=false;
    RoundResult result=RoundResult::None;
    std::vector<std::pair<int,int>> spinHistory;
    GoalView goal;
    FieldGrowthView growth;
    bool quickPlay=false;
    Runtime::VisualEffectsConfig effects;
    bool spatialEffects=true;
    std::optional<PlacementView> placement;
    int placementLines=0;
    Runtime::BoardMotionFeedback motion;
    double altitude=0,booster=0,experience=0,boosterProgress=0;
    int floor=0,windup=0,rank=0,stages=0,population=0,wins=0;
    // Game time since the active Windup warning began; zero without a warning.
    // Uses the shared Windup schedule's game clock, including pause and replay.
    Runtime::Duration windupAge;
    std::string headline,stageTask;

    const CellView& at(int row, int column) const;
};

// Copies semantic display values only. Hidden live cells are replaced with
// empty values. Capturing a view never consumes RNG, input, replay or time.
BoardView capture_board_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    Runtime::Duration elapsed,int hiddenRows=kBoardViewHiddenRows);

} // namespace Presentation
#endif
