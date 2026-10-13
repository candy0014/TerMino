#include "tetr/engine/autoplay_path.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/app/place.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/locked_piece_resolution.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/movement_input_state.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/legacy/bot_adapter.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/input/services.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/runtime/game_context.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <sstream>
#include <thread>
#include <filesystem>
namespace {
using namespace Engine;
using D=Runtime::Duration;
D dt(double t){return D::from_seconds(t);}
Runtime::Playfield empty(){Runtime::Playfield f;f.initialize(10,20);return f;}
struct Choice {Piece::Type type;Piece::Pose pose,start;Runtime::RotationRecord rotation;std::atomic<int> calls{0};std::atomic<bool> finished{false};};
struct FixedStrategy:Autoplay::Strategy {
    Piece::Pose target;Runtime::RotationRecord rotation;Piece::Type type=Piece::Type::none();double delay=0;
    std::string script;
    std::shared_ptr<Choice> choice=std::make_shared<Choice>();
    std::unique_ptr<Autoplay::Strategy> clone_for_search() const override {return std::make_unique<FixedStrategy>(*this);}
    bool initialize(Runtime::GameContext&) override{return true;}
    Piece::Type choose_position(Runtime::GameContext&,Runtime::PlayerSessionState& player,Runtime::Playfield,
        const std::vector<Piece::Type>& next,const std::vector<Piece::Type>&,Piece::Pose start,Autoplay::HoldInput,
        Piece::Pose& answer,Runtime::RotationRecord& record) override {
        ++choice->calls;choice->start=start;
        if(delay) std::this_thread::sleep_for(std::chrono::duration<double>(delay));
        player.gameplay.scoring.score=999; // Worker must only own a snapshot.
        answer=target;record=rotation;choice->type=type.valid()?type:next.front();choice->pose=answer;choice->rotation=record;
        choice->finished=true;return choice->type;
    }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,Piece::Pose,Piece::Pose,Runtime::RotationRecord) override{return script;}
};
struct RecordingChatGPT:Autoplay::Strategy {
    Legacy::BotStrategy bot;std::shared_ptr<Choice> choice=std::make_shared<Choice>();
    std::unique_ptr<Autoplay::Strategy> clone_for_search() const override {
        auto clone=std::make_unique<RecordingChatGPT>();clone->choice=choice;return clone;
    }
    bool initialize(Runtime::GameContext& c) override{return bot.initialize(c);}
    Piece::Type choose_position(Runtime::GameContext& c,Runtime::PlayerSessionState& p,Runtime::Playfield f,
        const std::vector<Piece::Type>& next,const std::vector<Piece::Type>& unseen,Piece::Pose start,Autoplay::HoldInput hold,
        Piece::Pose& answer,Runtime::RotationRecord& record) override {
        auto type=bot.choose_position(c,p,std::move(f),next,unseen,start,hold,answer,record);
        if(choice->calls.fetch_add(1)==0) {choice->type=type;choice->pose=answer;choice->rotation=record;choice->start=start;}
        choice->finished=true;return type;
    }
    std::string movement_script(Runtime::GameContext& c,Runtime::Playfield f,Piece::Definition piece,Piece::Pose start,Piece::Pose target,Runtime::RotationRecord rotation) override {
        return bot.movement_script(c,std::move(f),std::move(piece),start,target,rotation);
    }
};
struct Fixture {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    Input::Services input;Replay::ReplaySessionState recording;MovementInputState keys;AutoplayCarryState carry;
    Runtime::Playfield field=empty();
    Fixture(Autoplay::Strategy& bot) {
        context.attach_input(input);context.attach_replay(recording);context.attach_autoplay(bot);
        input.keyStateProvider=[](int){return false;};
        context.config.autoplay.mode=Runtime::AutoplayMode::ChatGPT;context.config.autoplay.handlingSimulation=true;
        context.config.autoplay.pps=0;context.config.autoplay.searchWidth=300;
        context.config.handling.das=dt(1./6);context.config.handling.arr=dt(1./30);context.config.handling.softDropFactor=20;
        session->gameStartTime=context.now();
    }
    auto step(Piece::Definition& piece,MovementState& movement,bool continuation=false,
        const std::function<Rules::PlayResult()>& pump=[]{return Rules::PlayResult::ContinueFrame;}) {
        return run_handling_autoplay_step(context,field,piece,session->player,movement,keys,pump,&carry,continuation);
    }
};
void thinking_freezes_piece_and_lock() {
    for(bool instant:{false,true}) {
        FixedStrategy bot;bot.delay=.18;Fixture f(bot);
        auto piece=Piece::get_definition(Piece::Type{3});prepend_piece(f.session->player,piece.type());
        f.context.config.handling.gravityInterval=instant ? Runtime::GravityInterval::instant() : Runtime::GravityInterval::periodic(dt(.003));
        f.context.config.handling.epld=dt(.001);f.context.config.handling.lockResetLimit=0;
        auto movement=make_spawn_movement_state(10,piece.type());auto floor=movement;
        apply_presentation_instant_gravity(floor,[&](auto pose){return piece.can_place(pose,f.field);});
        bot.target=floor.pose;bot.target.position.col=7;
        const auto birth=instant ? floor.pose : movement.pose;
        int polls=0;const auto began=f.context.real_now();
        const auto result=f.step(piece,movement,false,[&] {
            if(!bot.choice->finished) {assert(movement.pose==birth);++polls;}
            f.session->player.timing.framePoll=f.context.real_now();return Rules::PlayResult::ContinueFrame;
        });
        assert(result.hardDrop && result.locked && bot.choice->calls==1 && polls>=2);
        assert(bot.choice->start==birth && movement.pose==bot.target);
        assert((f.context.real_now()-began).seconds()>=.18);
        assert(f.session->player.gameplay.scoring.score<999);
    }
}
void display_rates_and_instant_hard_drop() {
    Runtime::HandlingConfig h;h.das=dt(1./6);h.arr=dt(1./30);h.softDropFactor=20;
    auto field=empty();auto piece=Piece::get_definition(Piece::Type{3});auto start=make_spawn_movement_state(10,piece.type());
    auto horizontal=make_autoplay_path(field,piece,start,h,{},h.gravityInterval,"RRRRH");assert(horizontal);
    assert(horizontal->steps[0].delay==D{} && horizontal->steps[1].delay==h.das);
    assert(horizontal->steps[2].delay==h.arr && horizontal->steps[3].delay==h.arr);
    assert(horizontal->steps.back().delay==D{});
    for(double interval:{.02,.03,.06}) {
        h.softDropFixed=interval!=.03;h.softDropFactor=interval==.03 ? 20 : interval;
        auto soft=make_autoplay_path(field,piece,start,h,{},h.gravityInterval,"sXH");assert(soft);
        for(const auto& step:soft->steps) if(step.command==MovementScriptCommand::SoftDropStep) assert(step.delay==dt(interval));
        auto hard=make_autoplay_path(field,piece,start,h,{},h.gravityInterval,"H");assert(hard);
        assert(hard->duration==D{} && hard->steps.size()==1 && hard->finalMovement.pose.position.row==19);
    }
    FixedStrategy bot;Fixture f(bot);prepend_piece(f.session->player,piece.type());bot.target={{19,8},{}};
    auto movement=start;assert(f.step(piece,movement).hardDrop);
    auto events=Replay::decode_operation_stream(f.recording.data,0,1);std::vector<double> times;
    while(!events.empty()) {
        const auto op=events.front();events.pop();
        if(op.kind==Replay::OperationKind::Move && op.payload_as<Replay::PosePayload>().pose.position.row==-2)
            times.push_back(op.timestamp.seconds());
    }
    assert(times.size()==4 && times[1]-times[0]>=h.das.seconds()-.004);
    assert(times[2]-times[1]>=h.arr.seconds()-.004 && times[3]-times[2]>=h.arr.seconds()-.004);
}
void hold_keeps_first_target() {
    FixedStrategy bot;Fixture f(bot);auto current=Piece::get_definition(Piece::Type{5});auto held=Piece::get_definition(Piece::Type{3});
    prepend_piece(f.session->player,current.type());f.session->player.pieces.nextPreview={held.type()};
    bot.type=held.type();bot.target={{19,0},{}};auto movement=make_spawn_movement_state(10,current.type());
    assert(f.step(current,movement).signal==Rules::PlayResult::Hold);
    const auto expected=bot.target;bot.target.position.col=8;
    prepend_piece(f.session->player,held.type());f.session->player.gameplay.hold.used=1;
    movement=make_spawn_movement_state(10,held.type());assert(f.step(held,movement,true).hardDrop);
    assert(movement.pose==expected && bot.choice->calls==1 && !f.carry.selectedPieceType.valid());
}
void cancelled_work_cannot_move_the_next_piece() {
    FixedStrategy bot;bot.delay=.15;bot.target={{19,0},{}};Fixture f(bot);
    auto piece=Piece::get_definition(Piece::Type{3});prepend_piece(f.session->player,piece.type());
    auto movement=make_spawn_movement_state(10,piece.type());const auto birth=movement.pose;
    const auto result=f.step(piece,movement,false,[]{return Rules::PlayResult::Retry;});
    assert(result.signal==Rules::PlayResult::Retry && movement.pose==birth);
    f.session->player.gameplay.scoring.score=123;
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    assert(f.session->player.gameplay.scoring.score==123 && movement.pose==birth);
}
void bug_replay_tuck_uses_unified_search() {
    auto field=empty();const int rows[]{999,621,513,769,512};
    for(int row=0;row<5;++row) for(int col=0;col<10;++col) if(rows[row]&(1<<col))
        field.set_cell(19-row,col,Runtime::BoardCell::garbage());
    auto piece=Piece::get_definition(Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[1]);
    const auto start=make_spawn_movement_state(10,piece.type());const Piece::Pose target{{18,4},Runtime::Rotation::from_index(3)};
    const auto rotation=Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(0));
    Runtime::HandlingConfig h;h.softDropFactor=20;
    auto path=search_autoplay_path(field,piece,start,target,rotation,h,{},h.gravityInterval,true);assert(path);
    assert(autoplay_landing_matches(field,piece,path->finalMovement,target,rotation) && path->duration>dt(.5));
    FixedStrategy bot;bot.target=target;bot.rotation=rotation;Fixture f(bot);f.field=field;
    prepend_piece(f.session->player,piece.type());auto movement=start;
    assert(f.step(piece,movement).hardDrop && autoplay_landing_matches(field,piece,movement,target,rotation) && bot.choice->calls==1);
}
void equivalent_kicks_preserve_spin() {
    auto piece=Piece::get_definition(Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[5]);
    const Piece::Pose target{{18,4},Runtime::Rotation::from_index(0)};
    const auto normal=Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(0));
    const auto fifth=Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(4));
    for(bool timed:{false,true}) {
        FixedStrategy bot;Fixture f(bot);
        for(const auto cell:{Runtime::CellPosition{17,3},{17,5},{19,5}})
            f.field.set_cell(cell.row,cell.col,Runtime::BoardCell::garbage());
        f.context.config.handling.gravityInterval=Runtime::GravityInterval::disabled();
        auto& r=f.context.config.movement;
        r.disableLeft=r.disableRight=r.disableClockwise=r.disable180=r.disableSoftDrop=true;
        auto movement=make_spawn_movement_state(10,piece.type());movement.pose=target;movement.pose.rotation=Runtime::Rotation::from_index(1);
        bot.target=target;bot.rotation=fifth;bot.script="ZH";prepend_piece(f.session->player,piece.type());
        // Only Z then H is available. Its kick 0 produces the same full T-spin
        // as the BOT's kick 4, so both shared route search modes must accept it.
        auto path=search_autoplay_path(f.field,piece,movement,target,fifth,f.context.config.handling,r,
            f.context.config.handling.gravityInterval,timed);assert(path && path->script=="ZH");
        assert(path->finalMovement.lastRotation==normal);
        assert(Bot::getop(f.context,f.field,piece,movement.pose,target,fifth)=="ZH");
        if(timed) assert(f.step(piece,movement).hardDrop);
        else {
            f.context.config.autoplay.handlingSimulation=false;f.session->player.timing.autoplay=f.context.real_now();
            const auto canPlace=[&](auto pose){return piece.can_place(pose,f.field);};
            const auto rotate=[&](Piece::Pose& pose,Runtime::Rotation rotation,Piece::KickIndex& kick){return piece.try_rotate(pose,rotation,f.field,kick);};
            assert(run_autoplay_step(f.context,f.field,piece,f.session->player,movement,f.carry,false,2.5,false,
                Runtime::FoolMode::Disabled,f.context.config.handling.gravityInterval,canPlace,rotate,
                []{return Rules::PlayResult::ContinueFrame;}).hardDrop);
        }
        assert(movement.pose==target && movement.lastRotation==normal && bot.choice->calls==1);
        auto expectedField=f.field,actualField=f.field;
        auto& player=f.session->player;player.gameplay.playfield.activeHeight=20;
        const auto expected=resolve_locked_piece(f.context,expectedField,piece,player,target,fifth);
        const auto actual=resolve_locked_piece(f.context,actualField,piece,player,target,movement.lastRotation);
        assert(expected.spin.spin && !expected.spin.mini && actual.spin.spin && !actual.spin.mini);
        assert(expected.score==actual.score && expected.attack.attack==actual.attack.attack);
    }
    // In a mini cavity, kick 4 upgrades the spin. With all moves disabled,
    // a grounded kick 0 may satisfy a mini target but must not satisfy a full one.
    auto field=empty();
    for(const auto cell:{Runtime::CellPosition{17,3},{19,3},{19,5}})
        field.set_cell(cell.row,cell.col,Runtime::BoardCell::garbage());
    MovementState actual;actual.pose=target;actual.lastRotation=normal;
    Runtime::MovementRestrictions restrictions=Runtime::MovementRestrictions::from_mask(63);
    Runtime::HandlingConfig h;h.gravityInterval=Runtime::GravityInterval::disabled();
    for(bool timed:{false,true}) {
        assert(search_autoplay_path(field,piece,actual,target,normal,h,restrictions,h.gravityInterval,timed));
        assert(!search_autoplay_path(field,piece,actual,target,fifth,h,restrictions,h.gravityInterval,timed));
    }
    assert(!autoplay_landing_matches(field,piece,actual,target,{})); // Spin cannot become an ordinary placement.
    for(const auto mode:{Runtime::SpinBonuses::MiniOnly,Runtime::SpinBonuses::None}) {
        AutoplaySpinRules rules;rules.bonuses=mode;
        auto path=search_autoplay_path(field,piece,actual,target,fifth,h,restrictions,h.gravityInterval,true,{},rules);
        assert(path && path->finalMovement.lastRotation==normal);
    }
    AutoplaySpinRules reverse;reverse.noHoldReverse=true;
    assert(search_autoplay_path(field,piece,actual,target,fifth,h,restrictions,h.gravityInterval,true,{},reverse));
    // When neither record is a spin, a direct hard drop needs no extra rotation.
    field=empty();actual.pose={{19,4},{}};actual.lastRotation={};
    auto path=search_autoplay_path(field,piece,actual,actual.pose,fifth,h,restrictions,h.gravityInterval,true);
    assert(path && path->script=="H" && !path->finalMovement.lastRotation.rotated());
    AutoplaySpinRules stupid;stupid.bonuses=Runtime::SpinBonuses::Stupid;
    assert(!search_autoplay_path(field,piece,actual,actual.pose,fifth,h,restrictions,h.gravityInterval,true,{},stupid));
}
void disabled_drop_keys_use_natural_lock() {
    for(bool instant:{false,true}) for(bool wholePiece:{false,true}) {
        FixedStrategy bot;Fixture f(bot);
        auto piece=Piece::get_definition(Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[3]);
        prepend_piece(f.session->player,piece.type());
        f.context.config.handling.gravityInterval=instant ? Runtime::GravityInterval::instant() : Runtime::GravityInterval::periodic(dt(.002));
        f.context.config.handling.epld=dt(.02);
        f.context.config.movement=Runtime::MovementRestrictions::from_mask(127);
        auto movement=make_spawn_movement_state(10,piece.type());bot.target=movement.pose;
        while(piece.can_place(bot.target.translated(1,0),f.field)) ++bot.target.position.row;
        bot.script="H"; // Reject a prohibited custom script and plan gravity + W.
        const auto began=f.context.real_now();
        if(wholePiece) {
            auto& player=f.session->player;auto& board=player.gameplay.playfield;
            board.width=10;board.visibleHeight=board.activeHeight=20;board.field=f.field;
            initialize_board_timers(f.context,player);
            App::PieceLoopState loop;
            assert(App::run_piece(f.context,board.field,piece,player,loop)==Rules::PlayResult::Locked);
            auto events=Replay::decode_operation_stream(f.recording.data,0,1);bool locked=false;
            while(!events.empty()) {
                const auto event=events.front();events.pop();
                if(event.kind==Replay::OperationKind::EndMoveData) {
                    const auto data=event.payload_as<Replay::EndMoveDataPayload>();
                    assert(data.pose==bot.target && data.score==0);locked=true;
                }
            }
            assert(locked);
        } else {
            const auto result=f.step(piece,movement);
            assert(result.locked && !result.hardDrop && movement.pose==bot.target);
        }
        assert(bot.choice->calls==1 && f.session->player.gameplay.scoring.score==0);
        assert(f.context.real_now()-began>=f.context.config.handling.epld);
    }
}
void native_targets_are_preserved() {
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    for(auto type:types) {
        RecordingChatGPT bot;Fixture f(bot);f.context.config.field.holdEnabled=false;
        auto piece=Piece::get_definition(type);prepend_piece(f.session->player,type);
        f.session->player.pieces.nextPreview={types[2],types[6],types[3],types[0],types[4]};
        assert(bot.initialize(f.context));auto movement=make_spawn_movement_state(10,type);
        assert(f.step(piece,movement).hardDrop && bot.choice->calls==1);
        assert(bot.choice->type==type && autoplay_landing_matches(f.field,piece,movement,bot.choice->pose,bot.choice->rotation,
            autoplay_spin_rules(f.context.config)));
    }
}
void persisted_option() {
    UserConfig::Settings user;user.autoplayMode=12;user.autoplayHandlingSimulation=1;
    user.persistencePath=(std::filesystem::temp_directory_path()/"tetr_autoplay_handling.ini").string();assert(UserConfig::save(user));
    UserConfig::Settings restoredUser;UserConfig::load(restoredUser,user.persistencePath);assert(restoredUser.autoplayHandlingSimulation==1);
    auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);Runtime::RuntimeConfig restored;Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",restored,prefix) && restored.autoplay.handlingSimulation);
    assert(Replay::decode_config_prefix("{{Model,0}}",restored,prefix) && !restored.autoplay.handlingSimulation);
}
}
int main() {
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    thinking_freezes_piece_and_lock();display_rates_and_instant_hard_drop();hold_keeps_first_target();
    cancelled_work_cannot_move_the_next_piece();bug_replay_tuck_uses_unified_search();equivalent_kicks_preserve_spin();
    disabled_drop_keys_use_natural_lock();native_targets_are_preserved();persisted_option();
    std::cout.rdbuf(previous);std::cerr<<"autoplay_handling: OK\n";
}
