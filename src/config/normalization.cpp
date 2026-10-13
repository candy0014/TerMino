#include "tetr/config/normalization.h"

#include "tetr/config/user_config.h"
#include "tetr/config/settings_schema.h"
#include "tetr/compat/config_encoding.h"
#include "tetr/compat/garbage_encoding.h"
#include "tetr/compat/gravity_encoding.h"

#include <algorithm>
#include <cmath>

namespace Runtime {
namespace {

template <class T>
T clamp(T value, T lower, T upper) {
    return std::min(std::max(value, lower), upper);
}

Runtime::Duration seconds(double value) {
    return Runtime::Duration::from_seconds(value);
}

} // namespace

GameMode selected_game_mode(const UserConfig::Settings& settings) {
    return Compat::game_mode_from_legacy(std::abs(UserConfig::clamped_int(settings, UserConfig::SettingId::GameMode)));
}

RuntimeConfig normalize_user_config(const UserConfig::Settings& user, GameMode selectedMode) {
    RuntimeConfig config;
    config.movement = {
        UserConfig::clamped_int(user, UserConfig::SettingId::DisableLeft) != 0,
        UserConfig::clamped_int(user, UserConfig::SettingId::DisableRight) != 0,
        UserConfig::clamped_int(user, UserConfig::SettingId::DisableClockwise) != 0,
        UserConfig::clamped_int(user, UserConfig::SettingId::DisableCounterclockwise) != 0,
        UserConfig::clamped_int(user, UserConfig::SettingId::Disable180) != 0,
        UserConfig::clamped_int(user, UserConfig::SettingId::DisableSoftDrop) != 0,
        UserConfig::clamped_int(user, UserConfig::SettingId::DisableHardDrop) != 0};
    config.disableUndo = UserConfig::clamped_int(user, UserConfig::SettingId::DisableUndo) != 0;
    config.disablePause = UserConfig::clamped_int(user, UserConfig::SettingId::DisablePause) != 0;
    config.mode.mode = selectedMode;
    config.mode.replayMode = false;
    config.mode.fixedBlitzSpeed = user.gameMode == -2;
    config.mode.racingDistance = UserConfig::clamped_int(user, UserConfig::SettingId::RacingDistance);
    config.mode.blitzTime = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::BlitzTime));

    config.handling.gravityInterval = user.gravityInterval < 0
        ? GravityInterval::disabled()
        : Compat::gravity_from_legacy_seconds(user.gravityInterval);
    config.handling.gravityProgression=static_cast<GravityProgression>(UserConfig::clamped_int(user,UserConfig::SettingId::GravityProgression));
    config.handling.gravityLevelLines=UserConfig::clamped_int(user,UserConfig::SettingId::GravityLevelLines);
    config.handling.gravityAcceleration=UserConfig::clamped_real(user,UserConfig::SettingId::GravityAcceleration);
    config.handling.softDropFactor = UserConfig::clamped_real(user, UserConfig::SettingId::SoftDropFactor);
    config.handling.softDropFixed = user.softDropFixed != 0;
    config.handling.das = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::Das));
    config.handling.arr = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::Arr));
    config.handling.dcd = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::Dcd));
    config.handling.preMove = UserConfig::clamped_int(user, UserConfig::SettingId::PreMove) != 0;
    config.handling.dasPrecharge = UserConfig::clamped_int(user, UserConfig::SettingId::DasPrecharge) != 0;
    config.handling.epld = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::LockDelay));
    config.handling.lockResetLimit = UserConfig::clamped_int(user, UserConfig::SettingId::LockResetLimit);
    config.handling.infiniteMovement = UserConfig::clamped_int(user, UserConfig::SettingId::InfiniteMovement) != 0;

    config.field.width = UserConfig::clamped_int(user, UserConfig::SettingId::FieldWidth);
    config.field.height = UserConfig::clamped_int(user, UserConfig::SettingId::FieldHeight);
    config.field.nextCount = UserConfig::clamped_int(user, UserConfig::SettingId::NextCount);
    config.field.holdEnabled = UserConfig::clamped_int(user, UserConfig::SettingId::HoldEnabled) != 0;
    config.field.invisible = UserConfig::clamped_int(user, UserConfig::SettingId::FieldInvisible) != 0;
    config.field.antiGravity = UserConfig::clamped_int(user, UserConfig::SettingId::AntiGravity) != 0;
    config.field.ghost = UserConfig::clamped_int(user, UserConfig::SettingId::Ghost) != 0;
    config.field.lineClearDelay = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::LineClearDelay));
    config.field.entryDelay = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::EntryDelay));

    config.rotationSystem = Compat::rotation_system_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::RotationSystem));
    config.mapRotationSystem = UserConfig::clamped_int(user, UserConfig::SettingId::MapRotationSystem);
    config.mode.mapGoal = static_cast<MapGoal>(UserConfig::clamped_int(user,UserConfig::SettingId::MapGoal));
    config.spinBonuses = spin_bonuses_from_index(UserConfig::clamped_int(user,UserConfig::SettingId::SpinBonuses));
    config.attackRules.b2bReward=static_cast<B2BReward>(UserConfig::clamped_int(user,UserConfig::SettingId::B2BReward));
    config.attackRules.b2bRewardAmount=UserConfig::clamped_int(user,UserConfig::SettingId::B2BRewardAmount);
    config.attackRules.b2bCharging=UserConfig::clamped_int(user,UserConfig::SettingId::B2BCharging) != 0;
    config.attackRules.b2bChargeThreshold=UserConfig::clamped_int(user,UserConfig::SettingId::B2BChargeThreshold);
    config.attackRules.b2bChargeReduction=UserConfig::clamped_int(user,UserConfig::SettingId::B2BChargeReduction);
    config.attackRules.perfectClearEnabled=UserConfig::clamped_int(user,UserConfig::SettingId::PerfectClearEnabled) != 0;
    config.attackRules.perfectClearAttack=UserConfig::clamped_int(user,UserConfig::SettingId::PerfectClearAttack);
    config.attackRules.perfectClearCharge=UserConfig::clamped_int(user,UserConfig::SettingId::PerfectClearCharge);
    config.attackRules.halfClearEnabled=UserConfig::clamped_int(user,UserConfig::SettingId::HalfClearEnabled) != 0;
    config.attackRules.halfClearAttack=UserConfig::clamped_int(user,UserConfig::SettingId::HalfClearAttack);
    config.attackRules.halfClearCharge=UserConfig::clamped_int(user,UserConfig::SettingId::HalfClearCharge);
    config.attackRules.garbageSpecialAttack=UserConfig::clamped_int(user,UserConfig::SettingId::GarbageSpecialAttack);
    config.bagRandomizer = bag_randomizer_from_index(UserConfig::clamped_int(user, UserConfig::SettingId::RandomPieces));
    config.randomPieces = config.bagRandomizer != BagRandomizer::SevenBag;

    config.garbage.mode = Compat::garbage_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::GarbageMode));
    config.garbage.cheeseModel = UserConfig::clamped_int(user, UserConfig::SettingId::CheesePattern);
    if (config.garbage.cheeseModel == 0) {
        config.garbage.cheeseModel = 1;
    }
    if (std::abs(config.garbage.cheeseModel) != 2 && std::abs(config.garbage.cheeseModel) != 3) {
        config.garbage.holePattern = Compat::hole_pattern_from_legacy(
            clamp(user.holeCount, -config.field.width, config.field.width));
    } else {
        config.garbage.holePattern = GarbageHolePattern::scattered(1);
    }
    if (config.garbage.holePattern.extent() == 0) {
        config.garbage.holePattern = GarbageHolePattern::scattered(1);
    }
    config.garbage.layerHeight = clamp(user.layerHeight, 0, config.field.height);
    config.garbage.generationInterval = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::GarbageInterval));
    // The generation/replay boundary retains its historical active-probability
    // slot. User profiles keep messiness and density as independent settings.
    config.garbage.cheeseMessiness = UserConfig::clamped_real(user,
        std::abs(config.garbage.cheeseModel)==2
            ? UserConfig::SettingId::CheeseDensity : UserConfig::SettingId::CheeseMessiness);
    config.garbage.attackMultiplier = UserConfig::clamped_real(user, UserConfig::SettingId::HitMultiplier);
    config.garbage.hitMultiplier = config.garbage.attackMultiplier;
    config.garbage.cancelMultiplier = UserConfig::clamped_real(user, UserConfig::SettingId::CancellationMultiplier);
    config.garbage.singleMessiness = UserConfig::clamped_real(user, UserConfig::SettingId::MessinessWithinAttack);
    config.garbage.avoidSameColumn = UserConfig::clamped_int(user, UserConfig::SettingId::AvoidSameColumn) != 0;
    config.garbage.cap = UserConfig::clamped_int(user, UserConfig::SettingId::GarbageCap);
    config.garbage.openerPhase = UserConfig::clamped_int(user, UserConfig::SettingId::OpenerPhase);
    config.garbage.survivalApm = UserConfig::clamped_real(user, UserConfig::SettingId::SurvivalApm);
    config.garbage.incomingDelay = seconds(UserConfig::clamped_real(user, UserConfig::SettingId::GarbageInTime));
    config.garbage.entryMode = static_cast<GarbageEntryMode>(UserConfig::clamped_int(user, UserConfig::SettingId::GarbageEntryMode));
    config.garbage.riseInterval = seconds(UserConfig::clamped_real(user,UserConfig::SettingId::GarbageRiseInterval));

    if (selectedMode == GameMode::QuickPlay) config.garbage.mode = GarbageMode::QuickPlay;
    if (selectedMode == GameMode::Multiplayer || selectedMode == GameMode::LocalVersus) {
        config.garbage.mode = GarbageMode::Multiplayer;
    }

    config.quickPlay.expert = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpExpert));
    config.quickPlay.noHold = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpNoHold));
    config.quickPlay.messy = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpMessy));
    config.quickPlay.gravity = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpGravity));
    config.quickPlay.volatileMode = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpVolatile));
    config.quickPlay.doubleHole = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpDoubleHole));
    config.quickPlay.invisible = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpInvisible));
    config.quickPlay.allSpin = Compat::all_spin_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpAllSpin));
    config.quickPlay.backfire = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpBackfire));
    config.quickPlay.snakeEyes = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpSnakeEyes));
    config.quickPlay.snowball = Compat::modifier_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpSnowball));
    config.quickPlay.fool = Compat::fool_mode_from_legacy(UserConfig::clamped_int(user, UserConfig::SettingId::QpFool));
    config.quickPlay.defaultFloor = UserConfig::clamped_int(user, UserConfig::SettingId::QpDefaultFloor);

    config.autoplay.mode = Runtime::autoplay_mode_from_index(UserConfig::clamped_int(user, UserConfig::SettingId::AutoplayMode));
    config.autoplay.pps = UserConfig::clamped_real(user, UserConfig::SettingId::AutoplayPps);
    config.autoplay.searchWidth = UserConfig::clamped_int(user, UserConfig::SettingId::AutoplayWidth);
    config.autoplay.handlingSimulation = UserConfig::clamped_int(user, UserConfig::SettingId::AutoplayHandlingSimulation)!=0;
    config.display.squareStyle = UserConfig::clamped_int(user, UserConfig::SettingId::SquareStyle);
    config.display.obscuredColors = UserConfig::clamped_int(user, UserConfig::SettingId::ObscuredColors) != 0;
    config.display.showSpawnPosition = UserConfig::clamped_int(user, UserConfig::SettingId::ShowSpawnPosition) != 0;
    config.timeMultiplier = 1.0;
    config.simulationPps = UserConfig::clamped_real(user, UserConfig::SettingId::SimulationPps);
    if (!std::isfinite(config.simulationPps)) config.simulationPps = 0.0;
    config.turnPieces = UserConfig::clamped_real(user, UserConfig::SettingId::TurnPieces);
    if (!std::isfinite(config.turnPieces)) config.turnPieces = 0.0;
    if (config.turnPieces > 0.0) config.turnPieces = std::max(1e-9, config.turnPieces);

    if (config.garbage.mode == GarbageMode::QuickPlay) {
        // QuickPlay owns its floor-dependent incoming delay; the Garbage page
        // setting applies only to other modes.
        config.garbage.incomingDelay = seconds(5.0);
        // QP2 continuous entry uses the client's five-frame garbage ARE.
        config.garbage.riseInterval = enabled(config.quickPlay.expert) ? Duration{} : seconds(5.0/60.0);
        config.mode.fixedBlitzSpeed = false;
        config.field = FieldConfig{};
        config.field.holdEnabled = UserConfig::clamped_int(user, UserConfig::SettingId::HoldEnabled) != 0;
        config.handling.gravityInterval = GravityInterval::periodic(seconds(5.0 / 6.0));
        config.handling.epld = seconds(0.5);
        config.handling.lockResetLimit = 15;
        config.rotationSystem = RotationSystem::SrsPlus;
        // Shapes are shared with other modes; QP still owns hole timing/favor.
        if (std::abs(config.garbage.cheeseModel)==1)
            config.garbage.holePattern = Compat::hole_pattern_from_legacy(
                clamp(user.holeCount, -config.field.width, config.field.width));
        if (config.garbage.holePattern.extent()==0)
            config.garbage.holePattern=GarbageHolePattern::scattered(1);
        config.garbage.singleMessiness = 0.0;
        config.garbage.attackMultiplier = 1.0;
        config.garbage.sendMultiplier = 1.0;
        config.garbage.avoidSameColumn = false;
        config.garbage.cap = 8;
        config.garbage.survivalApm = 0.0;
    }

    if (enabled(config.quickPlay.noHold)) {
        config.field.holdEnabled = false;
        if (reverse(config.quickPlay.noHold)) {
            config.randomPieces = true;
            config.bagRandomizer = BagRandomizer::TotalMayhem;
            config.field.nextCount = 1;
            config.field.ghost = false;
            config.garbage.holePattern = GarbageHolePattern::contiguous(2);
            config.garbage.cheeseModel = config.garbage.cheeseModel < 0 ? -1 : 1;
        }
    }
    if (enabled(config.quickPlay.allSpin)) config.spinBonuses = SpinBonuses::AllSpinPlus;
    if (config.quickPlay.gravity == ModifierMode::Enabled) config.handling.gravityInterval = GravityInterval::periodic(seconds(1.0 / 30.0));
    if (reverse(config.quickPlay.gravity)) {
        config.handling.gravityInterval = GravityInterval::instant();
        config.handling.epld = seconds(0.4);
    }
    if (enabled(config.quickPlay.invisible)) config.field.invisible = true;
    if (enabled(config.quickPlay.doubleHole)) {
        config.garbage.layerHeight = 4;
        config.garbage.cheeseModel = config.garbage.cheeseModel < 0 ? -1 : 1;
        if (!reverse(config.quickPlay.noHold))
            config.garbage.holePattern = GarbageHolePattern::scattered(1);
    }
    if (reverse(config.quickPlay.noHold) || enabled(config.quickPlay.doubleHole))
        config.garbage.cheeseMessiness = UserConfig::clamped_real(user, UserConfig::SettingId::CheeseMessiness);
    if (reverse(config.quickPlay.volatileMode)) config.field.height = static_cast<int>(config.field.height * 0.7);
    if (reverse(config.quickPlay.messy)) config.field.lineClearDelay = seconds(70.0 / 60.0);

    switch (config.quickPlay.fool) {
    case FoolMode::Pentomino:
        config.rotationSystem = RotationSystem::Pentomino;
        break;
    case FoolMode::Hexomino:
        config.rotationSystem = RotationSystem::Hexomino;
        break;
    case FoolMode::AprilFools2026:
        config.rotationSystem = RotationSystem::SrsPlus;
        config.field.height = static_cast<int>(config.field.height * 1.2);
        break;
    case FoolMode::ReverseAprilFools2026:
        config.rotationSystem = RotationSystem::SrsPlus;
        break;
    case FoolMode::Disabled:
        break;
    }
    if (config.quickPlay.fool != FoolMode::Disabled && config.quickPlay.allSpin == AllSpinMode::Extended) {
        config.quickPlay.allSpin = AllSpinMode::Reverse;
    }

    // Snowball owns the board dimensions, including in combinations with
    // Last Stand and event pieces. Other modifiers keep their handling rules.
    if (enabled(config.quickPlay.snowball)) {
        config.field.width = 4;
        config.field.height = 4;
        config.garbage.layerHeight = std::min(config.garbage.layerHeight, 4);
        if (reverse(config.quickPlay.snowball)) {
            config.handling.das = Duration{};
            config.handling.arr = Duration{};
        }
    }

    if (user.instantGravity != 0 ||
        (config.handling.gravityInterval.enabled() &&
         config.handling.gravityInterval.interval() <= seconds(1e-7))) {
        config.handling.gravityInterval = GravityInterval::instant();
    }

    config.field.infiniteHold = UserConfig::clamped_int(user, UserConfig::SettingId::InfiniteHold) != 0;
    config.field.holdCount = UserConfig::clamped_int(user, UserConfig::SettingId::HoldCount);
    return config;
}

} // namespace Runtime
