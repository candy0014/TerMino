#include "tetr/render/board_renderer.h"

#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/rules/quick_play.h"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Render {
namespace {

bool shifted_left_panel(const Runtime::GameContext& context) {
    return Runtime::uses_garbage_buffer(context.config.garbage.mode);
}

} // namespace

void move_to(
    const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    int row,
    int col,
    int offset) {
    OutputGuard output(context);
    if (shifted_left_panel(context) && col < 0) --col;
    move_cursor(
        row + player.presentation.topMargin,
        col * 2 + player.presentation.margin + offset);
}

void move_hold(
    const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    int row,
    int col,
    Piece::Type pieceType) {
    OutputGuard output(context);
    const Piece::Metadata metadata = Piece::metadata(pieceType);
    move_to(
        context,
        player,
        row + player.presentation.blockMaxHeight + 1 -
            (player.presentation.blockMaxHeight - 2 + metadata.displayRowOffset) / 2,
        col - (player.presentation.blockMaxWidth + 1) / 2 - 2,
        player.presentation.blockMaxWidth % 2 + metadata.displayWidth % 2);
}

void move_next(
    const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    int previewIndex,
    int row,
    int col,
    Piece::Type pieceType) {
    OutputGuard output(context);
    const Piece::Metadata metadata = Piece::metadata(pieceType);
    move_to(
        context,
        player,
        row + (previewIndex + 1) * (player.presentation.blockMaxHeight + 1) -
            (player.presentation.blockMaxHeight - 2 + metadata.displayRowOffset) / 2,
        col + player.gameplay.playfield.width + player.presentation.blockMaxWidth / 2,
        player.presentation.blockMaxWidth % 2 + metadata.displayWidth % 2);
}

void print_racing_goal_marker(const Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    const auto& playfield=player.gameplay.playfield;
    const auto& scoring=player.gameplay.scoring;
    const auto goal=Runtime::active_goal(context.config);
    double remaining=0;std::string unit;
    switch(goal) {
        case Runtime::GameGoal::Time:
            remaining=std::ceil(context.config.mode.blitzTime.seconds()-board_elapsed_seconds(context,player));unit="s";break;
        case Runtime::GameGoal::Lines: remaining=double(context.config.mode.racingDistance)-scoring.totalLines;unit="L";break;
        case Runtime::GameGoal::Garbage: remaining=double(context.config.mode.garbageTarget)-scoring.totalGarbageLines;unit="L";break;
        case Runtime::GameGoal::Pieces: remaining=double(context.config.mode.pieceTarget)-scoring.placedBlocks;unit="P";break;
        case Runtime::GameGoal::Attack: remaining=double(context.config.mode.attackTarget)-scoring.totalAttack;unit="A";break;
        default: break;
    }
    const int rows=remaining>0 && remaining<=playfield.activeHeight ? static_cast<int>(remaining) : 0;
    std::string label=rows ? " "+std::to_string(rows)+unit+" " : "";
    auto& previous=player.presentation.racingGoalRows;
    if (previous==rows && player.presentation.racingGoalLabel==label) return;
    const int oldRows=previous;
    previous=rows;player.presentation.racingGoalLabel=label;
    if (!rows && oldRows<=0) return;

    // Use a bright double border and a count in the existing floor, keeping
    // all active/ghost pieces, holes and stacked cells untouched.
    const int first=playfield.activeHeight-rows;
    for (int row=0;row<=playfield.activeHeight;++row) {
        const bool marked=rows && row>=first;
        set_ui_color(marked ? ColorRole::RacingGoal :
            (context.config.display.obscuredColors ? ColorRole::Ghost : ColorRole::Text),
            context.config.display.obscuredColors);
        const char* left;
        const char* right;
        if (marked) {
            left=row==first ? "╔" : row==playfield.activeHeight ? "╚" : "║";
            right=row==first ? "╗" : row==playfield.activeHeight ? "╝" : "║";
        } else {
            left=right=(row==0 || row==playfield.activeHeight) ? "+" : "|";
        }
        move_cursor(row+player.presentation.topMargin,player.presentation.margin-1);
        std::cout<<left;
        move_cursor(row+player.presentation.topMargin,player.presentation.margin+playfield.width*2);
        std::cout<<right;
    }
    set_ui_color(rows ? ColorRole::RacingGoal :
        (context.config.display.obscuredColors ? ColorRole::Ghost : ColorRole::Text),
        context.config.display.obscuredColors);
    move_cursor(playfield.activeHeight+player.presentation.topMargin,player.presentation.margin);
    const int floorWidth=playfield.width*2;
    if (static_cast<int>(label.size())>floorWidth) label.clear();
    const int labelStart=(floorWidth-static_cast<int>(label.size()))/2;
    for (int column=0;column<floorWidth;++column) {
        if (!label.empty() && column==labelStart) {
            std::cout<<label;
            column+=static_cast<int>(label.size())-1;
        } else std::cout<<(rows ? "═" : "-");
    }
    set_ui_color(context.config.display.obscuredColors ? ColorRole::Ghost : ColorRole::Text,
        context.config.display.obscuredColors);
}

