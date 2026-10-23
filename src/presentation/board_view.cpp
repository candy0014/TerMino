#include "tetr/presentation/board_view.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/construction.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Presentation {

GoalView capture_goal_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player,Runtime::Duration elapsed) {
    GoalView view;view.type=Runtime::active_goal(config);
    const auto& score=player.gameplay.scoring;
    switch(view.type) {
    case Runtime::GameGoal::Time:
        view.target=static_cast<int>(config.mode.blitzTime.seconds());
        view.remaining=static_cast<int>(std::max(0.,std::ceil(config.mode.blitzTime.seconds()-elapsed.seconds())));
        view.progress=view.target-view.remaining;view.unit="s";break;
    case Runtime::GameGoal::Lines:view.target=config.mode.racingDistance;view.progress=score.totalLines;view.unit="L";break;
    case Runtime::GameGoal::Garbage:view.target=config.mode.garbageTarget;view.progress=score.totalGarbageLines;view.unit="L";break;
    case Runtime::GameGoal::Pieces:view.target=config.mode.pieceTarget;view.progress=score.placedBlocks;view.unit="P";break;
    case Runtime::GameGoal::Attack:view.target=config.mode.attackTarget;view.progress=score.totalAttack;view.unit="A";break;
    case Runtime::GameGoal::AllClear:
        view.target=config.mode.allClearTarget;
        view.progress=static_cast<int>(std::min<std::uint64_t>(score.allClearCount,std::numeric_limits<int>::max()));
        if(config.mode.mode==Runtime::GameMode::Racing && config.mode.mapGoal==Runtime::MapGoal::AllClear && score.mapAllClearReached)
            view.progress=view.target;
        view.unit="PC";break;
    case Runtime::GameGoal::InitialCells:
        if(config.initialMap)for(const auto& row:config.initialMap->rows)for(auto cell:row)view.target+=cell!=' ';
        view.progress=view.target-player.gameplay.playfield.field.remaining_initial_cells();view.unit="C";break;
    default:break;
    }
    if(view.type!=Runtime::GameGoal::Time)view.remaining=std::max(0,view.target-view.progress);
    view.markerRows=view.supports_field_marker() && view.remaining<=player.gameplay.playfield.activeHeight?view.remaining:0;
    return view;
}

const CellView& BoardView::at(int row, int column) const {
    if (row < firstRow || row >= bottom || column < 0 || column >= width)
        throw std::out_of_range("board view cell outside captured bounds");
    return cells.at(static_cast<std::size_t>(row - firstRow) * width + column);
}

BoardView capture_board_view(const Runtime::RuntimeConfig& config,
    const Runtime::PlayerSessionState& player, const Runtime::Playfield& field,
    Runtime::Duration elapsed,int hiddenRows) {
    BoardView view;
    view.width = field.width();
    view.firstRow = -std::clamp(hiddenRows,0,Runtime::kHiddenRowCapacity);
    view.bottom = std::clamp(player.gameplay.playfield.visibleHeight, 0, field.height());
    view.elapsed = elapsed;
    view.obscuredColors = config.display.obscuredColors;
    view.cells.reserve(static_cast<std::size_t>(view.bottom - view.firstRow) * view.width);
    for (int row = view.firstRow; row < view.bottom; ++row) {
        for (int column = 0; column < view.width; ++column) {
            // Legacy remote boards transmit eight hidden rows. Do not treat
            // untransmitted deeper storage as an authoritative peer cell.
            const auto value = player.timing.remoteGameTime && row < -Runtime::kDisplayHiddenRows
                ? Runtime::BoardCell::empty() : field.cell(row, column);
            const float alpha=board_cell_opacity(config,player,row,column,value,elapsed);
            const auto appearance=alpha>0?CellAppearance::Visible:
                value.occupied() && config.mode.replayMode?CellAppearance::Dimmed:CellAppearance::Empty;
            const bool spawn=config.display.showSpawnPosition && player.gameplay.playfield.nextCount>0 &&
                !player.pieces.nextPreview.empty() && std::any_of(player.presentation.spawnMarkerCells.begin(),player.presentation.spawnMarkerCells.end(),
                    [&](auto cell){return cell.row==row && cell.col==column;});
            view.cells.push_back({appearance == CellAppearance::Empty ? Runtime::BoardCell::empty() : value, appearance,
                spawn || Runtime::construction_cell(config,field,row,column),
                value.occupied() && config.mode.replayMode?.35f+.65f*alpha:alpha});
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
    view.nextCount = player.gameplay.playfield.nextCount;
    view.username = player.presentation.username;
    const auto& score = player.gameplay.scoring;
    view.score = score.score; view.lines = score.totalLines; view.attack = score.totalAttack;
    view.pieces = score.placedBlocks; view.combo = score.combo; view.b2b = score.b2b;
    view.effects=config.display.effects;
    // Spatial feedback must not prolong or reconstruct hidden piece geometry.
    view.spatialEffects=!field_visibility(config,player).invisible &&
        config.challengeRules!=Runtime::ChallengeRules::Blind && config.challengeRules!=Runtime::ChallengeRules::Audio &&
        config.challengeRules!=Runtime::ChallengeRules::Instinct;
    if(view.spatialEffects)view.motion=player.presentation.boardMotion;
    const auto& feedback=player.presentation.placement;
    if(feedback.sequence==view.pieces && feedback.sequence>0)
        view.placementLines=std::max(feedback.clearedLines,static_cast<int>(feedback.clearedRows.size()));
    if(view.spatialEffects && feedback.sequence==view.pieces && feedback.sequence>0 && feedback.type.valid())
        view.placement=PlacementView{feedback.sequence,{feedback.type,feedback.pose},feedback.clearedRows,feedback.dropFromRow};
    view.squareStyle=config.display.squareStyle;
    view.spinPiece=player.presentation.eventSpinPiece;
    view.mini=player.presentation.eventMini;view.clearLines=player.presentation.eventClearLines;
    view.perfectClear=player.presentation.eventPerfectClear;view.halfClear=player.presentation.eventHalfClear;
    view.spike=player.presentation.spike.attack;view.clutch=player.clutch.consecutive;
    view.blighted=player.gameplay.quickPlay.blighted;view.headline=player.presentation.roundEndHeadline;
    view.rank=score.battleRank;view.stages=score.challengeStages;
    view.wins=score.wins;view.population=Runtime::challenge_population(config.challengeRules);
    view.finished=player.timing.completedGameTime.has_value();
    if(config.challengeRules==Runtime::ChallengeRules::StageMaster) {
        constexpr const char* tasks[]={"10 LINES","2 QUADS","2 SPINS","1 PC"};
        view.stageTask=tasks[std::clamp(score.challengeStages,0,3)];
    }
    if(config.quickPlay.allSpin==Runtime::AllSpinMode::Extended)
        for(const auto& operation:player.gameplay.quickPlay.operations)view.spinHistory.emplace_back(operation.pieceClass,operation.lineClass);
    for(const auto& packet:player.garbage.buffer) view.garbage.push_back(packet.line_count());
    view.goal=capture_goal_view(config,player,elapsed);
    return view;
}

} // namespace Presentation
