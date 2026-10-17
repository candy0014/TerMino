#include "tetr/render/board_renderer.h"
#include "tetr/presentation/context_view.h"

#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/util/numeric.h"
#include "tetr/util/terminal_text.h"

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <array>
#include <string>

namespace Render {
namespace {

void ui_color(const Runtime::GameContext& context, ColorRole role) {
    set_ui_color(
        context.config.display.obscuredColors ? ColorRole::Ghost : role,
        context.config.display.obscuredColors);
}

} // namespace

double board_elapsed_seconds(const Runtime::GameContext& context,const Runtime::PlayerSessionState& player) {
    return Presentation::board_elapsed(context,player).seconds();
}

bool board_cell_visible(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int row, int column, Runtime::BoardCell cell) {
    return Presentation::board_cell_visible(context.config,player,row,column,cell,Presentation::board_elapsed(context,player));
}

BoardCellAppearance board_cell_appearance(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int row, int column, Runtime::BoardCell cell) {
    return Presentation::board_cell_appearance(context.config,player,row,column,cell,Presentation::board_elapsed(context,player));
}

void draw_board_cell(const Runtime::GameContext& context, const Runtime::PlayerSessionState& player,
    int row, int column, Runtime::BoardCell cell) {
    OutputGuard output(context);
    move_to(context,player,row,column);
    const auto appearance=board_cell_appearance(context,player,row,column,cell);
    if (appearance==BoardCellAppearance::Empty && !cell.is_timed_number()) {
        draw_empty_board_cell(context,player,row,column);
        return;
    }
    const auto colors=board_cell_colors(cell,context.config.display.obscuredColors,appearance==BoardCellAppearance::Dimmed);
    set_colors(colors);
    const bool marked=board_hint_cell(context,player,row,column);
    if (cell.is_timed_number()) {
        if (marked) draw_spawn_number(context.config.display.squareStyle,cell.timed_countdown(),colors);
        else draw_number(context.config.display.squareStyle,cell.timed_countdown());
    } else if (marked) draw_spawn_square(context.config.display.squareStyle,colors);
    else draw_square(context.config.display.squareStyle);
}

void print_score(const Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    const auto mode=context.config.mode.mode;
    const bool battle=mode==Runtime::GameMode::Multiplayer || mode==Runtime::GameMode::LocalVersus;
    if(!battle && mode!=Runtime::GameMode::Zen && mode!=Runtime::GameMode::Blitz) return;
    OutputGuard output(context);
    if(battle) {
        const int width=player.gameplay.playfield.width*2;
        auto name=player.presentation.username;
        const int length=static_cast<int>(Util::TerminalText::width(name));
        if(length>width) {
            const int left=std::max(0,(width-3+1)/2),right=std::max(0,width-3-left);
            name=width<=3?std::string(width,'.'):Util::TerminalText::slice(name,0,left)+"..."+Util::TerminalText::slice(name,length-right,right);
        }
        set_ui_color(ColorRole::Accent,context.config.display.obscuredColors);
        move_cursor(player.gameplay.playfield.visibleHeight+player.presentation.topMargin+1,player.presentation.margin);
        const int padding=std::max(0,width-int(Util::TerminalText::width(name)));
        std::cout<<std::string(padding/2,' ')<<name<<std::string(padding-padding/2,' ');
        return;
    }
    long long value = player.gameplay.scoring.score;
    ui_color(context, ColorRole::Text);
    const int digits = Util::digit_count(value);
    move_cursor(
        player.gameplay.playfield.visibleHeight + player.presentation.topMargin + 1,
        player.presentation.margin + player.gameplay.playfield.width - (digits + 1) / 2);
    std::cout << value;
}

void print_altitude(const Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    ui_color(context, ColorRole::Text);
    const auto& qp = context.session.quickPlay;
    const int level = std::max(1, static_cast<int>(qp.speed));
    const int shown = std::min(level, 10);
    const std::string bars(shown / 2, '=');
    const std::string left = (shown % 2 ? "-" : "") + bars;
    const std::string right = bars + (shown % 2 ? "-" : "");
    std::ostringstream altitude;
    altitude << std::fixed << std::setprecision(1) << qp.altitude;
    const std::string altitudeText = left + " " + altitude.str() + " " + right;
    const auto& playfield = player.gameplay.playfield;
    const bool snowball = Runtime::enabled(context.config.quickPlay.snowball);
    // SB always uses the footer below the whole layout, with a fixed offset
    // from its bottom and the current field's center, regardless of text length.
    const int baseRow = snowball || altitudeText.size() > static_cast<std::size_t>(playfield.width * 2 + 1)
        ? board_footer_base_row(player) : playfield.visibleHeight;
    const int center = player.presentation.margin + playfield.width;
    const auto printCentered = [&](std::size_t index, int row, const std::string& text) {
        auto& previous = player.presentation.altitudeDisplay[index];
        if (previous.width) {
            move_cursor(previous.row, previous.column);
            std::cout << std::string(previous.width, ' ');
        }
        previous = {baseRow + player.presentation.topMargin + row,
            center - static_cast<int>((text.size() + 1) / 2),
            text.size()};
        if (!text.empty()) move_cursor(previous.row, previous.column);
    };
    printCentered(0, 1, altitudeText);
    constexpr std::array<std::array<int, 3>, 11> colors{{
        {{255,255,255}}, {{233,22,0}}, {{237,109,0}}, {{170,220,60}},
        {{60,120,240}}, {{165,70,220}}, {{255,170,40}}, {{54,235,168}},
        {{50,200,230}}, {{220,100,255}}, {{255,255,255}}
    }};
    const auto boosterColor = [&]() {
        if (context.config.display.obscuredColors) ui_color(context, ColorRole::Ghost);
        else {
            const auto& rgb = colors[std::min(level, 11) - 1];
            set_rgb(rgb[0], rgb[1], rgb[2]);
        }
    };
    boosterColor();
    std::cout << left << ' ';
    ui_color(context, ColorRole::Text);
    std::cout << altitude.str() << ' ';
    boosterColor();
    std::cout << right;
    ui_color(context, ColorRole::Text);
    const auto elapsed = context.config.mode.replayMode ? Replay::playback_game_elapsed(context,context.now())
        : Runtime::elapsed_game_time(context.session,context.now());
    const std::string warning = elapsed.seconds() < qp.progress.windupUntil ? "!" : "";
    printCentered(1, 3, warning);
    std::cout << warning;

}

void print_message(const Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    auto& presentation=player.presentation;
    const int baseRow=board_info_top_row(player)+9;
    // Required metrics share a 15-row region with the clear messages. Goals
    // use the last row unless the existing count/time row already shows them.
    const auto goal=Runtime::active_goal(context.config);
    std::array<int,6> rows{{0,1,4,2,3,5}}; // P, A, time, PPS, APM, goal.
    if(goal==Runtime::GameGoal::None || goal==Runtime::GameGoal::Time ||
       goal==Runtime::GameGoal::Pieces || goal==Runtime::GameGoal::Attack) rows={{1,2,5,3,4,-1}};
    if(Runtime::challenge_population(context.config.challengeRules) || context.config.challengeRules==Runtime::ChallengeRules::StageMaster)rows={{0,1,4,2,3,5}};
    const int absoluteRow=presentation.topMargin+baseRow;
    const bool layoutChanged=presentation.messageMargin!=presentation.margin || presentation.messageTopMargin!=absoluteRow ||
        presentation.messageRows!=rows || presentation.messageObscured!=context.config.display.obscuredColors;
    if(layoutChanged && presentation.messageTopMargin>=0 && presentation.messageMargin==presentation.margin) {
        for(std::size_t index=0;index<rows.size();++index)if(presentation.messageWidths[index] && presentation.messageRows[index]>=0) {
            const int suffixWidth=index<2?2:index>=3?4:0;
            const int right=index==2?-4:std::min(-2,-8+suffixWidth),eraseRight=index==5?-2:right;
            ui_color(context,ColorRole::Text);
            move_to(context,player,presentation.messageTopMargin-presentation.topMargin+presentation.messageRows[index],-4,
                eraseRight+9-int(presentation.messageWidths[index]));
            std::cout<<std::string(presentation.messageWidths[index],' ');
        }
    }
    if(layoutChanged) {
        presentation.messageText={};presentation.messageWidths={};presentation.messageRows=rows;presentation.messageMargin=presentation.margin;
        presentation.messageTopMargin=absoluteRow;presentation.messageObscured=context.config.display.obscuredColors;
    }
    // Network polling runs at 120 Hz; wall-clock statistics do not need that
    // console cadence. Placement/attack changes and completed times stay immediate.
    const auto paintTime=context.real_now();
    if(context.session.roomScene.enabled && !player.timing.completedGameTime &&
       presentation.messageText[0]==std::to_string(player.gameplay.scoring.placedBlocks)+" P" &&
       presentation.messageText[1]==std::to_string(player.gameplay.scoring.totalAttack)+" A" &&
       paintTime-presentation.lastMessagePaint<Runtime::Duration::from_seconds(0.05)) return;
    presentation.lastMessagePaint=paintTime;
    print_racing_goal_marker(context,player);
    const double elapsed=board_elapsed_seconds(context,player);
    const double pps = elapsed > 0 ? player.gameplay.scoring.placedBlocks / elapsed : 0.0;
    const double apm = elapsed > 0 ? player.gameplay.scoring.totalAttack * 60.0 / elapsed : 0.0;

    const auto printStat = [&](std::size_t index, const std::string& text,
                               const std::string& suffix) {
        if(rows[index]<0) return;
        const auto combined=text+suffix;
        if(presentation.messageText[index]==combined) return;
        presentation.messageText[index]=combined;
        ui_color(context, ColorRole::Text);
        auto& previousWidth = player.presentation.messageWidths[index];
        const int suffixWidth=static_cast<int>(Util::TerminalText::width(suffix));
        const auto totalWidth=Util::TerminalText::width(text)+suffixWidth;
        const int right=index==2 ? -4 : std::min(-2,-8+suffixWidth);
        const int eraseRight=index==5 ? -2 : right;
        const auto span=totalWidth+eraseRight-right;
        const int width = static_cast<int>(std::max(previousWidth, span));
        move_to(context, player, baseRow + rows[index], -4, eraseRight+9-width);
        std::cout << std::string(width, ' ');
        move_to(context, player, baseRow + rows[index], -4,
                right+9-static_cast<int>(totalWidth));
        std::cout << combined;
        previousWidth = span;
    };
    printStat(0, std::to_string(player.gameplay.scoring.placedBlocks)+
        (goal==Runtime::GameGoal::Pieces ? "/"+std::to_string(context.config.mode.pieceTarget) : ""), " P");
    printStat(1, std::to_string(player.gameplay.scoring.totalAttack)+
        (goal==Runtime::GameGoal::Attack ? "/"+std::to_string(context.config.mode.attackTarget) : ""), " A");
    printStat(2, Util::format_time(goal==Runtime::GameGoal::Time ?
        std::max(0.0,context.config.mode.blitzTime.seconds()-elapsed) : elapsed), "");
    printStat(3, Util::format_fixed_2(pps), " PPS");
    printStat(4, Util::format_fixed_2(apm), " APM");
    const auto& scoring=player.gameplay.scoring;
    const auto progress=[&](std::uint64_t current,int target,const std::string& suffix) {
        printStat(5,std::to_string(current)+"/"+std::to_string(target),suffix);
    };
    switch(goal) {
        case Runtime::GameGoal::Lines: progress(scoring.totalLines,context.config.mode.racingDistance," LINES");break;
        case Runtime::GameGoal::Garbage: progress(scoring.totalGarbageLines,context.config.mode.garbageTarget," GARBAGE");break;
        case Runtime::GameGoal::AllClear: progress(scoring.allClearCount,context.config.mode.allClearTarget," ALLCLEAR");break;
        case Runtime::GameGoal::InitialCells: {
            int total=0;
            if(context.config.initialMap) for(const auto& row:context.config.initialMap->rows)
                total+=static_cast<int>(std::count_if(row.begin(),row.end(),[](char cell){return cell!=' ';}));
            const int remaining=player.gameplay.playfield.field.remaining_initial_cells();
            progress(std::max(0,total-remaining),total," MAP CELLS");break;
        }
        default:
            if(Runtime::challenge_population(context.config.challengeRules))progress(scoring.battleRank,Runtime::challenge_population(context.config.challengeRules)," ALIVE");
            else if(context.config.challengeRules==Runtime::ChallengeRules::StageMaster) {
                const int stage=std::min(3,scoring.challengeStages);
                const std::array<std::string,4> tasks{{"10 LINES","2 QUADS","2 SPINS","1 PC"}};
                printStat(5,std::to_string(scoring.challengeStages)+"/4 ",tasks[stage]);
            }else printStat(5,"","");break;
    }
}

} // namespace Render