void print_border(const Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    // Borders must not inherit the color of the previously rendered piece,
    // garbage meter, or the other player's panel.
    set_ui_color(
        context.config.display.obscuredColors ? ColorRole::Ghost : ColorRole::Text,
        context.config.display.obscuredColors);
    const auto& playfield = player.gameplay.playfield;
    for (int row = 0; row <= playfield.activeHeight; ++row) {
        move_cursor(row + player.presentation.topMargin, player.presentation.margin - 1);
        std::cout << "|+"[row == 0 || row == playfield.activeHeight];
        for (int column = 0; column < playfield.width; ++column) {
            std::cout << (row != playfield.activeHeight ? "  " : "--");
        }
        std::cout << "|+"[row == 0 || row == playfield.activeHeight] << std::endl;
    }

    if (playfield.holdEnabled) {
        for (int row = 0; row <= player.presentation.blockMaxHeight + 3; ++row) {
            if (row >= playfield.activeHeight && !shifted_left_panel(context)) {
                move_cursor(row + player.presentation.topMargin, player.presentation.margin - 1);
                std::cout << (row != player.presentation.blockMaxHeight + 3 ? '|' : '+');
            }
            move_cursor(
                row + player.presentation.topMargin,
                player.presentation.margin -
                    (player.presentation.blockMaxWidth + 1) * 2 -
                    2 - 2 * shifted_left_panel(context));
            if (row == 0) {
                std::cout << '+';
                for (int i = 1; i <= player.presentation.blockMaxWidth - 1; ++i) std::cout << '-';
                std::cout << "HOLD";
                for (int i = 1; i <= player.presentation.blockMaxWidth - 1; ++i) std::cout << '-';
            } else if (row != player.presentation.blockMaxHeight + 3) {
                std::cout << '|';
            } else {
                std::cout << '+';
                for (int i = 1; i <= (player.presentation.blockMaxWidth + 1) * 2; ++i) {
                    std::cout << '-';
                }
            }
        }
    }

    if (playfield.nextCount) {
        const int panelBottom =
            (player.presentation.blockMaxHeight + 1) * playfield.nextCount + 2;
        for (int row = 0; row <= panelBottom; ++row) {
            if (row >= playfield.activeHeight) {
                move_cursor(
                    row + player.presentation.topMargin,
                    player.presentation.margin + playfield.width * 2);
                std::cout << (row != panelBottom ? '|' : '+');
            }
            move_cursor(
                row + player.presentation.topMargin,
                player.presentation.margin + playfield.width * 2 + 1);
            if (row == 0) {
                for (int i = 1; i <= player.presentation.blockMaxWidth - 1; ++i) std::cout << '-';
                std::cout << "NEXT";
                for (int i = 1; i <= player.presentation.blockMaxWidth - 1; ++i) std::cout << '-';
                std::cout << '+';
            } else if (row != panelBottom) {
                for (int i = 1; i <= (player.presentation.blockMaxWidth + 1) * 2; ++i) {
                    std::cout << ' ';
                }
                std::cout << '|';
            } else {
                for (int i = 1; i <= (player.presentation.blockMaxWidth + 1) * 2; ++i) {
                    std::cout << '-';
                }
                std::cout << '+';
            }
        }
    }
    player.presentation.racingGoalRows=-1;
    print_racing_goal_marker(context,player);
    flush();
}

