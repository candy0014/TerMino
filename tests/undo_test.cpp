#include "tetr/app/game.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/undo.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"

#include <any>
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

namespace TetrTests::undo_test {
namespace {
using Runtime::Duration;
using Runtime::TimePoint;
using Runtime::GameMode;
using Runtime::GarbageMode;

struct Capture {
    std::ostringstream output;
    std::streambuf* previous=std::cout.rdbuf(output.rdbuf());
    ~Capture() { std::cout.rdbuf(previous); }
};

struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    Input::Services input;
    Network::NetworkServices network;
    Engine::PieceLoopState loop;
    bool undoDown=false;
    explicit Fixture(UserConfig::Settings user={}, GameMode mode=GameMode::Zen) {
        user.fieldWidth=4; user.fieldHeight=6; user.nextCount=0;
        // Historical fixture labels now select explicit goals in Zen mode.
        if(mode==GameMode::Racing)user.goal=static_cast<int>(Runtime::GameGoal::Lines);
        if(mode==GameMode::Blitz)user.goal=static_cast<int>(Runtime::GameGoal::Time);
        context.config=Runtime::normalize_user_config(user,mode);
        context.attach_replay(replay); context.attach_input(input);context.attach_network(network);
        Input::refresh_bindings(input,UserConfig::default_key_codes());
        input.keyStateProvider=[this](int key) { return key==UserConfig::default_key_codes()[11] && undoDown; };
        auto& player=session.player;
        auto& board=player.gameplay.playfield;
        board.width=context.config.field.width; board.visibleHeight=context.config.field.height;
        board.nextCount=context.config.field.nextCount; board.holdEnabled=context.config.field.holdEnabled;
        player.presentation.margin=30;
        player.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem);
        player.pieces.random.seed(123); player.garbage.random.seed(456);
        player.attackRoundingRandom.seed(789); services.random.seed(101);
        Engine::initialize_board(context,player);
        Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
        Engine::initialize_board_timers(context,player);
        Engine::initialize_quick_play(context,player);
        replay.data="prefix";
    }
};

void equal_field(const Runtime::PlayfieldState& actual,const Runtime::PlayfieldState& expected) {
    assert(actual.width==expected.width && actual.visibleHeight==expected.visibleHeight);
    assert(actual.activeHeight==expected.activeHeight && actual.nextCount==expected.nextCount);
    assert(actual.holdEnabled==expected.holdEnabled && actual.garbageHeight==expected.garbageHeight);
    assert(actual.solidGarbageHeight==expected.solidGarbageHeight);
    assert(actual.field.initialized()==expected.field.initialized());
    if (actual.field.initialized()) for (int row=-Runtime::kHiddenRowCapacity; row<actual.field.height(); ++row)
        for (int col=0; col<actual.field.width(); ++col) assert(actual.field.cell(row,col)==expected.field.cell(row,col));
}

std::vector<long long> scores(const Runtime::ScoringState& s) {
    return {s.placedBlocks,s.totalAttack,s.totalLines,s.totalGarbageLines,s.score,s.wins,s.combo,s.b2b,
        s.difficultClears,s.b2bExtensions,s.b2bBreaks,s.maxB2B,s.maxCombo,s.clearMoves,s.zeroAttackClears,
        s.attacksAtLeast4,s.attacksAtLeast8,s.maxAttack,s.totalOutgoingAttack};
}

