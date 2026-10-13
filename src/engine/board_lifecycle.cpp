#include "tetr/engine/board_lifecycle.h"

#include "tetr/engine/piece_queue.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/rules/qp_post_lock.h"
#include "tetr/rules/quick_play.h"
#include "tetr/rules/garbage_generation.h"

#include <algorithm>
#include <stdexcept>
#include <numeric>

namespace Engine {

void initialize_board(Runtime::GameContext& context, Runtime::PlayerSessionState& player, bool initializeQueue){
    context.undoHistory.reset();
    auto& gameplay=player.gameplay;
    auto& field=gameplay.playfield;
    auto& quick=gameplay.quickPlay;
    auto& pieces=player.pieces;
    auto& presentation=player.presentation;
    presentation.topMargin=Runtime::kDisplayHiddenRows;
    presentation.dynamicLayout=false;
    presentation.terminalColumns=presentation.terminalRows=0;
    if (Runtime::enabled(context.config.quickPlay.snowball)) {
        field.width=4;
        field.visibleHeight=4;
        context.config.field.width=4;
        context.config.field.height=4;
    }
    presentation.qpInvisibleFlash=false;
    presentation.altitudeDisplay={};
    presentation.replayPromptDisplay.clear();
    presentation.roundEndHeadline.clear();
    presentation.racingGoalRows=-1;
    context.session.quickPlay.progress={};
    context.session.quickPlay.speed=1;
    context.session.quickPlay.altitude=0;
    pieces.bagCount=0;
    pieces.randomizerState={};
    pieces.qpBagBreather=1;
    pieces.foolLastTypes.clear();

    context.session.quickPlay.floor=std::clamp(context.config.quickPlay.defaultFloor,1,10);
    if(context.config.garbage.mode == Runtime::GarbageMode::QuickPlay)
        context.session.quickPlay.altitude=Rules::qp_floor_altitude(context.session.quickPlay.floor);

    if(initializeQueue){
        if (pieces.allowedPieces.empty()) {
            throw std::logic_error("cannot initialize board with an empty piece set");
        }
        pieces.bag.clear();
        if(finite_map_sequence(context)) {
            for(const auto type:context.config.initialMap->sequence) pieces.bag.push_back({type,false});
        } else if(context.config.legacyRandomizerRules) {
            const int piecesPerBag=context.config.randomPieces ? 1 : static_cast<int>(pieces.allowedPieces.size());
            for(int i=1;i<=field.nextCount/piecesPerBag+1;i++) add_bag(context,player);
        } else {
            while(pieces.bag.size()<=static_cast<std::size_t>(std::max(0,field.nextCount))) add_bag(context,player);
        }
    }
    if (field.nextCount < 0 || ((!initializeQueue || !finite_map_sequence(context)) &&
        pieces.bag.size() < static_cast<std::size_t>(field.nextCount))) {
        throw std::logic_error("cannot initialize board without enough queued NEXT pieces");
    }

    field.activeHeight=field.visibleHeight;
    field.field.initialize(field.width,field.activeHeight);
    field.garbageHeight=0;
    if(context.config.initialMap && initializeQueue) {
        const auto& map=*context.config.initialMap;
        for(int row=0;row<map.height;++row) for(int col=0;col<map.width;++col) {
            const auto cell=Runtime::map_cell(map.rows[row][col]);
            field.field.set_cell(row,col,cell);
            field.field.mark_initial_cell(row,col);
            if(cell.is_normal_garbage()) field.garbageHeight=std::max(field.garbageHeight,map.height-row);
        }
    }
    presentation.activePose.reset();
    presentation.activePieceType.reset();
    presentation.paintedActivePose.reset();
    presentation.paintedActivePieceType.reset();
    presentation.spawnMarkerCells.clear();
    presentation.nextSpawnPose.reset();
    presentation.nextSpawnType=pieces.bag.empty() ? std::optional<Piece::Type>{} : pieces.bag.front().type;

    pieces.nextPreview.clear();
    pieces.nextPreview.reserve(field.nextCount);
    for(int i=0;i<field.nextCount && static_cast<std::size_t>(i)<pieces.bag.size();i++) pieces.nextPreview.emplace_back(pieces.bag[i].type);
    quick.operations.clear();
    quick.blighted=false;
    quick.lastOperation.reset();
    if(initializeQueue){
        std::vector<Piece::Type> tmp=pieces.allowedPieces;
        if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
            context.config.quickPlay.continuousCancellation &&
            context.config.quickPlay.fool == Runtime::FoolMode::Disabled)
            tmp.push_back(Piece::Type::from_index(21));
        if(context.config.quickPlay.fool == Runtime::FoolMode::AprilFools2026){
            tmp = Piece::april_fools_piece_types();
        }
        presentation.blockMaxHeight=2,presentation.blockMaxWidth=3;
        for(auto x:tmp){
            const Piece::Metadata pieceMetadata=Piece::metadata(x);
            presentation.blockMaxHeight=std::max(presentation.blockMaxHeight,pieceMetadata.displayHeight);
            presentation.blockMaxWidth=std::max(presentation.blockMaxWidth,pieceMetadata.displayWidth);
        }
    }
    auto& multiplayer=player.multiplayer;
    multiplayer.pendingSends.clear();
    multiplayer.sendId={}; multiplayer.receiveId={};
}


