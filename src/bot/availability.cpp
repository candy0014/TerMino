#include "tetr/autoplay/availability.h"
#include "tetr/bot/environment.h"
#include "tetr/bot/bot_while1.h"
#include "tetr/bot/bot_candy0014.h"
#include "tetr/bot/bot_mech.h"
#include "tetr/bot/bot_zzztoj.h"
#include "tetr/bot/bot_chatgpt.h"
#include "tetr/bot/bot_coldclear.h"
#include "tetr/bot/quick_play_search.h"
#include "tetr/runtime/runtime_config.h"
#include "tetr/runtime/piece.h"
#include "tetr/compat/config_encoding.h"
#include "tetr/compat/gravity_encoding.h"
#include <algorithm>

namespace {
Autoplay::Availability unrestricted_movement(const Bot::Environment& env) {
    const auto& m = env.movement;
    if (m.disableLeft) return {false, "Left movement cannot be disabled."};
    if (m.disableRight) return {false, "Right movement cannot be disabled."};
    if (m.disableClockwise) return {false, "CW rotation cannot be disabled."};
    if (m.disableCounterclockwise) return {false, "CCW rotation cannot be disabled."};
    if (m.disable180) return {false, "180-degree rotation cannot be disabled."};
    if (m.disableSoftDrop) return {false, "Soft drop cannot be disabled."};
    if (m.disableHardDrop) return {false, "Bots require hard drop for locking."};
    return {};
}
Autoplay::Availability standard_geometry(const Bot::Environment& env) {
    if (env.fieldWidth != 10) return {false, "Requires a field width of 10."};
    if (env.fieldHeight != 20) return {false, "Requires a field height of 20."};
    if (env.antiGravity) return {false, "Anti-gravity is unsupported."};
    if (env.rotationSystem != 0) return {false, "SRS+ rotation system is required."};
    return {};
}
Autoplay::Availability single_hold(const Bot::Environment& env) {
    if (env.holdEnabled && (env.infiniteHold || env.holdCount>1))
        return {false,"Bots support at most one hold per piece."};
    return {};
}
Autoplay::Availability standard_spin_scoring(const Bot::Environment& env) {
    if (env.spinBonuses!=Runtime::SpinBonuses::AllMiniPlus)
        return {false,"This bot requires ALL-MINI+ spin bonuses."};
    return {};
}
Autoplay::Availability standard_attack_rules(const Bot::Environment& env) {
    const auto& r=env.attackRules;
    if (r.b2bReward!=Runtime::B2BReward::FixedAttack || r.b2bRewardAmount!=1 || !r.b2bCharging || r.b2bChargeThreshold!=4 ||
        r.b2bChargeReduction!=0 || !r.perfectClearEnabled || r.perfectClearAttack!=5 ||
        r.perfectClearCharge!=1 || r.halfClearEnabled || r.garbageSpecialAttack!=1)
        return {false,"This bot requires the default attack rules."};
    if (!r.legacy && env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay))
        return {false,"This bot does not model configurable Quick Play attack rules."};
    return {};
}
Autoplay::Availability standard_cancellation(const Bot::Environment& env) {
    if (env.garbageBufferEnabled && env.cancellationMultiplier!=1.0)
        return {false,"This bot requires a cancellation multiplier of 1."};
    return {};
}
Autoplay::Availability qp_field_modifiers(const Bot::Environment& env) {
    if (env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay) && (env.quickPlayVolatile || env.quickPlayDoubleHole ||
        env.quickPlayBackfire || env.quickPlaySnakeEyes || env.quickPlaySnowball))
        return {false,"This bot does not model the active Quick Play field modifiers."};
    return {};
}
}