void equal_player(const Runtime::PlayerSessionState& actual,const Runtime::PlayerSessionState& expected) {
    equal_field(actual.gameplay.playfield,expected.gameplay.playfield);
    assert(scores(actual.gameplay.scoring)==scores(expected.gameplay.scoring));
    assert(actual.gameplay.hold.piece==expected.gameplay.hold.piece);
    assert(actual.gameplay.quickPlay.blighted==expected.gameplay.quickPlay.blighted);
    assert(actual.gameplay.quickPlay.operations==expected.gameplay.quickPlay.operations);
    assert(actual.gameplay.quickPlay.lastOperation==expected.gameplay.quickPlay.lastOperation);
    assert(actual.pieces.bag==expected.pieces.bag && actual.pieces.nextPreview==expected.pieces.nextPreview);
    assert(actual.pieces.allowedPieces==expected.pieces.allowedPieces && actual.pieces.bagCount==expected.pieces.bagCount);
    assert(actual.pieces.foolLastTypes==expected.pieces.foolLastTypes && actual.pieces.qpBagBreather==expected.pieces.qpBagBreather);
    assert(actual.pieces.randomizerState.lastGenerated==expected.pieces.randomizerState.lastGenerated);
    assert(actual.pieces.randomizerState.extras==expected.pieces.randomizerState.extras);
    assert(actual.pieces.random==expected.pieces.random && actual.garbage.random==expected.garbage.random);
    assert(actual.attackRoundingRandom==expected.attackRoundingRandom);
    assert(actual.pieces.tetrioRandom.has_value()==expected.pieces.tetrioRandom.has_value());
    if (actual.pieces.tetrioRandom) assert(actual.pieces.tetrioRandom->state()==expected.pieces.tetrioRandom->state());
    if (actual.garbage.tetrioRandom) {
        assert(expected.garbage.tetrioRandom);
        assert(actual.garbage.tetrioRandom->random.state()==expected.garbage.tetrioRandom->random.state());
        assert(actual.garbage.tetrioRandom->column==expected.garbage.tetrioRandom->column);
    }
    assert(actual.garbage.holeOrder==expected.garbage.holeOrder);
    assert(actual.garbage.holePattern==expected.garbage.holePattern);
    assert(actual.garbage.chessboardParity==expected.garbage.chessboardParity);
    assert(actual.garbage.buffer.size()==expected.garbage.buffer.size());
    assert(actual.garbage.incomingFraction==expected.garbage.incomingFraction);
    assert(actual.garbage.cancellation.cancellationFraction==expected.garbage.cancellation.cancellationFraction);
    assert(actual.garbage.cancellation.outgoingFraction==expected.garbage.cancellation.outgoingFraction);
    for (std::size_t i=0; i<actual.garbage.buffer.size(); ++i) {
        assert(actual.garbage.buffer[i].line_count()==expected.garbage.buffer[i].line_count());
        assert(actual.garbage.buffer[i].arrival_time()==expected.garbage.buffer[i].arrival_time());
        assert(actual.garbage.buffer[i].preserve_hole()==expected.garbage.buffer[i].preserve_hole());
    }
    assert(actual.clutch.previousPieceCleared==expected.clutch.previousPieceCleared);
    assert(actual.clutch.currentTurnLifted==expected.clutch.currentTurnLifted && actual.clutch.consecutive==expected.clutch.consecutive);
    assert(actual.presentation.spike.attack==expected.presentation.spike.attack);
    assert(actual.presentation.spike.lastAttackAt==expected.presentation.spike.lastAttackAt);
    assert(actual.presentation.activePose==expected.presentation.activePose);
    assert(actual.presentation.activePieceType==expected.presentation.activePieceType);
    assert(actual.presentation.garbageBufferDisplay==expected.presentation.garbageBufferDisplay);
}

struct FakeStrategy final:Autoplay::Strategy {
    struct Snapshot {
        std::mt19937 random;
        int decisions;
        int* copies;
        Snapshot(const std::mt19937& rng,int count,int* observed)
            :random(rng),decisions(count),copies(observed) {}
        Snapshot(const Snapshot& other)
            :random(other.random),decisions(other.decisions),copies(other.copies) { ++*copies; }
        Snapshot(Snapshot&&)=default;
    };
    std::mt19937 random{246};
    int decisions=0;
    mutable int snapshotCopies=0;
    bool initialize(Runtime::GameContext&) override { return true; }
    std::any capture_state() const override { return Snapshot{random,decisions,&snapshotCopies}; }
    void restore_state(const std::any& saved) override {
        const auto& state=std::any_cast<const Snapshot&>(saved);
        random=state.random; decisions=state.decisions;
    }
    Piece::Type choose_position(Runtime::GameContext&,Runtime::PlayerSessionState& player,Runtime::Playfield field,
        const std::vector<Piece::Type>&,const std::vector<Piece::Type>&,Piece::Pose start,Autoplay::HoldInput,
        Piece::Pose& answer,Runtime::RotationRecord& rotation) override {
        ++decisions; (void)random();
        answer=start; rotation=Runtime::RotationRecord::none();
        const auto piece=Piece::get_definition(Engine::current_piece(player));
        while (piece.can_place(answer.translated(1,0),field)) ++answer.position.row;
        return piece.type();
    }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,
        Piece::Pose,Piece::Pose,Runtime::RotationRecord) override { return "H"; }
};

