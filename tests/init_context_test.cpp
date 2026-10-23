#include "tetr/compat/legacy_encoding.h"
#include "tetr/app/init.h"
#include "tetr/app/application_state.h"
#include "tetr/input/services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/file_path.h"
#include "tetr/config/user_config.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/config/normalization.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/presentation/board_view.h"
#include "../src/app/init_player_setup.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>

namespace TetrTests::init_context_test {
namespace {
struct InitializedStrategy : Autoplay::Strategy {
    bool initialize(Runtime::GameContext&) override { return true; }
    Piece::Type choose_position(Runtime::GameContext&,Runtime::PlayerSessionState&,Runtime::Playfield,
        const std::vector<Piece::Type>&,const std::vector<Piece::Type>&,Piece::Pose,Autoplay::HoldInput,
        Piece::Pose&,Runtime::RotationRecord&) override { return Piece::Type::none(); }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,
        Piece::Pose,Piece::Pose,Runtime::RotationRecord) override { return {}; }
};
void set_common_user_config(UserConfig::Settings& user){
    user.racingDistance=40;
    user.blitzTime=120;
    user.replayPath="";
    user.replayTimeMultiplier=1;
    user.garbageMode=0;
    user.cheesePattern=1;
    user.holeCount=1;
    user.layerHeight=6;
    user.garbageInterval=3;
    user.cheeseMessiness=.8;
    user.hitMultiplier=1;
    user.survivalApm=60;
    user.qpExpert=0;
    user.qpNoHold=0;
    user.qpMessy=0;
    user.qpGravity=0;
    user.qpVolatile=0;
    user.qpDoubleHole=0;
    user.qpInvisible=0;
    user.qpAllSpin=0;
    user.qpBackfire=0;
    user.qpSnakeEyes=0;
    user.qpFool=0;
    user.qpDefaultFloor=1;
    user.gravityInterval=.6;
    user.softDropFactor=20;
    user.das=.1;
    user.arr=.01;
    user.fieldWidth=10;
    user.fieldHeight=20;
    user.nextCount=5;
    user.holdEnabled=1;
    user.fieldInvisible=0;
    user.antiGravity=0;
    user.ghost=1;
    user.lockDelay=.5;
    user.lockResetLimit=15;
    user.rotationSystem=0;
    user.randomPieces=0;
    user.instantGravity=0;
    user.lineClearDelay=0;
    user.autoplayMode=0;
    user.autoplayHandlingSimulation=0;
    user.autoplayPps=2.5;
    user.autoplayWidth=300;
    user.squareStyle=1;
    user.obscuredColors=0;
}
void new_round_clears_placement_feedback() {
    auto session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    auto& player=session->player;
    App::InitDetail::reset_player_board(player,context.config);
    for(bool queue:{true,false,true}) {
        auto& p=player.presentation;
        p.eventSpinPiece=p.remoteSpinPiece=Piece::Type::from_index(5);
        p.eventClearLines=p.remoteClearLines=4;
        p.eventMini=p.eventPerfectClear=p.eventHalfClear=true;
        p.remoteMini=p.remotePerfectClear=p.remoteHalfClear=true;
        p.remotePlacementRevision=8;p.remoteAttackBaselineKnown=true;
        p.spike={24,context.now()};
        Engine::initialize_board(context,player,queue);
        const auto view=Presentation::capture_board_view(context.config,player,player.gameplay.playfield.field,{});
        assert(!view.spinPiece.valid() && !view.clearLines && !view.mini && !view.perfectClear && !view.halfClear && !view.spike);
        assert(!p.remoteSpinPiece.valid() && !p.remoteClearLines && !p.remoteMini && !p.remotePerfectClear && !p.remoteHalfClear);
        assert(!p.remotePlacementRevision && !p.remoteAttackBaselineKnown && !p.spike.lastAttackAt);
    }
}
void april_fools_initial_floor() {
    auto session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    UserConfig::Settings user;
    assert(user.qpDefaultFloor==1);
    user.rotationSystem=static_cast<int>(Runtime::RotationSystem::AprilFools2026);
    auto& player=session->player;
    for(int requested:{-1,0,1,2,3,4,5,6,7,8,9,10,11}) {
        user.qpDefaultFloor=requested;
        const int floor=std::clamp(requested,1,10);
        for(const auto& randomizer:Runtime::kBagRandomizers) {
            user.randomPieces=static_cast<int>(randomizer.value);
            context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
            App::InitDetail::reset_player_board(player,context.config);
            const auto allowed=player.pieces.allowedPieces;
            assert(allowed.size()==static_cast<std::size_t>(7+7*floor));
            for(int family=0;family<7;++family) assert(allowed[family]==Piece::Type::from_index(family));
            for(const auto type:allowed) {
                const auto metadata=Piece::metadata(type);
                if(metadata.rotationSystem==Runtime::RotationSystem::AprilFools2026)
                    assert(metadata.familyIndex/7+1<=floor);
            }
            // The pool used at startup and by every later bag obeys the limit,
            // including random draws and the repeated/extra-piece policies.
            player.pieces.random.seed(3915917203u);
            Engine::initialize_board(context,player);
            for(int bag=0;bag<12;++bag) Engine::add_bag(context,player);
            for(const auto& entry:player.pieces.bag)
                assert(std::find(allowed.begin(),allowed.end(),entry.type)!=allowed.end());
            const auto queue=player.pieces.bag;
            const auto random=player.pieces.random;
            Runtime::RuntimeConfig decoded;
            Replay::ConfigPrefixResult prefix;
            assert(Replay::decode_config_prefix(Replay::encode_config_prefix(context.config)+"}",decoded,prefix));
            assert(!decoded.legacyFoolPieceSet && decoded.quickPlay.defaultFloor==floor);
            context.config=decoded;
            App::InitDetail::reset_player_board(player,context.config);
            assert(player.pieces.allowedPieces==allowed);
            player.pieces.random.seed(3915917203u);
            Engine::initialize_board(context,player);
            for(int bag=0;bag<12;++bag) Engine::add_bag(context,player);
            assert(player.pieces.bag==queue && player.pieces.random==random);
        }
    }
    // Recordings predating the new pool rule used all ten floors, even when
    // they already selected the current rotation and randomizer rule versions.
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix("{{RotationSystem,6}{QP_default_floor,1}{RandomRules,0}{RandomizerVersion,1}{RotationRulesVersion,1}}",context.config,prefix));
    assert(context.config.legacyFoolPieceSet && !context.config.legacyRandomizerRules);
    App::InitDetail::reset_player_board(player,context.config);
    assert(player.pieces.allowedPieces.size()==77);
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(context.config)+"}",context.config,prefix));
    assert(context.config.legacyFoolPieceSet);
    App::InitDetail::reset_player_board(player,context.config);
    assert(player.pieces.allowedPieces.size()==77);
}
}