namespace Bot {
Environment make_environment(const Runtime::RuntimeConfig& config) {
    Environment env;
    env.movement = config.movement;
    env.garbageModel = Compat::to_legacy(config.garbage.mode);
    env.cheeseModel = config.garbage.cheeseModel;
    env.cheeseMessiness = config.garbage.cheeseMessiness;
    env.cheeseLayerHeight = config.garbage.layerHeight;
    env.incomingGarbageDelaySeconds = config.garbage.incomingDelay.seconds();
    env.quickPlayNoHold = Compat::to_legacy(config.quickPlay.noHold);
    env.quickPlayMessy = Compat::to_legacy(config.quickPlay.messy);
    env.quickPlayAllSpin = Compat::to_legacy(config.quickPlay.allSpin);
    env.quickPlayFool = Compat::to_legacy(config.quickPlay.fool);
    env.quickPlayGravity = Compat::to_legacy(config.quickPlay.gravity);
    env.quickPlayExpert = Compat::to_legacy(config.quickPlay.expert);
    env.quickPlayVolatile = Compat::to_legacy(config.quickPlay.volatileMode);
    env.quickPlayDoubleHole = Compat::to_legacy(config.quickPlay.doubleHole);
    env.quickPlayBackfire = Compat::to_legacy(config.quickPlay.backfire);
    env.quickPlaySnakeEyes = Compat::to_legacy(config.quickPlay.snakeEyes);
    env.quickPlaySnowball = Compat::to_legacy(config.quickPlay.snowball);
    if(config.garbage.mode==Runtime::GarbageMode::QuickPlay)
        env.quickPlayRules=std::make_shared<const Runtime::RuntimeConfig>(config);
    if(config.garbage.mode!=Runtime::GarbageMode::QuickPlay) {
        env.quickPlayNoHold=env.quickPlayMessy=env.quickPlayAllSpin=env.quickPlayFool=0;
        env.quickPlayExpert=env.quickPlayGravity=env.quickPlayVolatile=env.quickPlayDoubleHole=env.quickPlayBackfire=env.quickPlaySnakeEyes=env.quickPlaySnowball=0;
    }
    env.gravityIntervalSeconds = Compat::to_legacy_seconds(config.handling.gravityInterval);
    env.naturalGravityEnabled = config.handling.gravityInterval.enabled();
    env.softDropFactor = config.handling.softDropFactor;
    env.softDropFixed = config.handling.softDropFixed;
    env.dasSeconds = config.handling.das.seconds();
    env.arrSeconds = config.handling.arr.seconds();
    env.fieldWidth = config.field.width;
    env.fieldHeight = config.field.height;
    env.nextCount = config.field.nextCount;
    env.holdEnabled = Runtime::hold_available(config.field);
    env.infiniteHold = config.field.infiniteHold;
    env.holdCount = config.field.holdCount;
    env.spinBonuses = config.spinBonuses;
    env.attackRules = config.attackRules;
    env.legacySpinScoring = config.legacySpinScoring;
    env.antiGravity = config.field.antiGravity;
    env.rotationSystem = Compat::to_legacy(config.rotationSystem);
    env.randomPieces = Runtime::effective_bag_randomizer(config)!=Runtime::BagRandomizer::SevenBag;
    env.openerPhase = config.garbage.openerPhase;
    env.openerDoubleCancelEnabled = config.garbage.mode==Runtime::GarbageMode::Multiplayer;
    env.legacyOpenerPhase = config.garbage.legacyOpenerPhase;
    env.garbageCap = config.garbage.cap;
    env.sendMultiplier = config.garbage.sendMultiplier;
    env.hitMultiplier = config.garbage.hitMultiplier;
    env.cancellationMultiplier = config.garbage.cancelMultiplier;
    env.garbageBufferEnabled = Runtime::uses_garbage_buffer(config.garbage.mode);
    env.entryDelaySeconds = config.field.entryDelay.seconds();
    env.lineClearDelaySeconds = config.field.lineClearDelay.seconds();
    env.pieceTime = config.simulationPps>0;
    env.handlingSimulation = config.autoplay.handlingSimulation;
    env.lockDelaySeconds = config.handling.epld.seconds();
    env.dcdSeconds = config.handling.dcd.seconds();
    env.preMove = config.handling.preMove;
    env.dasPrecharge = config.handling.dasPrecharge;
    env.lockResetLimit = config.handling.lockResetLimit;
    env.infiniteMovement = config.handling.infiniteMovement;
    const auto types=Piece::types_for_rotation_system(config.rotationSystem,config.legacyRotationRules,
        config.legacyFoolPieceSet ? 10 : config.quickPlay.defaultFloor,config.legacyEventGeometry);
    if (types.size()==7) std::copy(types.begin(),types.end(),env.pieceTypes.begin());
    env.autoplayPps = config.autoplay.pps;
    env.autoplaySearchWidth = config.autoplay.searchWidth;
    env.autoplayMode = Compat::to_legacy(config.autoplay.mode);
    return env;
}
}