void long_game_history_is_bounded_without_copying_prior_states() {
    Fixture f;
    FakeStrategy bot;
    f.context.attach_autoplay(bot);
    Engine::begin_undo_history(f.context,&f.loop);
    const auto prefixSize=f.replay.data.size();
    constexpr int limit=static_cast<int>(Engine::kUndoPieceLimit);
    constexpr int locks=limit*4+1;
    for(int i=1;i<=locks;++i) {
        f.session.player.gameplay.scoring.placedBlocks=i;
        f.session.player.gameplay.scoring.score=i*100;
        bot.decisions=i;
        f.replay.data+='x';
        Engine::capture_undo_snapshot(f.context);
        // Copy-count evidence avoids scheduler-dependent timing assertions.
        assert(bot.snapshotCopies==0);
        assert(Engine::undo_snapshot_count(f.context)==static_cast<std::size_t>(std::min(i+1,limit+1)));
    }
    // Evicting undo snapshots must not truncate the full recording.
    assert(f.replay.data.size()==prefixSize+locks);
    constexpr int oldest=locks-limit;
    for(int i=locks-1;i>=oldest;--i) {
        assert(Engine::restore_previous_undo_snapshot(f.context));
        assert(f.session.player.gameplay.scoring.placedBlocks==i);
        assert(f.session.player.gameplay.scoring.score==i*100);
        assert(bot.decisions==i);
        assert(f.replay.data.size()==prefixSize+i);
    }
    assert(Engine::undo_snapshot_count(f.context)==1);
    assert(bot.snapshotCopies==0);
    // Resetting the active piece at the retained boundary cannot reach an
    // evicted placement, even after repeated Undo presses.
    for(int i=0;i<3;++i) {
        assert(Engine::restore_previous_undo_snapshot(f.context));
        assert(f.session.player.gameplay.scoring.placedBlocks==oldest);
        assert(f.session.player.gameplay.scoring.score==oldest*100);
        assert(bot.decisions==oldest && f.replay.data.size()==prefixSize+oldest);
    }
    // A new branch can still be undone to its retained starting boundary.
    f.session.player.gameplay.scoring.placedBlocks=oldest+1;
    Engine::capture_undo_snapshot(f.context);
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(f.session.player.gameplay.scoring.placedBlocks==oldest);
}

void replay_undo_limit_keeps_the_correct_event_suffix() {
    Fixture f;
    f.context.config.mode.replayMode=true;
    constexpr int limit=static_cast<int>(Engine::kUndoPieceLimit),locks=limit+19;
    Replay::configure_playback(f.context,1,Duration::from_seconds(locks+2));
    Replay::start_playback(f.context,f.context.now());
    for(int i=0;i<locks+2;++i)
        f.replay.operations.push({Replay::OperationKind::StartPlace,Duration::from_seconds(i)});
    Engine::begin_undo_history(f.context);
    for(int i=1;i<=locks;++i) {
        f.replay.operations.pop();
        f.session.player.gameplay.scoring.placedBlocks=i;
        f.replay.lastConsumedTimestamp=Duration::from_seconds(i);
        f.replay.lastGameTimestamp=Duration::from_seconds(i);
        Engine::capture_undo_snapshot(f.context);
    }
    for(int i=locks-1;i>=locks-limit;--i) {
        assert(Engine::restore_previous_undo_snapshot(f.context));
        assert(f.session.player.gameplay.scoring.placedBlocks==i);
        assert(f.replay.operations.size()==static_cast<std::size_t>(locks+2-i));
        assert(f.replay.operations.front().timestamp==Duration::from_seconds(i));
        assert(f.replay.lastConsumedTimestamp==Duration::from_seconds(i));
    }
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(f.session.player.gameplay.scoring.placedBlocks==locks-limit);
    // Starting another game/history starts its own window.
    f.session.player.gameplay.scoring.placedBlocks=0;
    Engine::begin_undo_history(f.context);
    assert(Engine::undo_snapshot_count(f.context)==1);
}

