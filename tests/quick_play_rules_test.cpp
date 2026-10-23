#include "tetr/rules/quick_play.h"
#include "tetr/rules/attack.h"
#include "tetr/rules/chain.h"
#include "tetr/rules/frame_update.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/engine/placement_application.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/frame_board_adapter.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/board_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/field_visibility.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <regex>
#include <sstream>
#include <iomanip>

namespace TetrTests::quick_play_rules_test {
namespace {
using Runtime::ModifierMode;
using Runtime::AllSpinMode;
using Runtime::Duration;
void close(double a, double b) { assert(std::abs(a-b) < 1e-8); }

void delays_and_mods() {
    Runtime::QuickPlayConfig c;
    for (int floor=1; floor<=10; ++floor) {
        close(Rules::qp_garbage_delay(c,floor,0).seconds(),5.5-floor*0.5);
        close(Rules::qp_garbage_delay(c,floor,25).seconds(),5.5-floor*0.5);
    }
    c.continuousCancellation=true;
    for (int floor=1; floor<=10; ++floor) {
        close(Rules::qp_garbage_delay(c,floor,25).seconds(),(5.5-floor*0.5)*0.5);
    }
    c.expert=ModifierMode::Enabled;
    close(Rules::qp_garbage_delay(c,1,0).seconds(),2.2);
    close(Rules::qp_garbage_delay(c,10,0).seconds(),0.4);
    c.expert=ModifierMode::Reverse;
    c.messy=ModifierMode::Reverse;
    c.volatileMode=ModifierMode::Reverse;
    c.doubleHole=ModifierMode::Reverse;
    for (int floor=1; floor<=10; ++floor)
        close(Rules::qp_garbage_delay(c,floor,100).seconds(),2.4-floor*0.2);
    c={};
    c.expert=ModifierMode::Disabled;
    c.messy=ModifierMode::Reverse;
    close(Rules::qp_garbage_delay(c,1,0).seconds(),2.5);
    close(Rules::qp_garbage_delay(c,10,0).seconds(),2.5);
    c.expert=ModifierMode::Enabled; // Combination: expert timing takes precedence.
    close(Rules::qp_garbage_delay(c,10,0).seconds(),0.4);
    c={}; c.noHold=ModifierMode::Reverse;
    close(Rules::qp_garbage_delay(c,1,0).seconds(),2.5);
    close(Rules::qp_garbage_delay(c,10,0).seconds(),0.5);
    assert(Rules::qp_floor(49.99)==1 && Rules::qp_floor(50)==2);
    assert(Rules::qp_floor(1650)==10 && Rules::qp_floor(9000)==10);
}

void booster() {
    Runtime::QuickPlayConfig c;c.legacyRules=true;
    Runtime::QuickPlayProgress p;
    double level=10, altitude=0;
    p.experience=20;
    Rules::qp_advance_booster(p,level,altitude,1,c,1);
    close(p.experience,14.5); // Document: level 10 loses 5.5 XP/s.
    close(altitude+p.pendingAltitude,2.5);
    p={}; p.experience=20; altitude=100; c.expert=ModifierMode::Enabled;
    Rules::qp_advance_booster(p,level,altitude,2,c,1);
    close(p.experience,20-110.0/12);
    p={}; p.experience=4; level=1; altitude=0; c={}; c.legacyRules=true;
    Rules::qp_advance_booster(p,level,altitude,1,c,0.2);
    close(level,2); close(p.experience,0); close(p.decayProtection,4.8);
    assert(p.upgradeFatigue==1);
    p.decayProtection=0;
    Rules::qp_advance_booster(p,level,altitude,1,c,0.2);
    close(level,1); close(p.experience,3.98); // Lower level loses XP during the remaining frame time.
    Rules::qp_advance_booster(p,level,altitude,1,c,0.2);
    close(level,1); close(p.experience,3.96);
    p.experience=4; // A genuine XP gain can upgrade again, with the existing fatigue.
    Rules::qp_advance_booster(p,level,altitude,1,c,0.2);
    close(level,2); close(p.decayProtection,3.8); // Repeated upgrade fatigue.
    p.experience=4;
    Rules::qp_advance_booster(p,level,altitude,1,c,0.2);
    assert(p.upgradeFatigue==0);

    p={}; p.experience=100; level=1; altitude=0;
    Rules::qp_advance_booster(p,level,altitude,1,c,0.35);
    close(level,14); close(p.experience,96);
    // One project frame performs one upgrade+skip even when its delta is large.
    Rules::qp_advance_booster(p,level,altitude,1,c,0.35);
    close(level,15); close(p.experience,40);

    p={}; level=1; altitude=49;
    Rules::qp_advance_booster(p,level,altitude,1,c,1);
    close(altitude,49); // Natural ascent stops 1m below the next floor.
    Rules::qp_reward_placement(p,level,altitude,0,0,1,false,0,c);
    close(p.pendingAltitude,3);
    Rules::qp_advance_booster(p,level,altitude,1,c,0.1);
    assert(altitude>50);

    c.expert=ModifierMode::Reverse; p={}; level=3; altitude=20;
    Rules::qp_advance_booster(p,level,altitude,1,c,1);
    close(altitude,19.4);
    altitude=50.1; Rules::qp_advance_booster(p,level,altitude,2,c,1);
    close(altitude,50);
}

void placement_rewards() {
    Runtime::QuickPlayProgress p;
    Runtime::QuickPlayConfig c;c.legacyRules=true;
    Rules::qp_reward_placement(p,4,0,8,2,2,false,0,c);
    close(p.experience,11.125); close(p.pendingAltitude,8);
    p={}; c.expert=ModifierMode::Enabled;
    Rules::qp_reward_placement(p,4,0,8,2,2,false,0,c);
    close(p.experience,8.05);
    p={}; Rules::qp_reward_placement(p,4,49,0,8,1,false,0,c);
    close(p.pendingAltitude,0);
    p={}; c={}; c.legacyRules=true; c.volatileMode=ModifierMode::Reverse;
    Rules::qp_reward_placement(p,4,0,0,2,2,false,0,c);
    close(p.experience,2.05);
    p={}; c={}; c.legacyRules=true; c.allSpin=AllSpinMode::Reverse;
    Rules::qp_reward_placement(p,4,800,8,0,3,false,4,c);
    close(p.experience,1.05); close(p.pendingAltitude,8);
    p={}; c={}; c.legacyRules=true; c.continuousCancellation=true; p.cancellation=200;
    Rules::qp_reward_placement(p,1,0,0,10,0,false,0,c);
    close(p.experience,10.05*0.05);
    p={}; p.cancellation=12;
    Rules::qp_record_placement(p,c,4,0,false,false,0,2,0);
    close(p.cancellation,5);
    Rules::qp_record_placement(p,c,1,1,false,false,3,0,8);
    close(p.cancellation,0);
    p={};
    for (int i=0; i<6; ++i) Rules::qp_record_placement(p,c,2,0,false,true,4,2,0);
    close(p.cancellation,2);
    Rules::qp_record_placement(p,c,0,0,false,false,0,0,0,true);
    close(p.cancellation,5);

    Engine::PlacementApplicationConfig config;
    config.garbageMode=Runtime::GarbageMode::QuickPlay;
    config.qpVolatile=ModifierMode::Reverse;
    Engine::PlacementApplicationState state;
    Rules::PlacementResult placement;
    placement.clear.clearedLines=2;
    placement.attack.attack=8;
    auto begin=Engine::begin_placement_application(state,config,placement,Piece::Type{5});
    assert(begin.effects.offsetAttack==8); // rVL never doubles cancellation.
    auto result=Engine::finish_placement_application(begin,config,6);
    assert(result.effects.outgoingAttack==6);
    close(result.state.qpProgress.experience,8.10);
    close(result.state.qpAltitude,0); // Height is pending until a frame.
    close(result.state.qpProgress.pendingAltitude,1.5);
}

void booster_idle_demotion() {
    Runtime::QuickPlayConfig c;
    Runtime::QuickPlayProgress p;
    double level=10, altitude=0;
    Rules::qp_advance_booster(p,level,altitude,1,c,0.1);
    assert(level==9);
    close(p.experience,36-4.5*0.1);
    const double afterDemotion=p.experience;
    Rules::qp_advance_booster(p,level,altitude,1,c,0.1);
    assert(level==9); // No earned XP: a downgrade must not trigger an upgrade next frame.
    assert(p.experience<afterDemotion);
    close(p.decayProtection,0);
    assert(p.upgradeFatigue==0);

    Rules::qp_reward_placement(p,level,altitude,2,0,0,false,0,c);
    Rules::qp_advance_booster(p,level,altitude,1,c,0.1);
    assert(level==10 && p.decayProtection>0); // Newly earned XP still upgrades normally.

    p={}; p.experience=0.05; level=10; altitude=0;
    double previous=level;
    for (int frame=0; frame<400; ++frame) {
        Rules::qp_advance_booster(p,level,altitude,1,c,0.05);
        assert(level<=previous && level>=1);
        previous=level;
    }
    const double steppedLevel=level, steppedExperience=p.experience;
    p={}; p.experience=0.05; level=10; altitude=0;
    Rules::qp_advance_booster(p,level,altitude,1,c,20);
    close(level,steppedLevel); close(p.experience,steppedExperience);
    assert(level==7); // One long project frame accounts for all elapsed degradation.

    p={}; p.decayProtection=0.1; p.experience=1; level=10; altitude=0;
    Rules::qp_advance_booster(p,level,altitude,1,c,0.3);
    close(level,9); close(p.experience,36-4.5*(0.2-1/5.5));

    p={}; level=2; altitude=0;
    Rules::qp_advance_booster(p,level,altitude,1,c,1e-16);
    assert(level==1 && p.experience<4);
    p.pendingAltitude=1e-18;
    p.decayProtection=1e-20;
    p.fatigueGarbageLines=4;
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState recording;
    context.attach_replay(recording);
    session.quickPlay.speed=level;
    session.quickPlay.progress=p;
    Replay::record_quick_play_state(context,session.player);
    const auto events=Replay::decode_operation_stream(recording.data,0,1);
    const auto& restored=events.front().payload_as<Replay::QuickPlayPlacementPayload>();
    assert(restored.progress->experience==p.experience && restored.progress->experience<4);
    assert(restored.progress->pendingAltitude==p.pendingAltitude);
    assert(restored.progress->decayProtection==p.decayProtection);
    assert(restored.progress->fatigueGarbageLines==p.fatigueGarbageLines);
    auto playbackProgress=*restored.progress;
    double playbackLevel=restored.speed, playbackAltitude=restored.altitude;
    Rules::qp_advance_booster(playbackProgress,playbackLevel,playbackAltitude,1,c,0.1);
    assert(playbackLevel==1); // Serialization must not restore a full, auto-upgrading bar.
    Rules::qp_advance_booster(p,level,altitude,1,c,0.1);
    assert(level==1);

    for (auto expert : {ModifierMode::Disabled,ModifierMode::Enabled,ModifierMode::Reverse}) {
        c.expert=expert; p={}; p.experience=0.05; level=10; altitude=0;
        for (double delta : {0.02,0.4,8.0,0.001,30.0,200.0}) {
            const double before=level;
            Rules::qp_advance_booster(p,level,altitude,1,c,delta);
            assert(level<=before && level>=1 && p.experience>=0);
            close(p.decayProtection,0);
        }
        close(level,1); close(p.experience,0);
    }
}

void attack_and_chain() {
    Runtime::AttackRules legacy; legacy.legacy=true; // Rules recorded before attack settings.
    Rules::AttackInput in;
    in.attackRules.legacy=true;
    in.garbageMode=Runtime::GarbageMode::QuickPlay;
    in.linesCleared=1; in.combo=1;
    in.b2bTransition=Rules::B2BTransition::Break;
    in.previousB2B=10; in.b2bCharging=9;
    assert(Rules::calculate_attack(in).attack==7); // Surge 6 plus the normal single's 1.
    in.qp.expert=ModifierMode::Enabled;
    assert(Rules::calculate_attack(in).attack==6);
    in={}; in.attackRules.legacy=true; in.garbageMode=Runtime::GarbageMode::QuickPlay;
    in.linesCleared=2; in.combo=1; in.spin=true; in.mini=true;
    in.piece.familyIndex=4;
    assert(Rules::calculate_attack(in).attack==1);
    in.qp.allSpin=AllSpinMode::Enabled;
    assert(Rules::calculate_attack(in).attack==4);
    in.qp.noHold=ModifierMode::Reverse;
    assert(Rules::calculate_attack(in).attack==1);
    in={}; in.attackRules.legacy=true; in.garbageMode=Runtime::GarbageMode::QuickPlay;
    in.linesCleared=4; in.spin=true; in.combo=1;
    assert(Rules::calculate_attack(in).attack==8);
    in.perfectClear=true;
    assert(Rules::calculate_attack(in).attack==11);

    const auto pc=Rules::update_chain({2,false,true,AllSpinMode::Disabled,0,0,Runtime::GarbageMode::QuickPlay,legacy});
    assert(pc.b2b==2);
    const auto spinPc=Rules::update_chain({2,true,true,AllSpinMode::Disabled,0,0,Runtime::GarbageMode::QuickPlay,legacy});
    assert(spinPc.b2b==3);
    const auto zeroSpin=Rules::update_chain({0,true,false,AllSpinMode::Reverse,5,0,Runtime::GarbageMode::QuickPlay,legacy});
    assert(zeroSpin.b2b==6);
    in={}; in.attackRules.legacy=true; in.garbageMode=Runtime::GarbageMode::QuickPlay;
    in.qp.allSpin=AllSpinMode::Reverse; in.linesCleared=3; in.combo=10;
    assert(Rules::calculate_attack(in).attack==0);
}

void reception_and_fatigue() {
    Runtime::QuickPlayConfig c;
    Runtime::QuickPlayProgress p;
    close(Rules::qp_receive_attack(4,0,0,1,c,p,0),4);
    c.expert=ModifierMode::Enabled;
    close(Rules::qp_receive_attack(4,0,0,1,c,p,0),4);
    c={}; c.volatileMode=ModifierMode::Reverse;
    close(Rules::qp_receive_attack(4,0,0,1,c,p,0),12);
    c={}; p.receiveProtection=18;
    close(Rules::qp_receive_attack(4,0,0,1,c,p,0),2);
    p={}; p.cancellation=100; c.continuousCancellation=true;
    close(Rules::qp_receive_attack(4,0,0,1,c,p,0),4);
    close(Rules::qp_receive_attack(1,0,0,1,c,p,0),4);
    close(Rules::qp_receive_attack(4,0,0,8,c,p,0),14);
    assert(Rules::qp_windup_segments(11,4500,false)==std::vector<int>({4,4,3}));
    assert(Rules::qp_windup_segments(21,4500,false)==std::vector<int>({4,4,5,5}));

    Rules::FrameRuleConfig cfg;
    cfg.garbageMode=Runtime::GarbageMode::QuickPlay; cfg.replayMode=true;
    cfg.elapsedGameTime=Duration::from_seconds(600);
    Rules::FrameRuleState state;
    std::mt19937 random(1);
    cfg.replayMode=false;
    const auto effects=Rules::advance_frame_before_presentation(state,cfg,Duration::from_seconds(0),random);
    assert(effects.permanentGarbageLines==3);
    const auto again=Rules::advance_frame_before_presentation(state,cfg,Duration::from_seconds(0),random);
    assert(again.permanentGarbageLines==0);
    cfg.qpModExpert=ModifierMode::Reverse; cfg.qpMods.expert=ModifierMode::Reverse;
    cfg.elapsedGameTime=Duration::from_seconds(720); state={};
    assert(Rules::advance_frame_before_presentation(state,cfg,Duration::from_seconds(0),random).permanentGarbageLines==9);
    for (const auto expert:{ModifierMode::Disabled,ModifierMode::Reverse}) {
        cfg.qpModExpert=cfg.qpMods.expert=expert;cfg.qpMods.legacyRules=true;cfg.generateOpponentPressure=false;state={};
        for (int wave=0;wave<3;++wave) {
            const int count=expert==ModifierMode::Reverse ? std::array<int,3>{3,5,12}[wave] : std::array<int,3>{2,3,5}[wave];
            for (int step=-1;step<=count*10;++step) {
                cfg.elapsedGameTime=Duration::from_seconds(480+120*wave+step*.1);
                const auto result=Rules::advance_frame_before_presentation(state,cfg,Duration::from_seconds(.1),random);
                assert(result.permanentGarbageLines==(step>=0 && step%10==0 && step<count*10 ? 1 : 0));
                assert(Rules::advance_frame_before_presentation(state,cfg,Duration{},random).permanentGarbageLines==0);
            }
        }
        const int total=expert==ModifierMode::Reverse ? 20 : 10;
        assert(state.qpProgress.fatigueGarbageLines==total);
        state={};cfg.elapsedGameTime=Duration::from_seconds(750);
        assert(Rules::advance_frame_before_presentation(state,cfg,Duration{},random).permanentGarbageLines==total);
        state={};cfg.replayMode=true;
        assert(Rules::advance_frame_before_presentation(state,cfg,Duration{},random).permanentGarbageLines==0);
        cfg.replayMode=false;
    }
    p={}; p.receiveProtection=10;
    close(Rules::qp_messiness(c,1,p,0),0);
    c.expert=ModifierMode::Reverse;
    close(Rules::qp_messiness(c,1,p,360),0.05);
    close(Rules::qp_messiness(c,1,p,660),1);
    c={}; c.volatileMode=ModifierMode::Reverse;
    close(Rules::qp_garbage_favor(c,10),50);
}

void hole_policy() {
    Rules::GarbageGenerationConfig c;
    c.qpPolicy=true; c.singleMessiness=1; c.continuousMessiness=1;
    c.garbageMode=Runtime::GarbageMode::QuickPlay;
    for (int col=0; col<10; ++col) c.digScores.push_back(col);
    double mean[2]{};
    for (int sign=0; sign<2; ++sign) {
        c.favor=sign==0 ? 30 : -30;
        Rules::GarbageGenerationState state;
        std::mt19937 random(32);
        Rules::prepare_garbage_batch(c,state,random,false);
        for (int row=0; row<2000; ++row) {
            const auto cells=Rules::generate_garbage_row(c,state,random);
            for (int col=0; col<10; ++col) if (cells[col].empty_cell()) mean[sign]+=col/2000.0;
        }
    }
    assert(mean[0]<3 && mean[1]>6);
    c.concentrate=true;
    Rules::GarbageGenerationState state;
    std::mt19937 random(1);
    Rules::prepare_garbage_batch(c,state,random,false);
    for (int row=0; row<200; ++row) {
        const auto cells=Rules::generate_garbage_row(c,state,random);
        assert(cells[0].occupied() && cells[1].occupied() && cells[8].occupied() && cells[9].occupied());
    }
    c.qpPolicy=false; c.doubleHoleMode=ModifierMode::Reverse;
    const int densityCases[][3]={{4,1,2},{6,1,3},{10,3,4},{14,4,6},{20,6,8},{99,29,40},{100,30,40}};
    for (const auto& density:densityCases) {
        const int width=density[0];
        c.width=width; state={};
        Rules::prepare_garbage_batch(c,state,random,false);
        for (int row=0; row<50; ++row) {
            const auto cells=Rules::generate_garbage_row(c,state,random);
            const int occupied=std::count_if(cells.begin(),cells.end(),[](auto cell){return cell.occupied();});
            assert(occupied>=density[1] && occupied<=density[2]);
        }
    }
}

void integration_and_display() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    context.config.simulationPps=1;
    session.gameStartTime=Runtime::TimePoint{};
    auto& player=session.player;
    auto& field=player.gameplay.playfield;
    field.width=10; field.visibleHeight=20; field.activeHeight=20;
    field.field.initialize(10,20);
    for (int i=0;i<7;++i) player.pieces.allowedPieces.push_back(Piece::Type{i});
    player.pieces.random.seed(7); player.garbage.random.seed(8); player.attackRoundingRandom.seed(9);
    Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
    Engine::initialize_quick_play(context,player);
    close(context.config.garbage.incomingDelay.seconds(),5);
    session.quickPlay.progress.cancellation=60;
    Engine::add_bag(context,player);
    assert(player.pieces.bag.size()==7);
    assert(!session.quickPlay.progress.extraI5Issued);
    player.pieces.bag.clear();
    context.config.quickPlay.continuousCancellation=true;
    Engine::add_bag(context,player);
    assert(player.pieces.bag.size()==14);
    assert(std::count_if(player.pieces.bag.begin(),player.pieces.bag.end(),[](const auto& p){
        return p.type==Piece::Type::from_index(21);
    })==1);
    player.pieces.bag.clear();
    Engine::add_bag(context,player);
    assert(player.pieces.bag.size()==13);
    assert(std::none_of(player.pieces.bag.begin(),player.pieces.bag.end(),[](const auto& p){
        return p.type==Piece::Type::from_index(21);
    }));
    session.quickPlay.progress={};
    session.quickPlay.progress.piecesSinceRelief=74;
    context.config.quickPlay.volatileMode=ModifierMode::Reverse;
    Engine::GarbageService::add_buffer(context,player,field.field,4);
    assert(player.garbage.buffer.front().line_count()==12);
    assert(session.quickPlay.progress.receiveProtection==4);
    assert(session.quickPlay.progress.piecesSinceRelief==0);
    const auto records=Replay::decode_operation_stream(replay.data,0,1);
    assert(records.front().payload_as<Replay::AddBufferPayload>().resolvedLines==12);
    context.config.mode.replayMode=true;
    player.garbage.buffer.clear();
    Engine::GarbageService::add_buffer(context,player,field.field,4,false,12);
    assert(player.garbage.buffer.front().line_count()==12);