namespace Bot_candy0014 {
Autoplay::Availability availability(const Bot::Environment& env) {
    if(env.handlingSimulation) return {false,"Handling simulation is currently supported only by bot_chatgpt."};
    if(Bot::QuickPlaySearch::enabled(env))return Bot::QuickPlaySearch::availability(env);
    if (env.gravityIntervalSeconds == 0) return {false, "Instant gravity (20G) is unsupported."};
    if (auto result = standard_geometry(env); !result) return result;
    if (env.movement.disableHardDrop) return {false, "Bots require hard drop for locking."};
    if (env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay) && env.quickPlayGravity) return {false,"This bot cannot handle Quick Play's gravity progression."};
    if (auto result=qp_field_modifiers(env); !result) return result;
    return single_hold(env);
}
}
namespace Bot_while1 {
Autoplay::Availability availability(const Bot::Environment& env) {
    if(env.handlingSimulation) return {false,"Handling simulation is currently supported only by bot_chatgpt."};
    if (Bot::QuickPlaySearch::enabled(env)) return Bot::QuickPlaySearch::availability(env);
    if (auto result=standard_attack_rules(env); !result) return result;
    if (env.fieldWidth != 10) return {false, "Requires a field width of 10."};
    if (env.fieldHeight > 24) return {false, "Requires a field height of at most 24."};
    if (env.nextCount>18) return {false,"Requires at most 18 preview pieces."};
    if (env.antiGravity) return {false, "Anti-gravity is unsupported."};
    if (env.movement.disableHardDrop) return {false, "Bots require hard drop for locking."};
    if (auto result=qp_field_modifiers(env); !result) return result;
    if (env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay) && (env.quickPlayExpert || env.quickPlayFool || env.quickPlayMessy==2 || env.quickPlayAllSpin==2))
        return {false,"This bot does not model the active Quick Play piece and attack modifiers."};
    if (auto result=single_hold(env); !result) return result;
    if (auto result=standard_spin_scoring(env); !result) return result;
    if (env.garbageBufferEnabled && !env.pieceTime && (env.entryDelaySeconds>0 || env.lineClearDelaySeconds>0))
        return {false,"This bot cannot forecast garbage with entry or line-clear delay."};
    return {};
}
}
namespace Bot_mech {
Autoplay::Availability availability(const Bot::Environment& env) {
    if(env.handlingSimulation) return {false,"Handling simulation is currently supported only by bot_chatgpt."};
    if(Bot::QuickPlaySearch::enabled(env))return Bot::QuickPlaySearch::availability(env);
    if (env.wall_only_horizontal()) return {false, "DAS must be greater than zero."};
    if (env.gravityIntervalSeconds == 0) return {false, "Instant gravity (20G) is unsupported."};
    if (auto result = standard_geometry(env); !result) return result;
    if (env.nextCount < 5) return {false, "Requires at least 5 preview pieces."};
    if (!env.holdEnabled) return {false, "Hold must be enabled."};
    if (env.randomPieces) return {false,"This bot requires the 7-BAG randomizer."};
    if (auto result=single_hold(env); !result) return result;
    if (auto result=standard_spin_scoring(env); !result) return result;
    if (auto result=standard_cancellation(env); !result) return result;
    if (env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay)) return {false,"This bot cannot model Quick Play's changing attack and field rules."};
    return unrestricted_movement(env);
}
}
namespace Bot_zzztoj {
Autoplay::Availability availability(const Bot::Environment& env) {
    if(env.handlingSimulation) return {false,"Handling simulation is currently supported only by bot_chatgpt."};
    if (Bot::QuickPlaySearch::enabled(env))
        return Bot::QuickPlaySearch::availability(env);
    if (auto result=standard_attack_rules(env); !result) return result;
    if (env.wall_only_horizontal()) return {false, "DAS must be greater than zero."};
    if (env.gravityIntervalSeconds==0.0) return {false,"Instant gravity (20G) is unsupported."};
    if (auto result = standard_geometry(env); !result) return result;
    if (env.randomPieces) return {false, "Random pieces are unsupported."};
    if (env.quickPlayNoHold != 0) return {false, "Nohold Mod is unsupported."};
    if (env.quickPlayMessy != 0) return {false, "Messy Mod is unsupported."};
    if (env.quickPlayAllSpin != 0) return {false, "Allspin Mod is unsupported."};
    if (env.quickPlayFool != 0) return {false, "Fool Mod is unsupported."};
    if (env.nextCount <= 0) return {false, "Requires at least 1 preview piece."};
    if (auto result=single_hold(env); !result) return result;
    if (auto result=standard_spin_scoring(env); !result) return result;
    if (auto result=standard_cancellation(env); !result) return result;
    if (env.garbageModel==static_cast<int>(Runtime::GarbageMode::QuickPlay)) return {false,"This bot cannot model Quick Play's changing attack and field rules."};
    return unrestricted_movement(env);
}
}
namespace Bot_chatgpt {
Autoplay::Availability availability(const Bot::Environment& env) {
    if (Bot::QuickPlaySearch::enabled(env)) return Bot::QuickPlaySearch::availability(env);
    auto base=env;
    base.handlingSimulation=false;
    if(env.handlingSimulation) {
        if(env.movement.disableHardDrop && !env.naturalGravityEnabled)
            return {false,"Bots require hard drop for locking."};
        base.dasSeconds=std::max(.001,base.dasSeconds);
        if(base.gravityIntervalSeconds<=1e-7) base.gravityIntervalSeconds=.6;
        base.movement={};
    }
    // ChatGPT's own rule evaluator handles configurable cancellation/spins;
    // the upstream bots still require the standard values.
    base.cancellationMultiplier=1.0;
    base.spinBonuses=Runtime::SpinBonuses::AllMiniPlus;
    base.attackRules={};
    // ChatGPT's movement graph applies instant gravity after each action and
    // the host proves the returned route. The upstream Botris guard below
    // describes its own graph, not this BOT's actual capabilities.
    if(base.gravityIntervalSeconds<=1e-7)base.gravityIntervalSeconds=.6;
    if (auto result = Bot_zzztoj::availability(base); !result) return result;
    return {};
}
}
namespace Autoplay {
Availability availability(const Runtime::RuntimeConfig& config, Runtime::AutoplayMode mode) {
    if(mode!=Runtime::AutoplayMode::Disabled && config.initialMap && config.initialMap->generator==Runtime::MapGenerator::CustomSequence) {
        const auto types=Piece::types_for_rotation_system(config.rotationSystem,config.legacyRotationRules,10,config.legacyEventGeometry);
        for(const auto type:config.initialMap->sequence)
            if(std::find(types.begin(),types.end(),type)==types.end()) return {false,"The map sequence contains pieces unsupported by this bot."};
    }
    auto env = Bot::make_environment(config);
    env.autoplayMode = Compat::to_legacy(mode);
    switch (mode) {
        case Runtime::AutoplayMode::Disabled: return {};
        case Runtime::AutoplayMode::While1: return Bot_while1::availability(env);
        case Runtime::AutoplayMode::Candy0014: return Bot_candy0014::availability(env);
        case Runtime::AutoplayMode::Mech: return Bot_mech::availability(env);
        case Runtime::AutoplayMode::ZzztojBotris:
        case Runtime::AutoplayMode::ZzztojToj:
        case Runtime::AutoplayMode::ZzztojDig:
        case Runtime::AutoplayMode::ZzztojQq:
        case Runtime::AutoplayMode::ZzztojC2: return Bot_zzztoj::availability(env);
        case Runtime::AutoplayMode::ChatGPT: return Bot_chatgpt::availability(env);
        case Runtime::AutoplayMode::ColdClear: return Bot_coldclear::availability(env);
    }
    return {false, "Unknown bot."};
}
}

