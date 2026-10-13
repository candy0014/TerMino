#include "tetr/render/board_renderer.h"

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

bool board_cell_visible(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int row, Runtime::BoardCell cell) {
    if (!cell.occupied()) return false;
    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        Runtime::reverse(context.config.quickPlay.invisible) && cell.is_normal_garbage()) {
        const auto& field=player.gameplay.playfield;
        return row < field.activeHeight-field.garbageHeight+3;
    }
    return !context.config.field.invisible || cell.is_garbage_like() || player.presentation.qpInvisibleFlash;
}

BoardCellAppearance board_cell_appearance(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, int row, Runtime::BoardCell cell) {
    if (!cell.occupied()) return BoardCellAppearance::Empty;
    if (board_cell_visible(context,player,row,cell)) return BoardCellAppearance::Visible;
    return context.config.mode.replayMode ? BoardCellAppearance::Dimmed : BoardCellAppearance::Empty;
}

void draw_board_cell(const Runtime::GameContext& context, const Runtime::PlayerSessionState& player,
    int row, int column, Runtime::BoardCell cell) {
    OutputGuard output(context);
    move_to(context,player,row,column);
    const auto appearance=board_cell_appearance(context,player,row,cell);
    if (appearance==BoardCellAppearance::Empty && !cell.is_timed_number()) {
        draw_empty_board_cell(context,player,row,column);
        return;
    }
    const auto colors=board_cell_colors(cell,context.config.display.obscuredColors,appearance==BoardCellAppearance::Dimmed);
    set_colors(colors);
    const bool marked=spawn_marker_cell(context,player,row,column);
    if (cell.is_timed_number()) {
        if (marked) draw_spawn_number(context.config.display.squareStyle,cell.timed_countdown(),colors);
        else draw_number(context.config.display.squareStyle,cell.timed_countdown());
    } else if (marked) draw_spawn_square(context.config.display.squareStyle,colors);
    else draw_square(context.config.display.squareStyle);
}

void print_score(const Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    long long value = player.gameplay.scoring.score;
    if (context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) {
        value = player.gameplay.scoring.wins;
    }
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
    print_racing_goal_marker(context,player);
    ui_color(context, ColorRole::Text);
    const double timelineElapsed=context.config.mode.replayMode
        ? Replay::playback_game_elapsed(context,context.now()).seconds()
        : Runtime::elapsed_game_time(context.session,context.now()).seconds() *
            context.config.timeMultiplier;
    double elapsed = std::max(0.0, timelineElapsed);
    const double pps = elapsed > 0 ? player.gameplay.scoring.placedBlocks / elapsed : 0.0;
    const double apm = elapsed > 0 ? player.gameplay.scoring.totalAttack * 60.0 / elapsed : 0.0;
    if (context.config.mode.mode == Runtime::GameMode::Blitz) {
        elapsed = context.config.mode.blitzTime.seconds() - elapsed;
    }

    const int baseRow = 11 + player.presentation.hold_panel_height();
    const auto printStat = [&](std::size_t index, const std::string& text,
                               const std::string& suffix) {
        auto& previousWidth = player.presentation.messageWidths[index];
        const int suffixWidth=static_cast<int>(Util::TerminalText::width(suffix));
        const auto totalWidth=Util::TerminalText::width(text)+suffixWidth;
        const int right=std::min(-2,-8+suffixWidth);
        const int eraseRight=index==4 ? -2 : right;
        const auto span=totalWidth+eraseRight-right;
        const int width = static_cast<int>(std::max(previousWidth, span));
        move_to(context, player, baseRow + static_cast<int>(index), -4, eraseRight+9-width);
        std::cout << std::string(width, ' ');
        move_to(context, player, baseRow + static_cast<int>(index), -4,
                right+9-static_cast<int>(totalWidth));
        std::cout << text << suffix;
        previousWidth = span;
    };
    printStat(0, std::to_string(player.gameplay.scoring.placedBlocks), " P");
    printStat(1, std::to_string(player.gameplay.scoring.totalAttack), " A");
    printStat(2, Util::format_fixed_2(elapsed), " S");
    printStat(3, Util::format_fixed_2(pps), " PPS");
    if (context.config.mode.mode != Runtime::GameMode::Racing) {
        printStat(4, Util::format_fixed_2(apm), " APM");
        return;
    }
    if(context.config.initialMap && context.config.mode.mapGoal==Runtime::MapGoal::ClearInitial) {
        printStat(4,std::to_string(player.gameplay.playfield.field.remaining_initial_cells())," MAP CELLS");return;
    }
    if(context.config.initialMap && context.config.mode.mapGoal==Runtime::MapGoal::AllClear) {
        printStat(4,player.gameplay.scoring.mapAllClearReached ? "1/1" : "0/1"," ALLCLEAR");return;
    }
    const bool garbageRace =
        context.config.garbage.mode == Runtime::GarbageMode::CheeseLayer ||
        context.config.garbage.mode == Runtime::GarbageMode::CheeseTimer;
    const int completedLines = garbageRace
        ? player.gameplay.scoring.totalGarbageLines
        : player.gameplay.scoring.totalLines;
    printStat(4, std::to_string(completedLines) + "/" +
        std::to_string(context.config.mode.racingDistance), " LINES");
}

} // namespace Render