    session.quickPlay.altitude=12.34;
    session.quickPlay.speed=7;
    session.quickPlay.progress.experience=1.234;
    context.config.garbage.survivalApm=987654;
    std::ostringstream captured;
    auto* previous=std::cout.rdbuf(captured.rdbuf());
    Render::print_altitude(context,player);
    std::cout.rdbuf(previous);
    const auto plain=std::regex_replace(captured.str(),std::regex("\x1b\\[[0-9;]*[A-Za-z]"),"");
    assert(plain.find("-=== 12.3 ===-")!=std::string::npos);
    assert(plain.find("XP")==std::string::npos && plain.find("1.234")==std::string::npos);
    assert(plain.find("987654")==std::string::npos);
    assert(captured.str().find("38;2;255;170;40")!=std::string::npos);
    session.quickPlay.speed=15;
    captured.str(""); captured.clear(); previous=std::cout.rdbuf(captured.rdbuf());
    Render::print_altitude(context,player); std::cout.rdbuf(previous);
    assert(captured.str().find("======")==std::string::npos);
    assert(captured.str().find("38;2;255;255;255")!=std::string::npos);
    const auto legacy=Replay::decode_operation_fields({"end_place_data","0","2.5","12","1","1","1","0"},1);
    assert(!legacy.payload_as<Replay::QuickPlayPlacementPayload>().progress);
    const auto modern=Replay::decode_operation_fields({"end_place_data","0","7","12","1","1","1","0","4.25","2.5","3.5","2"},1);
    const auto& progress=*modern.payload_as<Replay::QuickPlayPlacementPayload>().progress;
    close(progress.experience,4.25); close(progress.pendingAltitude,2.5);

