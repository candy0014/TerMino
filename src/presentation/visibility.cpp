#include "tetr/presentation/visibility.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/player_session_state.h"
#include <algorithm>
#include <cmath>

namespace Presentation {

Runtime::FieldVisibility field_visibility(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player) {
    const auto& f=config.field;
    return player.presentation.remoteFieldVisibility.value_or(Runtime::FieldVisibility{
        f.invisible,f.periodicInvisible,f.invisibilityDelay,f.invisibilityInterval,f.invisibilityDuration});
}

float board_cell_opacity(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player,int row,int column,
    Runtime::BoardCell cell,Runtime::Duration elapsed) {
    if(!cell.occupied())return 0;
    if(player.timing.completedGameTime)return 1;
    if(config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
       Runtime::reverse(config.quickPlay.invisible) && cell.is_normal_garbage())
        return row<player.gameplay.playfield.activeHeight-player.gameplay.playfield.garbageHeight+3?1:0;
    const auto f=field_visibility(config,player);
    if(!f.invisible || cell.is_garbage_like())return 1;
    if(f.periodic) {
        if(!Runtime::periodic_invisible_flash(elapsed,f.interval,f.duration))return 0;
        if(f.interval<=Runtime::Duration{})return 1;
        const double phase=std::fmod(elapsed.seconds(),f.interval.seconds())/f.duration.seconds();
        return float(std::clamp(std::min(3*phase,3*(1-phase)),0.,1.));
    }
    if(f.delay<=Runtime::Duration{})return 0;
    const double age=(elapsed-player.gameplay.playfield.field.cell_lock_time(row,column)).seconds();
    return float(std::clamp(2*(1-age/f.delay.seconds()),0.,1.));
}

bool board_cell_visible(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, int row, int column,
    Runtime::BoardCell cell, Runtime::Duration elapsed) {
    if (!cell.occupied()) return false;
    if (player.timing.completedGameTime) return true;
    if (config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        Runtime::reverse(config.quickPlay.invisible) && cell.is_normal_garbage()) {
        const auto& field = player.gameplay.playfield;
        return row < field.activeHeight - field.garbageHeight + 3;
    }
    const auto field = field_visibility(config,player);
    if (!field.invisible || cell.is_garbage_like() || player.presentation.invisibleFlash) return true;
    if (field.periodic || field.delay <= Runtime::Duration{}) return false;
    return elapsed < player.gameplay.playfield.field.cell_lock_time(row, column) + field.delay;
}

CellAppearance board_cell_appearance(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, int row, int column,
    Runtime::BoardCell cell, Runtime::Duration elapsed) {
    if (!cell.occupied()) return CellAppearance::Empty;
    if (board_cell_visible(config, player, row, column, cell, elapsed)) return CellAppearance::Visible;
    return config.mode.replayMode ? CellAppearance::Dimmed : CellAppearance::Empty;
}

bool active_piece_visible(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, Runtime::Duration elapsed) {
    const auto rule = config.challengeRules;
    if (rule == Runtime::ChallengeRules::Blind || rule == Runtime::ChallengeRules::Audio) return false;
    return rule != Runtime::ChallengeRules::Instinct ||
        elapsed < player.presentation.activePieceBorn + Runtime::Duration::from_seconds(.2);
}

CellAppearance active_piece_appearance(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, Runtime::Duration elapsed) {
    if (active_piece_visible(config, player, elapsed)) return CellAppearance::Visible;
    return config.mode.replayMode ? CellAppearance::Dimmed : CellAppearance::Empty;
}

} // namespace Presentation