namespace Bot_coldclear {
Autoplay::Availability availability(const Bot::Environment& env) {
    if(env.handlingSimulation) return {false,"Handling simulation is currently supported only by bot_chatgpt."};
    if(Bot::QuickPlaySearch::enabled(env))return Bot::QuickPlaySearch::availability(env);
    if (env.fieldWidth != 10 || env.fieldHeight != 20) return {false,"Cold Clear requires a 10 by 20 field."};
    if (env.antiGravity) return {false,"Cold Clear does not support anti-gravity."};
    if (!Runtime::standard_rotation_system(static_cast<Runtime::RotationSystem>(env.rotationSystem)) || env.quickPlayFool)
        return {false,"Cold Clear requires the seven standard tetrominoes."};
    if (env.movement.disableHardDrop) return {false,"Bots require hard drop for locking."};
    if (auto result = single_hold(env); !result) return result;
    if (env.garbageModel == static_cast<int>(Runtime::GarbageMode::Unclear))
        return {false,"Cold Clear does not model solid garbage transformations."};
    if (env.garbageModel == static_cast<int>(Runtime::GarbageMode::QuickPlay) && env.quickPlayGravity)
        return {false,"Cold Clear does not forecast Quick Play gravity progression."};
    return qp_field_modifiers(env);
}
}
