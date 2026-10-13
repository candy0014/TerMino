#include "tetr/legacy/bot_adapter.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/runtime/game_context.h"
#include "tetr/autoplay/availability.h"
#include "tetr/bot/environment.h"

#include <cassert>
#include <vector>
#include <limits>

namespace {
void capability_matrix() {
    using Mode=Runtime::AutoplayMode;
    Runtime::RuntimeConfig config;
    config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    for(const auto& bot:Runtime::kAutoplayModes) {
        assert(Autoplay::availability(config,bot.mode));
        for(double multiplier:{0.0,.5,1.75}) {
            config.garbage.cancelMultiplier=multiplier;
            const bool supported=bot.mode==Mode::Disabled || bot.mode==Mode::Candy0014 || bot.mode==Mode::While1 || bot.mode==Mode::ChatGPT || bot.mode==Mode::ColdClear;
            const auto available=Autoplay::availability(config,bot.mode);
            assert(bool(available)==supported);
            if(!available)assert(available.reason.find("cancellation")!=std::string::npos);
        }
        config.garbage.cancelMultiplier=1;
        config.garbage.hitMultiplier=.125;
        assert(Autoplay::availability(config,bot.mode)); // Queue packets are already scaled.
        config.field.infiniteHold=true;
        assert(bool(Autoplay::availability(config,bot.mode))==(bot.mode==Mode::Disabled));
        config.field.infiniteHold=false;
        config.field.holdCount=2;
        assert(bool(Autoplay::availability(config,bot.mode))==(bot.mode==Mode::Disabled));
        config.field.holdCount=1;
        config.spinBonuses=Runtime::SpinBonuses::None;
        assert(bool(Autoplay::availability(config,bot.mode))==(bot.mode==Mode::Disabled || bot.mode==Mode::Candy0014 || bot.mode==Mode::ChatGPT || bot.mode==Mode::ColdClear));
        config.spinBonuses=Runtime::SpinBonuses::AllMiniPlus;
        config.attackRules.b2bRewardAmount=3;
        assert(bool(Autoplay::availability(config,bot.mode))==(bot.mode==Mode::Disabled ||
            bot.mode==Mode::Candy0014 || bot.mode==Mode::Mech || bot.mode==Mode::ChatGPT || bot.mode==Mode::ColdClear));
        config.attackRules={};
        config.attackRules.halfClearEnabled=true;
        config.attackRules.halfClearAttack=7;
        config.attackRules.b2bChargeReduction=-2;
        const bool customSupported=bot.mode==Mode::Disabled || bot.mode==Mode::Candy0014 || bot.mode==Mode::Mech ||
            bot.mode==Mode::ChatGPT || bot.mode==Mode::ColdClear;
        assert(bool(Autoplay::availability(config,bot.mode))==customSupported);
        config.attackRules={};
        config.disableUndo=true;config.display.obscuredColors=true;config.display.squareStyle=1;
        config.handling.softDropFixed=true;config.handling.softDropFactor=.05;
        config.handling.dcd=Runtime::Duration::from_seconds(.1);config.handling.infiniteMovement=true;
        assert(Autoplay::availability(config,bot.mode));
        config.field.entryDelay=Runtime::Duration::from_seconds(.5);
        assert(bool(Autoplay::availability(config,bot.mode))==(bot.mode!=Mode::While1));
        config.simulationPps=4;
        assert(Autoplay::availability(config,bot.mode));
        config.field.entryDelay={};config.simulationPps=0;
    }
}

void paths_use_frame_order_and_infinite_falling(Runtime::GameContext& context) {
    Runtime::Playfield field;field.initialize(10,20);
    for(int row=16;row<20;++row)for(int col=4;col<10;++col)field.set_cell(row,col,Runtime::BoardCell::garbage());
    field.set_cell(18,3,Runtime::BoardCell::garbage());
    field.set_cell(19,2,Runtime::BoardCell::garbage());field.set_cell(19,3,Runtime::BoardCell::garbage());
    const auto piece=Piece::get_definition(Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[4]);
    const Piece::Pose start{{18,1},Runtime::Rotation{2}},target{{18,2},Runtime::Rotation{0}};
    context.config.handling.gravityInterval=Runtime::GravityInterval::instant();
    context.config.handling.arr={};context.config.handling.das=Runtime::Duration::from_seconds(.1);
    const auto script=Bot::getop(context,field,piece,start,target,{});
    assert(!script.empty());
    bool combined=false;
    Engine::MovementState state;state.pose=start;
    for(char code:script) {
        const auto command=Engine::decode_movement_script_command(code);
        combined|=Engine::movement_script_components(command)[1]!=Engine::MovementScriptCommand::Invalid;
        Engine::apply_movement_script_command(state,command,
            [&](Piece::Pose p){return piece.can_place(p,field);},
            [&](Piece::Pose& p,Runtime::Rotation r,Piece::KickIndex& k){return piece.try_rotate(p,r,field,k);},
            false,true,{},true,true);
    }
    assert(combined && state.pose==target);
    context.config.handling.gravityInterval=Runtime::GravityInterval::disabled();
    context.config.handling.softDropFactor=0;
    field.clear();
    state.pose={{0,4},Runtime::Rotation{}};
    const auto o=Piece::get_definition(Piece::Type{3});
    Engine::apply_movement_script_command(state,Engine::MovementScriptCommand::SoftDropStep,
        [&](Piece::Pose p){return o.can_place(p,field);},{},false,true,{},false,true);
    assert(state.pose.position.row==19);
    context.config.handling.gravityInterval=Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.6));
}

