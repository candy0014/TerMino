#include "tetr/bot/quick_play_search.h"
#include "tetr/bot/bot_chatgpt.h"
#include "tetr/legacy/bot_adapter.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/rules/quick_play.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/replay/session_state.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "../src/bot/chatgpt/policy.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <set>

namespace TetrTests::bot_quick_play_mods_test {
namespace {
using Runtime::AutoplayMode;
using Runtime::Duration;
using Runtime::ModifierMode;
using Runtime::AllSpinMode;
constexpr AutoplayMode allBots[]{AutoplayMode::While1,AutoplayMode::Candy0014,AutoplayMode::Mech,
    AutoplayMode::ZzztojBotris,AutoplayMode::ZzztojToj,AutoplayMode::ZzztojDig,
    AutoplayMode::ZzztojQq,AutoplayMode::ZzztojC2,AutoplayMode::ChatGPT,AutoplayMode::ColdClear};
constexpr AutoplayMode bots[]{AutoplayMode::While1,AutoplayMode::ZzztojBotris,AutoplayMode::ChatGPT};
struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    Input::Services input;
    Runtime::PlayerSessionState& p=session.player;
    explicit Fixture(UserConfig::Settings user={},AutoplayMode bot=AutoplayMode::ChatGPT) {
        context.attach_replay(replay);context.attach_input(input);
        context.config=Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay);
        context.config.autoplay.mode=bot;context.config.autoplay.searchWidth=12;
        context.config.field.lineClearDelay={};context.config.field.entryDelay={};
        input.keyStateProvider=[](int){return false;};
        auto& b=p.gameplay.playfield;
        b.width=context.config.field.width;b.visibleHeight=b.activeHeight=context.config.field.height;
        b.nextCount=context.config.field.nextCount;b.holdEnabled=context.config.field.holdEnabled;
        p.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem,false,context.config.quickPlay.defaultFloor);
        p.pieces.random.seed(123);p.garbage.random.seed(456);p.attackRoundingRandom.seed(789);services.random.seed(101);
        Engine::initialize_board(context,p);Engine::GarbageService::initialize(p,context.config.garbage.holePattern);
        Engine::initialize_board_timers(context,p);Engine::initialize_quick_play(context,p);
    }
    Bot::Environment env() const {
        auto result=Bot::make_environment(context.config);
        result.fieldWidth=p.gameplay.playfield.field.width();result.fieldHeight=p.gameplay.playfield.field.height();
        result.quickPlayProgress=session.quickPlay.progress;result.quickPlayAltitude=session.quickPlay.altitude;
        result.quickPlayBooster=session.quickPlay.speed;result.nowSeconds=context.now().seconds();
        result.quickPlayFloor=session.quickPlay.floor;
        result.elapsedGameSeconds=Runtime::elapsed_game_time(session,context.now()).seconds();
        result.secondsPerPiece=.4;return result;
    }
};

std::vector<UserConfig::Settings> variants() {
    std::vector<UserConfig::Settings> result(1);
    int UserConfig::Settings::* mods[]{&UserConfig::Settings::qpExpert,&UserConfig::Settings::qpNoHold,
        &UserConfig::Settings::qpMessy,&UserConfig::Settings::qpGravity,&UserConfig::Settings::qpVolatile,
        &UserConfig::Settings::qpDoubleHole,&UserConfig::Settings::qpInvisible,&UserConfig::Settings::qpAllSpin,
        &UserConfig::Settings::qpBackfire,&UserConfig::Settings::qpSnakeEyes,&UserConfig::Settings::qpSnowball};
    for(auto mod:mods)for(int value:{1,2}) {UserConfig::Settings u;u.*mod=value;result.push_back(u);}
    for(int fool=1;fool<=4;++fool) {UserConfig::Settings u;u.qpFool=fool;result.push_back(u);}
    UserConfig::Settings extended;extended.qpAllSpin=3;result.push_back(extended);
    for(int sb:{0,1,2})for(int reverse:{1,2}) {
        UserConfig::Settings u;u.qpSnowball=sb;u.qpAllSpin=reverse;u.qpGravity=reverse;
        u.qpVolatile=reverse;u.qpDoubleHole=reverse;u.qpExpert=reverse;
        u.qpBackfire=reverse;u.qpSnakeEyes=reverse;u.qpNoHold=reverse;u.qpMessy=reverse;
        u.qpInvisible=reverse;result.push_back(u);
    }
    return result;
}

Piece::Pose spawn(Fixture& f,Piece::Type type) {
    const auto piece=Piece::get_definition(type);auto pose=Piece::spawn_pose(f.p.gameplay.playfield.field.width(),type);
    if(Runtime::enabled(f.context.config.quickPlay.snowball))
        pose=Piece::fit_spawn_inside_walls(pose,piece.cells(pose.rotation),f.p.gameplay.playfield.field.width());
    if(f.p.clutch.previousPieceCleared)while(!piece.can_place(pose,f.p.gameplay.playfield.field) && pose.position.row>-90)--pose.position.row;
    if(f.context.config.handling.gravityInterval.is_instant()) {
        Engine::MovementState movement;movement.pose=pose;
        Engine::apply_presentation_instant_gravity(movement,[&](Piece::Pose p){return piece.can_place(p,f.p.gameplay.playfield.field);});
        pose=movement.pose;
    }
    return pose;
}
void check_route(Fixture& f,const std::vector<Piece::Type>& next,Piece::Pose start,Autoplay::HoldInput hold) {
    auto& field=f.p.gameplay.playfield.field;
    Piece::Pose answer=start;Runtime::RotationRecord record;
    const auto pieceRandom=f.p.pieces.random,garbageRandom=f.p.garbage.random,attackRandom=f.p.attackRoundingRandom;
    const auto selected=Bot::getpos(f.context,f.p,field,next,{},start,hold,answer,record);
    assert(selected.valid() && (selected==next.front() || (hold.enabled &&
        (hold.piece.valid()?selected==hold.piece:next.size()>1 && selected==next[1]))));
    const auto piece=Piece::get_definition(selected);
    assert(piece.can_place(answer,field) && !piece.can_place(answer.translated(1,0),field));
    if(selected!=next.front())start=spawn(f,selected);
    Engine::MovementState movement;movement.pose=start;
    const auto path=Engine::search_autoplay_path(field,piece,movement,answer,record,
        f.context.config.handling,f.context.config.movement,f.context.config.handling.gravityInterval,
        false,{},Engine::autoplay_spin_rules(f.context.config));
    if(!path)std::cerr<<"QP route failure bot="<<int(f.context.config.autoplay.mode)<<" piece="<<selected.index()<<'\n';
    assert(path && Engine::autoplay_landing_matches(field,piece,path->finalMovement,answer,record,
        Engine::autoplay_spin_rules(f.context.config)));
    assert(pieceRandom==f.p.pieces.random && garbageRandom==f.p.garbage.random && attackRandom==f.p.attackRoundingRandom);
    if(f.context.config.autoplay.mode==AutoplayMode::ChatGPT)
        assert(Bot_chatgpt::last_diagnostics().fallbackReason==0);
}

void all_mods_and_combinations_have_legal_paths() {
    int cases=0;
    for(auto bot:allBots)for(const auto& u:variants()) {
        auto f=std::make_unique<Fixture>(u,bot);
        assert(Autoplay::availability(f->context.config,bot));
        assert(Bot::init(f->context));
        std::vector<Piece::Type> next;
        const auto& allowed=f->p.pieces.allowedPieces;
        for(std::size_t i=0;i<std::min<std::size_t>(3,allowed.size());++i)next.push_back(allowed[i]);
        auto start=spawn(*f,next.front());
        assert(Piece::get_definition(next.front()).can_place(start,f->p.gameplay.playfield.field));
        check_route(*f,next,start,{f->context.config.field.holdEnabled,Piece::Type::none()});++cases;
    }
    std::cerr<<"QP mod/combination legal paths: "<<cases<<'\n';
}