void complete_state_and_time_restore() {
    Fixture f;
    auto& player=f.session.player;
    FakeStrategy bot;
    f.context.attach_autoplay(bot);
    player.pieces.tetrioRandom.emplace(12345);
    player.pieces.randomizerState.lastGenerated=5;
    player.pieces.randomizerState.extras={1,6,3};
    player.garbage.tetrioRandom.emplace(67890);
    player.gameplay.playfield.field.set_cell(-99,1,Runtime::BoardCell::piece(4));
    player.gameplay.playfield.field.set_cell(5,2,Runtime::BoardCell::timed_solid(3));
    player.gameplay.hold.piece=Piece::Type::from_index(5);
    player.gameplay.scoring={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19};
    player.gameplay.quickPlay.blighted=true;
    player.gameplay.quickPlay.lastOperation=Runtime::QuickPlayClearOperation{Piece::Type::from_index(4),2,true,true};
    player.gameplay.quickPlay.operations={{1,2,true,true,3}};
    player.clutch={true,true,3}; player.presentation.spike={12,f.context.now()};
    player.garbage.buffer={{3,f.context.now()-Duration::from_seconds(0.75),true}};
    player.garbage.incomingFraction=.123456789;
    player.garbage.cancellation={.75,.25};
    player.timing.frameUpdate=f.context.now()-Duration::from_seconds(0.03);
    player.timing.timedGarbage=f.context.now()-Duration::from_seconds(0.6);
    player.timing.qpGravity=f.context.now()-Duration::from_seconds(0.1);
    f.session.quickPlay.speed=7; f.session.quickPlay.altitude=123.4;
    f.session.quickPlay.progress.experience=0.37; f.session.quickPlay.progress.cancellation=47;
    f.session.dynamicRules.incomingGarbageDelay=Duration::from_seconds(2.2);
    f.loop.input.setPhase(105,Engine::KeyPhase::Repeating);
    f.loop.input.setTimestamp(105,f.context.real_now()-Duration::from_seconds(0.1));
    f.loop.autoplay.selectedPieceType=Piece::Type::from_index(3);
    const auto expected=f.session;
    const auto serviceRandom=f.services.random,botRandom=bot.random;
    const auto before=f.context.now();
    Engine::begin_undo_history(f.context,&f.loop);
    const auto after=f.context.now();
    f.services.clock.sleep_for(Duration::from_seconds(0.025));
    player.gameplay.scoring={}; player.gameplay.playfield.field.clear();
    player.gameplay.hold={}; player.garbage.buffer.clear(); player.clutch={}; player.presentation.spike={};
    player.garbage.incomingFraction=0; player.garbage.cancellation={};
    player.gameplay.quickPlay={}; player.pieces.bag.clear(); player.pieces.random(); player.garbage.random();
    player.attackRoundingRandom(); player.pieces.tetrioRandom->next(); player.garbage.tetrioRandom->row(4);
    player.pieces.randomizerState.lastGenerated=0;
    player.pieces.randomizerState.extras.clear();
    f.services.random(); bot.random(); bot.decisions=9; f.session.quickPlay={}; f.session.dynamicRules.clear();
    f.replay.data+="discarded"; f.loop.reset();
    assert(Engine::restore_previous_undo_snapshot(f.context));
    equal_player(player,expected.player);
    assert(f.replay.undoCount==1);
    assert(f.services.random==serviceRandom && bot.random==botRandom && bot.decisions==0);
    assert(f.session.quickPlay.speed==7 && f.session.quickPlay.altitude==123.4);
    assert(f.session.quickPlay.progress.experience==0.37 && f.session.quickPlay.progress.cancellation==47);
    assert(f.session.dynamicRules.incomingGarbageDelay==expected.dynamicRules.incomingGarbageDelay);
    assert(player.timing.frameUpdate==expected.player.timing.frameUpdate);
    assert(player.timing.timedGarbage==expected.player.timing.timedGarbage && player.timing.qpGravity==expected.player.timing.qpGravity);
    assert(f.context.now()>=before && f.context.now()<after+Duration::from_seconds(0.003));
    assert(f.replay.data=="prefix" && Engine::undo_snapshot_count(f.context)==1);
    assert(f.loop.input.phase(105)==Engine::KeyPhase::Repeating);
    assert(f.loop.autoplay.selectedPieceType==Piece::Type::from_index(3));
}

void game_loop_saves_initial_and_each_lock() {
    UserConfig::Settings user; user.racingDistance=3;
    Fixture f(user,GameMode::Racing);
    auto& player=f.session.player;
    player.pieces.bag.assign(8,{Piece::Type::from_index(0),false});
    const auto play=[&](Runtime::Playfield& field,Piece::Definition piece,Runtime::PlayerSessionState& p,bool held) {
        assert(!held && Engine::undo_snapshot_count(f.context)==static_cast<std::size_t>(p.gameplay.scoring.placedBlocks+1));
        auto movement=Engine::make_spawn_movement_state(4);
        while (piece.can_place(movement.pose.translated(1,0),field)) ++movement.pose.position.row;
        return Engine::finalize_locked_piece(f.context,field,piece,p,movement,0,[] { return Rules::PlayResult::ContinueFrame; });
    };
    assert(Engine::run_piece_loop(f.context,player,play,&f.loop)==Engine::GameLoopExit::GoalReached);
    assert(player.gameplay.scoring.placedBlocks==3 && Engine::undo_snapshot_count(f.context)==4);
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(player.gameplay.scoring.placedBlocks==2 && player.pieces.bag.size()==6);
    assert(Engine::run_piece_loop(f.context,player,play,&f.loop)==Engine::GameLoopExit::GoalReached);
    assert(Engine::undo_snapshot_count(f.context)==4);
    for (int expected=2; expected>=0; --expected) {
        assert(Engine::restore_previous_undo_snapshot(f.context));
        assert(player.gameplay.scoring.placedBlocks==expected && player.gameplay.scoring.totalLines==expected);
        assert(player.pieces.bag.size()==static_cast<std::size_t>(8-expected));
    }
}

