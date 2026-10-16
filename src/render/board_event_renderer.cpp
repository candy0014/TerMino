#include "tetr/render/board_renderer.h"

#include "tetr/render/terminal_renderer.h"
#include "tetr/rules/spike.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/replay/playback_clock.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <cmath>

namespace Render {
namespace {

void ui_color(const Runtime::GameContext& context, ColorRole role) {
    set_ui_color(
        context.config.display.obscuredColors ? ColorRole::Ghost : role,
        context.config.display.obscuredColors);
}

} // namespace

void print_windup(const Runtime::GameContext& context,Runtime::PlayerSessionState& player,bool force) {
    if (context.config.garbage.mode!=Runtime::GarbageMode::QuickPlay) return;
    const double elapsed=(context.config.mode.replayMode ? Replay::playback_game_elapsed(context,context.now())
        : Runtime::elapsed_game_time(context.session,context.now())).seconds();
    const Runtime::QuickPlayWindupWarning* active=nullptr;
    for (const auto& warning:context.session.quickPlay.progress.windupWarnings)
        if (warning.start<=elapsed+1e-7 && elapsed<warning.end) { active=&warning; break; }
    auto& p=player.presentation;
    const int holdColumn=p.margin-(p.blockMaxWidth+1)*2-4;
    const int column=std::max(0,holdColumn-14);
    const int segments=active ? active->segments : 0;
    const int pulse=active ? static_cast<int>(std::fmod(std::max(0.0,elapsed-active->start)*4,2)) : 0;
    const double start=active ? active->start : -1;
    const int row=p.topMargin+1;
    const auto holdOverlapsRow=[&](int y) {
        return player.gameplay.playfield.holdEnabled && y>=p.topMargin &&
            y<=p.topMargin+p.hold_panel_height();
    };
    std::array<Runtime::TextDisplaySpan,5> visible{};
    if (active) for (std::size_t i=0;i<visible.size();++i) {
        const int y=row+static_cast<int>(i);
        const int width=holdOverlapsRow(y) ? std::clamp(holdColumn-column,0,12) : 12;
        visible[i]={y,column,static_cast<std::size_t>(width)};
    }
    const bool sameLayout=std::equal(visible.begin(),visible.end(),p.windupDisplay.begin(),
        [](const auto& a,const auto& b) { return a.row==b.row && a.column==b.column && a.width==b.width; });
    if (!force && segments==p.windupDisplayedSegments && pulse==p.windupPulse &&
        start==p.windupDisplayedStart && sameLayout) return;
    OutputGuard output(context);
    const auto clearSpan=[](int y,int begin,int end) {
        if (end<=begin) return;
        move_cursor(y,begin); std::cout<<std::string(static_cast<std::size_t>(end-begin),' ');
    };
    for (const auto& span:p.windupDisplay) if (span.width) {
        const int end=span.column+static_cast<int>(span.width);
        if (holdOverlapsRow(span.row)) {
            // HOLD may now occupy cells painted before a layout/setting change.
            // Neither warning repaint nor expiration may erase that panel.
            clearSpan(span.row,span.column,std::min(end,holdColumn));
            clearSpan(span.row,std::max(span.column,p.margin-2),end);
        } else clearSpan(span.row,span.column,end);
    }
    p.windupDisplay=visible;
    p.windupDisplayedSegments=segments; p.windupPulse=pulse; p.windupDisplayedStart=start;
    if (!active) return;
    // Move right at the window edge, with HOLD drawn above the warning.
    set_ui_color(context.config.display.obscuredColors ? ColorRole::Ghost :
        pulse ? ColorRole::Accent : ColorRole::Danger,context.config.display.obscuredColors);
    std::string bars,dots;
    for (int i=0;i<segments;++i) { if (i) {bars+=' ';dots+=' ';} bars+='!';dots+='.'; }
    const auto inside=[](const std::string& text) {
        const auto left=(10-text.size())/2;
        return "|"+std::string(left,' ')+text+std::string(10-left-text.size(),' ')+"|";
    };
    const std::array<std::string,5> lines{{"+--WINDUP--+",inside(bars),inside(bars),inside(dots),"+----------+"}};
    for (int i=0;i<5;++i) {
        if (!visible[i].width) continue;
        move_cursor(row+i,column); std::cout<<lines[i].substr(0,visible[i].width);
    }
}

void print_spin(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Piece::Type pieceType,
    bool mini) {
    OutputGuard output(context);
    int nameOffset = 10;
    Piece::Metadata metadata;
    if (pieceType.valid()) {
        metadata = Piece::metadata(pieceType);
        nameOffset = metadata.nameWidth - 1;
    }
    move_to(
        context,
        player,
        1 + board_info_top_row(player),
        -6,
        -nameOffset);
    if (!pieceType.valid()) {
        std::cout << "                     ";
        return;
    }
    if (context.config.display.obscuredColors) set_ui_color(ColorRole::Ghost, true);
    else set_cell_color(metadata.colorId, false);
    std::cout << metadata.name << "-Spin";
    if (mini) std::cout << " Mini";
}

void print_clear(const Runtime::GameContext& context, Runtime::PlayerSessionState& player, int lines) {
    OutputGuard output(context);
    move_to(context, player, 2 + board_info_top_row(player), -7, 1);
    ui_color(context, ColorRole::Text);
    switch (lines) {
        case -1: std::cout << "ＶＯＩＤ　　"; break;
        case 0: std::cout << "　　　　　　"; break;
        case 1: std::cout << "ＳＩＮＧＬＥ"; break;
        case 2: std::cout << "ＤＯＵＢＬＥ"; break;
        case 3: std::cout << "ＴＲＩＰＬＥ"; break;
        case 4: std::cout << "ＱＵＡＤ　　"; break;
        case 5: std::cout << "ＰＥＮＴＡ　"; break;
        case 6: std::cout << "ＨＥＸＡ　　"; break;
        default: break;
    }
}

void print_b2b(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    std::optional<int> requestedValue) {
    OutputGuard output(context);
    const int value = requestedValue.value_or(player.gameplay.scoring.b2b);
    move_to(context, player, 3 + board_info_top_row(player), -7, 2);
    std::cout << "           ";
    if (value < 2) return;
    move_to(context, player, 3 + board_info_top_row(player), -7, 2);
    ui_color(context, ColorRole::Text);
    std::cout << "B2B×" << value - 1;
}

void print_combo(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    std::optional<int> requestedValue) {
    OutputGuard output(context);
    const int value = requestedValue.value_or(player.gameplay.scoring.combo);
    move_to(context, player, 5 + board_info_top_row(player), -9, 0);
    std::cout << "                 ";
    if (value < 2) return;

    int digits = 0;
    for (int remaining = value - 1; remaining; remaining /= 10) ++digits;
    move_to(
        context,
        player,
        5 + board_info_top_row(player),
        -7 - digits / 2 + !(digits & 1),
        digits & 1);
    ui_color(context, ColorRole::Text);
    std::cout << value - 1 << " ＣＯＭＢＯ";
}

void print_all_clear(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    bool enabled, bool halfClear) {
    OutputGuard output(context);
    move_to(context, player, 7 + board_info_top_row(player), -5, 0);
    std::cout << "　　　　";
    move_to(context, player, 8 + board_info_top_row(player), -6, 0);
    std::cout << "　　　　　";
    if (!enabled && !halfClear) return;

    if (context.config.display.obscuredColors) set_ui_color(ColorRole::Ghost, true);
    else set_rgb(238, 219, 47);
    move_to(context, player, 7 + board_info_top_row(player), -5, 0);
    std::cout << (enabled ? "ＡＬＬ" : "ＨＡＬＦ");
    move_to(context, player, 8 + board_info_top_row(player), -6, 0);
    std::cout << "ＣＬＥＡＲ";
}

void print_blighted(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    std::optional<bool> requestedValue) {
    OutputGuard output(context);
    const bool value = requestedValue.value_or(player.gameplay.quickPlay.blighted);
    move_to(
        context,
        player,
        player.presentation.dynamicLayout
            ? std::min(player.presentation.next_panel_height(player.gameplay.playfield.nextCount)+1,
                board_footer_base_row(player)+1)
            : player.presentation.next_panel_height(player.gameplay.playfield.nextCount)+1,
        player.gameplay.playfield.width + 1);
    if (value) {
        set_ui_color(
            context.config.display.obscuredColors ? ColorRole::Ghost : ColorRole::Accent,
            context.config.display.obscuredColors);
        std::cout << "BLIGHTED";
    } else {
        std::cout << "         ";
    }
}

void print_clutch(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int consecutive) {
    OutputGuard output(context);
    // From column -10, 19 spaces end immediately before the board border in
    // ordinary layouts and remain safely left of it in garbage layouts.
    // The former width of 22 erased the border and part of the first cell.
    constexpr int kDisplayWidth = 19;
    const int row = 6 + board_info_top_row(player);
    move_to(context, player, row, -10, 0);
    std::cout << std::string(kDisplayWidth, ' ');
    if (consecutive <= 0) return;

    static constexpr const char* kPrefixes[] = {
        "", "", "DOUBLE ", "TRIPLE ", "QUAD ", "PENTA ",
        "HEXA ", "HEPTA ", "OCTA ", "ENNEA ", "DECA "};
    std::string label;
    if (consecutive <= 10) {
        label = std::string(kPrefixes[consecutive]) + "CLUTCH";
    } else {
        label = std::to_string(consecutive) + "x CLUTCH";
    }

    ui_color(context, ColorRole::Accent);
    move_to(
        context,
        player,
        row,
        -10,
        std::max(0, (kDisplayWidth - static_cast<int>(label.size())) / 2));
    std::cout << label;
}

void print_spike(
    const Runtime::GameContext& context,
    Runtime::PlayerSessionState& player) {
    OutputGuard output(context);
    // This row is immediately below HOLD. Spin/clear messages begin one row
    // later and statistics begin nine rows later, so the left-side labels never
    // share a terminal row.  Width 19 ends before the board border even when
    // the garbage meter shifts the whole left panel two columns left.
    constexpr int kDisplayWidth = 19;
    const int row = board_info_top_row(player);
    move_to(context, player, row, -10, 0);
    std::cout << std::string(kDisplayWidth, ' ');
    if (!Rules::spike_visible(player.presentation.spike)) return;

    const std::string label =
        std::to_string(player.presentation.spike.attack) + " SPIKE";
    if (context.config.display.obscuredColors) {
        set_ui_color(ColorRole::Ghost, true);
    } else {
        set_rgb(238, 219, 47);
    }
    move_to(
        context,
        player,
        row,
        -10,
        std::max(0, kDisplayWidth - static_cast<int>(label.size()) - 1));
    std::cout << label;
}

} // namespace Render
