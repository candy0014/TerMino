#ifndef TETR_RENDER_BOARD_RENDERER_H
#define TETR_RENDER_BOARD_RENDERER_H
#include "tetr/presentation/visibility.h"

#include "tetr/runtime/strong_types.h"
#include "tetr/runtime/board_cell.h"
#include "tetr/render/terminal_renderer.h"
#include <optional>

namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace Render {

bool board_cell_visible(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int row, int column, Runtime::BoardCell cell);
using BoardCellAppearance = Presentation::CellAppearance;
BoardCellAppearance board_cell_appearance(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int row, int column, Runtime::BoardCell cell);
void draw_board_cell(const Runtime::GameContext& context, const Runtime::PlayerSessionState& player,
    int row, int column, Runtime::BoardCell cell);
bool spawn_marker_cell(const Runtime::GameContext& context, const Runtime::PlayerSessionState& player,
    int row, int column);
bool board_hint_cell(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,int row,int column);
void draw_empty_board_cell(const Runtime::GameContext& context, const Runtime::PlayerSessionState& player,
    int row, int column);
// Recompute the footprint and repaint old/new cells from the current field,
// painted active piece and ghost. Does not mutate gameplay state.
void refresh_spawn_marker(const Runtime::GameContext& context, Runtime::PlayerSessionState& player);

void move_to(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,int row,int col,int offset=0);
void move_hold(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,int row,int col,Piece::Type pieceType);
void move_next(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player,int previewIndex,int row,int col,Piece::Type pieceType);

void print_score(const Runtime::GameContext& context,Runtime::PlayerSessionState& player);
void print_altitude(const Runtime::GameContext& context,Runtime::PlayerSessionState& player);
void print_message(const Runtime::GameContext& context,Runtime::PlayerSessionState& player);
double board_elapsed_seconds(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player);
void print_border(const Runtime::GameContext& context,Runtime::PlayerSessionState& player);
void print_racing_goal_marker(const Runtime::GameContext& context,Runtime::PlayerSessionState& player);
int board_footer_base_row(const Runtime::PlayerSessionState& player);
// Fifteen terminal rows, centered between HOLD and the field floor when
// they fit; otherwise the first row immediately follows HOLD.
inline constexpr int kBoardInfoRows=15;
int board_info_top_row(const Runtime::PlayerSessionState& player);
struct BoardVerticalExtent { int coreBottom, fullBottom, footer; }; // Relative, exclusive bottoms.
BoardVerticalExtent board_vertical_extent(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum=true);
// Absolute terminal extent, including the largest permitted Snowball field
// and the mode's footer. Dimensions are in terminal cells, not board cells.
TerminalSize board_terminal_size(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum=true);
struct HorizontalExtent { int left, right; }; // Right is exclusive.
HorizontalExtent board_core_horizontal_extent(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum=true);
HorizontalExtent board_horizontal_extent(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum=true);
struct BoardTextLayout { int column, columns; };
// Compact text beneath the field, ending before the NEXT panel.
BoardTextLayout board_text_layout(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player);
void erase_player_board(const Runtime::GameContext& context,Runtime::PlayerSessionState& player);
void print_spin(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,Piece::Type pieceType,bool mini);
void print_clear(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,int lines);
void print_b2b(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,std::optional<int> value=std::nullopt);
void print_combo(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,std::optional<int> value=std::nullopt);
void print_all_clear(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,bool enabled,bool halfClear=false);
void print_blighted(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,std::optional<bool> value=std::nullopt);
void print_clutch(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int consecutive);
void print_spike(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player);
void print_windup(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,
                   bool force=false);

// Repaints a player entirely from current runtime state. Replay seeking uses
// this after silently rebuilding instead of exposing every intermediate frame.
void redraw_player_board(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player);


} // namespace Render

#endif
