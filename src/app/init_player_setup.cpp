#include "init_player_setup.h"

#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
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
    const auto score = gameplay.scoring.score;
    const auto wins = gameplay.scoring.wins;
    gameplay.scoring = {};
    gameplay.scoring.wins = wins;
    if (config.mode.mode == Runtime::GameMode::Zen) gameplay.scoring.score = score;
    player.clutch = {};
    player.presentation.spike = {};
    player.pieces.allowedPieces = Piece::types_for_rotation_system(config.rotationSystem,config.legacyRotationRules,
        config.legacyFoolPieceSet ? 10 : config.quickPlay.defaultFloor);
    if(config.initialMap && config.initialMap->generator==Runtime::MapGenerator::CustomSequence && !config.initialMap->sequence.empty()) {
        player.pieces.allowedPieces=config.initialMap->sequence;
        std::sort(player.pieces.allowedPieces.begin(),player.pieces.allowedPieces.end());
        player.pieces.allowedPieces.erase(std::unique(player.pieces.allowedPieces.begin(),player.pieces.allowedPieces.end()),player.pieces.allowedPieces.end());
    }
}

void draw_initial_board(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    Render::print_border(context, player);
    if(context.config.initialMap)
        for(int row=0;row<player.gameplay.playfield.visibleHeight;++row)
            for(int col=0;col<player.gameplay.playfield.width;++col) {
                const auto cell=player.gameplay.playfield.field.cell(row,col);
                if(cell.occupied()) Render::draw_board_cell(context,player,row,col,cell);
            }
    for (int index = 0; index < player.gameplay.playfield.nextCount; ++index) {
        Render::draw_board_next(context, player, index);
    }
    Render::refresh_spawn_marker(context,player);
}

void initialize_player_field(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    Engine::initialize_board(context, player);
    Engine::GarbageService::initialize(player, context.config.garbage.holePattern);
}

} // namespace App::InitDetail