void hold_is_part_of_the_undone_turn() {
    for (bool occupied:{false,true}) {
        UserConfig::Settings user; user.racingDistance=1;
        Fixture f(user,GameMode::Racing);
        auto& player=f.session.player;
        const auto initialPiece=Piece::Type::from_index(4);
        const auto nextPiece=Piece::Type::from_index(0);
        const auto heldPiece=Piece::Type::from_index(5);
        player.pieces.bag={{initialPiece,false},{nextPiece,false},{Piece::Type::from_index(3),false}};
        if (occupied) player.gameplay.hold.piece=heldPiece;
        const auto initial=player;
        const auto play=[&](Runtime::Playfield& field,Piece::Definition piece,Runtime::PlayerSessionState& p,bool held) {
            if (!held) return Rules::PlayResult::Hold;
            assert(piece.type()==(occupied?heldPiece:nextPiece));
            ++p.gameplay.scoring.placedBlocks; ++p.gameplay.scoring.totalLines;
            field.set_cell(5,1,Runtime::BoardCell::piece(4));
            return Rules::PlayResult::Locked;
        };
        assert(Engine::run_piece_loop(f.context,player,play,&f.loop)==Engine::GameLoopExit::GoalReached);
        const auto locked=player;
        assert(player.gameplay.hold.piece==initialPiece && Engine::undo_snapshot_count(f.context)==2);
        assert(Engine::restore_previous_undo_snapshot(f.context));
        equal_player(player,initial);
        assert(Engine::run_piece_loop(f.context,player,play,&f.loop)==Engine::GameLoopExit::GoalReached);
        equal_player(player,locked);
    }
}

void snowball_restore_and_random_branch() {
    for (int sb:{1,2}) {
        UserConfig::Settings user; user.qpSnowball=sb; user.qpDoubleHole=2;
        Fixture f(user,GameMode::QuickPlay);
        auto& player=f.session.player;
        auto& board=player.gameplay.playfield;
        f.context.config.simulationPps=2;
        Engine::GarbageService::add_garbage(f.context,player,board.field,2);
        Engine::begin_undo_history(f.context,&f.loop);
        const auto initial=f.session;
        player.gameplay.scoring.placedBlocks=1; player.gameplay.scoring.totalLines=20;
        f.session.simulationTime=TimePoint::from_seconds(0.5); f.session.completedSimulationPieces=1;
        assert(Engine::grow_snowball_board(f.context,player));
        Engine::GarbageService::add_buffer(f.context,player,board.field,2,false,2);
        Engine::capture_undo_snapshot(f.context);
        const auto first=f.session;
        const auto random=f.services.random;
        const auto branch=[&] {
            player.gameplay.scoring.placedBlocks=2; player.gameplay.scoring.totalLines=40;
            f.session.simulationTime=TimePoint::from_seconds(1); f.session.completedSimulationPieces=2;
            assert(Engine::grow_snowball_board(f.context,player));
            Engine::add_bag(f.context,player);
            Engine::GarbageService::add_garbage(f.context,player,board.field,2);
            Engine::GarbageService::add_buffer(f.context,player,board.field,7);
            Engine::run_frame(f.context,board.field,player,false);
            Engine::capture_undo_snapshot(f.context);
        };
        branch();
        const auto expected=f.session;
        const auto expectedRandom=f.services.random;
        assert(Engine::restore_previous_undo_snapshot(f.context));
        equal_player(player,first.player);
        assert(board.width==5 && f.context.config.field.width==5 && f.session.completedSimulationPieces==1);
        assert(f.context.now()==TimePoint::from_seconds(0.5) && f.services.random==random);
        branch();
        equal_player(player,expected.player);
        assert(f.services.random==expectedRandom);
        assert(Engine::restore_previous_undo_snapshot(f.context));
        assert(Engine::restore_previous_undo_snapshot(f.context));
        equal_player(player,initial.player);
        assert(board.width==4 && f.context.config.field.height==4 && f.session.completedSimulationPieces==0);
    }
}

void interrupted_lock_key_debounce_and_modes() {
    Fixture f;
    auto& scoring=f.session.player.gameplay.scoring;
    Engine::begin_undo_history(f.context);
    scoring.placedBlocks=1; Engine::capture_undo_snapshot(f.context);
    // A second lock is being settled but has no complete snapshot yet.
    scoring.placedBlocks=2;
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(scoring.placedBlocks==1 && Engine::undo_snapshot_count(f.context)==2);
    const auto pressed=[&] { try { Engine::poll_undo_key(f.context); return false; } catch (Engine::UndoRequested&) { return true; } };
    f.undoDown=true; assert(pressed()); assert(!pressed());
    f.undoDown=false; assert(!pressed()); f.undoDown=true; assert(pressed());
    for (auto mode:{GameMode::Zen,GameMode::Racing,GameMode::Blitz,GameMode::QuickPlay,GameMode::Replay,GameMode::Multiplayer,GameMode::LocalVersus})
        for (auto garbage:{GarbageMode::None,GarbageMode::Backfire,GarbageMode::Unclear,GarbageMode::CheeseLayer,
            GarbageMode::CheeseTimer,GarbageMode::Survival,GarbageMode::QuickPlay,GarbageMode::Multiplayer}) {
            f.context.config.mode.mode=mode; f.context.config.garbage.mode=garbage;
            const bool enabled=mode!=GameMode::Multiplayer && mode!=GameMode::LocalVersus && garbage!=GarbageMode::Multiplayer;
            assert(Engine::undo_enabled(f.context)==enabled);
            if (!enabled) { assert(!Engine::restore_previous_undo_snapshot(f.context)); assert(!pressed()); }
            f.context.config.disableUndo=true;
            assert(!Engine::undo_enabled(f.context));
            assert(!Engine::restore_previous_undo_snapshot(f.context) && !pressed());
            f.context.config.disableUndo=false;
        }
    f.context.config.mode.mode=GameMode::Replay; f.context.config.garbage.mode=GarbageMode::None;
    f.replay.localVersusReplay=true; assert(!Engine::undo_enabled(f.context));
    f.replay.localVersusReplay=false;
    f.context.config.disableUndo=true;
    const auto count=f.replay.undoCount;
    Engine::begin_undo_history(f.context);
    assert(Engine::undo_snapshot_count(f.context)==0);
    assert(!Engine::restore_previous_undo_snapshot(f.context) && f.replay.undoCount==count);
}

