#include "init_player_setup.h"

#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/config/user_config.h"
#include "tetr/runtime/construction.h"
#include <algorithm>

namespace App::InitDetail {

void reset_player_board(Runtime::PlayerSessionState& player, const Runtime::RuntimeConfig& config) {
    auto& gameplay = player.gameplay;
    gameplay.playfield.visibleHeight = config.field.height;
    gameplay.playfield.width = config.field.width;
    gameplay.playfield.nextCount = config.field.nextCount;
    gameplay.playfield.holdEnabled = config.field.holdEnabled;
    gameplay.hold = {};
    gameplay.playfield.field.clear();
    const auto wins = gameplay.scoring.wins;
    gameplay.scoring = {};
    gameplay.scoring.wins = wins;
    player.clutch = {};
    player.presentation.spike = {};
    player.pieces.allowedPieces = Piece::types_for_rotation_system(config.rotationSystem,config.legacyRotationRules,
        config.legacyFoolPieceSet ? 10 : config.quickPlay.defaultFloor,config.legacyEventGeometry);
    if(config.challengeRules!=Runtime::ChallengeRules::ConstructMap && config.initialMap && config.initialMap->generator==Runtime::MapGenerator::CustomSequence && !config.initialMap->sequence.empty()) {
        player.pieces.allowedPieces=config.initialMap->sequence;
        std::sort(player.pieces.allowedPieces.begin(),player.pieces.allowedPieces.end());
        player.pieces.allowedPieces.erase(std::unique(player.pieces.allowedPieces.begin(),player.pieces.allowedPieces.end()),player.pieces.allowedPieces.end());
    }
}

void draw_initial_board(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    Render::print_border(context, player);
    if(context.config.initialMap || Runtime::construction_rules(context.config.challengeRules))
        for(int row=0;row<player.gameplay.playfield.visibleHeight;++row)
            for(int col=0;col<player.gameplay.playfield.width;++col) {
                const auto cell=player.gameplay.playfield.field.cell(row,col);
                if(cell.occupied() || Runtime::construction_cell(context.config,player.gameplay.playfield.field,row,col)) Render::draw_board_cell(context,player,row,col,cell);
            }
    for (int index = 0; index < player.gameplay.playfield.nextCount; ++index) {
        Render::draw_board_next(context, player, index);
    }
    Render::refresh_spawn_marker(context,player);
}

void initialize_player_field(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    if(context.user_settings_component()) player.presentation.username=context.user_settings().username;
    player.presentation.remotePlacementRevision=0;
    Engine::initialize_board(context, player);
    Engine::GarbageService::initialize(player, context.config.garbage.holePattern);
}

} // namespace App::InitDetail
