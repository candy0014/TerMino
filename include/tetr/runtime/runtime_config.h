#ifndef TETR_RUNTIME_RUNTIME_CONFIG_H
#define TETR_RUNTIME_RUNTIME_CONFIG_H
#include "tetr/runtime/map_definition.h"
#include <memory>

#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/garbage_hole_pattern.h"
#include "tetr/runtime/gravity.h"
#include "tetr/runtime/gravity_progression.h"
#include "tetr/runtime/time.h"
#include "tetr/runtime/movement_restrictions.h"
#include "tetr/runtime/spin_bonuses.h"
#include "tetr/runtime/bag_randomizer.h"
#include "tetr/runtime/attack_rules.h"
#include "tetr/runtime/challenge_rules.h"

namespace Runtime {

struct ModeConfig {
    GameMode mode=GameMode::Zen;
    bool replayMode=false;
    bool fixedBlitzSpeed=false;
    int racingDistance=40;
    MapGoal mapGoal=MapGoal::Disabled;
    Duration blitzTime=Duration::from_seconds(120.0);
    GameGoal goal=GameGoal::None;
    int garbageTarget=40;
    int pieceTarget=100;
    int attackTarget=100;
    int allClearTarget=1;
    bool legacyBlitzScoring=false;
};
struct HandlingConfig {
    GravityInterval gravityInterval=GravityInterval::periodic(Duration::from_seconds(0.6));
    GravityProgression gravityProgression=GravityProgression::Fixed;
    int gravityLevelLines=10;
    double gravityAcceleration=1.0;
    double softDropFactor=0.0;
    bool softDropFixed=false;
    Duration das=Duration::from_seconds(0.1);
    Duration arr=Duration::from_seconds(0.01);
    Duration dcd{};
    bool preMove=true;
    bool dasPrecharge=true;
    Duration epld=Duration::from_seconds(0.5);
    int lockResetLimit=15;
    bool infiniteMovement=false;
};
struct FieldConfig {
    int width=10;
    int height=20;
    int nextCount=5;
    bool holdEnabled=true;
    bool invisible=false;
    bool periodicInvisible=false;
    Duration invisibilityDelay{};
    Duration invisibilityInterval=Duration::from_seconds(5);
    Duration invisibilityDuration=Duration::from_seconds(1);
    bool antiGravity=false;
    bool ghost=true;
    Duration lineClearDelay{};
    bool infiniteHold=false;
    int holdCount=1;
    Duration entryDelay{};
};
inline bool hold_available(const FieldConfig& field, int used = 0) noexcept {
    return field.holdEnabled && (field.infiniteHold || used < field.holdCount);
}
struct GarbageConfig {
    GarbageMode mode=GarbageMode::None;
    int cheeseModel=1;
    GarbageHolePattern holePattern{};
    int layerHeight=6;
    Duration generationInterval=Duration::from_seconds(3.0);
    double cheeseMessiness=0.8;
    double singleMessiness=0.0;
    double attackMultiplier=1.0;
    double survivalApm=60.0;
    Duration incomingDelay=Duration::from_seconds(0.3);
    GarbageEntryMode entryMode=GarbageEntryMode::AfterNonClear;
    Duration riseInterval{};
    double sendMultiplier=1.0;
    double hitMultiplier=1.0;
    double cancelMultiplier=1.0;
    bool legacyIncomingRounding=false;
    bool avoidSameColumn=false;
    int cap=8;
    int openerPhase=14;
    bool legacyOpenerPhase=false;
    bool legacyShapeRules=false;
};
struct QuickPlayConfig {
    ModifierMode expert=ModifierMode::Disabled;
    ModifierMode noHold=ModifierMode::Disabled;
    ModifierMode messy=ModifierMode::Disabled;
    ModifierMode gravity=ModifierMode::Disabled;
    ModifierMode volatileMode=ModifierMode::Disabled;
    ModifierMode doubleHole=ModifierMode::Disabled;
    ModifierMode invisible=ModifierMode::Disabled;
    AllSpinMode allSpin=AllSpinMode::Disabled;
    ModifierMode backfire=ModifierMode::Disabled;
    ModifierMode snakeEyes=ModifierMode::Disabled;
    FoolMode fool=FoolMode::Disabled;
    int defaultFloor=1;
    bool continuousCancellation=false;
    ModifierMode snowball=ModifierMode::Disabled;
};
struct AutoplayConfig {
    AutoplayMode mode=AutoplayMode::Disabled;
    double pps=2.5;
    int searchWidth=300;
    bool handlingSimulation=false;
};
struct DisplayConfig {
    int squareStyle=0;
    bool obscuredColors=false;
    bool showSpawnPosition=false;
};
struct RuntimeConfig {
    ChallengeRules challengeRules=ChallengeRules::None;
    std::shared_ptr<const MapDefinition> initialMap;
    int mapRotationSystem=-1;
    ModeConfig mode;
    HandlingConfig handling;
    FieldConfig field;
    GarbageConfig garbage;
    QuickPlayConfig quickPlay;
    AutoplayConfig autoplay;
    DisplayConfig display;
    RotationSystem rotationSystem=RotationSystem::SrsPlus;
    bool randomPieces=false;
    double timeMultiplier=1.0;
    // Zero uses wall time; otherwise each completed piece advances 1/PPS.
    double simulationPps=0.0;
    double turnPieces=0.0;
    MovementRestrictions movement;
    bool disableUndo=false;
    bool disablePause=false;
    SpinBonuses spinBonuses=SpinBonuses::AllMiniPlus;
    bool legacySpinScoring=false; // Compatibility for recordings without this setting.
    bool legacyRotationRules=false;
    BagRandomizer bagRandomizer=BagRandomizer::SevenBag;
    bool legacyRandomizerRules=false;
    bool legacyFoolPieceSet=false; // Older recordings used all ten event floors.
    AttackRules attackRules;
};

inline BagRandomizer effective_bag_randomizer(const RuntimeConfig& config) noexcept {
    return config.bagRandomizer==BagRandomizer::SevenBag && config.randomPieces
        ? BagRandomizer::TotalMayhem : config.bagRandomizer;
}
inline GameGoal active_goal(const RuntimeConfig& config) noexcept {
    if(config.mode.mode==GameMode::Racing) {
        if(config.initialMap && config.mode.mapGoal==MapGoal::ClearInitial) return GameGoal::InitialCells;
        if(config.initialMap && config.mode.mapGoal==MapGoal::AllClear) return GameGoal::AllClear;
        return GameGoal::Lines;
    }
    if(config.mode.mode==GameMode::Blitz) return GameGoal::Time;
    if(config.mode.mode!=GameMode::Zen || (config.mode.goal==GameGoal::InitialCells && !config.initialMap)) return GameGoal::None;
    return config.mode.goal;
}
inline void redirect_legacy_mode(RuntimeConfig& config) {
    const auto encoded=static_cast<int>(config.mode.mode);
    const auto previous=encoded==-1 ? GameMode::Racing : encoded==-2 ? GameMode::Blitz : config.mode.mode;
    if(previous!=GameMode::Racing && previous!=GameMode::Blitz) return;
    config.mode.goal=previous==GameMode::Racing ? GameGoal::Lines : GameGoal::Time;
    if(encoded==-2) config.mode.fixedBlitzSpeed=true;
    config.mode.mode=GameMode::Zen;
    if(previous==GameMode::Blitz && !config.mode.fixedBlitzSpeed) {
        config.handling.gravityProgression=GravityProgression::Blitz;
        config.mode.legacyBlitzScoring=true;
    }
}
inline int effective_lock_reset_limit(const HandlingConfig& config) noexcept {
    return config.infiniteMovement ? -1 : config.lockResetLimit;
}


} // namespace Runtime

#endif
