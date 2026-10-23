#include "tetr/autoplay/strategy.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/input/services.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace TetrTests::autoplay_parameters_test {
namespace {
using D=Runtime::Duration;using T=Runtime::TimePoint;
D seconds(double value) {return D::from_seconds(value);}
struct Profile {std::string name;Runtime::HandlingConfig handling;};
std::vector<Profile> profiles() {
    Runtime::HandlingConfig base;base.das=seconds(1./6);base.arr=seconds(1./30);base.softDropFactor=20;
    std::vector<Profile> result{{"current",base}};
    const auto add=[&](std::string name,auto change) {auto h=base;change(h);result.push_back({std::move(name),h});};
    for(const auto [das,arr]:{std::pair<double,double>{0,0},{0,.01},{.1,0},{.1,.01},{.25,.06}})
        add("das="+std::to_string(das)+",arr="+std::to_string(arr),[&](auto& h){h.das=seconds(das);h.arr=seconds(arr);});
    add("no_gravity",[](auto& h){h.gravityInterval=Runtime::GravityInterval::disabled();});
    add("fast_gravity",[](auto& h){h.gravityInterval=Runtime::GravityInterval::periodic(seconds(.03));});
    add("20G",[](auto& h){h.gravityInterval=Runtime::GravityInterval::instant();});
    add("DCD_40ms",[](auto& h){h.dcd=seconds(.04);});
    add("zero_DAS_DCD",[](auto& h){h.das={};h.arr=seconds(.02);h.dcd=seconds(.08);});
    for(double sdf:{0.,1.,40.}) add("SDF="+std::to_string(sdf),[&](auto& h){h.softDropFactor=sdf;});
    for(double sdf:{.02,.06}) add("fixed_SDF="+std::to_string(sdf),[&](auto& h){h.softDropFixed=true;h.softDropFactor=sdf;});
    for(int limit:{0,1}) add("lock_limit="+std::to_string(limit),[&](auto& h){h.lockResetLimit=limit;});
    add("infinite_resets",[](auto& h){h.infiniteMovement=true;});
    add("20G_no_resets",[](auto& h){h.gravityInterval=Runtime::GravityInterval::instant();h.lockResetLimit=0;});
    add("short_lock",[](auto& h){h.epld=seconds(.05);});
    for(const auto [move,charge]:{std::pair<bool,bool>{false,true},{true,false},{false,false}})
        add("pre_move="+std::to_string(move)+",precharge="+std::to_string(charge),[&](auto& h){h.preMove=move;h.dasPrecharge=charge;});
    return result;
}
Runtime::Playfield board(int kind) {
    Runtime::Playfield field;field.initialize(10,20);
    if(kind==1) for(int col=0;col<10;++col) for(int row=20-(col*3+2)%7;row<20;++row)
        field.set_cell(row,col,Runtime::BoardCell::garbage());
    if(kind==2) for(int row:{18,19}) for(int col=2;col<10;++col)
        field.set_cell(row,col,Runtime::BoardCell::garbage());
    return field;
}
void path_replay_matrix(const std::vector<Profile>& settings) {
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);int searches=0,routes=0;
    for(const auto& profile:settings) for(int kind=0;kind<3;++kind) for(auto type:types) {
        auto field=board(kind);const auto piece=Piece::get_definition(type);const auto& h=profile.handling;
        auto origin=Engine::make_spawn_movement_state(10,type);
        if(h.gravityInterval.is_instant()) Engine::apply_presentation_instant_gravity(origin,[&](auto pose){return piece.can_place(pose,field);});
        int minCol=0,maxCol=0;for(auto cell:piece.cells(origin.pose.rotation)) {minCol=std::min(minCol,cell[1]);maxCol=std::max(maxCol,cell[1]);}
        for(int col:{-minCol,4,9-maxCol}) {
            auto target=Piece::spawn_pose(10,type);target.position.col=col;
            while(piece.can_place(target.translated(1,0),field)) ++target.position.row;
            ++searches;auto path=Engine::search_autoplay_path(field,piece,origin,target,{},h,{},h.gravityInterval,true);
            if(!path) continue;
            Runtime::Duration elapsed;auto state=origin;
            for(const auto& step:path->steps) {
                assert(piece.can_place(step.movement.pose,field));assert(step.delay>=Runtime::Duration{});
                if(h.gravityInterval.is_instant()) assert(!piece.can_place(step.movement.pose.translated(1,0),field));
                state=step.movement;elapsed+=step.delay;
            }
            assert(Engine::autoplay_landing_matches(field,piece,state,target,{}));
            assert(std::abs((elapsed-path->duration).seconds())<1e-9);++routes;
        }
    }
    assert(routes>=504);std::cerr<<"presentation paths: searches="<<searches<<" routes="<<routes<<'\n';
}
struct Selection {Piece::Type type;Piece::Pose pose;Runtime::RotationRecord rotation;std::atomic<int> calls{0};};
struct NativeStrategy:Autoplay::Strategy {
    Legacy::BotStrategy bot;std::shared_ptr<Selection> first=std::make_shared<Selection>();
    std::unique_ptr<Autoplay::Strategy> clone_for_search() const override {
        auto clone=std::make_unique<NativeStrategy>();clone->first=first;return clone;
    }
    bool initialize(Runtime::GameContext& context) override {return bot.initialize(context);}
    Piece::Type choose_position(Runtime::GameContext& context,Runtime::PlayerSessionState& player,Runtime::Playfield field,
        const std::vector<Piece::Type>& next,const std::vector<Piece::Type>& unseen,Piece::Pose start,Autoplay::HoldInput hold,
        Piece::Pose& answer,Runtime::RotationRecord& rotation) override {
        const auto type=bot.choose_position(context,player,std::move(field),next,unseen,start,hold,answer,rotation);
        if(first->calls.fetch_add(1)==0) {first->type=type;first->pose=answer;first->rotation=rotation;}
        return type;
    }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,Piece::Pose,Piece::Pose,Runtime::RotationRecord) override {return {};}
};
void native_perfect_clear(const Profile& profile,int family,bool held) {
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    Input::Services input;Replay::ReplaySessionState recording;NativeStrategy strategy;
    context.attach_input(input);context.attach_replay(recording);context.attach_autoplay(strategy);
    context.config.autoplay.mode=Runtime::AutoplayMode::ChatGPT;context.config.autoplay.handlingSimulation=true;
    context.config.autoplay.pps=0;context.config.autoplay.searchWidth=300;context.config.field.holdEnabled=held;
    context.config.handling=profile.handling;
    auto& player=session->player;auto& live=player.gameplay.playfield;
    live.width=10;live.visibleHeight=live.activeHeight=20;live.field.initialize(10,20);
    const int top=family==3 ? 18 : 16;
    for(int row=top;row<20;++row) for(int col=0;col<10;++col)
        if(!(family==3 ? col<2 : col==9)) live.field.set_cell(row,col,Runtime::BoardCell::garbage());
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    auto piece=Piece::get_definition(types[held ? 5 : family]);Engine::prepend_piece(player,piece.type());
    player.pieces.nextPreview={types[2],types[6],types[3],types[0],types[4]};
    if(held) player.gameplay.hold.piece=types[family];
    assert(strategy.initialize(context));auto movement=Engine::make_spawn_movement_state(10,piece.type());
    movement.lockTimer=context.real_now();Engine::MovementInputState keys;Engine::AutoplayCarryState carry;
    const auto pump=[]{return Rules::PlayResult::ContinueFrame;};
    auto result=Engine::run_handling_autoplay_step(context,live.field,piece,player,movement,keys,pump,&carry);
    if(result.signal==Rules::PlayResult::Hold) {
        assert(held && carry.selectedPieceType==types[family]);
        const auto incoming=player.gameplay.hold.piece;
        player.gameplay.hold.piece=piece.type();player.gameplay.hold.used=1;
        Engine::consume_current_piece(player);Engine::prepend_piece(player,incoming);piece=Piece::get_definition(incoming);
        movement=Engine::make_spawn_movement_state(10,piece.type());movement.lockTimer=context.real_now();
        keys.preparePiece(input.bindings,[](int){return false;},movement.lockTimer);
        Engine::apply_initial_movement(context,keys,movement,[&](auto pose){return piece.can_place(pose,live.field);});
        result=Engine::run_handling_autoplay_step(context,live.field,piece,player,movement,keys,pump,&carry,true);
    }
    if(!result.hardDrop || strategy.first->calls.load()!=1 || piece.type()!=types[family] ||
        !Engine::autoplay_landing_matches(live.field,piece,movement,strategy.first->pose,strategy.first->rotation,
            Engine::autoplay_spin_rules(context.config))) {
        std::cerr<<"native target failure: "<<profile.name<<" family="<<family<<" held="<<held<<" calls="<<strategy.first->calls.load()<<'\n';
        std::abort();
    }
    for(const auto& cell:piece.cells(movement.pose.rotation)) {
        const int row=movement.pose.position.row+cell[0],col=movement.pose.position.col+cell[1];
        if(!(row>=top && row<20 && (family==3 ? col<2 && col>=0 : col==9))) {
            std::cerr<<"perfect clear missed: "<<profile.name<<" family="<<family<<" held="<<held<<'\n';std::abort();
        }
    }
    assert(!carry.selectedPieceType.valid());
}
void physical_keys_do_not_steer_bot_birth() {
    Runtime::SessionState session;Runtime::RuntimeServices services;Runtime::GameContext context{session,services};Input::Services input;context.attach_input(input);
    context.config.autoplay.mode=Runtime::AutoplayMode::ChatGPT;context.config.autoplay.handlingSimulation=true;
    Engine::MovementInputState keys;auto movement=Engine::make_spawn_movement_state(10,Piece::Type{3});const auto before=movement.pose;
    assert(!Engine::apply_initial_movement(context,keys,movement,[](auto){return true;}));assert(movement.pose==before);
}
}
int run(int argc,char** argv) {
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());const auto settings=profiles();
    path_replay_matrix(settings);physical_keys_do_not_steer_bot_birth();int nativeCases=0;
    for(const auto& profile:settings) {
        for(int family:{3,0}) {native_perfect_clear(profile,family,false);++nativeCases;}
        native_perfect_clear(profile,0,true);++nativeCases;
    }
    std::cout.rdbuf(previous);std::cerr<<"autoplay parameters: profiles="<<settings.size()<<" native_cases="<<nativeCases<<" OK\n";
    return 0;
}
} // namespace TetrTests::autoplay_parameters_test
