#include "challenge_replay_analysis.h"
#include "init_player_setup.h"
#include "tetr/app/application_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/constants.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/replay_application.h"
#include "tetr/engine/placement_coordinator.h"
#include "../engine/next_preview_presentation.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include <algorithm>

namespace App {
std::optional<Challenges::PrefixBoundary> analyze_challenge_replay(const std::string& data,const Challenges::Definition& definition) {
    // Use the real placement/scoring/garbage pipeline on an isolated context.
    // No menu, clock waiting, user-profile writes, or live BOT/network service.
    try {
        auto owned=std::make_unique<Runtime::SessionState>();auto& session=*owned;
        Runtime::RuntimeServices services;Runtime::GameContext c(session,services);
        Replay::ReplaySessionState replay;c.attach_replay(replay);Input::Services input;c.attach_input(input);
        UserConfig::Settings user;c.attach_user_settings(user);ApplicationState app;app.primaryContext=&c;c.attach_app(app);
        Replay::ConfigPrefixResult prefix;
        if(!Replay::decode_config_prefix(data,c.config,prefix) || !prefix.blockSeed || !prefix.garbageSeed)return {};
        auto original=c.config;original.mode.replayMode=false;
        if(!definition.check(original))return {};
        c.config.mode.replayMode=true;
        replay.operations=Replay::decode_operation_stream(data,prefix.operationOffset);
        if(replay.operations.size()>10000000)return {};
        // Reject incomplete lock transactions before entering marker waiters.
        auto validation=replay.operations;bool lock=false,clear=false,collapsed=false,offset=false;
        while(!validation.empty()) {
            const auto kind=validation.front().kind;validation.pop();
            if(kind==Replay::OperationKind::EndMoveData){if(lock)return {};lock=true;clear=collapsed=offset=false;}
            if(kind==Replay::OperationKind::OffsetData)offset=true;
            if(kind==Replay::OperationKind::ClearLine)clear=true;
            if(kind==Replay::OperationKind::EndClearLine)collapsed=true;
            if(kind==Replay::OperationKind::EndPlace && lock){if(!clear || !collapsed || (Runtime::uses_garbage_buffer(original.garbage.mode) && !offset))return {};lock=false;}
        }
        if(lock)return {};
        auto& p=session.player;auto& b=p.gameplay.playfield;
        InitDetail::reset_player_board(p,c.config);
        p.pieces.random.seed(*prefix.blockSeed);p.garbage.random.seed(*prefix.garbageSeed);
        p.attackRoundingRandom.seed(*prefix.garbageSeed ^ Runtime::kAttackRoundingSeedSalt);
        Engine::initialize_board(c,p);Engine::GarbageService::initialize(p,c.config.garbage.holePattern);
        Engine::initialize_board_timers(c,p);Engine::initialize_quick_play(c,p);
        Challenges::Result initial;initial.config=original;initial.statistics=p.gameplay.scoring;
        Challenges::PrefixTracker tracker({definition},user,initial,definition.id);
        bool violation=false;
        app.challengePlacementObserver=[&](const Challenges::Placement& sample,Runtime::PlayerSessionState& player) {
            Challenges::Result result;result.config=original;result.statistics=player.gameplay.scoring;
            result.gameSeconds=replay.lastGameTimestamp.seconds();result.realSeconds=replay.lastConsumedTimestamp.seconds();
            violation=tracker.observe(sample,result);return violation;
        };
        const bool suppressed=Render::output_suppressed();
        struct Suppress {bool restore;~Suppress(){if(restore)Render::end_output_suppression();}} guard{!suppressed};
        if(!suppressed)Render::begin_output_suppression();
        if(!replay.operations.empty())Replay::configure_playback(c,1024,replay.operations.back().timestamp);
        replay.playback.rebuilding=true;replay.playback.paused=true;
        auto piece=Piece::get_definition(Engine::current_piece(p));Engine::MovementState move;
        while(!replay.operations.empty() && !violation) {
            const auto op=replay.operations.front();replay.operations.pop();
            replay.playback.elapsed=replay.playback.duration;
            Replay::mark_operation_consumed(c,op);
            if(op.kind==Replay::OperationKind::EndAll)break;
            if(op.kind==Replay::OperationKind::StartPlace) {
                if(p.pieces.bag.empty())return {};
                Engine::prepare_next_preview(c,p);piece=Piece::get_definition(Engine::current_piece(p));
                p.gameplay.scoring.roundPieceTypes.insert(piece.type().index());
            }
            Engine::apply_replay_gameplay_event(c,op,b.field,p,piece);
            if(op.kind==Replay::OperationKind::Hold) {
                const auto old=Engine::current_piece(p);Engine::consume_current_piece(p);
                if(p.gameplay.hold.piece.valid())Engine::prepend_piece(p,p.gameplay.hold.piece);
                p.gameplay.hold.piece=old;++p.gameplay.scoring.totalHolds;
            }
            if(op.kind==Replay::OperationKind::EndMoveData) {
                const auto payload=op.payload_as<Replay::EndMoveDataPayload>();move.pose=payload.pose;move.lastRotation=payload.lastRotation;
                move.inputActions=payload.inputActions;p.gameplay.scoring.score=payload.score;
                if(!piece.can_place(move.pose,b.field))return {};
                const auto signal=Engine::finalize_locked_piece(c,b.field,piece,p,move,0,[]{return Rules::PlayResult::ContinueFrame;});
                if(signal!=Rules::PlayResult::Locked && !violation)return {};
                Engine::consume_current_piece(p);
            }
        }
        Challenges::Result full;full.config=original;full.statistics=p.gameplay.scoring;
        full.gameSeconds=prefix.resultGameTime.value_or(replay.lastGameTimestamp).seconds();
        full.realSeconds=prefix.resultRealTime.value_or(replay.lastConsumedTimestamp).seconds();
        tracker.finish(full);
        const auto* result=tracker.find(definition.id);return result?std::optional<Challenges::PrefixBoundary>{result->boundary}:std::nullopt;
    }catch(const std::exception&){return {};}
}
}
