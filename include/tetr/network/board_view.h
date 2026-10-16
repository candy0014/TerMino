#ifndef TETR_NETWORK_BOARD_VIEW_H
#define TETR_NETWORK_BOARD_VIEW_H
#include "tetr/runtime/board_cell.h"
#include "tetr/network/board_state_codec.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace Runtime { struct PlayerSessionState; class GameContext; }
namespace Network {
// Dimensions are terminal pixels, not logical board cells. Every projection
// keeps the floor and scales by width; a tall field is cropped from the top.
struct BoardView {
    int slot=-1; bool full=false; int columns=0,rows=0,fps=0;
};
struct BoardViews { int remaining=0; std::vector<BoardView> players; bool highRate=false; };
struct BoardBudget { bool full=true; int columns=0,rows=0,fps=0; bool highRate=false; };
struct MiniBoardFrame {
    std::uint64_t sequence=0;
    int logicalWidth=0,logicalHeight=0,columns=0,rows=0;
    std::vector<Runtime::BoardCell> cells;
    std::optional<RemoteClockSample> clock;
    std::uint64_t receivedRevision=0; // Local reception marker, never serialized.
    Runtime::BoardCell at(int row,int column) const;
};
std::string encode_board_views(const BoardViews& views);
std::optional<BoardViews> decode_board_views(std::string_view wire);
std::string encode_board_budget(const BoardBudget& budget);
std::optional<BoardBudget> decode_board_budget(std::string_view wire);
MiniBoardFrame capture_mini_board(const Runtime::PlayerSessionState& player,int columns,int rows,std::uint64_t sequence);
MiniBoardFrame project_mini_board(const MiniBoardFrame& frame,int columns,int rows);
std::string encode_mini_board(const MiniBoardFrame& frame);
std::optional<MiniBoardFrame> decode_mini_board(std::string_view wire);
// Validate a transport queue candidate without allocating its decoded grid.
std::optional<std::uint64_t> validated_mini_board_sequence(std::string_view wire);
void publish_board_views(Runtime::GameContext& context,const BoardViews& views);
void send_mini_board_frame(Runtime::GameContext& context,const Runtime::PlayerSessionState& player);
void send_board_visual_tick(Runtime::GameContext& context);
}
#endif