int run(int argc,char** argv){
    new_round_clears_placement_feedback();
    april_fools_initial_floor();
#ifdef __linux__
    setenv("TERM","xterm",1);
#endif
    Runtime::SessionState baselineSession;
    Runtime::RuntimeServices baselineServices;
    Runtime::GameContext baseline(baselineSession, baselineServices);
    const int baselineWidth=baseline.config.field.width;
    const int baselinePlayerWidth=baseline.session.player.gameplay.playfield.width;

    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Input::Services inputServices;
    UserConfig::Settings user;
    services.random.seed(12345);
    Runtime::GameContext context(session,services);
    context.attach_input(inputServices);
    context.attach_user_settings(user);
    Replay::ReplaySessionState replaySession;
    context.attach_replay(replaySession);

    set_common_user_config(user);
    user.gameMode=0;
    user.fieldWidth=3;      // clamp to 4
    user.fieldHeight=101;   // clamp to 100
    user.nextCount=-7;      // clamp to 0
    user.holdEnabled=9;      // clamp to 1
    user.rotationSystem=99;
    user.randomPieces=2;
    user.holeCount=0;       // zero is forbidden -> one
    App::initialize_game(context);
    assert(context.config.mode.mode==Runtime::GameMode::Zen);
    assert(context.config.field.width==4);
    assert(context.config.field.height==100);
    assert(context.config.field.nextCount==0);
    assert(context.config.field.holdEnabled);
    assert(context.config.rotationSystem==Runtime::RotationSystem::None); // New maximum selectable value.
    assert(context.config.randomPieces);
    assert(Compat::to_legacy(context.config.garbage.holePattern)==1);
    assert(context.session.player.gameplay.playfield.width==4);
    assert(!context.session.gameStartTime);
    // Explicit non-default initialization must not publish to legacy/default state.
    assert(baseline.config.field.width==baselineWidth);
    assert(baseline.session.player.gameplay.playfield.width==baselinePlayerWidth);

    set_common_user_config(user);
    user.gameMode=3; // Quick Play
    user.qpNoHold=2;
    user.qpGravity=2;
    user.qpVolatile=2;
    user.qpDoubleHole=1;
    user.qpInvisible=1;
    user.qpMessy=2;
    user.qpFool=3;
    user.qpAllSpin=3;
    App::initialize_game(context);
    assert(context.config.garbage.mode==Runtime::GarbageMode::QuickPlay);
    assert(context.config.field.width==10);
    assert(context.config.field.height==16); // 20 * .7, then * 1.2 with legacy int truncation
    assert(context.config.field.nextCount==1);
    assert(!context.config.field.holdEnabled);
    assert(context.config.field.invisible);
    assert(!context.config.field.ghost);
    assert(context.config.handling.gravityInterval.is_instant());
    assert(context.config.handling.gravityInterval.is_instant());
    assert(context.config.handling.epld.seconds()==.4);
    assert(context.config.garbage.layerHeight==4);
    assert(Compat::to_legacy(context.config.garbage.holePattern)==-2);
    assert(context.config.field.lineClearDelay.seconds()==70.0/60.0);
    assert(context.config.rotationSystem==Runtime::RotationSystem::SrsPlus);
    assert(context.config.quickPlay.allSpin==Runtime::AllSpinMode::Reverse);
    assert(!context.config.quickPlay.continuousCancellation);
    App::ApplicationState app;
    context.attach_app(app);
    app.qpContinuousCancellation=true;
    App::initialize_game(context);
    assert(context.config.quickPlay.continuousCancellation);

    // ChatGPT keeps its configured gravity for path geometry and presentation;
    // switching the animation option must not change its placement candidates.
    InitializedStrategy strategy;
    context.attach_autoplay(strategy);
    set_common_user_config(user);
    user.gameMode=0;
    user.autoplayMode=static_cast<int>(Runtime::AutoplayMode::ChatGPT);
    user.autoplayHandlingSimulation=1;
    App::initialize_game(context);
    assert(context.config.handling.gravityInterval==Runtime::GravityInterval::periodic(
        Runtime::Duration::from_seconds(.6)));
    const auto configuredGravity=context.config.handling.gravityInterval;
    Runtime::RuntimeConfig startupReplayConfig;
    Replay::ConfigPrefixResult startupReplayPrefix;
    assert(Replay::decode_config_prefix(replaySession.configPrefix+"}",startupReplayConfig,startupReplayPrefix));
    assert(startupReplayConfig.handling.gravityInterval.enabled());
    assert(std::abs(startupReplayConfig.handling.gravityInterval.interval().seconds()-.6)<.000001);
    user.autoplayHandlingSimulation=0;
    App::initialize_game(context);
    assert(context.config.autoplay.mode==Runtime::AutoplayMode::ChatGPT);
    assert(context.config.handling.gravityInterval==configuredGravity);
    assert(Replay::decode_config_prefix(replaySession.configPrefix+"}",startupReplayConfig,startupReplayPrefix));
    assert(startupReplayConfig.handling.gravityInterval.enabled());
    user.autoplayMode=1;
    App::initialize_game(context);
    assert(!context.config.handling.gravityInterval.enabled());

    // Replay gameplay rules come from the recording, but its rendering must
    // still honor the viewer's current display preferences.
    Runtime::RuntimeConfig recordedConfig;
    recordedConfig.mode.mode=Runtime::GameMode::Zen;
    recordedConfig.display.squareStyle=1;
    recordedConfig.display.obscuredColors=false;
    const std::string replayData=Replay::encode_config_prefix(recordedConfig)+
        "{block_seed,123}{garbage_seed,456}}";
    const std::filesystem::path replayPath=
        Replay::replay_directory()/"init_context_display_replay_test.txt";
    std::filesystem::create_directories(replayPath.parent_path());
    {
        std::ofstream replayFile(replayPath);
        replayFile<<replayData<<Replay::replay_hash_suffix(replayData);
    }
    set_common_user_config(user);
    user.gameMode=5;
    user.replayPath=replayPath.filename().string();
    user.squareStyle=0;
    user.obscuredColors=1;
    user.disableUndo=1;
    App::initialize_game(context);
    std::filesystem::remove(replayPath);
    assert(user.replayPath==replayPath.generic_u8string());
    assert(context.config.mode.replayMode);
    assert(!context.config.quickPlay.continuousCancellation); // Recording overrides startup option.
    assert(context.config.display.squareStyle==0);
    assert(context.config.display.obscuredColors);
    assert(context.config.disableUndo);

    std::cerr<<"init_context_test: OK\n";
    return 0;
}
} // namespace TetrTests::init_context_test