int board_info_top_row(const Runtime::PlayerSessionState& player) {
    const int hold=player.presentation.hold_panel_height();
    const int available=player.gameplay.playfield.visibleHeight-hold;
    return hold+1+std::max(0,(available-kBoardInfoRows)/2);
}

int board_footer_base_row(const Runtime::PlayerSessionState& player) {
    return std::max({player.gameplay.playfield.visibleHeight,
        player.presentation.hold_panel_height()+15,
        !player.presentation.dynamicLayout && player.gameplay.playfield.nextCount>0
            ? player.presentation.next_panel_height(player.gameplay.playfield.nextCount)+1 : 0});
}

BoardVerticalExtent board_vertical_extent(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum) {
    const auto& field=player.gameplay.playfield;
    const auto& p=player.presentation;
    const int snowball=maximum && Runtime::enabled(context.config.quickPlay.snowball)
        ? Rules::qp_snowball_size(std::numeric_limits<int>::max()) : 0;
    const int height=std::max({field.visibleHeight,field.activeHeight,snowball});
    const int footer=std::max(height,p.hold_panel_height()+15);
    int coreBottom=std::max(footer+4,height+5);
    if (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) coreBottom=std::max(coreBottom,footer+7);
    const int nextBottom=field.nextCount>0 ? p.next_panel_height(field.nextCount) : 0;
    return {coreBottom,std::max(coreBottom,nextBottom),footer};
}

TerminalSize board_terminal_size(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum) {
    const auto& field=player.gameplay.playfield;
    const auto& presentation=player.presentation;
    const int snowball=maximum && Runtime::enabled(context.config.quickPlay.snowball)
        ? Rules::qp_snowball_size(std::numeric_limits<int>::max()) : 0;
    const int width=std::max(field.width,snowball);
    const int height=std::max({field.visibleHeight,field.activeHeight,snowball});
    const int nextHeight=field.nextCount>0 ? presentation.next_panel_height(field.nextCount) : 0;
    const int footer=std::max({height,presentation.hold_panel_height()+15,
        nextHeight>0 ? nextHeight+1 : 0});
    int right=presentation.margin+width*2+1;
    if (field.nextCount>0)
        right=std::max(right,presentation.margin+width*2+(presentation.blockMaxWidth+1)*2+2);
    int bottom=std::max(footer+3,height+4); // Pause can be lower on a tall field.
    if (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        right=std::max(right,presentation.margin+width*2+11); // BLIGHTED, even with no NEXT.
        bottom=footer+6; // Altitude/windup footer, then the post-round rows.
    }
    return {right,presentation.dynamicLayout
        ? board_vertical_extent(context,player,maximum).fullBottom+presentation.topMargin
        : bottom+Runtime::kDisplayHiddenRows+1};
}

HorizontalExtent board_core_horizontal_extent(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum) {
    const int shift=2*shifted_left_panel(context);
    int leftSpace=22+shift; // Spin erasure, Clutch, combo and statistics.
    if (player.gameplay.playfield.holdEnabled)
        leftSpace=std::max(leftSpace,(player.presentation.blockMaxWidth+1)*2+2+shift);
    const auto goal=Runtime::active_goal(context.config);
    if(goal!=Runtime::GameGoal::None && goal!=Runtime::GameGoal::Time) {
        const auto& mode=context.config.mode;
        const int target=goal==Runtime::GameGoal::Garbage ? mode.garbageTarget : goal==Runtime::GameGoal::Pieces ? mode.pieceTarget :
            goal==Runtime::GameGoal::Attack ? mode.attackTarget : goal==Runtime::GameGoal::AllClear ? mode.allClearTarget : mode.racingDistance;
        const int progressWidth=static_cast<int>(std::to_string(std::max(0LL,static_cast<long long>(target))+6).size()+1+
            std::to_string(target).size());
        leftSpace=std::max(leftSpace,11+progressWidth+shift);
    }
    return {player.presentation.margin-leftSpace,board_terminal_size(context,player,maximum).columns};
}

