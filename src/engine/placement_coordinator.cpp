#include "tetr/engine/placement_coordinator.h"

#include "placement_presentation.h"
#include "placement_side_effects.h"
#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/locked_piece_resolution.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/entry_delay.h"
#include "tetr/engine/placement_application.h"
#include "tetr/engine/placement_board_adapter.h"
#include "tetr/engine/spike_presentation.h"
#include "tetr/engine/simulation_time.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include "tetr/app/application_state.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/rules/quick_play.h"
#include "tetr/runtime/field_metrics.h"
#include "tetr/runtime/challenge_puzzles.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/engine/finesse.h"
#include "tetr/render/audio_cues.h"
#include "tetr/runtime/construction.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>

namespace Engine {

Rules::PlayResult finalize_locked_piece(
    Runtime::GameContext& context,
    Runtime::Playfield& field,
    Piece::Definition& piece,
    Runtime::PlayerSessionState& player,
    MovementState& movementState,
    int blitzLevel,
    const std::function<Rules::PlayResult()>& pumpFrame, MovementInputState* movementInput) {
    if (context.config.field.invisible) {
        Render::draw_piece(
            context,
            piece,
            player,
            movementState.pose,
            field,
            Render::PieceDrawMode::Erase);
    }

    const int minimumInputs=Runtime::finesse_rules(context.config.challengeRules)?minimal_input_count(field,piece,movementState.pose,context.config.movement):-1;
    const bool finesseFault=Runtime::finesse_rules(context.config.challengeRules) && (minimumInputs<0 || movementState.inputActions<0 || movementState.inputActions>minimumInputs);
    const Rules::PlacementResult placement = resolve_locked_piece(
        context,
        field,
        piece,
        player,
        movementState.pose,
        movementState.lastRotation,
        Runtime::Duration::from_seconds(Render::board_elapsed_seconds(context,player)));
    record_locked_piece_for_clutch(player, placement.clear.clearedLines);
    if (context.config.field.invisible) {
        for (const auto& offset:piece.cells(movementState.pose.rotation)) {
            const int row=movementState.pose.position.row+offset[0];
            const int column=movementState.pose.position.col+offset[1];
            if (row>=-Runtime::kDisplayHiddenRows && row<player.gameplay.playfield.visibleHeight)
                Render::draw_board_cell(context,player,row,column,field.cell(row,column));
        }
    }

    auto& gameplay = player.gameplay;
    gameplay.playfield.garbageHeight -= placement.clear.clearedGarbageLines;

    PlacementApplicationConfig applicationConfig =
        capture_placement_application_config(context, blitzLevel);
    if(context.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
        Runtime::reverse(context.config.quickPlay.doubleHole) &&
        !context.config.quickPlay.legacyRules && gameplay.quickPlay.blighted) {
        applicationConfig.qpBlightedRoundingRoll=static_cast<double>(player.attackRoundingRandom()) /
            (static_cast<double>(std::mt19937::max())+1);
    }
    applicationConfig.pieceFamily = Piece::metadata(piece.type()).familyIndex;
    applicationConfig.pieceColumn = movementState.pose.position.col;
    if(applicationConfig.pieceFamily==0 && !context.config.quickPlay.legacyRules) {
        const auto& cells=piece.cells(movementState.pose.rotation);
        applicationConfig.pieceColumn+=std::min_element(cells.begin(),cells.end(),
            [](const auto& a,const auto& b){return a[1]<b[1];})->at(1);
    }
    if (applicationConfig.pieceFamily == 0 && placement.clear.clearedLines == 0) {
        const auto& cells = piece.cells(movementState.pose.rotation);
        const int column = movementState.pose.position.col + cells.front()[1];
        if (cells.size() == 4 && (column <= 1 || column >= field.width()-2)) {
            const int neighbor = column <= 1 ? 1-column : 2*field.width()-3-column;
            applicationConfig.stackedEdgeI = true;
            for (const auto& cell : cells) {
                const auto adjacent = field.cell(movementState.pose.position.row+cell[0],neighbor);
                applicationConfig.stackedEdgeI = applicationConfig.stackedEdgeI &&
                    movementState.pose.position.col+cell[1] == column &&
                    adjacent.is_piece() && adjacent.color_id() == piece.color_id();
            }
        }
    }
    PlacementApplicationBeginResult applicationBegin = begin_placement_application(
        capture_placement_application_state(gameplay, context),
        applicationConfig,
        placement,
        piece.type());

    store_pre_offset_application_state(gameplay, applicationBegin.state);
    if (placement.allClear) ++gameplay.scoring.allClearCount;
    if (placement.clear.clearedLines>0 || placement.spin.spin) {
        const int category=placement.spin.spin ? (placement.attack.effectiveMini ? 2 : 1) : 0;
        ++gameplay.scoring.clearStatistics[{piece.type().index(),placement.clear.clearedLines}][category];
    }
    present_locked_piece_result(context, piece, player, placement);

    apply_pre_offset_placement_garbage(
        context, field, player, applicationBegin.effects);
    Render::print_message(context, player);
    Render::move_to(context, player, gameplay.playfield.visibleHeight + 2, 0);

    const PlacementOffsetResolution offset = resolve_placement_offset(
        context, field, piece, player, applicationBegin.effects,
        placement.attack.perfectClearAttack + placement.attack.halfClearAttack);
    if (offset.signal != Rules::PlayResult::ContinueFrame) return offset.signal;
    applicationBegin.state.qpProgress = context.session.quickPlay.progress;
    applicationBegin.state.qpSpeed = context.session.quickPlay.speed;
    applicationBegin.state.qpAltitude = context.session.quickPlay.altitude;

    const PlacementApplicationResult application = finish_placement_application(
        applicationBegin,
        applicationConfig,
        offset.rawOffsetResult);
    store_post_offset_application_state(gameplay, application.state, context);
    player.presentation.placement={gameplay.scoring.placedBlocks,piece.type(),movementState.pose,
        placement.clear.clearedRows,movementState.hardDropFromRow};

    if (std::getenv("TETR_TEST_METRICS") && !context.config.mode.replayMode) {
        std::unique_lock<std::recursive_mutex> outputLock;
        if (context.output_mutex()) outputLock = std::unique_lock<std::recursive_mutex>(*context.output_mutex());
        static std::mutex metricsMutex;
        std::lock_guard<std::mutex> lock(metricsMutex);
        const bool secondSide = context.app_component() &&
            context.app_component()->primaryContext != &context;
        std::cerr << "TETR_TEST_PLACEMENT"
                  << " side=" << (secondSide ? "second" : "first")
                  << " t=" << Runtime::elapsed_game_time(context.session, context.now()).seconds()
                  << " piece=" << Piece::metadata(piece.type()).name
                  << " lines=" << placement.clear.clearedLines
                  << " garbage_lines=" << placement.clear.clearedGarbageLines
                  << " spin=" << int(placement.spin.spin)
                  << " mini=" << int(placement.attack.effectiveMini)
                  << " attack=" << application.effects.spikeAttack
                  << " outgoing=" << application.effects.outgoingAttack
                  << " b2b_before=" << placement.chain.previousB2B
                  << " b2b_after=" << placement.chain.b2b
                  << " b2b_charge=" << placement.chain.b2bCharging
                  << " total_pieces=" << application.state.placedBlocks
                  << '\n';
    }

    present_spike_attack(context, player, application.effects.spikeAttack);

    present_post_offset_quick_play_state(
        context,
        field,
        player,
        placement,
        application.effects.decayTimedSolidGarbage);
    apply_post_offset_placement_garbage(context, field, player, application.effects);
    present_extended_all_spin_history(context, player, placement);

    const Rules::PlayResult lineClearResult = run_locked_line_clear_phase(
        context, field, piece, player, placement, pumpFrame,movementInput);
    if (lineClearResult != Rules::PlayResult::ContinueFrame) return lineClearResult;

    // rIN's visibility boundary moves when garbage lines disappear. Equal
    // garbage values are omitted by the collapse diff, but can change color.
    if (placement.clear.clearedGarbageLines>0 &&
        context.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
        Runtime::reverse(context.config.quickPlay.invisible)) {
        Render::OutputGuard output(context);
        for (int row=-Runtime::kDisplayHiddenRows;row<gameplay.playfield.visibleHeight;++row)
            for (int column=0;column<gameplay.playfield.width;++column) {
                const auto cell=field.cell(row,column);
                if (cell.is_normal_garbage()) Render::draw_board_cell(context,player,row,column,cell);
            }
    }

    // Complete the old-width clear and collapse before the next piece spawns.
    // Replay derives identical growth from its resolved cumulative line count.
    if (Runtime::enabled(context.config.quickPlay.snowball) &&
        Rules::qp_snowball_size(gameplay.scoring.totalLines)>gameplay.playfield.width) {
        Render::OutputGuard output(context);
        Render::erase_player_board(context,player);
        grow_snowball_board(context,player);
        if (const auto size=Render::terminal_size()) center_game_boards(context,*size);
        Render::redraw_player_board(context,player);
        GarbageService::redraw_buffer(context,player);
    }

    if (auto* recorder = Replay::tetrio_recorder(context)) recorder->lock(context, placement);
    const auto timeResult = advance_piece_time(context, player);

    // The piece and line clears are settled before an overdue timed row rises.
    // Record the row before EndPlace so replay applies the same ordering.
    apply_due_cheese_timer_after_autoplay_lock(context, field, player);
    const Rules::PlayResult result = complete_locked_piece_side_effects(
        context, field, piece, player, placement, application.effects);
    const auto metrics=Runtime::field_metrics(field,context.config.rotationSystem);
    gameplay.scoring.stackHeight=metrics.height;
    const auto metricTime=context.config.mode.replayMode?Replay::playback_game_elapsed(context,context.now()):Runtime::elapsed_game_time(context.session,context.now());
    if(metrics.secretGrade>gameplay.scoring.secretGrade){gameplay.scoring.secretGradePieces=gameplay.scoring.placedBlocks;gameplay.scoring.secretGradeTime=metricTime;}
    if(metrics.checkerboardRows>gameplay.scoring.patternRows){gameplay.scoring.patternPieces=gameplay.scoring.placedBlocks;gameplay.scoring.patternTime=metricTime;}
    gameplay.scoring.secretGrade=std::max(gameplay.scoring.secretGrade,metrics.secretGrade);
    gameplay.scoring.patternRows=std::max(gameplay.scoring.patternRows,metrics.checkerboardRows);
    if(context.config.challengeRules==Runtime::ChallengeRules::ConstructSymmetry)
        for(int y=0;y<field.height();++y)for(int x=0;x<field.width();++x)Render::draw_board_cell(context,player,y,x,field.cell(y,x));
    if(!context.config.mode.replayMode && context.config.challengeRules==Runtime::ChallengeRules::Audio) {
        Render::play_audio_cue(Render::AudioCue::Clear,placement.clear.clearedLines);
        std::vector<int> heights;for(int x=0;x<field.width();++x){int height=0;for(int y=0;y<field.height();++y)if(field.cell(y,x).occupied()){height=field.height()-y;break;}heights.push_back(height);}Render::play_audio_heights(heights);
    }
    if(context.config.challengeRules==Runtime::ChallengeRules::StageMaster) {
        auto& task=gameplay.challenge;task.stageLines+=placement.clear.clearedLines;
        task.stageQuads+=placement.clear.clearedLines==4;task.stageSpins+=placement.spin.spin && placement.clear.clearedLines>0;
        const bool complete=task.stage==0?task.stageLines>=10:task.stage==1?task.stageQuads>=2:task.stage==2?task.stageSpins>=2:placement.allClear;
        if(complete && task.stage<4){++task.stage;task.stageLines=task.stageQuads=task.stageSpins=0;gameplay.scoring.challengeStages=task.stage;}
        Runtime::set_current_epld(context,Runtime::Duration::from_seconds(std::array<double,4>{.8,.5,.25,.12}[std::min(task.stage,3)]));
    }
    {
        if(auto* app=context.app_component();app && app->challenge_observer(&context)) {
            int ruleColumn=movementState.pose.position.col;
            const auto family=Piece::metadata(piece.type()).familyIndex;
            const auto& cells=piece.cells(movementState.pose.rotation);
            if(family==0) {
                int minimum=cells.front()[1];for(const auto& cell:cells)minimum=std::min(minimum,cell[1]);ruleColumn+=minimum;
            } else if(family==5) {
                // The T junction is its physical rotation centre. Comparing
                // raw pose origins lets equivalent I/T rotations evade a
                // column-history rule in rotation systems with shifted poses.
                for(const auto& center:cells) {
                    int neighbors=0;for(const auto& cell:cells)neighbors+=std::abs(cell[0]-center[0])+std::abs(cell[1]-center[1])==1;
                    if(neighbors==3){ruleColumn+=center[1];break;}
                }
            }
            const Challenges::Placement sample{piece.type().index(),ruleColumn,
                placement.clear.clearedLines,placement.clear.clearedGarbageLines,
                placement.spin.spin,placement.attack.effectiveMini,placement.allClear,
                placement.chain.previousB2B>0 && placement.chain.b2bTransition==Rules::B2BTransition::Break,metrics.symmetric,finesseFault};
            if(app->challenge_observer(&context)(sample,player))return Rules::PlayResult::Dead;
        }
    }
    if(context.config.challengeRules==Runtime::ChallengeRules::StageMaster && gameplay.challenge.stage>=4)return Rules::PlayResult::Won;
    if(Runtime::construction_map_matches(context.config,field))return Rules::PlayResult::Won;
    if(context.config.challengeRules==Runtime::ChallengeRules::PerfectClearSeries && placement.allClear && gameplay.scoring.allClearCount<3) {
        const auto next=Runtime::challenge_puzzle(static_cast<int>(gameplay.scoring.allClearCount));
        field.clear();gameplay.playfield.garbageHeight=0;
        for(int y=0;y<next.height;++y)for(int x=0;x<next.width;++x) {
            const auto cell=Runtime::map_cell(next.rows[y][x]);field.set_cell(y,x,cell);field.mark_initial_cell(y,x);
            if(cell.is_normal_garbage())gameplay.playfield.garbageHeight=std::max(gameplay.playfield.garbageHeight,next.height-y);
        }
        // The outer loop consumes this lock's current piece after returning.
        while(player.pieces.bag.size()>1)player.pieces.bag.pop_back();
        for(auto type:next.sequence)player.pieces.bag.push_back({type,false});
        Render::redraw_player_board(context,player);
    }
    if (!context.config.mode.replayMode) {
        Render::OutputGuard output(context);
        std::fflush(stdout);
    }
    // rAS specifies immediate death. On a small Snowball board, clamping its
    // 20-row penalty to the available height must not let Clutch keep it alive.
    if (result==Rules::PlayResult::Locked &&
        context.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
        context.config.quickPlay.allSpin==Runtime::AllSpinMode::Reverse &&
        application.effects.solidGarbageLines>=20)
        return Rules::PlayResult::Dead;
    const auto finalResult = timeResult != Rules::PlayResult::ContinueFrame ? timeResult : result;
    if (finalResult == Rules::PlayResult::Locked) schedule_entry_delay(context,player);
    return finalResult;
}

} // namespace Engine
