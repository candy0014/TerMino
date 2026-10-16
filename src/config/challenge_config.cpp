#include "tetr/challenges/challenges.h"
#include "tetr/config/map_store.h"
#include "tetr/config/normalization.h"
#include "tetr/compat/garbage_encoding.h"
#include "tetr/compat/gravity_encoding.h"

namespace Challenges {
Runtime::RuntimeConfig preview_config(const UserConfig::Settings& settings) {
    auto config=Runtime::normalize_user_config(settings,Runtime::selected_game_mode(settings));
    if(const auto map=UserConfig::Maps::find(settings.mapId))Runtime::apply_map_config(config,*map);
    if(Runtime::autoplay_enabled(config.autoplay.mode) && config.autoplay.mode!=Runtime::AutoplayMode::ChatGPT &&
       !config.autoplay.handlingSimulation && config.handling.gravityInterval.enabled() && !config.handling.gravityInterval.is_instant())
        config.handling.gravityInterval=Runtime::GravityInterval::disabled();
    return config;
}
std::optional<UserConfig::PresetValue> runtime_value(const Runtime::RuntimeConfig& c,UserConfig::SettingId id) {
    using Id=UserConfig::SettingId;using V=UserConfig::PresetValue;
    // Deliberately no fallback to a user's editable Settings. Unsupported UI
    // fields cannot silently satisfy gameplay constraints.
#define FIELD(name,value) case Id::name:return V{value}
#define INT(name,value) FIELD(name,static_cast<int>(value))
    switch(id) {
        INT(ChallengeRules,c.challengeRules);
        INT(GameMode,c.mode.mode);INT(Goal,Runtime::active_goal(c));FIELD(RacingDistance,c.mode.racingDistance);
        FIELD(BlitzTime,c.mode.blitzTime.seconds());FIELD(GarbageTarget,c.mode.garbageTarget);
        FIELD(PieceTarget,c.mode.pieceTarget);FIELD(AttackTarget,c.mode.attackTarget);FIELD(AllClearTarget,c.mode.allClearTarget);
        FIELD(MapId,c.initialMap?c.initialMap->id:0);FIELD(MapRotationSystem,c.mapRotationSystem);INT(MapGoal,c.mode.mapGoal);
        INT(GarbageMode,c.garbage.mode);FIELD(CheesePattern,c.garbage.cheeseModel);
        FIELD(HoleCount,Compat::to_legacy(c.garbage.holePattern));FIELD(LayerHeight,c.garbage.layerHeight);
        FIELD(GarbageInterval,c.garbage.generationInterval.seconds());FIELD(CheeseMessiness,c.garbage.cheeseMessiness);FIELD(CheeseDensity,c.garbage.cheeseMessiness);
        FIELD(HitMultiplier,c.garbage.hitMultiplier);FIELD(SurvivalApm,c.garbage.survivalApm);
        FIELD(GarbageInTime,c.garbage.incomingDelay.seconds());INT(GarbageEntryMode,c.garbage.entryMode);
        FIELD(GarbageRiseInterval,c.garbage.riseInterval.seconds());FIELD(CancellationMultiplier,c.garbage.cancelMultiplier);
        FIELD(AttackMultiplier,c.garbage.sendMultiplier);FIELD(MessinessWithinAttack,c.garbage.singleMessiness);
        INT(AvoidSameColumn,c.garbage.avoidSameColumn);FIELD(GarbageCap,c.garbage.cap);FIELD(OpenerPhase,c.garbage.openerPhase);
        INT(QpExpert,c.quickPlay.expert);INT(QpNoHold,c.quickPlay.noHold);INT(QpMessy,c.quickPlay.messy);INT(QpGravity,c.quickPlay.gravity);
        INT(QpVolatile,c.quickPlay.volatileMode);INT(QpDoubleHole,c.quickPlay.doubleHole);INT(QpInvisible,c.quickPlay.invisible);
        INT(QpAllSpin,c.quickPlay.allSpin);INT(QpBackfire,c.quickPlay.backfire);INT(QpSnakeEyes,c.quickPlay.snakeEyes);
        INT(QpSnowball,c.quickPlay.snowball);INT(QpFool,c.quickPlay.fool);FIELD(QpDefaultFloor,c.quickPlay.defaultFloor);
        FIELD(GravityInterval,Compat::to_legacy_seconds(c.handling.gravityInterval));INT(InstantGravity,c.handling.gravityInterval.is_instant());
        INT(GravityProgression,c.handling.gravityProgression);FIELD(GravityLevelLines,c.handling.gravityLevelLines);FIELD(GravityAcceleration,c.handling.gravityAcceleration);
        FIELD(SoftDropFactor,c.handling.softDropFactor);INT(SoftDropFixed,c.handling.softDropFixed);
        FIELD(Das,c.handling.das.seconds());FIELD(Arr,c.handling.arr.seconds());FIELD(Dcd,c.handling.dcd.seconds());
        INT(PreMove,c.handling.preMove);INT(DasPrecharge,c.handling.dasPrecharge);FIELD(LockDelay,c.handling.epld.seconds());
        FIELD(LockResetLimit,c.handling.lockResetLimit);INT(InfiniteMovement,c.handling.infiniteMovement);
        FIELD(FieldWidth,c.field.width);FIELD(FieldHeight,c.field.height);FIELD(NextCount,c.field.nextCount);
        INT(HoldEnabled,Runtime::hold_available(c.field));INT(InfiniteHold,c.field.holdEnabled && c.field.infiniteHold);FIELD(HoldCount,c.field.holdEnabled?c.field.holdCount:0);
        FIELD(FieldInvisible,c.field.invisible?(c.field.periodicInvisible?2:1):0);
        FIELD(InvisibilityDelay,c.field.invisibilityDelay.seconds());FIELD(InvisibilityInterval,c.field.invisibilityInterval.seconds());FIELD(InvisibilityDuration,c.field.invisibilityDuration.seconds());
        INT(AntiGravity,c.field.antiGravity);INT(Ghost,c.field.ghost);FIELD(LineClearDelay,c.field.lineClearDelay.seconds());FIELD(EntryDelay,c.field.entryDelay.seconds());
        INT(RotationSystem,c.rotationSystem);INT(SpinBonuses,c.spinBonuses);INT(RandomPieces,Runtime::effective_bag_randomizer(c));
        INT(B2BReward,c.attackRules.b2bReward);FIELD(B2BRewardAmount,c.attackRules.b2bRewardAmount);INT(B2BCharging,c.attackRules.b2bCharging);
        FIELD(B2BChargeThreshold,c.attackRules.b2bChargeThreshold);FIELD(B2BChargeReduction,c.attackRules.b2bChargeReduction);
        INT(PerfectClearEnabled,c.attackRules.perfectClearEnabled);FIELD(PerfectClearAttack,c.attackRules.perfectClearAttack);FIELD(PerfectClearCharge,c.attackRules.perfectClearCharge);
        INT(HalfClearEnabled,c.attackRules.halfClearEnabled);FIELD(HalfClearAttack,c.attackRules.halfClearAttack);FIELD(HalfClearCharge,c.attackRules.halfClearCharge);
        FIELD(GarbageSpecialAttack,c.attackRules.garbageSpecialAttack);
        FIELD(SimulationPps,c.simulationPps);FIELD(TurnPieces,c.turnPieces);
        INT(DisableLeft,c.movement.disableLeft);INT(DisableRight,c.movement.disableRight);INT(DisableClockwise,c.movement.disableClockwise);
        INT(DisableCounterclockwise,c.movement.disableCounterclockwise);INT(Disable180,c.movement.disable180);
        INT(DisableSoftDrop,c.movement.disableSoftDrop);INT(DisableHardDrop,c.movement.disableHardDrop);
        INT(DisableUndo,c.disableUndo);INT(DisablePause,c.disablePause);
        INT(AutoplayMode,c.autoplay.mode);FIELD(AutoplayPps,c.autoplay.pps);FIELD(AutoplayWidth,c.autoplay.searchWidth);INT(AutoplayHandlingSimulation,c.autoplay.handlingSimulation);
        INT(SquareStyle,c.display.squareStyle);INT(ObscuredColors,c.display.obscuredColors);INT(ShowSpawnPosition,c.display.showSpawnPosition);
        default:return {};
    }
#undef INT
#undef FIELD
}
}