void modern_piece_ids_are_preserved(Runtime::GameContext& context,Runtime::PlayerSessionState& player) {
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    context.config.autoplay.searchWidth=3;
    context.config.handling.gravityInterval=Runtime::GravityInterval::disabled();
    for(const auto& bot:Runtime::kAutoplayModes) {
        if(bot.mode==Runtime::AutoplayMode::Disabled)continue;
        context.config.autoplay.mode=bot.mode;
        assert(Bot::init(context));
        for(int family=0;family<7;++family) {
            auto& field=player.gameplay.playfield.field;field.clear();
            std::vector<Piece::Type> next{types[family]};next.insert(next.end(),types.begin(),types.end());
            const Piece::Pose start{{-2,4},Runtime::Rotation{}};
            auto answer=start;Runtime::RotationRecord rotation;
            const auto selected=Bot::getpos(context,player,field,next,{},start,{true,Piece::Type::none()},answer,rotation);
            assert(selected==next[0] || selected==next[1]);
            assert(selected.index()>=173);
            const auto definition=Piece::get_definition(selected);
            assert(definition.can_place(answer,field) && !definition.can_place(answer.translated(1,0),field));
        }
    }
}

void zero_handling_capabilities(Runtime::GameContext& context) {
    for (const auto& bot:Runtime::kAutoplayModes) {
        context.config.autoplay.mode=bot.mode;
        context.config.handling.das=Runtime::Duration::from_seconds(0.1);
        context.config.handling.arr=Runtime::Duration::from_seconds(0.01);
        assert(Bot::init(context));
        context.config.handling.das={}; context.config.handling.arr={};
        const bool supported=bot.mode==Runtime::AutoplayMode::Disabled ||
            bot.mode==Runtime::AutoplayMode::Candy0014 || bot.mode==Runtime::AutoplayMode::While1 || bot.mode==Runtime::AutoplayMode::ColdClear;
        assert(Bot::init(context)==supported);
        context.config.handling.arr=Runtime::Duration::from_seconds(0.01);
        assert(Bot::init(context)==supported); // zero DAS + positive ARR
        context.config.handling.das=Runtime::Duration::from_seconds(0.1);
        context.config.handling.arr={};
        assert(Bot::init(context)); // ARR=0 preserves tap-based search
    }
}

