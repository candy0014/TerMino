#ifndef TETR_CONFIG_SETTINGS_SCHEMA_H
#define TETR_CONFIG_SETTINGS_SCHEMA_H

#include "tetr/config/user_config.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace UserConfig {

enum class Section {
    Global,
    Model,
    Garbage,
    QuickPlay,
    Handling,
    Game,
    Autoplay,
    Controls,
    Network,
    Others,
    Maps
};

enum class SettingId {
    ServerIp,
    GameMode, RacingDistance, BlitzTime, ReplayPath, ReplayTimeMultiplier,
    GarbageMode, CheesePattern, HoleCount, LayerHeight, GarbageInterval,
    CheeseMessiness, GarbageMultiplier, HitMultiplier = GarbageMultiplier, SurvivalApm, GarbageInTime,
    QpExpert, QpNoHold, QpMessy, QpGravity, QpVolatile, QpDoubleHole,
    QpInvisible, QpAllSpin, QpBackfire, QpSnakeEyes, QpSnowball, QpFool, QpDefaultFloor,
    GravityInterval, SoftDropFactor, SoftDropFixed, Das, Arr, Dcd,
    FieldWidth, FieldHeight, NextCount, HoldEnabled, FieldInvisible, AntiGravity,
    Ghost, LockDelay, LockResetLimit, RotationSystem, RandomPieces,
    InstantGravity, LineClearDelay, SimulationPps, TurnPieces,
    AutoplayMode, AutoplayPps, AutoplayWidth,
    ControlLeft, ControlRight, ControlSoftDrop, ControlHardDrop,
    ControlRotateClockwise, ControlRotateCounterclockwise, ControlRotate180,
    ControlHold, ControlRetry, ControlOpenSettings, ControlReplay, ControlUndo,
    SquareStyle, ObscuredColors, UiLanguage,
    DisableLeft, DisableRight, DisableClockwise, DisableCounterclockwise,
    Disable180, DisableSoftDrop, DisableHardDrop,
    InfiniteHold, HoldCount, CheeseDensity, SpinBonuses,
    InfiniteMovement, EntryDelay, OutgoingGarbageMultiplier,
    CancellationMultiplier = OutgoingGarbageMultiplier, MessinessWithinAttack,
    AvoidSameColumn, GarbageCap, OpenerPhase, DisableUndo, GarbageEntryMode,
    DisablePause, ControlPause, PreMove, DasPrecharge,
    B2BReward, B2BCharging, B2BChargeThreshold, B2BChargeReduction,
    PerfectClearEnabled, PerfectClearAttack, PerfectClearCharge,
    HalfClearEnabled, HalfClearAttack, HalfClearCharge, GarbageSpecialAttack,
    B2BRewardAmount, ShowSpawnPosition, GravityProgression, GravityLevelLines, GravityAcceleration,
    AutoplayHandlingSimulation, MapId, MapRotationSystem, MapGoal, ContinueInputUnfocused,
    GarbageRiseInterval, Username,
    ForceSdf, ForcedSdf, ForcedSdfFixed, ForceDas, ForcedDas, ForceArr, ForcedArr,
    ForceDcd, ForcedDcd, ForcePreMove, ForcedPreMove, ForceDasPrecharge, ForcedDasPrecharge,
    AttackMultiplier,
    Goal, GarbageTarget, PieceTarget, AttackTarget, AllClearTarget,
    InvisibilityDelay, InvisibilityInterval, InvisibilityDuration, ChallengeRules,
    EffectsIntensity, FieldShake, ParticleIntensity, LandingEffect, LineClearEffect, HardDropTrail, BoardBounciness
};

enum class ChoiceKind {
    None,
    Goal,
    MapGoal,
    GameMode,
    GarbageMode,
    Modifier,
    AllSpin,
    Fool,
    Boolean,
    RotationSystem,
    Autoplay,
    SquareStyle,
    Language,
    SpinBonuses,
    BagRandomizer,
    GarbageEntryMode,
    B2BReward,
    GravityProgression,
    Invisibility, ChallengeRules
};

using SettingMember = std::variant<
    std::monostate,
    int Settings::*,
    double Settings::*,
    std::string Settings::*>;

using SettingReference = std::variant<
    std::reference_wrapper<int>,
    std::reference_wrapper<double>,
    std::reference_wrapper<std::string>>;

struct NumericRange {
    double minimum = 0.0;
    double maximum = 0.0;
    bool enabled = false;
};

struct SettingDescriptor {
    SettingId id;
    Section section;
    std::string_view diskName;
    std::string_view label;
    SettingMember member;
    ChoiceKind choice = ChoiceKind::None;
    NumericRange range{};
    bool cycleImmediately = false;
    bool keyBinding = false;
    bool squarePreview = false;
    std::size_t controlIndex = Settings::kControlCount;
};

struct ChoiceOptions {
    std::vector<std::string> labels;
    std::vector<int> values;
};

const std::vector<SettingDescriptor>& setting_schema();
const SettingDescriptor& setting_descriptor(SettingId id);
const SettingDescriptor* find_setting(Section section, std::string_view diskName);
std::string_view section_name(Section section) noexcept;
Section parse_section_name(std::string_view name, Section fallback) noexcept;
SettingReference bind_setting(Settings& settings, const SettingDescriptor& descriptor);
bool assign_setting_text(Settings& settings, const SettingDescriptor& descriptor, std::string_view text);
bool assign_setting_text(Settings& settings, SettingId id, std::string_view text);
ChoiceOptions choice_options(ChoiceKind kind);
int clamped_int(const Settings& settings, SettingId id);
double clamped_real(const Settings& settings, SettingId id);
void reset_settings(Settings& settings, bool playerTwo = false);
void reset_section(Settings& settings, Section section, bool playerTwo = false);

struct ForcedHandlingSetting { SettingId personal, enabled, value; };
const std::vector<ForcedHandlingSetting>& forced_handling_settings();
// Resolve mechanism overrides without changing the player's personal values.
Settings effective_handling_settings(const Settings& settings);
SettingId personal_handling_id(SettingId id);

} // namespace UserConfig

#endif