    context.config.field.invisible=true;
    context.config.quickPlay.invisible=ModifierMode::Enabled;
    assert(!Render::board_cell_visible(context,player,10,0,Runtime::BoardCell::piece(0)));
    player.presentation.invisibleFlash=true;
    assert(Render::board_cell_visible(context,player,10,0,Runtime::BoardCell::piece(0)));
    player.presentation.invisibleFlash=false;
    context.config.quickPlay.invisible=ModifierMode::Reverse;
    field.garbageHeight=10;
    assert(Render::board_cell_visible(context,player,12,0,Runtime::BoardCell::garbage()));
    assert(!Render::board_cell_visible(context,player,13,0,Runtime::BoardCell::garbage()));
}

void initial_floor_state() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    UserConfig::Settings user;
    auto& player=session.player;
    for (int requested : {-3,0,1,2,3,4,5,6,7,8,9,10,20})
        for (int expert : {0,1,2}) for (int gravity : {0,1,2}) {
            user.qpDefaultFloor=requested; user.qpExpert=expert; user.qpGravity=gravity;
            context.config=Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay);
            Runtime::reset_dynamic_rules(context);
            auto& field=player.gameplay.playfield;
            field.width=context.config.field.width; field.visibleHeight=context.config.field.height;
            field.nextCount=context.config.field.nextCount;
            player.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem);
            session.quickPlay.altitude=9999;
            session.quickPlay.progress.pendingAltitude=100;
            session.quickPlay.progress.floorTime=90;
            session.quickPlay.progress.windups={{10,5}};
            Engine::initialize_board(context,player);
            Engine::GarbageService::initialize(player,context.config.garbage.holePattern);
            Engine::initialize_quick_play(context,player);
            const int floor=std::clamp(requested,1,10);
            assert(session.quickPlay.floor==floor);
            close(session.quickPlay.altitude,Rules::qp_floor_altitude(floor));
            assert(Rules::qp_floor(session.quickPlay.altitude)==floor);
            close(session.quickPlay.speed,1);
            close(session.quickPlay.progress.pendingAltitude,0);
            close(session.quickPlay.progress.floorTime,0);
            assert(session.quickPlay.progress.windups.empty());
            close(Runtime::current_survival_apm(context),Rules::qp_survival_apm(session.quickPlay.altitude));
            close(Runtime::current_incoming_garbage_delay(context).seconds(),
                Rules::qp_garbage_delay(context.config.quickPlay,floor,0).seconds());
            close(context.config.garbage.singleMessiness,
                Rules::qp_messiness(context.config.quickPlay,floor,session.quickPlay.progress,0));
            if (gravity==1) {
                close(Runtime::current_gravity_interval(context).interval().seconds(),1.0/(30+18*(floor-1)));
                constexpr int epld[]={30,29,28,27,26,24,22,20,18,16};
                close(Runtime::current_epld(context).seconds(),epld[floor-1]/60.0);
            } else if (gravity==2) {
                assert(Runtime::current_gravity_interval(context).is_instant());
                constexpr int epld[]={24,22,20,18,16,15,14,13,12,11};
                close(Runtime::current_epld(context).seconds(),epld[floor-1]/60.0);
            }
            auto state=Engine::capture_frame_rule_state(player,context);
            state.qpBagBreather=false;
            auto config=Engine::make_frame_rule_config(context,{});
            config.replayMode=true;
            std::mt19937 random{123};
            Rules::advance_frame_before_presentation(state,config,{},random);
            assert(state.qpFloor==floor && !state.qpBagBreather); // No spurious floor transition on the first frame.
            close(state.qpAltitude,session.quickPlay.altitude);
        }
    user.qpDefaultFloor=8;
    context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    Engine::initialize_board(context,player);
    close(session.quickPlay.altitude,0);
}

