#include "tetr/engine/placement_board_adapter.h"

#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_state.h"

namespace Engine {

PlacementApplicationState capture_placement_application_state(const Runtime::PlayerState& player, const Runtime::GameContext& context) {
    PlacementApplicationState state;
    state.placedBlocks = player.scoring.placedBlocks;
    state.totalAttack = player.scoring.totalAttack;
    state.totalLines = player.scoring.totalLines;
    state.totalGarbageLines = player.scoring.totalGarbageLines;
    state.score = player.scoring.score;
    state.b2b = player.scoring.b2b;
    state.combo = player.scoring.combo;
    state.difficultClears = player.scoring.difficultClears;
    state.b2bExtensions = player.scoring.b2bExtensions;
    state.b2bBreaks = player.scoring.b2bBreaks;
    state.maxB2B = player.scoring.maxB2B;
    state.maxCombo = player.scoring.maxCombo;
    state.clearMoves = player.scoring.clearMoves;
    state.zeroAttackClears = player.scoring.zeroAttackClears;
    state.attacksAtLeast4 = player.scoring.attacksAtLeast4;
    state.attacksAtLeast8 = player.scoring.attacksAtLeast8;
    state.maxAttack = player.scoring.maxAttack;
    state.totalOutgoingAttack = player.scoring.totalOutgoingAttack;
    state.qpBlighted = player.quickPlay.blighted;
    state.qpLastOperation = player.quickPlay.lastOperation;
    state.qpOperations = player.quickPlay.operations;
    state.qpAltitude = context.session.quickPlay.altitude;
    state.qpSpeed = context.session.quickPlay.speed;
    state.qpProgress = context.session.quickPlay.progress;
    return state;
}

void store_pre_offset_application_state(Runtime::PlayerState& player, const PlacementApplicationState& state) {
    player.scoring.placedBlocks = state.placedBlocks;
    player.scoring.totalAttack = state.totalAttack;
    player.scoring.totalLines = state.totalLines;
    player.scoring.totalGarbageLines = state.totalGarbageLines;
    player.scoring.score = state.score;
    player.scoring.b2b = state.b2b;
    player.scoring.combo = state.combo;
    player.scoring.difficultClears = state.difficultClears;
    player.scoring.b2bExtensions = state.b2bExtensions;
    player.scoring.b2bBreaks = state.b2bBreaks;
    player.scoring.maxB2B = state.maxB2B;
    player.scoring.maxCombo = state.maxCombo;
    player.scoring.clearMoves = state.clearMoves;
    player.scoring.zeroAttackClears = state.zeroAttackClears;
    player.scoring.attacksAtLeast4 = state.attacksAtLeast4;
    player.scoring.attacksAtLeast8 = state.attacksAtLeast8;
    player.scoring.maxAttack = state.maxAttack;
    player.scoring.totalOutgoingAttack = state.totalOutgoingAttack;
    player.quickPlay.blighted = state.qpBlighted;
}

void store_post_offset_application_state(Runtime::PlayerState& player, const PlacementApplicationState& state, Runtime::GameContext& context) {
    player.scoring.totalOutgoingAttack = state.totalOutgoingAttack;
    player.quickPlay.lastOperation = state.qpLastOperation;
    player.quickPlay.operations = state.qpOperations;
    context.session.quickPlay.altitude = state.qpAltitude;
    context.session.quickPlay.speed = state.qpSpeed;
    context.session.quickPlay.progress = state.qpProgress;
}


PlacementApplicationConfig capture_placement_application_config(const Runtime::GameContext& context, int blitzLevel) {
    const Runtime::RuntimeConfig& runtime = context.config;
    PlacementApplicationConfig config;
    config.mode = runtime.mode.mode;
    config.blitzFlag = runtime.mode.fixedBlitzSpeed;
    config.blitzLevel = blitzLevel;
    config.garbageMode = runtime.garbage.mode;
    config.garbageMultiple = runtime.garbage.mode==Runtime::GarbageMode::Unclear && !runtime.garbage.legacyIncomingRounding
        ? 1.0 : runtime.garbage.attackMultiplier;
    config.qpExpert = runtime.quickPlay.expert;
    config.qpVolatile = runtime.quickPlay.volatileMode;
    config.qpDoubleHole = runtime.quickPlay.doubleHole;
    config.qpAllSpin = runtime.quickPlay.allSpin;
    config.qpBackfire = runtime.quickPlay.backfire;
    config.qpSnakeEyes = runtime.quickPlay.snakeEyes;
    config.qpMods = runtime.quickPlay;
    return config;
}



} // namespace Engine