void undo_usage_survives_all_branches() {
    Fixture f;
    assert(f.replay.undoCount==0 && !Engine::restore_previous_undo_snapshot(f.context));
    assert(f.replay.undoCount==0);
    Engine::begin_undo_history(f.context);
    f.session.player.gameplay.scoring.placedBlocks=1; Engine::capture_undo_snapshot(f.context);
    f.session.player.gameplay.scoring.placedBlocks=2; Engine::capture_undo_snapshot(f.context);
    assert(f.replay.undoCount==0);
    assert(Engine::restore_previous_undo_snapshot(f.context) && f.replay.undoCount==1);
    assert(Engine::restore_previous_undo_snapshot(f.context) && f.replay.undoCount==2);
    assert(f.session.player.gameplay.scoring.placedBlocks==0);
    assert(Engine::restore_previous_undo_snapshot(f.context) && f.replay.undoCount==3);
    Engine::begin_undo_history(f.context); // Starting another branch does not clear the attempt's count.
    assert(f.replay.undoCount==3);
    f.context.config.mode.mode=GameMode::Multiplayer;
    assert(!Engine::restore_previous_undo_snapshot(f.context) && f.replay.undoCount==3);
}

void replay_cursor_and_clock_restore() {
    Fixture f;
    f.context.config.mode.replayMode=true;
    Replay::configure_playback(f.context,2,Duration::from_seconds(10));
    Replay::start_playback(f.context,f.context.now());
    f.replay.playback.paused=true;
    for (int i=0; i<7; ++i) f.replay.operations.push({Replay::OperationKind::StartPlace,Duration::from_seconds(i)});
    Engine::begin_undo_history(f.context);
    for (int i=0; i<3; ++i) f.replay.operations.pop();
    f.session.player.gameplay.scoring.placedBlocks=1;
    f.replay.lastConsumedTimestamp=Duration::from_seconds(3);
    f.replay.lastGameTimestamp=Duration::from_seconds(0.5);
    f.replay.playback.elapsed=Duration::from_seconds(3);
    Engine::capture_undo_snapshot(f.context);
    for (int i=0; i<2; ++i) f.replay.operations.pop();
    f.session.player.gameplay.scoring.placedBlocks=2;
    f.replay.playback.elapsed=Duration::from_seconds(5); f.replay.playback.speed=100;
    Engine::capture_undo_snapshot(f.context);
    Replay::finish_playback(f.context,f.context.now());
    assert(Replay::playback_finished(f.context));
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(!Replay::playback_finished(f.context));
    assert(f.replay.operations.size()==4 && f.replay.operations.front().timestamp==Duration::from_seconds(3));
    assert(Replay::playback_elapsed(f.context,f.context.now())==Duration::from_seconds(3));
    assert(f.replay.playback.speed==2 && f.replay.playback.paused);
    assert(f.replay.lastGameTimestamp==Duration::from_seconds(0.5));

    // A seek schedules its destination before the board catches up. Undo
    // must leave rebuild mode at the actual saved lock, including pause state.
    f.replay.playback.rebuilding=true; f.replay.playback.resuming=true;
    f.replay.playback.resumePaused=true;
    f.replay.playback.elapsed=Duration::from_seconds(9);
    Engine::begin_undo_history(f.context);
    f.replay.operations.pop();
    f.session.player.gameplay.scoring.placedBlocks=2;
    f.replay.lastConsumedTimestamp=Duration::from_seconds(4);
    Engine::capture_undo_snapshot(f.context);
    f.replay.playback.rebuilding=false; f.replay.playback.resuming=false;
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(Replay::playback_elapsed(f.context,f.context.now())==Duration::from_seconds(3));
    assert(!f.replay.playback.rebuilding && !f.replay.playback.resuming && f.replay.playback.paused);
    assert(f.replay.operations.size()==4);
}

