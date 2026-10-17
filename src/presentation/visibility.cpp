#include "tetr/presentation/visibility.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/player_session_state.h"

namespace Presentation {

bool board_cell_visible(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, int row, int column,
    Runtime::BoardCell cell, Runtime::Duration elapsed) {
    if (!cell.occupied()) return false;
    if (config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        Runtime::reverse(config.quickPlay.invisible) && cell.is_normal_garbage()) {
        const auto& field = player.gameplay.playfield;
        return row < field.activeHeight - field.garbageHeight + 3;
    }
    const auto& field = config.field;
    if (!field.invisible || cell.is_garbage_like() || player.presentation.invisibleFlash) return true;
    if (field.periodicInvisible || field.invisibilityDelay <= Runtime::Duration{}) return false;
    return elapsed < player.gameplay.playfield.field.cell_lock_time(row, column) + field.invisibilityDelay;
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
