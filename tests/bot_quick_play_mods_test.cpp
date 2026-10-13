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
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>

namespace {
using Runtime::AutoplayMode;
using Runtime::Duration;
using Runtime::ModifierMode;
using Runtime::AllSpinMode;
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
    for(auto bot:bots)for(const auto& u:variants()) {
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
    for(auto bot:bots)for(int fool:{1,2,3,4}) {
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
    for(auto bot:bots)for(int sb:{1,2})for(int size:{4,5,10,20})for(bool instant:{false,true}) {
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
    for(auto bot:bots)for(const auto& user:variants()) {
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
        assert(view && Bot::QuickPlaySearch::plain(env));
        assert(!Bot::QuickPlaySearch::enabled(view->environment));
        assert(view->environment.autoplaySearchWidth==env.autoplaySearchWidth);
        assert(view->environment.pieceTypes==env.pieceTypes && view->rowOffset==solid);
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
        assert(!Bot::QuickPlaySearch::native_baseline(profile,f.env(),f.p,b.field));
    }
    std::cerr<<"QP native policy/pressure/fatigue-floor bridge cases: 9\n";
}

void attack_trajectories(int target,unsigned seed,double hit,int searchWidth,bool freePlay=false,int onlyBot=0) {
    for(auto bot:bots) {
        if(onlyBot && int(bot)!=onlyBot)continue;
        Fixture f({},bot);
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
        int survived=0,multiple=0,quads=0,spins=0,depth=0;double milliseconds=0;
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
                {true,f.p.gameplay.hold.piece},answer,record);
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
            const auto outcome=Engine::finalize_locked_piece(f.context,b.field,piece,f.p,movement,0,
                [&]{return Engine::run_frame(f.context,b.field,f.p);});
            Engine::consume_current_piece(f.p);++survived;
            if(outcome==Rules::PlayResult::Dead)break;
            assert(outcome==Rules::PlayResult::Locked);
        }
        const auto& s=f.p.gameplay.scoring;
        std::cerr<<(freePlay?"Free-play":"QP")<<" attack trajectory bot="<<int(bot)<<" seed="<<seed<<" hit="<<hit<<" width="<<searchWidth<<" survived="<<survived<<" lines="<<s.totalLines
            <<" attack="<<s.totalAttack<<" outgoing="<<s.totalOutgoingAttack<<" clears="<<s.clearMoves
            <<" zero_attack_clears="<<s.zeroAttackClears<<" difficult="<<s.difficultClears<<" max_attack="<<s.maxAttack
            <<" multiple="<<multiple<<" quads="<<quads<<" spins="<<spins<<" mean_ms="<<milliseconds/std::max(1,survived)
            <<" mean_depth="<<double(depth)/std::max(1,survived)<<'\n';
        // A legal, surviving all-single trajectory is the reported regression.
        // Check real attack strength and multi-line clears, not only paths.
        assert(survived==target);
        // The original Botris can sustain B2B with spin singles on some bags.
        // Those are attacks, unlike the reported non-spin single regression.
        const bool bursts=s.maxAttack>=4 && s.attacksAtLeast4>0;
        const bool sustainedSpins=s.maxAttack>=3 && spins>=target/10 &&
            s.b2bExtensions>=target/10 && s.totalAttack>=target/3;
        assert(bursts || sustainedSpins);
        assert((multiple>=std::max(2,target/50) || sustainedSpins) && quads+spins>0);
        assert(s.totalAttack>=target/5 && (freePlay || s.totalOutgoingAttack>0));
    }
}
}
int main(int argc,char** argv) {
    Render::begin_output_suppression();
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
    default_search_budget_has_legal_results();
    Render::end_output_suppression();
}