void app_undo_lifecycle(bool afterGameEnd) {
    UserConfig::Settings user; user.racingDistance=3; user.autoplayMode=1; user.autoplayPps=1000;
    Fixture f(user,GameMode::Racing);
    f.replay.configPrefix=Replay::encode_config_prefix(f.context.config)+"{block_seed,123}{garbage_seed,456}}";
    f.replay.undoCount=7; // A previous attempt's count must reset when this game starts.
    auto& player=f.session.player;
    player.pieces.bag.assign(8,{Piece::Type::from_index(0),false});
    FakeStrategy bot; f.context.attach_autoplay(bot);
    int stage=0;
    bool countReset=false;
    std::size_t finalSnapshots=0;
    f.input.keyStateProvider=[&](int key) {
        if (key==UserConfig::default_key_codes()[11]) {
            if (stage==0 && f.context.undoHistory && player.gameplay.scoring.placedBlocks==0) {
                assert(f.replay.undoCount==0); countReset=true;
            }
            const int undoAt=afterGameEnd?3:1;
            if (stage==0 && player.gameplay.scoring.placedBlocks==undoAt) { stage=1; return true; }
            if (stage==1 && player.gameplay.scoring.placedBlocks==undoAt-1) stage=2;
        }
        if (afterGameEnd && key==UserConfig::default_key_codes()[8] && player.gameplay.scoring.placedBlocks==3) {
            // Hold Undo on the completed game while Retry exits the end screen.
            stage=2;
        }
        const bool retry=key==UserConfig::default_key_codes()[8] && stage==2 && player.gameplay.scoring.placedBlocks==3;
        if (retry) finalSnapshots=Engine::undo_snapshot_count(f.context);
        return retry;
    };
    App::run(f.context);
    assert(stage==2 && player.gameplay.scoring.placedBlocks==3 && bot.decisions==3);
    const std::size_t expectedUndos=afterGameEnd ? 0 : 1;
    assert(countReset && f.replay.undoCount==expectedUndos);
    const auto saved=Replay::recording_for_save(f.context);
    Runtime::RuntimeConfig decoded;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(saved,decoded,prefix) && prefix.undoCount==expectedUndos);
    assert(finalSnapshots==0 && Engine::undo_snapshot_count(f.context)==0);
    const auto operations=Replay::decode_operation_stream(f.replay.data,0,1);
    auto remaining=operations;
    int starts=0;
    while (!remaining.empty()) {
        starts+=remaining.front().kind==Replay::OperationKind::StartPlace; remaining.pop();
    }
    assert(starts==3);
}
void post_game_discards_undo_history() {
    for (bool replayMode:{false,true}) {
        Fixture f;
        f.context.config.mode.replayMode=replayMode;
        Engine::begin_undo_history(f.context);
        f.session.player.gameplay.scoring.placedBlocks=1;
        Engine::capture_undo_snapshot(f.context);
        f.input.keyStateProvider=[](int key) {
            return key==UserConfig::default_key_codes()[11] || key==UserConfig::default_key_codes()[8];
        };
        bool requested=false;
        try { App::run_post_game_interaction(f.context,f.session.player); }
        catch (Engine::UndoRequested&) { requested=true; }
        if (replayMode) {
            assert(requested && Engine::undo_snapshot_count(f.context)==2);
            assert(Engine::restore_previous_undo_snapshot(f.context));
            assert(f.session.player.gameplay.scoring.placedBlocks==0);
        } else {
            assert(!requested && Engine::undo_snapshot_count(f.context)==0);
            Engine::poll_undo_key(f.context);
            assert(!Engine::restore_previous_undo_snapshot(f.context));
            assert(f.session.player.gameplay.scoring.placedBlocks==1);
        }
        assert(f.replay.undoCount==0);
    }
}

void replay_short_taps_and_multiple_presses() {
    Fixture f;
    f.context.config.mode.replayMode=true;
    Input::KeyEventBuffer events;
    f.input.keyEventSource=&events;
    Input::GameplayInputScope capture(&f.input);
    const int undo=UserConfig::default_key_codes()[11];
    events.update(undo,true);
    f.undoDown=true;
    Engine::begin_undo_history(f.context);
    const auto pressed=[&] {
        try { Engine::poll_undo_key(f.context); return false; }
        catch (Engine::UndoRequested&) { return true; }
    };
    assert(!pressed()); // A held key at startup is not a fresh request.
    events.update(undo,false); f.undoDown=false;
    assert(!pressed());
    for (int i=0;i<2;++i) { events.update(undo,true); events.update(undo,false); }
    assert(pressed() && pressed() && !pressed()); // Two short taps between polls.
    events.update(undo,true); f.undoDown=true;
    assert(pressed() && !pressed());
    events.update(undo,true); assert(!pressed()); // OS repeat is ignored.
    events.update(undo,false); events.update(undo,true); events.update(undo,false); f.undoDown=false;
    assert(pressed() && !pressed()); // Release/repress was also between polls.
    auto keys=UserConfig::default_key_codes();
    keys[11]=-1;
    std::array<std::vector<int>,UserConfig::Settings::kControlCount> additional;
    additional[11]={123};
    Input::refresh_bindings(f.input,keys,additional);
    events.update(123,true); events.update(123,false);
    assert(pressed() && !pressed());
}