void special_pieces_growth_zero_das_and_gravity() {
    int cases=0;
    for(auto bot:allBots)for(int fool:{1,2,3,4}) {
        UserConfig::Settings u;u.qpFool=fool;u.qpDefaultFloor=10;
        auto f=std::make_unique<Fixture>(u,bot);assert(Bot::init(f->context));
        auto types=Piece::types_for_rotation_system(f->context.config.rotationSystem,false,10);
        if(fool>=3) {
            const auto event=Piece::types_for_rotation_system(Runtime::RotationSystem::AprilFools2026,false,10);
            types.insert(types.end(),event.begin(),event.end());
        }
        for(auto type:types) {
            const auto start=spawn(*f,type);
            if(!Piece::get_definition(type).can_place(start,f->p.gameplay.playfield.field))continue;
            check_route(*f,{type},start,{false,Piece::Type::none()});++cases;
        }
    }
    for(auto bot:allBots)for(int sb:{1,2})for(int size:{4,5,10,20})for(bool instant:{false,true}) {
        UserConfig::Settings u;u.qpSnowball=sb;u.qpGravity=instant?2:0;
        auto f=std::make_unique<Fixture>(u,bot);
        f->context.config.field.width=f->context.config.field.height=size;
        auto& b=f->p.gameplay.playfield;b.width=b.visibleHeight=b.activeHeight=size;b.field.initialize(size,size);
        assert(Bot::init(f->context));
        for(auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)) {
            auto start=spawn(*f,type);assert(Piece::get_definition(type).can_place(start,b.field));
            check_route(*f,{type},start,{false,Piece::Type::none()});++cases;
        }
    }
    std::cerr<<"QP special-piece/size/handling legal paths: "<<cases<<'\n';
}

struct Quad {Piece::Type type;Piece::Pose pose;};
Quad quad(Fixture& f,bool garbage=false) {
    auto& b=f.p.gameplay.playfield;b.field.clear();b.garbageHeight=garbage?4:0;
    const auto type=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[0];
    const auto piece=Piece::get_definition(type);auto pose=Piece::spawn_pose(b.width,type);pose.rotation=Runtime::Rotation::from_index(1);
    const int column=pose.position.col+piece.cells(pose.rotation).front()[1];
    for(int y=b.activeHeight-4;y<b.activeHeight;++y)for(int x=0;x<b.width;++x)
        if(x!=column)b.field.set_cell(y,x,garbage?Runtime::BoardCell::garbage():Runtime::BoardCell::piece(1));
    while(piece.can_place(pose.translated(1,0),b.field))++pose.position.row;
    assert(piece.can_place(pose,b.field));return {type,pose};
}
void mixed_geometries_keep_distinct_piece_ids() {
    int checked=0;
    for(auto bot:allBots)for(int fool:{3,4}) {
        UserConfig::Settings user;user.qpFool=fool;user.qpNoHold=1;
        Fixture f(user,bot);assert(Bot::init(f.context));
        const auto target=quad(f);
        const auto event=Piece::april_fools_piece(0);
        assert(Piece::metadata(event).familyIndex==Piece::metadata(target.type).familyIndex);
        const std::vector<Piece::Type> next{target.type,event};
        auto answer=spawn(f,target.type);Runtime::RotationRecord rotation;
        const auto selected=Bot::getpos(f.context,f.p,f.p.gameplay.playfield.field,next,{},answer,
            {false,Piece::Type::none()},answer,rotation);
        assert(selected==target.type);
        const auto forecast=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,
            f.p.gameplay.playfield.field,selected,answer,rotation);
        assert(forecast.placement.clear.clearedLines==4 && forecast.placement.clear.perfectClear);
        if(bot==AutoplayMode::ChatGPT) {
            const auto diagnostics=Bot_chatgpt::last_diagnostics();
            assert(diagnostics.rootNext==std::vector<int>({target.type.index(),event.index()}));
            assert(diagnostics.fallbackReason==0);
        }
        ++checked;
    }
    // A cancelled search on a small field must use the shared geometry-aware
    // host fallback; the native fallback assumes a 10x20 tetromino field.
    for(auto bot:{AutoplayMode::ChatGPT,AutoplayMode::ZzztojBotris}) {
        UserConfig::Settings user;user.qpSnowball=1;Fixture f(user,bot);
        assert(Bot::init(f.context));f.context.autoplaySearchCancelled=std::make_shared<std::atomic<bool>>(true);
        const auto type=f.p.pieces.allowedPieces.front();auto answer=spawn(f,type);Runtime::RotationRecord rotation;
        const auto selected=Bot::getpos(f.context,f.p,f.p.gameplay.playfield.field,{type},{},answer,
            {false,Piece::Type::none()},answer,rotation);
        assert(selected==type && Piece::get_definition(type).can_place(answer,f.p.gameplay.playfield.field));
        assert(!Piece::get_definition(type).can_place(answer.translated(1,0),f.p.gameplay.playfield.field));
        ++checked;
    }
    std::cerr<<"QP mixed-family geometry/cancelled small-field regressions: "<<checked<<'\n';
}
void projections_match_real_mod_settlement() {
    for(int mode=0;mode<7;++mode) {
        auto f=std::make_unique<Fixture>();auto& c=f->context.config;
        if(mode==1)c.quickPlay.allSpin=AllSpinMode::Enabled;
        if(mode==2)c.quickPlay.allSpin=AllSpinMode::Reverse;
        if(mode==3)c.quickPlay.volatileMode=ModifierMode::Enabled;
        if(mode==4)c.quickPlay.backfire=ModifierMode::Reverse;
        if(mode==5)c.quickPlay.doubleHole=ModifierMode::Reverse;
        if(mode==6)c.quickPlay.snakeEyes=ModifierMode::Enabled;
        if(mode==1 || mode==2)f->p.gameplay.quickPlay.lastOperation=Runtime::QuickPlayClearOperation{
            Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[0],4,false,false};
        const auto target=quad(*f,mode==5);
        f->p.garbage.buffer.push_back(Rules::make_garbage_packet(3,1,f->context.now(),false,Duration::from_seconds(5)));
        const auto forecast=Bot::QuickPlaySearch::forecast_placement(f->env(),f->p,f->p.gameplay.playfield.field,
            target.type,target.pose,{});
        auto piece=Piece::get_definition(target.type);Engine::MovementState movement;movement.pose=target.pose;
        const auto actual=Engine::finalize_locked_piece(f->context,f->p.gameplay.playfield.field,piece,f->p,movement,0,
            []{return Rules::PlayResult::ContinueFrame;});
        assert(forecast.dead==(actual==Rules::PlayResult::Dead));
        assert(forecast.application.state.b2b==f->p.gameplay.scoring.b2b);
        assert(forecast.application.state.combo==f->p.gameplay.scoring.combo);
        assert(forecast.application.state.totalAttack==f->p.gameplay.scoring.totalAttack);
        assert(forecast.application.state.totalOutgoingAttack==f->p.gameplay.scoring.totalOutgoingAttack);
        assert(forecast.activeHeight==f->p.gameplay.playfield.activeHeight);
        assert(Rules::pending_garbage(forecast.garbage.buffer)==Rules::pending_garbage(f->p.garbage.buffer));
        if(mode!=5)for(int y=-Runtime::kHiddenRowCapacity;y<f->p.gameplay.playfield.field.height();++y)
            for(int x=0;x<c.field.width;++x) {
                // Timed rows have unknown future hole/countdown columns.
                const auto a=forecast.field->cell(y,x),b=f->p.gameplay.playfield.field.cell(y,x);
                assert(a.occupied()==b.occupied());
                if(mode!=1 && mode!=2)assert(a==b);
            }
    }
    UserConfig::Settings u;u.qpSnowball=1;
    auto f=std::make_unique<Fixture>(u);f->p.gameplay.scoring.totalLines=19;
    const auto target=quad(*f);
    const auto prediction=Bot::QuickPlaySearch::forecast_placement(f->env(),f->p,f->p.gameplay.playfield.field,target.type,target.pose,{});
    auto piece=Piece::get_definition(target.type);Engine::MovementState movement;movement.pose=target.pose;
    Engine::finalize_locked_piece(f->context,f->p.gameplay.playfield.field,piece,f->p,movement,0,
        []{return Rules::PlayResult::ContinueFrame;});
    assert(prediction.field->width()==5 && prediction.field->height()==5);
    for(int y=-Runtime::kHiddenRowCapacity;y<5;++y)for(int x=0;x<5;++x)
        assert(prediction.field->cell(y,x)==f->p.gameplay.playfield.field.cell(y,x));
    std::cerr<<"QP settlement/penalty/cancellation/growth reference cases: 8\n";
}