void continuous_cancellation_switch() {
    Runtime::QuickPlayConfig c;
    Runtime::QuickPlayProgress p;
    assert(!c.continuousCancellation);
    for (int i=0; i<80; ++i)
        Rules::qp_record_placement(p,c,2,0,true,true,4,2,10);
    close(p.cancellation,0);
    assert(p.piecesSinceRelief==0 && p.szPieces[0]==-1);

    p.cancellation=200; // Disabled effects ignore even previously recorded counters.
    close(Rules::qp_receive_attack(4,0,0,8,c,p,0),4);
    Rules::qp_reward_placement(p,1,0,0,10,0,false,0,c);
    close(p.experience,10.05*0.5); // Normal cancellation XP is retained.

    Rules::FrameRuleConfig cfg;
    cfg.garbageMode=Runtime::GarbageMode::QuickPlay;
    cfg.replayMode=true;
    cfg.elapsedGameTime=Duration::from_seconds(31);
    Rules::FrameRuleState state;
    std::mt19937 random(11);
    Rules::advance_frame_before_presentation(state,cfg,Duration::from_seconds(31),random);
    close(state.qpProgress.cancellation,0);
    close(state.qpProgress.sinceRelief,0);
    cfg.qpMods.continuousCancellation=true;
    Rules::advance_frame_before_presentation(state,cfg,Duration::from_seconds(31),random);
    close(state.qpProgress.cancellation,5);
    close(state.qpProgress.sinceRelief,1);

    c.continuousCancellation=true;
    p={};
    Rules::qp_record_placement(p,c,2,0,true,false,5,2,10);
    close(p.cancellation,13);

    Runtime::RuntimeConfig recorded, decoded;
    Replay::ConfigPrefixResult prefix;
    for (bool enabled : {false,true}) {
        recorded.quickPlay.continuousCancellation=enabled;
        decoded.quickPlay.continuousCancellation=!enabled;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(recorded)+"}",decoded,prefix));
        assert(decoded.quickPlay.continuousCancellation==enabled);
    }
    assert(Replay::decode_config_prefix("{{Model,3}}",decoded,prefix));
    assert(decoded.quickPlay.continuousCancellation); // Preserve older recordings' rules.
    assert(decoded.quickPlay.legacyRules);
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(recorded)+"}",decoded,prefix));
    assert(!decoded.quickPlay.legacyRules);
}