bool grow_snowball_board(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    if (!Runtime::enabled(context.config.quickPlay.snowball)) return false;
    auto& playfield=player.gameplay.playfield;
    const int size=Rules::qp_snowball_size(player.gameplay.scoring.totalLines);
    if (size <= playfield.width) return false;
    const int oldWidth=playfield.width;
    const int oldHeight=playfield.visibleHeight;
    const int oldActiveHeight=playfield.activeHeight;
    // Alternate right/left additions to keep the stack centered on the spawn.
    const int columnOffset=(size-4)/2-(oldWidth-4)/2;
    const int rowOffset=size-oldHeight;
    playfield.field.expand(size,size,columnOffset);

    Rules::GarbageGenerationConfig generation;
    generation.width=size;
    generation.garbageMode=context.config.garbage.mode;
    generation.cheeseModel=context.config.garbage.cheeseModel;
    generation.cheeseMessiness=context.config.garbage.cheeseMessiness;
    generation.doubleHoleMode=context.config.quickPlay.doubleHole;
    generation.legacyShapeRules=context.config.garbage.legacyShapeRules;
    generation.reverseNoHold=Runtime::reverse(context.config.quickPlay.noHold);
    // Extend visible and hidden garbage alike, preserving old holes and pieces.
    for (int row=-Runtime::kHiddenRowCapacity+rowOffset; row<size; ++row) {
        std::vector<Runtime::BoardCell> oldRow;
        oldRow.reserve(oldWidth);
        for (int column=0; column<oldWidth; ++column)
            oldRow.push_back(playfield.field.cell(row,column+columnOffset));
        const auto expanded=Rules::expand_garbage_row(generation,oldRow,columnOffset,player.garbage.random);
        for (int column=0; column<size; ++column)
            if (column<columnOffset || column>=columnOffset+oldWidth)
                playfield.field.set_cell(row,column,expanded[column]);
    }

    auto& holes=player.garbage.holeOrder;
    if (static_cast<int>(holes.size())==oldWidth) {
        for (int& column:holes) column+=columnOffset;
        for (int column=0; column<size; ++column)
            if (column<columnOffset || column>=columnOffset+oldWidth) holes.push_back(column);
    } else {
        holes.resize(size);
        std::iota(holes.begin(),holes.end(),0);
    }
    auto& progress=context.session.quickPlay.progress;
    for (auto* history:{&progress.quadColumns,&progress.szColumns,&progress.jlColumns})
        for (int& column:*history) if (column>=0) column+=columnOffset;

    playfield.width=size;
    playfield.visibleHeight=size;
    playfield.activeHeight=oldActiveHeight+rowOffset;
    context.config.field.width=size;
    context.config.field.height=size;
    player.presentation.activePose.reset();
    player.presentation.activePieceType.reset();
    player.presentation.paintedActivePose.reset();
    player.presentation.paintedActivePieceType.reset();
    player.presentation.garbageBufferDisplay.assign(size+Runtime::kDisplayHiddenRows+1,0);
    player.presentation.garbageBufferSegmentTops.assign(player.presentation.garbageBufferDisplay.size(),false);
    return true;
}

void initialize_board_timers(Runtime::GameContext& context, Runtime::PlayerSessionState& player){
    if (&player == &context.session.player) {
        context.session.simulationTime = {};
        context.session.completedSimulationPieces = 0;
        context.session.turnWaitDuration = {};
        context.session.turnWaitStarted.reset();
        context.session.clockOffset={};
    }
    const Runtime::TimePoint now=context.now();
    player.timing.realGameStart=context.real_now();
    player.timing.completedGameTime.reset();
    player.timing.completedRealTime.reset();
    player.timing.timedGarbage=now;
    player.timing.frameUpdate=now;
    player.timing.autoplay=context.real_now();
    player.timing.framePoll=context.real_now();
    player.timing.qpGravity=now;
    player.timing.multiplayerSync=context.real_now();
    player.timing.entryDelayEnd.reset();
    context.session.gameStartTime=now;
}


} // namespace Engine