void search_uses_known_events_and_keeps_state_isolated() {
    UserConfig::Settings user;user.qpVolatile=2;
    Fixture f(user);
    f.p.garbage.buffer.clear();f.session.quickPlay.progress.windups.push_back({12,.1});
    auto env=f.env();env.autoplaySearchWidth=1;
    const auto type=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[5];
    const auto start=spawn(f,type);
    const int legacy=5;
    const auto result=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,env,f.p,
        f.p.gameplay.playfield.field,{legacy},{},start,-2);
    assert(result.selected==legacy && !result.candidates.empty());
    auto quiet=env;quiet.quickPlayProgress.windups.clear();
    const auto withoutWarning=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,quiet,f.p,
        f.p.gameplay.playfield.field,{legacy},{},start,-2);
    assert(result.value<withoutWarning.value);
    const auto random=f.p.garbage.random,attack=f.p.attackRoundingRandom;
    f.p.pieces.random();f.p.garbage.random();f.p.attackRoundingRandom();
    f.p.pieces.bag.assign(20,{Piece::Type::from_index(0),false});
    const auto changed=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,env,f.p,
        f.p.gameplay.playfield.field,{legacy},{},start,-2);
    assert(result.selected==changed.selected && result.pose==changed.pose && result.value==changed.value);
    assert(f.session.quickPlay.progress.windups.size()==1);
    assert(f.p.garbage.random!=random && f.p.attackRoundingRandom!=attack);
    env.searchCancelled=std::make_shared<std::atomic<bool>>(true);
    const auto cancelled=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,env,f.p,
        f.p.gameplay.playfield.field,{legacy},{},start,-2);
    assert(cancelled.selected==-1 && cancelled.candidates.empty());
}

void cancellation_removes_armed_rows_before_pending_rows() {
    Fixture f;auto& b=f.p.gameplay.playfield;b.field.clear();
    const auto type=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[0];
    const auto piece=Piece::get_definition(type);auto pose=Piece::spawn_pose(10,type);
    std::vector<int> holes;for(const auto& cell:piece.cells(pose.rotation))holes.push_back(pose.position.col+cell[1]);
    for(int x=0;x<10;++x)if(std::find(holes.begin(),holes.end(),x)==holes.end())
        b.field.set_cell(19,x,Runtime::BoardCell::piece(1));
    while(piece.can_place(pose.translated(1,0),b.field))++pose.position.row;
    f.p.garbage.buffer.push_back(Rules::make_garbage_packet(8,1,f.context.now(),false,Duration::from_seconds(5)));
    f.p.garbage.rising.remainingLines=4;f.p.garbage.rising.interval=Duration::from_seconds(5.0/60);
    f.p.garbage.rising.nextRow=f.context.now()+f.p.garbage.rising.interval;
    const auto forecast=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,type,pose,{});
    auto mutablePiece=piece;Engine::MovementState movement;movement.pose=pose;
    Engine::finalize_locked_piece(f.context,b.field,mutablePiece,f.p,movement,0,
        []{return Rules::PlayResult::ContinueFrame;});
    assert(forecast.cancelled>4 && Rules::pending_garbage(forecast.garbage.buffer)>0);
    assert(forecast.garbage.rising.remainingLines==0 && f.p.garbage.rising.remainingLines==0);
}

void reverse_allspin_death_cannot_be_bought_with_attack_reward() {
    UserConfig::Settings user;user.qpAllSpin=2;
    Fixture f(user);const auto target=quad(f);
    f.p.gameplay.quickPlay.lastOperation=Runtime::QuickPlayClearOperation{target.type,4,false,false};
    auto env=f.env();env.attackRules.perfectClearAttack=1000000;
    const auto prediction=Bot::QuickPlaySearch::forecast_placement(env,f.p,
        f.p.gameplay.playfield.field,target.type,target.pose,{});
    assert(prediction.dead && prediction.application.effects.solidGarbageLines>=20);
    for(auto profile:{Bot::QuickPlaySearch::Profile::While1,Bot::QuickPlaySearch::Profile::Botris,
        Bot::QuickPlaySearch::Profile::ChatGPT}) {
        const auto choice=Bot::QuickPlaySearch::search(profile,env,f.p,
            f.p.gameplay.playfield.field,{0},{},spawn(f,target.type),-2);
        assert(choice.selected==0 && !choice.dead);
        const auto chosen=Bot::QuickPlaySearch::forecast_placement(env,f.p,
            f.p.gameplay.playfield.field,target.type,choice.pose,choice.rotation);
        assert(!chosen.dead);
    }
}

void default_search_budget_has_legal_results() {
    for(auto bot:bots) {
        double maximum=0,total=0;
        for(int mode=0;mode<4;++mode) {
            UserConfig::Settings user;
            if(mode==1){user.qpSnowball=2;user.qpGravity=2;}
            if(mode==2)user.qpFool=4;
            if(mode==3){user.qpAllSpin=1;user.qpVolatile=2;user.qpBackfire=1;}
            Fixture f(user,bot);f.context.config.autoplay.searchWidth=300;
            assert(Bot::init(f.context));
            std::vector<Piece::Type> next;
            for(int i=0;i<6;++i)next.push_back(f.p.pieces.allowedPieces[i%f.p.pieces.allowedPieces.size()]);
            f.session.quickPlay.progress.windups.push_back({12,.1});
            const auto began=std::chrono::steady_clock::now();
            check_route(f,next,spawn(f,next.front()),{f.context.config.field.holdEnabled,Piece::Type::none()});
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
            maximum=std::max(maximum,ms);total+=ms;
        }
        std::cerr<<"QP default-budget bot="<<int(bot)<<" mean_ms="<<total/4<<" max_ms="<<maximum<<'\n';
    }
}

void windup_fatigue_and_piece_time_match_the_host() {
    for(int expert:{0,2})for(bool repeat:{false,true}) {
        UserConfig::Settings user;user.qpExpert=expert;
        auto f=std::make_unique<Fixture>(user);
        f->context.config.simulationPps=2.5;
        f->context.config.garbage.hitMultiplier=0; // Exclude unannounced opponent randomness.
        if(repeat)f->context.config.quickPlay.allSpin=AllSpinMode::Enabled;
        Engine::initialize_board_timers(f->context,f->p);
        f->session.gameStartTime=f->context.now()-Duration::from_seconds(479.9);
        f->session.quickPlay.progress.fatigueStage=7;
        f->session.quickPlay.progress.windups.push_back({12,480.0});
        const auto target=quad(*f);
        if(repeat)f->p.gameplay.quickPlay.lastOperation=Runtime::QuickPlayClearOperation{target.type,4,false,false};
        const auto prediction=Bot::QuickPlaySearch::forecast_placement(f->env(),f->p,
            f->p.gameplay.playfield.field,target.type,target.pose,{},Duration::from_seconds(.4));
        auto piece=Piece::get_definition(target.type);Engine::MovementState movement;movement.pose=target.pose;
        Engine::finalize_locked_piece(f->context,f->p.gameplay.playfield.field,piece,f->p,movement,0,
            []{return Rules::PlayResult::ContinueFrame;});
        assert(prediction.activeHeight==f->p.gameplay.playfield.activeHeight);
        assert(prediction.solidHeight==f->p.gameplay.playfield.solidGarbageHeight);
        assert(Rules::pending_garbage(prediction.garbage.buffer)==Rules::pending_garbage(f->p.garbage.buffer));
        const auto& a=prediction.application.state;const auto& b=f->session.quickPlay;
        assert(a.qpProgress.windups.empty() && b.progress.windups.empty());
        assert(std::abs(a.qpSpeed-b.speed)<1e-8);
        assert(std::abs(a.qpAltitude-b.altitude)<1e-8);
        assert(std::abs(a.qpProgress.experience-b.progress.experience)<1e-8);
        assert(a.qpProgress.receiveProtection==b.progress.receiveProtection);
        for(int y=-Runtime::kHiddenRowCapacity;y<f->context.config.field.height;++y)
            for(int x=0;x<f->context.config.field.width;++x)
                assert(prediction.field->cell(y,x).kind()==f->p.gameplay.playfield.field.cell(y,x).kind());
    }
    std::cerr<<"QP Windup/fatigue/piece-time reference cases: 4\n";
}

