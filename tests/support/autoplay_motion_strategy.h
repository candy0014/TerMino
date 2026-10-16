#ifndef TETR_TEST_AUTOPLAY_MOTION_STRATEGY_H
#define TETR_TEST_AUTOPLAY_MOTION_STRATEGY_H
#include "tetr/autoplay/strategy.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include <memory>
#include <string>
namespace Test {
struct ScriptMotionBot:Autoplay::Strategy {
    std::string script;
    ScriptMotionBot() {
        for(auto command:{Engine::MovementScriptCommand::RightStep,Engine::MovementScriptCommand::RotateCW,
            Engine::MovementScriptCommand::SoftDropStep,Engine::MovementScriptCommand::RightStep,Engine::MovementScriptCommand::HardDrop})
            script+=Engine::movement_script_command_char(command);
    }
    std::unique_ptr<Autoplay::Strategy> clone_for_search() const override {return std::make_unique<ScriptMotionBot>(*this);}
    bool initialize(Runtime::GameContext&) override {return true;}
    Piece::Type choose_position(Runtime::GameContext&,Runtime::PlayerSessionState&,Runtime::Playfield field,
        const std::vector<Piece::Type>& next,const std::vector<Piece::Type>&,Piece::Pose start,Autoplay::HoldInput,
        Piece::Pose& target,Runtime::RotationRecord& rotation) override {
        auto piece=Piece::get_definition(next.front());Engine::MovementState movement;movement.pose=start;
        for(char command:script) Engine::apply_movement_script_command(movement,Engine::decode_movement_script_command(command),
            [&](auto pose){return piece.can_place(pose,field);},
            [&](auto& pose,auto turns,auto& kick){return piece.try_rotate(pose,turns,field,kick);});
        target=movement.pose;rotation=movement.lastRotation;return next.front();
    }
    std::string movement_script(Runtime::GameContext&,Runtime::Playfield,Piece::Definition,Piece::Pose,Piece::Pose,Runtime::RotationRecord) override {return script;}
};
}
#endif
