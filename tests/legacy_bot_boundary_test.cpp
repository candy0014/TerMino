#include "tetr/legacy/bot_adapter.h"
#include "tetr/runtime/game_context.h"

#include <cassert>
#include <iostream>

int main(){
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);

    // Candy bot is a compact probe for the explicit BOT environment snapshot:
    // its init() reads gravity/field/anti-gravity/rotation values passed by the adapter.
    context.config.autoplay.mode=Runtime::AutoplayMode::Candy0014;
    context.config.handling.gravityInterval=Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.6));
    context.config.field.width=10;
    context.config.field.height=20;
    context.config.field.antiGravity=false;
    context.config.rotationSystem=Runtime::RotationSystem::SrsPlus;
    context.config.field.nextCount=5;
    context.config.field.holdEnabled=true;
    context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    context.config.randomPieces=false;
    context.config.quickPlay=Runtime::QuickPlayConfig{};

    // Every surviving BOT has a dispatch entry after Stage 105 renumbering.
    for (const Runtime::AutoplayMode mode : {
             Runtime::AutoplayMode::While1,
             Runtime::AutoplayMode::Candy0014,
             Runtime::AutoplayMode::Mech,
             Runtime::AutoplayMode::ZzztojBotris,
             Runtime::AutoplayMode::ZzztojToj,
             Runtime::AutoplayMode::ZzztojDig,
             Runtime::AutoplayMode::ZzztojQq,
             Runtime::AutoplayMode::ZzztojC2,
             Runtime::AutoplayMode::ChatGPT,
             Runtime::AutoplayMode::ColdClear}) {
        context.config.autoplay.mode=mode;
        assert(Bot::init(context));
    }
    context.config.autoplay.mode=Runtime::AutoplayMode::Candy0014;

    context.config.handling.gravityInterval=Runtime::GravityInterval::instant();
    assert(!Bot::init(context));
    context.config.handling.gravityInterval=Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.6));

    context.config.field.width=11;
    assert(!Bot::init(context));
    context.config.field.width=10;

    context.config.field.antiGravity=true;
    assert(!Bot::init(context));
    context.config.field.antiGravity=false;

    context.config.rotationSystem=Runtime::RotationSystem::Srs;
    assert(!Bot::init(context));

    std::cout<<"legacy_bot_boundary_test: OK\n";
}