void continuous_games_use_live_placement_and_frame_rules() {
    int placements=0,ended=0;
    for(auto bot:allBots)for(const auto& user:variants()) {
        auto f=std::make_unique<Fixture>(user,bot);
        f->context.config.simulationPps=2.5;Engine::initialize_board_timers(f->context,f->p);
        assert(Bot::init(f->context));
        int survived=0;
        const bool ordinary=user.qpFool==0 && user.qpSnowball==0 && user.qpExpert==0 && user.qpNoHold==0 &&
            user.qpMessy==0 && user.qpGravity==0 && user.qpVolatile==0 && user.qpDoubleHole==0 &&
            user.qpInvisible==0 && user.qpAllSpin==0 && user.qpBackfire==0 && user.qpSnakeEyes==0;
        for(int turn=0;turn<(ordinary?40:12);++turn) {
            auto& b=f->p.gameplay.playfield;
            while(f->p.pieces.bag.size()<=static_cast<std::size_t>(b.nextCount+1))Engine::add_bag(f->context,f->p);
            const auto current=Engine::current_piece(f->p);
            auto piece=Piece::get_definition(current);
            Engine::MovementState movement;movement.pose=Piece::spawn_pose(b.width,current);
            if(Engine::handle_spawn_collision(f->context,b.field,piece,f->p,movement)){++ended;break;}
            const auto gravity=Runtime::current_gravity_interval(f->context);
            if(gravity.is_instant())Engine::apply_presentation_instant_gravity(movement,
                [&](Piece::Pose p){return piece.can_place(p,b.field);});
            std::vector<Piece::Type> next;
            for(std::size_t i=0;i<std::min<std::size_t>(f->p.pieces.bag.size(),b.nextCount+1);++i)
                next.push_back(f->p.pieces.bag[i].type);
            Piece::Pose answer=movement.pose;Runtime::RotationRecord record;
            const Autoplay::HoldInput hold{f->context.config.field.holdEnabled,f->p.gameplay.hold.piece};
            const auto selected=Bot::getpos(f->context,f->p,b.field,next,{},movement.pose,hold,answer,record);
            assert(selected.valid());
            if(selected!=current) {
                const auto old=f->p.gameplay.hold.piece;f->p.gameplay.hold.piece=current;
                Engine::consume_current_piece(f->p);if(old.valid())Engine::prepend_piece(f->p,old);
                piece=Piece::get_definition(selected);movement.pose=Piece::spawn_pose(b.width,selected);
                assert(!Engine::handle_spawn_collision(f->context,b.field,piece,f->p,movement));
                if(gravity.is_instant())Engine::apply_presentation_instant_gravity(movement,
                    [&](Piece::Pose p){return piece.can_place(p,b.field);});
            }
            const auto path=Engine::search_autoplay_path(b.field,piece,movement,answer,record,
                f->context.config.handling,f->context.config.movement,gravity,false,{},Engine::autoplay_spin_rules(f->context.config));
            if(!path)std::cerr<<"trajectory path failure bot="<<int(bot)<<" turn="<<turn
                <<" expert="<<user.qpExpert<<" nh="<<user.qpNoHold<<" messy="<<user.qpMessy
                <<" gravity="<<user.qpGravity<<" volatile="<<user.qpVolatile<<" dh="<<user.qpDoubleHole
                <<" invisible="<<user.qpInvisible<<" as="<<user.qpAllSpin<<" bf="<<user.qpBackfire
                <<" se="<<user.qpSnakeEyes<<" sb="<<user.qpSnowball<<" fool="<<user.qpFool<<'\n';
            assert(path);movement=path->finalMovement;
            const auto outcome=Engine::finalize_locked_piece(f->context,b.field,piece,f->p,movement,0,
                [&]{return Engine::run_frame(f->context,b.field,f->p);});
            Engine::consume_current_piece(f->p);++placements;++survived;
            if(outcome==Rules::PlayResult::Dead){++ended;break;}
            assert(outcome==Rules::PlayResult::Locked);
        }
        if(ordinary)assert(survived==40);
    }
    std::cerr<<"QP live-rule trajectories: "<<placements<<" placements; "<<ended<<" terminal games\n";
}

void native_baseline_preserves_search_and_fatigue_floor() {
    for(auto bot:bots)for(int solid:{0,1,3}) {
        Fixture f({},bot);f.context.config.simulationPps=2.5;
        Engine::initialize_board_timers(f.context,f.p);
        auto& b=f.p.gameplay.playfield;
        b.activeHeight=20-solid;b.solidGarbageHeight=solid;b.field.clear();
        for(int y=b.activeHeight;y<20;++y)for(int x=0;x<10;++x)
            b.field.set_cell(y,x,Runtime::BoardCell::solid_garbage());
        for(int y=b.activeHeight-2;y<b.activeHeight;++y)for(int x=0;x<9;++x)
            b.field.set_cell(y,x,Runtime::BoardCell::piece(1));
        f.session.quickPlay.progress.windups.push_back({12,f.env().elapsedGameSeconds+.5});
        const auto env=f.env();
        const auto profile=bot==AutoplayMode::While1?Bot::QuickPlaySearch::Profile::While1:
            bot==AutoplayMode::ZzztojBotris?Bot::QuickPlaySearch::Profile::Botris:Bot::QuickPlaySearch::Profile::ChatGPT;
        const auto view=Bot::QuickPlaySearch::native_baseline(profile,env,f.p,b.field);
        assert(view && Bot::QuickPlaySearch::native_policy_compatible(env));
        assert(!Bot::QuickPlaySearch::enabled(view->environment));
        assert(view->environment.autoplaySearchWidth==env.autoplaySearchWidth);
        assert(view->environment.pieceTypes==env.pieceTypes && view->rowOffset==solid);
        assert(view->player->gameplay.playfield.width==10);
        for(int y=-5;y<b.activeHeight;++y)for(int x=0;x<10;++x)
            assert(view->field->cell(y+solid,x)==b.field.cell(y,x));
        assert(view->player->gameplay.playfield.solidGarbageHeight==0);
        assert(view->player->garbage.buffer.size()==f.p.garbage.buffer.size()+1);
        assert(view->player->garbage.buffer.back().line_count()==12);
        assert(std::abs((view->player->garbage.buffer.back().arrival_time().seconds()-env.nowSeconds)-.5)<1e-8);
        assert(Bot::init(f.context));
        const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
        check_route(f,{types[5],types[0],types[1]},spawn(f,types[5]),{true,types[6]});
        for(int y=b.activeHeight;y<20;++y)for(int x=0;x<10;++x)
            assert(b.field.cell(y,x)==Runtime::BoardCell::solid_garbage());
        f.context.config.quickPlay.allSpin=AllSpinMode::Enabled;
        assert(bool(Bot::QuickPlaySearch::native_baseline(profile,f.env(),f.p,b.field))==
            (bot==AutoplayMode::ChatGPT));
    }
    std::cerr<<"QP native policy/pressure/fatigue-floor bridge cases: 9\n";
}