void instant_bot_paths(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    context.config.autoplay.searchWidth=5;
    for (int handling=0; handling<3; ++handling)
      for (auto mode:{Runtime::AutoplayMode::Candy0014,Runtime::AutoplayMode::While1}) {
        const bool zeroDas=handling!=2, zeroArr=handling!=1;
        context.config.handling.das=Runtime::Duration::from_seconds(zeroDas ? 0 : 0.1);
        context.config.handling.arr=Runtime::Duration::from_seconds(zeroArr ? 0 : 0.01);
        context.config.autoplay.mode=mode;
        assert(Bot::init(context));
        for (int layout=0; layout<2; ++layout) for (int type=0; type<7; ++type) {
            auto& field=player.gameplay.playfield.field;
            field.clear();
            if (layout) {
                for (int column=0; column<10; ++column)
                    for (int row=19-column%4; row<20; ++row)
                        field.set_cell(row,column,Runtime::BoardCell::piece(1));
            }
            std::vector<Piece::Type> next{Piece::Type::from_index(type)};
            for (int i=0; i<6; ++i) next.push_back(Piece::Type::from_index(i));
            const Piece::Pose start{{-2,4},Runtime::Rotation{}};
            Piece::Pose answer=start;
            Runtime::RotationRecord answerRotation{};
            const auto selected=Bot::getpos(context,player,field,next,{},start,
                Autoplay::HoldInput{false,Piece::Type::none()},answer,answerRotation);
            assert(selected==next.front());
            auto piece=Piece::get_definition(selected);
            assert(piece.can_place(answer,field) && !piece.can_place(answer.translated(1,0),field));
            const auto script=Bot::getop(context,field,piece,start,answer,answerRotation);
            assert(!script.empty() && script.back()=='H');
            if (zeroDas) assert(script.find('L')==std::string::npos && script.find('R')==std::string::npos);
            Engine::MovementState movement;
            movement.pose=start;
            for (const char command:script) {
                Engine::apply_movement_script_command(movement,Engine::decode_movement_script_command(command),
                    [&](Piece::Pose pose) { return piece.can_place(pose,field); },
                    [&](Piece::Pose& pose,Runtime::Rotation target,Piece::KickIndex& kick) {
                        return piece.try_rotate(pose,target,field,kick);
                    },zeroDas,zeroArr);
            }
            assert(Engine::autoplay_landing_matches(field,piece,movement,answer,answerRotation,
                Engine::autoplay_spin_rules(context.config)));
        }
    }
}
}

int main() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);

    context.config.autoplay.mode = Runtime::AutoplayMode::Candy0014;
    context.config.handling.gravityInterval =
        Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(0.6));
    context.config.field.width = 10;
    context.config.field.height = 20;
    context.config.field.nextCount = 5;
    context.config.field.holdEnabled = true;
    context.config.field.antiGravity = false;
    context.config.rotationSystem = Runtime::RotationSystem::SrsPlus;

    Runtime::PlayerSessionState player;
    player.gameplay.playfield.width = 10;
    player.gameplay.playfield.visibleHeight = 20;
    player.gameplay.playfield.activeHeight = 20;
    player.gameplay.playfield.nextCount = 5;
    player.gameplay.playfield.holdEnabled = true;
    player.gameplay.playfield.field.initialize(10, 20);
    player.gameplay.playfield.field.clear();

    assert(Bot::init(context));

    const Piece::Type current = Piece::Type::from_index(5); // T
    const std::vector<Piece::Type> next{
        current,
        Piece::Type::from_index(0),
        Piece::Type::from_index(1),
        Piece::Type::from_index(2),
        Piece::Type::from_index(3)};
    const Piece::Pose start{{-2, 4}, Runtime::Rotation::from_index(0)};
    Piece::Pose answer = start;
    Runtime::RotationRecord answerRotation = Runtime::RotationRecord::none();

    const Piece::Type selected = Bot::getpos(
        context,
        player,
        player.gameplay.playfield.field,
        next,
        {},
        start,
        Autoplay::HoldInput{false, Piece::Type::none()},
        answer,
        answerRotation);

    assert(selected == current);
    const Piece::Definition piece = Piece::get_definition(selected);
    assert(piece.can_place(answer, player.gameplay.playfield.field));
    assert(!piece.can_place(answer.translated(1, 0), player.gameplay.playfield.field));

    const std::string script = Bot::getop(
        context,
        player.gameplay.playfield.field,
        piece,
        start,
        answer,
        answerRotation);
    assert(!script.empty());
    assert(script.back() == 'H');
    zero_handling_capabilities(context);
    instant_bot_paths(context,player);
    capability_matrix();
    paths_use_frame_order_and_infinite_falling(context);
    context.config.handling.das=Runtime::Duration::from_seconds(.1);
    context.config.handling.arr=Runtime::Duration::from_seconds(.01);
    modern_piece_ids_are_preserved(context,player);
    context.config={};
    context.config.autoplay.mode=Runtime::AutoplayMode::While1;
    context.config.autoplay.searchWidth=3;
    context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    player.gameplay.playfield.field.clear();
    player.garbage.buffer={{std::numeric_limits<int>::max(),context.now()},
        {std::numeric_limits<int>::max(),context.now()}};
    const auto buffered=Bot::getpos(context,player,player.gameplay.playfield.field,next,{},start,
        Autoplay::HoldInput{false,Piece::Type::none()},answer,answerRotation);
    assert(buffered==current);
    const auto bufferedPiece=Piece::get_definition(buffered);
    assert(bufferedPiece.can_place(answer,player.gameplay.playfield.field));
    assert(!bufferedPiece.can_place(answer.translated(1,0),player.gameplay.playfield.field));
    return 0;
}