void audited_rule_regressions() {
    Runtime::QuickPlayConfig c;c.continuousCancellation=true;
    Runtime::QuickPlayProgress p;p.cancellation=100;
    for(int column:{0,1})Rules::qp_record_placement(p,c,4,0,false,false,0,column,0);
    close(p.cancellation,94); // Two columns do not yet establish a third-column change.
    Rules::qp_record_placement(p,c,4,0,false,false,0,0,0); // Refresh LRU order.
    assert((p.quadColumns==std::array<int,2>({0,1})));
    Rules::qp_record_placement(p,c,4,0,false,false,0,2,0);close(p.cancellation,84);
    Rules::qp_record_placement(p,c,4,0,false,false,0,0,0);close(p.cancellation,81);
    Rules::qp_record_placement(p,c,4,0,false,false,0,1,0);close(p.cancellation,74);
    p={};Rules::qp_record_placement(p,c,1,1,true,false,3,0,8);close(p.cancellation,11);
    p={};for(int i=0;i<150;++i)Rules::qp_record_placement(p,c,0,0,false,false,3,0,0);
    close(p.cancellation,5); // One bump at piece 75, not every 75 pieces.
    for(auto mode:{ModifierMode::Enabled,ModifierMode::Reverse}) {
        c.volatileMode=mode;
        close(Rules::qp_garbage_delay(c,1,50).seconds(),mode==ModifierMode::Reverse?2.5:5);
    }
    c={};c.expert=ModifierMode::Reverse;p={};p.stallMultiplier=1.5;
    close(Rules::qp_receive_attack(4,0,0,1,c,p,540),8); // 1 + .5 fatigue + .5 stall.
    c={};p={};
    for(int floor=1;floor<=10;++floor)assert(Rules::qp_garbage_concentrates(c,floor,p,0)==(floor<=5));
    c.expert=ModifierMode::Enabled;p.receiveProtection=18;
    assert(Rules::qp_garbage_concentrates(c,2,p,0));
    assert(!Rules::qp_garbage_concentrates(c,3,p,0)); // Raw .05*3, before grace reduction.
    UserConfig::Settings user;user.qpDoubleHole=2;
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay).garbage.cap==2);
    c={};p={};double level=1,altitude=0;p.experience=4;
    Rules::qp_advance_booster(p,level,altitude,1,c,1.0/60);assert(level==1);
    p={};p.experience=7;p.upgradeFatigue=2;
    Rules::qp_advance_booster(p,level,altitude,1,c,1.0/60);
    assert(level==2 && p.upgradeFatigue==0);close(p.decayProtection,3);
    p={};Rules::qp_reward_placement(p,1,47,4,0,0,false,0,c);close(p.pendingAltitude,4);
    p={};Rules::qp_reward_placement(p,1,47,16,0,0,false,0,c);close(p.pendingAltitude,4);
    c.allSpin=AllSpinMode::Reverse;p={};
    Rules::qp_reward_placement(p,1,800,4,4,2,true,4,c);close(p.experience,0);
    close(Rules::qp_fatigue_garbage_due(c,480.49),1);
    close(Rules::qp_fatigue_garbage_due(c,480.5),2);
    close(Rules::qp_fatigue_garbage_due(c,722.7),10);
    for(int attack=1;attack<=8;++attack) {
        const double raw=attack*1.75;
        const auto low=Rules::apply_double_hole_reverse({true,attack,1,1,true,1});
        const auto high=Rules::apply_double_hole_reverse({true,attack,1,1,true,0});
        assert(low.attack==std::floor(raw) && high.attack==std::ceil(raw));
        assert(Rules::apply_double_hole_reverse({true,attack,1,0,true,0}).attack==high.attack+1);
    }
    Rules::PlacementInput placement;
    placement.mapWidth=10;placement.mapHeight=4;placement.garbageStartRow=4;
    placement.garbageMode=Runtime::GarbageMode::QuickPlay;
    placement.qp.doubleHole=ModifierMode::Reverse;
    const auto filled=[](int,int){return Runtime::BoardCell::piece(1);};
    const auto occupied=[](int,int){return true;};
    assert(Rules::resolve_placement(placement,filled,occupied).chain.b2b==0);
    placement.legacyQuickPlayRules=true;
    assert(Rules::resolve_placement(placement,filled,occupied).chain.b2b>0);
    Engine::PlacementApplicationConfig app;app.garbageMode=Runtime::GarbageMode::QuickPlay;
    app.qpDoubleHole=ModifierMode::Reverse;app.qpBlightedRoundingRoll=0;
    Engine::PlacementApplicationState state;state.qpBlighted=true;
    Rules::PlacementResult clear;clear.clear.clearedLines=1;clear.clear.clearedGarbageLines=1;clear.attack.attack=1;
    assert(Engine::begin_placement_application(state,app,clear,Piece::Type{0}).effects.attack==2);
    app.qpMods.legacyRules=true;
    assert(Engine::begin_placement_application(state,app,clear,Piece::Type{0}).effects.attack==1);
}