void non_structural_mods_preserve_native_policy() {
    // These modifiers change visibility, pressure, holes or HOLD availability,
    // not placement settlement. They must not select a different AI policy.
    int UserConfig::Settings::* mods[]{&UserConfig::Settings::qpInvisible,
        &UserConfig::Settings::qpExpert,&UserConfig::Settings::qpMessy,
        &UserConfig::Settings::qpNoHold,&UserConfig::Settings::qpVolatile,
        &UserConfig::Settings::qpDoubleHole,&UserConfig::Settings::qpGravity};
    int cases=0;
    for(auto bot:bots)for(auto mod:mods)for(int value:{1,2}) {
        // Reverse NH changes spin scoring/randomization, rVL changes height,
        // rDH replenishes the field, and rG is unsupported by native Botris.
        if(value==2 && mod!=&UserConfig::Settings::qpInvisible &&
            mod!=&UserConfig::Settings::qpExpert && mod!=&UserConfig::Settings::qpMessy)continue;
        UserConfig::Settings user;user.*mod=value;
        Fixture f(user,bot);
        const auto profile=bot==AutoplayMode::While1?Bot::QuickPlaySearch::Profile::While1:
            bot==AutoplayMode::ZzztojBotris?Bot::QuickPlaySearch::Profile::Botris:Bot::QuickPlaySearch::Profile::ChatGPT;
        const auto baseline=Bot::QuickPlaySearch::native_baseline(profile,f.env(),f.p,f.p.gameplay.playfield.field);
        assert(baseline && "A non-structural mod replaced the bot's native policy");
        assert(Autoplay::availability(f.context.config,bot));
        assert(Bot::init(f.context));
        const auto target=quad(f);
        check_route(f,{target.type},spawn(f,target.type),{false,Piece::Type::none()});
        ++cases;
    }
    // ChatGPT's native spin evaluator also supports Extended AS. Its history
    // only changes host XP; it does not add the repeat penalties of AS/rAS.
    UserConfig::Settings extended;extended.qpAllSpin=3;
    Fixture f(extended);
    const auto baseline=Bot::QuickPlaySearch::native_baseline(Bot::QuickPlaySearch::Profile::ChatGPT,
        f.env(),f.p,f.p.gameplay.playfield.field);
    assert(baseline && Bot_chatgpt::availability(baseline->environment));
    assert(Bot::init(f.context));
    const auto target=quad(f);
    check_route(f,{target.type},spawn(f,target.type),{false,Piece::Type::none()});
    ++cases;
    std::cerr<<"QP modifiers retaining native policy: "<<cases<<'\n';
}

void chatgpt_rule_search_preserves_native_candidate_valuation() {
    // Compare the two actual search paths at one-ply roots, where equal rules
    // must rank terrain, clears and T/I conservation identically. A different
    // search horizon or QP XP must not mask an unrelated evaluation switch.
    int checked=0,dominated=0;
    for(int terrain=0;terrain<4;++terrain)for(int family:{0,5}) {
        Fixture f;f.context.config.garbage.hitMultiplier=0;
        auto& field=f.p.gameplay.playfield.field;
        if(terrain==1)quad(f);
        if(terrain==2)for(int x=0;x<10;++x)for(int y=20-x%4;y<20;++y)
            field.set_cell(y,x,Runtime::BoardCell::piece(1));
        if(terrain==3)for(int x=0;x<10;++x) {
            if(x!=9)field.set_cell(16,x,Runtime::BoardCell::piece(1));
            if(x%3)field.set_cell(19,x,Runtime::BoardCell::piece(1));
        }
        const auto type=f.p.pieces.allowedPieces[family];
        const auto start=spawn(f,type);
        assert(Bot::init(f.context));
        check_route(f,{type},start,{false,Piece::Type::none()});
        const auto native=Bot_chatgpt::last_candidates();
        const auto simulated=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,
            f.env(),f.p,field,{family},{},start,-2);
        assert(!native.empty() && !simulated.candidates.empty());
        const auto cells=[&](const Autoplay::PlacementCandidate& candidate) {
            auto result=Piece::get_definition(type).cells(candidate.pose.rotation);
            for(auto& cell:result) {cell[0]+=candidate.pose.position.row;cell[1]+=candidate.pose.position.col;}
            std::sort(result.begin(),result.end());return result;
        };
        for(const auto& candidate:simulated.candidates) {
            bool matched=false,stronger=false;
            const auto classification=[&](const auto& placement) {
                const auto forecast=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,field,type,placement.pose,placement.rotation);
                return forecast.placement.spin.spin?(forecast.placement.spin.mini?1:2):0;
            };
            const int category=classification(candidate);
            for(const auto& original:native)if(cells(original)==cells(candidate)) {
                if(classification(original)>category && original.value+16000>=candidate.value)stronger=true;
                if(classification(original)==category && std::abs(original.value+16000-candidate.value)<1e-6)matched=true;
            }
            // Native search retains only the strongest spin for equivalent
            // geometry. The rule search also exposes executable non-spin
            // variants: these may be dominated, never differently scored.
            if(!matched && stronger){++dominated;continue;}
            if(!matched)std::cerr<<"valuation mismatch terrain="<<terrain<<" family="<<family<<" pose="
                <<candidate.pose.position.row<<','<<candidate.pose.position.col<<','<<candidate.pose.rotation.index()
                <<" category="<<category<<" simulated="<<candidate.value<<'\n';
            assert(matched && "QP simulation changed ChatGPT's native placement valuation");
            ++checked;
        }
    }
    std::cerr<<"QP/native ChatGPT candidate valuations matched: "<<checked<<"; weaker spin variants="<<dominated<<'\n';
}

void inactive_floor_does_not_change_terrain_policy() {
    UserConfig::Settings user;user.qpAllSpin=1;
    Fixture fatigue(user),shortField(user);
    auto prepare=[](Fixture& f,int visible) {
        auto& b=f.p.gameplay.playfield;
        f.context.config.field.height=visible;f.context.config.garbage.hitMultiplier=0;
        b.visibleHeight=visible;b.activeHeight=17;b.solidGarbageHeight=visible-17;
        b.field.initialize(10,visible);
        for(int y=17;y<visible;++y)for(int x=0;x<10;++x)b.field.set_cell(y,x,Runtime::BoardCell::solid_garbage());
        for(int x=0;x<10;++x)for(int y=17-x%3;y<17;++y)b.field.set_cell(y,x,Runtime::BoardCell::piece(1));
    };
    prepare(fatigue,20);prepare(shortField,17);
    const auto type=fatigue.p.pieces.allowedPieces[5];
    const auto a=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,
        fatigue.env(),fatigue.p,fatigue.p.gameplay.playfield.field,{5},{},spawn(fatigue,type),-2);
    const auto b=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,
        shortField.env(),shortField.p,shortField.p.gameplay.playfield.field,{5},{},spawn(shortField,type),-2);
    assert(!a.candidates.empty() && a.candidates.size()==b.candidates.size());
    for(std::size_t i=0;i<a.candidates.size();++i) {
        assert(a.candidates[i].pose==b.candidates[i].pose);
        assert(std::abs(a.candidates[i].value-b.candidates[i].value)<1e-6);
    }
}

void hold_validates_from_grounded_pose() {
    UserConfig::Settings user;user.qpGravity=2;Fixture f(user);
    auto& field=f.p.gameplay.playfield.field;
    for(int y=12;y<20;++y)for(int x=6;x<10;++x)field.set_cell(y,x,Runtime::BoardCell::piece(1));
    const auto type=f.p.pieces.allowedPieces[3];const auto piece=Piece::get_definition(type);
    assert(Piece::metadata(type).familyIndex==3);
    const auto origin=Piece::spawn_pose(10,type);
    auto target=origin.translated(0,1);
    while(piece.can_place(target.translated(1,0),field))++target.position.row;
    Engine::MovementState airborne;airborne.pose=origin;
    const auto gravity=Runtime::current_gravity_interval(f.context);
    const auto route=[&](const auto& start) {
        return Engine::search_autoplay_path(field,piece,start,target,{},f.context.config.handling,
            f.context.config.movement,gravity,false,{},Engine::autoplay_spin_rules(f.context.config));
    };
    assert(route(airborne)); // The incorrect hold validator accepted this aerial shift.
    auto grounded=airborne;Engine::apply_presentation_instant_gravity(grounded,
        [&](Piece::Pose pose){return piece.can_place(pose,field);});
    assert(!route(grounded)); // Eight rows cannot be climbed after instant gravity.
    f.p.gameplay.hold.piece=type;
    assert(!Bot::QuickPlaySearch::native_answer_valid(f.env(),f.p,field,{5},origin,3,3,target,{}));
}

