#include "placement_presentation.h"

#include "tetr/engine/field_mutation.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/rules/placement_resolution.h"
#include "tetr/rules/qp_post_lock.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"

#include <iostream>

namespace Engine {
namespace {

void present_previous_all_spin_clear(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int clearedLines,
    bool isSpin) {
    const auto& qp = player.gameplay.quickPlay;
    if (!Runtime::enabled(context.config.quickPlay.allSpin) ||
        !qp.lastOperation.has_value() || clearedLines || isSpin) {
        return;
    }

    const auto& previous = *qp.lastOperation;
    Render::move_to(context, player, 2 + Render::board_info_top_row(player), -7, 1);
    Render::set_ui_color(
        context.config.display.obscuredColors ? Render::ColorRole::Ghost : Render::ColorRole::Text,
        context.config.display.obscuredColors);
    int displayedClear = previous.linesCleared;
    if (context.config.quickPlay.allSpin == Runtime::AllSpinMode::Reverse && !previous.spin) {
        displayedClear = -static_cast<int>(displayedClear != 0);
    }
    Render::print_clear(context, player, displayedClear);
}

void present_previous_all_spin_spin(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int clearedLines,
    bool isSpin) {
    const auto& qp = player.gameplay.quickPlay;
    if (!Runtime::enabled(context.config.quickPlay.allSpin) ||
        !qp.lastOperation.has_value() || clearedLines || isSpin) {
        return;
    }

    Render::print_spin(context, player, Piece::Type::none(), 0);
    const auto& previous = *qp.lastOperation;
    if (previous.spin) {
        Render::print_spin(context, player, previous.pieceType, previous.mini);
    }
}

void present_extended_all_spin_history_impl(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int clearedLines,
    bool isSpin) {
    Render::OutputGuard output(context);
    if (context.config.quickPlay.allSpin != Runtime::AllSpinMode::Extended ||
        (!clearedLines && !isSpin)) {
        return;
    }

    const auto& operations = player.gameplay.quickPlay.operations;
    const int operationCount = static_cast<int>(operations.size());
    static constexpr char names[2][16] = {"IJLOSTZ-", "ijlostz-"};
    for (int row = 0; row < 3; ++row) {
        Render::move_to(context, player, Render::board_info_top_row(player) + 3 + row, -7, 0);
        for (int index = row * 4; index < (row + 1) * 4; ++index) {
            if (index < operationCount) {
                const auto& signature = operations[index];
                int color = signature.pieceClass & 7;
                if (color == 7) color = -3;
                if (context.config.display.obscuredColors) {
                    Render::set_ui_color(Render::ColorRole::Ghost, true);
                } else if (color == -3) {
                    Render::set_ui_color(Render::ColorRole::Text, false);
                } else {
                    Render::set_cell_color(color, false);
                }
                std::cout << names[signature.lineClass & 1][signature.pieceClass & 7]
                          << ((signature.pieceClass >> 3) & 7) << ' ';
            } else {
                Render::set_ui_color(
                    context.config.display.obscuredColors
                        ? Render::ColorRole::Ghost
                        : Render::ColorRole::Text,
                    context.config.display.obscuredColors);
                std::cout << "## ";
            }
        }
    }
}

void decay_timed_solid_garbage(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player) {
    auto& playfield = player.gameplay.playfield;
    const auto decay = Engine::decay_timed_solid_garbage(
        context.config.field.width,
        -Runtime::kHiddenRowCapacity,
        playfield.visibleHeight,
        [&field](int row, int col) { return field.cell(row, col); },
        [&field](int row, int col, Runtime::BoardCell value) { field.set_cell(row,col,value); });

    for (const auto& change : decay.changes) {
        if (change.row < -Runtime::kDisplayHiddenRows) continue;
        Render::draw_board_cell(context,player,change.row,change.col,change.value);
    }
    playfield.activeHeight += decay.releasedRows;
}

} // namespace

void present_locked_piece_result(
    Runtime::GameContext& context,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement) {
    Render::OutputGuard output(context);
    const int clearedLines = placement.clear.clearedLines;
    const bool isSpin = placement.spin.spin;
    const bool isMini = placement.attack.effectiveMini;

    Render::print_spin(context, player, Piece::Type::none(), 0);
    if (isSpin) Render::print_spin(context, player, piece.type(), isMini);

    Render::print_clear(context, player, 0);
    if (context.config.quickPlay.allSpin == Runtime::AllSpinMode::Reverse) {
        if (clearedLines && !isSpin) Render::print_clear(context, player, -1);
        else if (clearedLines) Render::print_clear(context, player, clearedLines);
    } else if (clearedLines) {
        Render::print_clear(context, player, clearedLines);
    }

    present_previous_all_spin_clear(context, player, clearedLines, isSpin);
    Render::print_b2b(context, player);
    Render::print_combo(context, player);
    if (!context.config.attackRules.legacy || context.config.quickPlay.allSpin != Runtime::AllSpinMode::Extended) {
        Render::print_all_clear(context, player, placement.clear.perfectClear, placement.clear.halfClear);
    }
    Render::move_to(context, player, 0, 0);
}

void present_post_offset_quick_play_state(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement,
    bool decayTimedSolidGarbage) {
    Render::OutputGuard output(context);
    if (decayTimedSolidGarbage) decay_timed_solid_garbage(context, field, player);
    present_previous_all_spin_spin(
        context, player, placement.clear.clearedLines, placement.spin.spin);
}

void present_extended_all_spin_history(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    const Rules::PlacementResult& placement) {
    Render::OutputGuard output(context);
    present_extended_all_spin_history_impl(
        context, player, placement.clear.clearedLines, placement.spin.spin);
}

} // namespace Engine