HorizontalExtent board_horizontal_extent(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool maximum) {
    auto bounds=board_core_horizontal_extent(context,player,maximum);
    if (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        // Twelve-column warning, two-column gap to HOLD, one outer column.
        const int windupLeft=player.presentation.margin-
            (player.presentation.blockMaxWidth+1)*2-19;
        bounds.left=std::min(bounds.left,windupLeft);
    }
    return bounds;
}

BoardTextLayout board_text_layout(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player) {
    const int right=player.presentation.margin+player.gameplay.playfield.width*2;
    const int left=board_horizontal_extent(context,player,false).left;
    const int columns=std::min(44,right-left);
    const int centered=player.presentation.margin+player.gameplay.playfield.width-columns/2;
    const int column=std::max(0,std::min(centered,right-columns));
    return {column,std::min(columns,std::max(1,right-column))};
}

void erase_player_board(const Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    const auto& playfield=player.gameplay.playfield;
    auto& presentation=player.presentation;
    int left=std::max(0,presentation.margin-std::max(24,(presentation.blockMaxWidth+1)*2+4));
    int right=presentation.margin+playfield.width*2+(presentation.blockMaxWidth+1)*2+2;
    int bottom=presentation.topMargin+board_footer_base_row(player)+5;
    for (const auto& span:presentation.altitudeDisplay) {
        if (!span.width) continue;
        left=std::min(left,span.column);
        right=std::max(right,span.column+static_cast<int>(span.width)-1);
        bottom=std::max(bottom,span.row);
    }
    for (const auto& span:presentation.windupDisplay) if (span.width) {
        left=std::min(left,span.column);
        right=std::max(right,span.column+static_cast<int>(span.width)-1);
        bottom=std::max(bottom,span.row);
    }
    const auto& undoSpan=presentation.undoCountDisplay;
    if (undoSpan.width) {
        left=std::min(left,undoSpan.column);
        right=std::max(right,undoSpan.column+static_cast<int>(undoSpan.width)-1);
        bottom=std::max(bottom,undoSpan.row);
    }
    for (int row=0; row<=bottom; ++row) {
        move_cursor(row,left);
        std::cout << std::string(right-left+1,' ');
    }
    // These rows may become the grown board's border on the next redraw.
    presentation.altitudeDisplay={};
    presentation.undoCountDisplay={};
    presentation.windupDisplay={}; presentation.windupDisplayedSegments=-1;
}

void redraw_player_board(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    // A complete redraw starts from a cleared/erased layout. The former
    // statistics coordinates may now belong to HOLD, so do not erase them
    // after painting the new frame. Keep widths to clear shortened values.
    player.presentation.messageTopMargin=-1;
    print_border(context,player);
    player.presentation.messageText={};

    const auto& playfield=player.gameplay.playfield;
    for(int row=-Runtime::kDisplayHiddenRows;row<playfield.visibleHeight;++row){
        for(int column=0;column<playfield.width;++column){
            const Runtime::BoardCell cell=playfield.field.cell(row,column);
            draw_board_cell(context,player,row,column,cell);
        }
    }

    if(player.gameplay.hold.piece.valid()) draw_board_hold(context,player);
    const int previewCount=std::min(
        playfield.nextCount,static_cast<int>(player.pieces.nextPreview.size()));
    for(int index=0;index<previewCount;++index) draw_board_next(context,player,index);
    if(player.presentation.activePose&&player.presentation.activePieceType){
        draw_board_piece(
            context,player,*player.presentation.activePieceType,
            *player.presentation.activePose);
    }else{
        player.presentation.paintedActivePose.reset();
        player.presentation.paintedActivePieceType.reset();
        ++player.presentation.activePiecePaintRevision;
    }

    if(context.config.garbage.mode==Runtime::GarbageMode::QuickPlay){
        print_altitude(context,player);
        print_blighted(context,player);
    }else{
        print_score(context,player);
    }
    print_message(context,player);
    print_spike(context,player);
    print_windup(context,player,true);
    print_b2b(context,player);
    print_combo(context,player);
    print_clutch(context,player,player.clutch.consecutive);
    refresh_spawn_marker(context,player);
    flush();
}

} // namespace Render