void capability_scoring_and_geometry() {
    using namespace Bot_chatgpt;
    const auto ordinary=capability_policy({},20,true,false);
    const auto noHold=capability_policy({},20,false,false);
    const auto fullSpin=capability_policy({},20,true,true);
    const auto digging=capability_policy({14,8,6,3},20,true,false);
    assert(noHold.tWaste<ordinary.tWaste && noHold.holdReserve==0);
    assert(fullSpin.tWaste<ordinary.tWaste && digging.garbageClear>ordinary.garbageClear);
    const std::array<std::uint32_t,4> well{0x1ff,0x1ff,0x1ff,0x1ff};
    assert(capability_terrain(well.data(),4,10,0,{false,false,false,4})<
        capability_terrain(well.data(),4,10,0,{false,false,true,4}));
    int cases=0;
    // Compare the accelerated host entry and the complete general movement
    // graph, including distinct spin classes at the same occupied cells.
    for(int variant=0;variant<9;++variant)for(int family=0;family<7;++family) {
        UserConfig::Settings user;user.qpAllSpin=variant%2?2:1;
        Fixture f(user);f.context.config.garbage.hitMultiplier=0;
        auto& b=f.p.gameplay.playfield;b.field.clear();b.garbageHeight=0;
        if(variant==1)for(int y=12;y<20;++y)b.field.set_cell(y,0,Runtime::BoardCell::piece(1));
        if(variant==2)for(int x=0;x<10;++x)for(int y=20-x%4;y<20;++y)
            b.field.set_cell(y,x,Runtime::BoardCell::piece(1));
        if(variant==3) {
            b.activeHeight=12;b.solidGarbageHeight=8;
            for(int y=12;y<20;++y)for(int x=0;x<10;++x)b.field.set_cell(y,x,Runtime::BoardCell::solid_garbage());
            for(int x=1;x<9;++x)b.field.set_cell(11,x,Runtime::BoardCell::piece(1));
        }
        if(variant==4)f.context.config.handling.gravityInterval=Runtime::GravityInterval::instant();
        if(variant==5) {f.context.config.handling.das={};f.context.config.handling.arr={};}
        if(variant==6)f.context.config.movement.disableLeft=true;
        if(variant==7) {
            f.context.config.movement.disableClockwise=true;f.context.config.movement.disableCounterclockwise=true;
            f.context.config.movement.disable180=true;
        }
        if(variant==8) {
            f.context.config.legacyRotationRules=true;
            f.p.pieces.allowedPieces=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus,true);
        }
        const auto type=f.p.pieces.allowedPieces[family];const auto start=spawn(f,type);
        assert(Bot::init(f.context));check_route(f,{type},start,{false,Piece::Type::none()});
        // Native tree search keeps its strongest representative per geometry.
        // Test the rule-search accelerator itself, which must retain all
        // ordinary/Mini/full variants required by the repeat rules.
        auto geometry=Bot_chatgpt::enumerate_placements(f.env(),b.field,b.activeHeight,type,start,
            f.context.config.handling.gravityInterval);
        const auto accelerated=geometry?*geometry:Bot::QuickPlaySearch::search(
            Bot::QuickPlaySearch::Profile::ChatGPT,f.env(),f.p,b.field,{family},{},start,-2).candidates;
        const auto reference=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::Botris,
            f.env(),f.p,b.field,{family},{},start,-2);
        const auto keys=[&](const auto& candidates) {
            std::set<std::vector<int>> result;
            for(const auto& candidate:candidates) {
                const auto p=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,type,candidate.pose,candidate.rotation);
                std::vector<int> key{p.placement.spin.spin?(p.placement.spin.mini?1:2):0};
                auto cells=Piece::get_definition(type).cells(candidate.pose.rotation);
                for(auto& cell:cells){cell[0]+=candidate.pose.position.row;cell[1]+=candidate.pose.position.col;}
                std::sort(cells.begin(),cells.end());
                for(const auto& cell:cells){key.push_back(cell[0]);key.push_back(cell[1]);}
                result.insert(std::move(key));
            }
            return result;
        };
        const auto a=keys(accelerated),r=keys(reference.candidates);
        if(a!=r)std::cerr<<"Capability geometry mismatch variant="<<variant<<" family="<<family<<" fast="<<a.size()<<" reference="<<r.size()<<'\n';
        assert(!a.empty() && a==r);++cases;
    }
    // Equivalent capabilities must score identically regardless of whether
    // they came from a named modifier or direct runtime settings.
    for(int mechanism=0;mechanism<3;++mechanism) {
        UserConfig::Settings user;
        if(mechanism==0)user.qpNoHold=1;
        if(mechanism==1)user.qpGravity=2;
        if(mechanism==2)user.qpAllSpin=3;
        Fixture named(user),direct;
        direct.context.config=named.context.config;
        direct.context.config.quickPlay.noHold=Runtime::ModifierMode::Disabled;
        direct.context.config.quickPlay.gravity=Runtime::ModifierMode::Disabled;
        direct.context.config.quickPlay.allSpin=Runtime::AllSpinMode::Disabled;
        named.p.gameplay.playfield.field.clear();direct.p.gameplay.playfield.field.clear();
        const auto type=named.p.pieces.allowedPieces[5];
        auto a=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,named.env(),named.p,
            named.p.gameplay.playfield.field,{5},{},spawn(named,type),-2);
        auto b=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,direct.env(),direct.p,
            direct.p.gameplay.playfield.field,{5},{},spawn(direct,type),-2);
        assert(!a.candidates.empty() && a.candidates.size()==b.candidates.size());
        for(std::size_t i=0;i<a.candidates.size();++i) {
            assert(a.candidates[i].pose==b.candidates[i].pose);
            assert(std::abs(a.candidates[i].value-b.candidates[i].value)<1e-6);
        }
    }
    std::cerr<<"Capability geometry parity: "<<cases<<"; named/direct scoring parity: 3\n";
}

void all_pair_capabilities() {
    int UserConfig::Settings::* mods[]{&UserConfig::Settings::qpExpert,&UserConfig::Settings::qpNoHold,
        &UserConfig::Settings::qpMessy,&UserConfig::Settings::qpGravity,&UserConfig::Settings::qpVolatile,
        &UserConfig::Settings::qpDoubleHole,&UserConfig::Settings::qpInvisible,&UserConfig::Settings::qpAllSpin,
        &UserConfig::Settings::qpBackfire,&UserConfig::Settings::qpSnakeEyes,&UserConfig::Settings::qpSnowball,
        &UserConfig::Settings::qpFool};
    const auto levels=[](int family){return family==7?3:family==11?4:2;};
    int checked=0;
    for(int i=0;i<12;++i)for(int j=i+1;j<12;++j)for(int a=1;a<=levels(i);++a)for(int b=1;b<=levels(j);++b) {
        UserConfig::Settings user;user.*mods[i]=a;user.*mods[j]=b;Fixture f(user);
        std::vector<Piece::Type> next;
        for(auto type:f.p.pieces.allowedPieces) {
            if(Piece::get_definition(type).can_place(spawn(f,type),f.p.gameplay.playfield.field))next.push_back(type);
            if(next.size()==3)break;
        }
        assert(!next.empty());assert(Bot::init(f.context));
        check_route(f,next,spawn(f,next.front()),{f.env().holdEnabled,Piece::Type::none()});++checked;
    }
    assert(checked==332);std::cerr<<"ChatGPT two-family modifier capability paths: "<<checked<<'\n';
}