void app_replay_undo_after_end_all() {
    Fixture f;
    auto& p=f.session.player;
    p.pieces.bag.assign(8,{Piece::Type::from_index(0),false});
    auto piece=Piece::get_definition(Piece::Type::from_index(0));
    Piece::Pose start{{0,1},Runtime::Rotation{0}}, drop=start;
    assert(piece.can_place(start,p.gameplay.playfield.field));
    while (piece.can_place(drop.translated(1,0),p.gameplay.playfield.field)) ++drop.position.row;
    f.replay.data.clear();
    for (int i=0;i<3;++i) {
        Replay::add_replay(f.context,Replay::OperationKind::StartPlace);
        Replay::record_pose_event(f.context,Replay::OperationKind::PutDraw,start);
        Replay::record_pose_event(f.context,Replay::OperationKind::Move,drop);
        Replay::add_replay(f.context,Replay::OperationKind::EndMove);
        Replay::record_end_move_data(f.context,Runtime::RotationRecord::none(),0,drop);
        Replay::add_replay(f.context,Replay::OperationKind::ClearLine);
        Replay::add_replay(f.context,Replay::OperationKind::EndClearLine);
        Replay::add_replay(f.context,Replay::OperationKind::EndPlace);
    }
    Replay::add_replay(f.context,Replay::OperationKind::EndAll);
    f.replay.operations=Replay::decode_operation_stream(f.replay.data,0,1);
    // All events are due immediately; this test covers the actual App loop.
    auto queued=f.replay.operations;
    f.replay.operations={};
    while (!queued.empty()) { auto op=queued.front(); queued.pop(); op.timestamp={}; f.replay.operations.push(op); }
    f.context.config.mode.replayMode=true;
    Replay::configure_playback(f.context,1,Duration{});
    int stage=0,reads=0;
    f.input.keyStateProvider=[&](int key) {
        assert(++reads<100000);
        if (key==UserConfig::default_key_codes()[11]) {
            // EndAll's extra current piece has been consumed only after the
            // piece loop returns, so this press occurs on the end screen.
            if (stage==0 && p.gameplay.scoring.placedBlocks==3 && p.pieces.bag.size()==4) { stage=1; return true; }
            if (stage==1 && p.gameplay.scoring.placedBlocks==2) stage=2;
        }
        return key==UserConfig::default_key_codes()[8] && stage==2 &&
            p.gameplay.scoring.placedBlocks==3 && p.pieces.bag.size()==4;
    };
    App::run(f.context);
    assert(stage==2 && p.gameplay.scoring.placedBlocks==3 && f.replay.operations.empty());
    assert(!f.context.undoHistory);
}

void app_replay_goal_finishes_with_an_unconsumed_end_marker() {
    UserConfig::Settings user; user.racingDistance=1;user.goal=2;
    Fixture f(user,GameMode::Zen);
    f.context.config.mode.replayMode=true;
    f.session.player.gameplay.scoring.totalLines=1;
    f.replay.operations.push({Replay::OperationKind::EndAll,Duration::from_seconds(20)});
    Replay::configure_playback(f.context,1024,Duration::from_seconds(20));
    Replay::prepare_playback_resume(f.context,Duration::from_seconds(19),1024,false);
    f.input.keyStateProvider=[&](int key) {
        return key==UserConfig::default_key_codes()[8] && Replay::playback_finished(f.context);
    };
    Capture rendered;
    App::run(f.context);
    assert(Replay::playback_finished(f.context));
    assert(Replay::playback_progress_elapsed(f.context,f.context.now()).seconds()==20);
    assert(f.replay.operations.size()==1 && f.replay.operations.front().kind==Replay::OperationKind::EndAll);
    assert(rendered.output.str().find("["+std::string(11,'=')+"|]")!=std::string::npos);
}
} // namespace

int run(int argc,char** argv) {
    Capture capture;
    long_game_history_is_bounded_without_copying_prior_states();
    replay_undo_limit_keeps_the_correct_event_suffix();
    complete_state_and_time_restore(); game_loop_saves_initial_and_each_lock(); hold_is_part_of_the_undone_turn();
    snowball_restore_and_random_branch(); interrupted_lock_key_debounce_and_modes(); undo_usage_survives_all_branches();
    replay_cursor_and_clock_restore(); app_undo_lifecycle(false); app_undo_lifecycle(true); post_game_discards_undo_history();
    replay_short_taps_and_multiple_presses(); app_replay_undo_after_end_all();
    app_replay_goal_finishes_with_an_unconsumed_end_marker();
    std::cerr << "undo: OK\n";
    return 0;
}
} // namespace TetrTests::undo_test
