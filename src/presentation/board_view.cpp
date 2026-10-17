#include "tetr/presentation/board_view.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/construction.h"
#include <algorithm>
#include <stdexcept>

namespace Presentation {

const CellView& BoardView::at(int row, int column) const {
    if (row < firstRow || row >= bottom || column < 0 || column >= width)
        throw std::out_of_range("board view cell outside captured bounds");
    return cells.at(static_cast<std::size_t>(row - firstRow) * width + column);
}

BoardView capture_board_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    Runtime::Duration elapsed) {
    BoardView view;
    view.width = field.width();
    view.firstRow = -Runtime::kDisplayHiddenRows;
    view.bottom = std::clamp(player.gameplay.playfield.visibleHeight, 0, field.height());
    view.elapsed = elapsed;
    view.obscuredColors = config.display.obscuredColors;
    view.cells.reserve(static_cast<std::size_t>(view.bottom - view.firstRow) * view.width);
    for (int row = view.firstRow; row < view.bottom; ++row) {
        for (int column = 0; column < view.width; ++column) {
            const auto value = field.cell(row, column);
            const auto appearance = board_cell_appearance(config, player, row, column, value, elapsed);
            const bool spawn=config.display.showSpawnPosition && player.gameplay.playfield.nextCount>0 &&
                !player.pieces.nextPreview.empty() && std::any_of(player.presentation.spawnMarkerCells.begin(),player.presentation.spawnMarkerCells.end(),
                    [&](auto cell){return cell.row==row && cell.col==column;});
            view.cells.push_back({appearance == CellAppearance::Empty ? Runtime::BoardCell::empty() : value, appearance,
                spawn || Runtime::construction_cell(config,field,row,column)});
        }
    }
    const auto type = player.presentation.activePieceType;
    const auto pose = player.presentation.activePose;
    const auto appearance = active_piece_appearance(config, player, elapsed);
    if (type && type->valid() && pose && appearance != CellAppearance::Empty) {
        view.active = PieceView{*type, *pose, appearance};
        if (config.field.ghost) {
            const auto piece = Piece::get_definition(*type);
            auto landing = *pose;
            while (piece.can_place(landing.translated(1, 0), field)) ++landing.position.row;
            view.ghost = PieceView{*type, landing, CellAppearance::Dimmed};
        }
    }
    const auto& hold = player.gameplay.hold;
    view.hold = hold.piece;
    view.holdEnabled = player.gameplay.playfield.holdEnabled;
    view.holdAvailable = hold.remoteAvailable.value_or(Runtime::hold_available(config.field, hold.used));
    view.next = player.pieces.nextPreview;
    view.username = player.presentation.username;
    const auto& score = player.gameplay.scoring;
    view.score = score.score; view.lines = score.totalLines; view.attack = score.totalAttack;
    view.pieces = score.placedBlocks; view.combo = score.combo; view.b2b = score.b2b;
    view.squareStyle=config.display.squareStyle;
    view.spinPiece=player.presentation.eventSpinPiece;
    view.mini=player.presentation.eventMini;view.clearLines=player.presentation.eventClearLines;
    view.perfectClear=player.presentation.eventPerfectClear;view.halfClear=player.presentation.eventHalfClear;
    view.spike=player.presentation.spike.attack;view.clutch=player.clutch.consecutive;
    view.blighted=player.gameplay.quickPlay.blighted;view.headline=player.presentation.roundEndHeadline;
    view.rank=score.battleRank;view.stages=score.challengeStages;
    view.wins=score.wins;view.population=Runtime::challenge_population(config.challengeRules);
    if(config.challengeRules==Runtime::ChallengeRules::StageMaster) {
        constexpr const char* tasks[]={"10 LINES","2 QUADS","2 SPINS","1 PC"};
        view.stageTask=tasks[std::clamp(score.challengeStages,0,3)];
    }
    if(config.quickPlay.allSpin==Runtime::AllSpinMode::Extended)
        for(const auto& operation:player.gameplay.quickPlay.operations)view.spinHistory.emplace_back(operation.pieceClass,operation.lineClass);
    for(const auto& packet:player.garbage.buffer) view.garbage.push_back(packet.line_count());
    view.goal=Runtime::active_goal(config);
    switch(view.goal) {
    case Runtime::GameGoal::Time:view.goalTarget=static_cast<int>(config.mode.blitzTime.seconds());view.goalProgress=static_cast<int>(elapsed.seconds());break;
    case Runtime::GameGoal::Lines:view.goalTarget=config.mode.racingDistance;view.goalProgress=score.totalLines;break;
    case Runtime::GameGoal::Garbage:view.goalTarget=config.mode.garbageTarget;view.goalProgress=score.totalGarbageLines;break;
    case Runtime::GameGoal::Pieces:view.goalTarget=config.mode.pieceTarget;view.goalProgress=score.placedBlocks;break;
    case Runtime::GameGoal::Attack:view.goalTarget=config.mode.attackTarget;view.goalProgress=score.totalAttack;break;
    case Runtime::GameGoal::AllClear:view.goalTarget=config.mode.allClearTarget;view.goalProgress=static_cast<int>(score.allClearCount);break;
    case Runtime::GameGoal::InitialCells:
        if(config.initialMap) for(const auto& row:config.initialMap->rows) for(auto cell:row) view.goalTarget+=cell!=' ';
        view.goalProgress=view.goalTarget-field.remaining_initial_cells();break;
    default:break;
    }
    return view;
}

} // namespace Presentation