void productive_spin_objective() {
    using namespace Bot_chatgpt;
    // Rotating without removing any cells must not farm the completion score.
    const auto policy=capability_policy({},20,true,true);
    assert(placement_reward(policy,0,0,0,1,true,false,0)==
        placement_reward(policy,0,0,0,1,false,false,0));
    int offered=0,chosen=0;
    // A real, reachable uneven surface with several non-T Spin exits. Use
    // a singleton queue so the complete first-ply comparison is independent
    // of the machine's timed search depth. Include its mirrored terrain.
    constexpr unsigned slots[][6]{
        {1019,1019,993,1019,1019,1019}, {1017,1009,1017,1017,1017,1017},
        {1019,1009,1009,1017,1017,1017}, {1019,1017,1009,1017,1017,1017},
        {1021,1009,1017,1017,1017,1017}};
    for(int mode:{1,2,3})for(bool mirror:{false,true})for(int family:{0,1,2,4,6}) {
        UserConfig::Settings user;user.qpAllSpin=mode;Fixture f(user);
        const auto& rows=slots[family==0?0:family==1?1:family==2?2:family==4?3:4];
        auto& b=f.p.gameplay.playfield;b.field.clear();b.garbageHeight=std::size(rows);
        for(int y=0;y<int(std::size(rows));++y)for(int x=0;x<10;++x)if(rows[y]&(1u<<x))
            b.field.set_cell(19-y,mirror?9-x:x,Runtime::BoardCell::garbage());
        f.context.config.garbage.hitMultiplier=0;
        const auto type=f.p.pieces.allowedPieces[family];auto pose=spawn(f,type);Runtime::RotationRecord record;
        assert(Bot::init(f.context));
        const auto selected=Bot::getpos(f.context,f.p,b.field,{type},{},pose,{false,Piece::Type::none()},pose,record);
        assert(selected==type);
        bool available=false;
        for(const auto& candidate:last_candidates()) {
            const auto p=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,type,candidate.pose,candidate.rotation);
            available|=!p.dead && p.placement.spin.spin && p.placement.clear.clearedLines>0;
        }
        if(!available)continue;
        ++offered;
        const auto p=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,type,pose,record);
        chosen+=!p.dead && p.placement.spin.spin && p.placement.clear.clearedLines>0;
        assert(!p.dead && p.placement.spin.spin && p.placement.clear.clearedLines>0);
        Engine::MovementState start;start.pose=spawn(f,type);
        assert(Engine::search_autoplay_path(b.field,Piece::get_definition(type),start,pose,record,
            f.context.config.handling,f.context.config.movement,f.context.config.handling.gravityInterval,
            false,{},Engine::autoplay_spin_rules(f.context.config)));
        if(mode==2) {
            f.p.gameplay.quickPlay.lastOperation=Runtime::QuickPlayClearOperation{
                type,p.placement.clear.clearedLines,true,p.placement.spin.mini};
            auto answer=start.pose;Runtime::RotationRecord rotation;
            assert(Bot::getpos(f.context,f.p,b.field,{type},{},answer,{false,Piece::Type::none()},answer,rotation)==type);
            assert(!Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,type,answer,rotation).dead);
        }
    }
    assert(offered>0 && chosen==offered);
    // Instant gravity is a movement capability, shared by ordinary settings
    // and QP modifiers. It must not silently replace the native search.
    for(int mode:{0,1,2}) {
        UserConfig::Settings user;user.qpAllSpin=mode;user.qpGravity=2;Fixture f(user);
        f.context.config.autoplay.searchWidth=300;
        assert(Bot_chatgpt::availability(Bot::QuickPlaySearch::native_environment(f.env())));
        assert(Bot::init(f.context));
        check_route(f,f.p.pieces.allowedPieces,spawn(f,f.p.pieces.allowedPieces.front()),{true,Piece::Type::none()});
        assert(last_diagnostics().evaluated==0 && last_diagnostics().searchWidth==300);
    }
    for(int mode:{1,2}) {
        UserConfig::Settings user;user.qpAllSpin=mode;Fixture f(user);
        auto& b=f.p.gameplay.playfield;b.field.clear();b.activeHeight=1;b.solidGarbageHeight=19;
        for(int y=1;y<20;++y)for(int x=0;x<10;++x)b.field.set_cell(y,x,Runtime::BoardCell::solid_garbage());
        const auto type=f.p.pieces.allowedPieces[3];const auto piece=Piece::get_definition(type);
        const auto start=spawn(f,type);auto target=start;
        while(piece.can_place(target.translated(1,0),b.field))++target.position.row;
        unsigned holes=0;for(const auto& cell:piece.cells(target.rotation))if(target.position.row+cell[0]==0)
            holes|=1u<<(target.position.col+cell[1]);
        assert(holes);
        for(int x=0;x<10;++x)if(!(holes&(1u<<x)))b.field.set_cell(0,x,Runtime::BoardCell::garbage());
        f.p.gameplay.quickPlay.lastOperation=Runtime::QuickPlayClearOperation{type,1,false,false};
        assert(Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,type,target,{}).dead);
        assert(!Bot::QuickPlaySearch::native_answer_valid(f.env(),f.p,b.field,{3},start,-2,3,target,{}));
    }
    int referenceCases=0;
    for(int mode:{0,1,2})for(int height:{17,20})for(int top:{-100,-99,-30,-1,0,10}) {
        UserConfig::Settings user;user.qpAllSpin=mode;Fixture f(user);
        f.context.config.field.height=height;f.context.config.garbage.hitMultiplier=0;
        auto& b=f.p.gameplay.playfield;b.visibleHeight=b.activeHeight=height;b.field.initialize(10,height);
        const auto target=quad(f);
        b.field.set_cell(top,0,Runtime::BoardCell::piece(2));
        const auto p=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,target.type,target.pose,{});
        auto piece=Piece::get_definition(target.type);Engine::MovementState movement;movement.pose=target.pose;
        Engine::finalize_locked_piece(f.context,b.field,piece,f.p,movement,0,[]{return Rules::PlayResult::ContinueFrame;});
        assert(p.placement.clear.clearedLines==4 && p.activeHeight==b.activeHeight);
        for(int y=-Runtime::kHiddenRowCapacity;y<height;++y)for(int x=0;x<10;++x)
            assert(p.field->cell(y,x)==b.field.cell(y,x));
        ++referenceCases;
    }
    // Default and larger user widths must survive the QP dispatch unchanged;
    // a modifier must not impose the former 17/48-candidate ceiling.
    for(int width:{300,600}) {
        UserConfig::Settings user;user.qpAllSpin=1;Fixture f(user);
        f.context.config.autoplay.searchWidth=width;f.context.config.garbage.hitMultiplier=0;
        const auto next=f.p.pieces.allowedPieces;
        assert(Bot::init(f.context));
        std::vector<int> queue;for(auto type:next)queue.push_back(Piece::metadata(type).familyIndex);
        const auto d=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,f.env(),f.p,
            f.p.gameplay.playfield.field,queue,{},spawn(f,next.front()),-1);
        assert(d.searchWidth==width && d.peakFrontier>48 && d.peakFrontier<=width);
        std::cerr<<"Spin search width="<<d.searchWidth<<" peak_frontier="<<d.peakFrontier
            <<" depth="<<d.depth<<" evaluated="<<d.evaluated<<'\n';
    }
    std::cerr<<"Productive non-T Spin choices: "<<chosen<<'/'<<offered<<"; reverse repeats rejected; "
        <<referenceCases<<" hidden-row/short-field settlement references\n";
}