// Optional fixtures are consumed by the unchanged official engine oracle.
void audit_fixtures() {
    std::cout<<std::setprecision(17)<<"{\"booster\":[";
    bool first=true;
    for(int expert=0;expert<3;++expert)for(double level:{1.,2.,8.})
        for(double excess:{0.,.01,3.,20.})for(double protection:{0.,2.})for(int fatigue:{0,2,4}) {
        Runtime::QuickPlayConfig c;c.expert=static_cast<ModifierMode>(expert);
        Runtime::QuickPlayProgress p;p.experience=4*level+excess;p.decayProtection=protection;
        p.upgradeFatigue=fatigue;p.pendingAltitude=excess==.01?.04:excess==20?100:3;
        const double startAltitude=excess==3?48:excess==20?50:10;
        const auto before=p;double resultLevel=level,altitude=startAltitude;
        Rules::qp_advance_booster(p,resultLevel,altitude,Rules::qp_floor(altitude),c,1./60);
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"expert\":"<<expert<<",\"level\":"<<level<<",\"xp\":"<<before.experience
            <<",\"protection\":"<<protection<<",\"fatigue\":"<<fatigue<<",\"altitude\":"<<startAltitude
            <<",\"pending\":"<<before.pendingAltitude<<",\"result\":["<<resultLevel<<','<<p.experience<<','
            <<altitude<<','<<p.pendingAltitude<<','<<p.decayProtection<<','<<p.upgradeFatigue<<"]}";
    }
    std::cout<<"],\"awards\":[";first=true;
    for(int mode=0;mode<4;++mode)for(double altitude:{46.,47.,48.,49.,50.,799.,800.})
        for(int outgoing:{0,1,4,16})for(int lines:{0,1,4})for(int dug:{4,5}) {
        Runtime::QuickPlayConfig c;
        if(mode==1)c.expert=ModifierMode::Enabled;
        if(mode==2)c.expert=ModifierMode::Reverse;
        if(mode==3)c.allSpin=AllSpinMode::Reverse;
        Runtime::QuickPlayProgress p;p.pendingAltitude=.5;
        Rules::qp_reward_placement(p,2,altitude,outgoing,0,lines,true,dug,c);
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"mode\":"<<mode<<",\"altitude\":"<<altitude<<",\"outgoing\":"<<outgoing
            <<",\"lines\":"<<lines<<",\"dug\":"<<dug<<",\"result\":["<<p.experience<<','<<p.pendingAltitude<<"]}";
    }
    std::cout<<"],\"center\":[";first=true;
    for(int mode=0;mode<8;++mode)for(int floor=1;floor<=10;++floor)for(int grace:{0,18}) {
        Runtime::QuickPlayConfig c;
        if(mode==1 || mode==2)c.expert=static_cast<ModifierMode>(mode);
        if(mode==3 || mode==4)c.messy=static_cast<ModifierMode>(mode-2);
        if(mode==5)c.volatileMode=ModifierMode::Reverse;
        if(mode==6)c.allSpin=AllSpinMode::Reverse;
        if(mode==7)c.expert=ModifierMode::Reverse;
        Runtime::QuickPlayProgress p;p.receiveProtection=grace;
        if(!first)std::cout<<',';first=false;
        std::cout<<"["<<mode<<','<<floor<<','<<grace<<','
            <<Rules::qp_garbage_concentrates(c,floor,p,mode==7?660:0)<<']';
    }
    std::cout<<"],\"fatigue\":[";first=true;
    for(bool expert:{false,true})for(int frame=28799;frame<=43900;++frame) {
        Runtime::QuickPlayConfig c;if(expert)c.expert=ModifierMode::Reverse;
        if(!first)std::cout<<',';first=false;
        std::cout<<'['<<expert<<','<<frame<<','<<Rules::qp_fatigue_garbage_due(c,frame/60.)<<']';
    }
    std::cout<<"],\"holes\":[";first=true;
    for(int board=0;board<5;++board) {
        std::vector<std::string> rows(8,std::string(10,'.'));
        constexpr int heights[]={1,5,2,0,4,3,7,6,2,1};
        for(int x=0;x<10;++x)for(int y=8-heights[x];y<8;++y)rows[y][x]='x';
        if(board==1 || board==2) {
            rows[7]="gg.gggg.gg";
            if(board==2)rows[7]=std::string(10,'x');
        }
        if(board==3) {rows[6]="gg.gggg.gg";rows[7]=std::string(10,'.');}
        if(board==4)for(auto& row:rows)row=std::string(10,'.');
        const auto scores=Rules::qp_garbage_dig_scores(10,0,8,[&](int y,int x) {
            return rows[y][x]=='.'?Runtime::BoardCell::empty():
                rows[y][x]=='g'?Runtime::BoardCell::garbage():Runtime::BoardCell::piece(1);
        });
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"rows\":[";
        for(int y=0;y<8;++y)std::cout<<(y?",":"")<<'"'<<rows[y]<<'"';
        std::cout<<"],\"scores\":[";
        for(int x=0;x<10;++x)std::cout<<(x?",":"")<<scores[x];
        std::cout<<"]}";
    }
    std::cout<<"]}\n";
}
}

int run(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--audit-fixtures") {audit_fixtures();return 0;}
    for (double t:{0.0,1.0,4.999,6.0,9.999,11.0})
        assert(!Runtime::periodic_invisible_flash(Duration::from_seconds(t),Duration::from_seconds(5),Duration::from_seconds(1)));
    for (double t:{5.0,5.15,5.5,5.999,10.0,10.999})
        assert(Runtime::periodic_invisible_flash(Duration::from_seconds(t),Duration::from_seconds(5),Duration::from_seconds(1)));
    audited_rule_regressions();booster_idle_demotion(); delays_and_mods(); booster(); placement_rewards(); attack_and_chain();
    reception_and_fatigue(); hole_policy(); integration_and_display(); initial_floor_state(); continuous_cancellation_switch();
    std::cout << "quick play rules: OK\n";
    return 0;
}
} // namespace TetrTests::quick_play_rules_test