void attack_trajectories(int target,unsigned seed,double hit,int searchWidth,bool freePlay=false,int onlyBot=0,
    UserConfig::Settings user={}) {
    for(auto bot:bots) {
        if(onlyBot && int(bot)!=onlyBot)continue;
        Fixture f(user,bot);
        f.context.config.autoplay.searchWidth=searchWidth;
        f.context.config.garbage.hitMultiplier=hit;
        f.context.config.simulationPps=2.5;
        if(freePlay) {
            f.context.config.mode.mode=Runtime::GameMode::Zen;
            f.context.config.garbage.mode=Runtime::GarbageMode::None;
        }
        f.p.pieces.random.seed(seed);Engine::initialize_board(f.context,f.p);
        Engine::initialize_quick_play(f.context,f.p);
        Engine::initialize_board_timers(f.context,f.p);
        assert(Bot::init(f.context));
        int survived=0,multiple=0,quads=0,spins=0,damagingSingles=0,depth=0;double milliseconds=0;
        for(int turn=0;turn<target;++turn) {
            auto& b=f.p.gameplay.playfield;
            while(f.p.pieces.bag.size()<=static_cast<std::size_t>(b.nextCount+1))Engine::add_bag(f.context,f.p);
            auto piece=Piece::get_definition(Engine::current_piece(f.p));
            Engine::MovementState movement;movement.pose=Piece::spawn_pose(b.width,piece.type());
            if(Engine::handle_spawn_collision(f.context,b.field,piece,f.p,movement))break;
            const auto gravity=Runtime::current_gravity_interval(f.context);
            if(gravity.is_instant())Engine::apply_presentation_instant_gravity(movement,
                [&](Piece::Pose p){return piece.can_place(p,b.field);});
            std::vector<Piece::Type> next;
            for(int i=0;i<=b.nextCount;++i)next.push_back(f.p.pieces.bag[i].type);
            Piece::Pose answer=movement.pose;Runtime::RotationRecord record;
            const auto current=piece.type();
            const auto began=std::chrono::steady_clock::now();
            const auto selected=Bot::getpos(f.context,f.p,b.field,next,
                Engine::unseen_pieces_in_preview_endpoint_bag(f.context,f.p),movement.pose,
                {f.context.config.field.holdEnabled,f.p.gameplay.hold.piece},answer,record);
            milliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
            if(bot==AutoplayMode::ChatGPT)depth+=Bot_chatgpt::last_diagnostics().projectedDepth;
            assert(selected.valid());
            if(selected!=current) {
                const auto old=f.p.gameplay.hold.piece;f.p.gameplay.hold.piece=current;
                Engine::consume_current_piece(f.p);if(old.valid())Engine::prepend_piece(f.p,old);
                piece=Piece::get_definition(selected);movement.pose=Piece::spawn_pose(b.width,selected);
                assert(!Engine::handle_spawn_collision(f.context,b.field,piece,f.p,movement));
                if(gravity.is_instant())Engine::apply_presentation_instant_gravity(movement,
                    [&](Piece::Pose p){return piece.can_place(p,b.field);});
            }
            const auto path=Engine::search_autoplay_path(b.field,piece,movement,answer,record,
                f.context.config.handling,f.context.config.movement,gravity,false,{},Engine::autoplay_spin_rules(f.context.config));
            assert(path);movement=path->finalMovement;
            const auto resolved=Bot::QuickPlaySearch::forecast_placement(f.env(),f.p,b.field,piece.type(),movement.pose,movement.lastRotation);
            multiple+=resolved.placement.clear.clearedLines>=2;quads+=resolved.placement.clear.clearedLines>=4;
            spins+=resolved.placement.spin.spin && resolved.placement.clear.clearedLines>0;
            const int attackBefore=f.p.gameplay.scoring.totalAttack;
            const auto outcome=Engine::finalize_locked_piece(f.context,b.field,piece,f.p,movement,0,
                [&]{return Engine::run_frame(f.context,b.field,f.p);});
            damagingSingles+=resolved.placement.clear.clearedLines==1 && f.p.gameplay.scoring.totalAttack>attackBefore;
            Engine::consume_current_piece(f.p);++survived;
            if(outcome==Rules::PlayResult::Dead)break;
            assert(outcome==Rules::PlayResult::Locked);
        }
        const auto& s=f.p.gameplay.scoring;
        std::cerr<<(freePlay?"Free-play":"QP")<<" attack trajectory bot="<<int(bot)
            <<" AS="<<user.qpAllSpin<<" BF="<<user.qpBackfire<<" SE="<<user.qpSnakeEyes
            <<" seed="<<seed<<" hit="<<hit<<" width="<<searchWidth<<" survived="<<survived<<" lines="<<s.totalLines
            <<" attack="<<s.totalAttack<<" outgoing="<<s.totalOutgoingAttack<<" clears="<<s.clearMoves
            <<" zero_attack_clears="<<s.zeroAttackClears<<" difficult="<<s.difficultClears<<" max_attack="<<s.maxAttack
            <<" multiple="<<multiple<<" quads="<<quads<<" spins="<<spins<<" mean_ms="<<milliseconds/std::max(1,survived)
            <<" damaging_singles="<<damagingSingles
            <<" mean_depth="<<double(depth)/std::max(1,survived)<<'\n';
        // A legal, surviving all-single trajectory is the reported regression.
        // Check real attack strength and multi-line clears, not only paths.
        assert(survived==target);
        // The original Botris can sustain B2B with spin singles on some bags.
        // Those are attacks, unlike the reported non-spin single regression.
        const bool bursts=s.maxAttack>=4 && s.attacksAtLeast4>0;
        const bool sustainedSpins=s.maxAttack>=3 && spins>=target/10 &&
            s.b2bExtensions>=target/10 && s.totalAttack>=target/3;
        // BF may prefer smaller repeated spin attacks to limit self-garbage.
        // SE explicitly removes attacks from non-singles, so quads cannot be
        // its strength requirement. Both must still produce real attacks.
        const bool backfireSpins=user.qpBackfire && s.maxAttack>=2 && spins>=target/20 &&
            s.difficultClears>=target/20 && s.totalAttack>=target/5;
        // A valid Snake Eyes single may send exactly one line. Require
        // sustained positive singles and their real total attack, rather
        // than an incidental combo burst from a time-limited trajectory.
        const bool snakeEyesSingles=user.qpSnakeEyes && s.maxAttack>=1 &&
            damagingSingles>=std::max(1,target/10) && s.totalAttack>=damagingSingles;
        assert(bursts || sustainedSpins || backfireSpins || snakeEyesSingles);
        assert((multiple>=std::max(2,target/50) || sustainedSpins || snakeEyesSingles) && quads+spins>0);
        // The lower total-attack threshold belongs to the actual single-only
        // rule, including trajectories admitted by the burst alternative.
        // It must not depend on whether the single-count witness also passed.
        const bool singleOnly=Runtime::enabled(f.context.config.quickPlay.snakeEyes);
        assert(s.totalAttack>=(singleOnly?target/10:target/5) && (freePlay || s.totalOutgoingAttack>0));
    }
}
}
int run(int argc,char** argv) {
    Render::begin_output_suppression();
    if(argc>1 && std::string(argv[1])=="--spin-objective") {
        productive_spin_objective();Render::end_output_suppression();return 0;
    }
    if(argc>1 && std::string(argv[1])=="--capability-regression") {
        capability_scoring_and_geometry();Render::end_output_suppression();return 0;
    }
    if(argc>1 && std::string(argv[1])=="--capability-pairs") {
        all_pair_capabilities();Render::end_output_suppression();return 0;
    }
    hold_validates_from_grounded_pose();
    mixed_geometries_keep_distinct_piece_ids();
    if(argc>1 && std::string(argv[1])=="--geometry-regression") {
        Render::end_output_suppression();return 0;
    }
    if(argc>1 && std::string(argv[1])=="--live-trajectories") {
        continuous_games_use_live_placement_and_frame_rules();
        Render::end_output_suppression();return 0;
    }
    if(argc>1 && std::string(argv[1])=="--policy-regression") {
        non_structural_mods_preserve_native_policy();
        chatgpt_rule_search_preserves_native_candidate_valuation();
        inactive_floor_does_not_change_terrain_policy();
        Render::end_output_suppression();return 0;
    }
    if(argc>1 && std::string(argv[1])=="--mod-benchmark") {
        for(int mod=0;mod<3;++mod) {
            UserConfig::Settings user;
            if(mod==0)user.qpAllSpin=3;
            if(mod==1)user.qpBackfire=1;
            if(mod==2)user.qpSnakeEyes=1;
            attack_trajectories(argc>2?std::stoi(argv[2]):100,123,0,300,false,
                argc>3?std::stoi(argv[3]):0,user);
        }
        Render::end_output_suppression();return 0;
    }
    if(argc>1 && (std::string(argv[1])=="--attack-benchmark" || std::string(argv[1])=="--free-benchmark")) {
        attack_trajectories(argc>2?std::stoi(argv[2]):100,argc>3?std::stoul(argv[3]):123,
            argc>4?std::stod(argv[4]):0,argc>5?std::stoi(argv[5]):300,std::string(argv[1])=="--free-benchmark",
            argc>6?std::stoi(argv[6]):0);
        Render::end_output_suppression();return 0;
    }
    all_mods_and_combinations_have_legal_paths();
    special_pieces_growth_zero_das_and_gravity();
    projections_match_real_mod_settlement();
    search_uses_known_events_and_keeps_state_isolated();
    cancellation_removes_armed_rows_before_pending_rows();
    reverse_allspin_death_cannot_be_bought_with_attack_reward();
    windup_fatigue_and_piece_time_match_the_host();
    continuous_games_use_live_placement_and_frame_rules();
    native_baseline_preserves_search_and_fatigue_floor();
    non_structural_mods_preserve_native_policy();
    chatgpt_rule_search_preserves_native_candidate_valuation();
    inactive_floor_does_not_change_terrain_policy();
    default_search_budget_has_legal_results();
    Render::end_output_suppression();
    return 0;
}
} // namespace TetrTests::bot_quick_play_mods_test
