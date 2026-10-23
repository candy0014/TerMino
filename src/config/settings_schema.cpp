#include "tetr/challenges/challenges.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"

#include "tetr/runtime/game_modes.h"
#include "tetr/runtime/spin_bonuses.h"
#include "tetr/runtime/rotation_systems.h"
#include "tetr/runtime/bag_randomizer.h"
#include "tetr/compat/legacy_text.h"
#include "tetr/util/text_codec.h"
#include "tetr/util/numeric.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace UserConfig {
void migrate_legacy_modes(Settings& settings) {
    const bool racing=settings.gameMode==1 || settings.gameMode==-1;
    const bool blitz=settings.gameMode==2 || settings.gameMode==-2;
    if(!racing && !blitz) return;
    settings.goal=racing ? 2 : 1;
    if(racing && settings.mapId && settings.mapGoal) settings.goal=settings.mapGoal==1 ? 7 : 6;
    if(blitz && settings.gameMode==2) settings.gravityProgression=3;
    settings.gameMode=0;
}
const std::vector<ForcedHandlingSetting>& forced_handling_settings() {
    using Id=SettingId;
    static const std::vector<ForcedHandlingSetting> entries={
        {Id::SoftDropFactor,Id::ForceSdf,Id::ForcedSdf}, {Id::Das,Id::ForceDas,Id::ForcedDas},
        {Id::Arr,Id::ForceArr,Id::ForcedArr}, {Id::Dcd,Id::ForceDcd,Id::ForcedDcd},
        {Id::PreMove,Id::ForcePreMove,Id::ForcedPreMove},
        {Id::DasPrecharge,Id::ForceDasPrecharge,Id::ForcedDasPrecharge}};
    return entries;
}
SettingId personal_handling_id(SettingId id) {
    for (const auto& entry:forced_handling_settings()) if(entry.value==id) return entry.personal;
    return id;
}
Settings effective_handling_settings(const Settings& settings) {
    auto result=settings;
    for(const auto& entry:forced_handling_settings()) if(clamped_int(settings,entry.enabled)) {
        const auto& target=setting_descriptor(entry.personal);
        const auto& value=setting_descriptor(entry.value);
        if(const auto member=std::get_if<int Settings::*>(&target.member))
            result.**member=clamped_int(settings,value.id);
        else result.*std::get<double Settings::*>(target.member)=clamped_real(settings,value.id);
        if(entry.personal==SettingId::SoftDropFactor) result.softDropFixed=clamped_int(settings,SettingId::ForcedSdfFixed);
    }
    return result;
}
QuickPlayModeSnapshot snapshot_quick_play_mods(const Settings& s) {
    return {s.gameMode,{s.qpExpert,s.qpNoHold,s.qpMessy,s.qpGravity,s.qpVolatile,
        s.qpDoubleHole,s.qpInvisible,s.qpAllSpin,s.qpBackfire,s.qpSnakeEyes,
        s.qpSnowball,s.qpFool,s.qpDefaultFloor},s.quickPlayModBackup};
}
void reconcile_quick_play_mods(Settings& s,const QuickPlayModeSnapshot& before) {
    const auto qp=[](int mode) { return mode==static_cast<int>(Runtime::GameMode::QuickPlay) || mode==-static_cast<int>(Runtime::GameMode::QuickPlay); };
    if (!qp(before.mode) && qp(s.gameMode)) {
        s.quickPlayModBackup=before.values;
    } else if (qp(before.mode) && !qp(s.gameMode) && before.backup) {
        const auto values=*before.backup;
        const std::array<int Settings::*,13> fields{{&Settings::qpExpert,&Settings::qpNoHold,&Settings::qpMessy,
            &Settings::qpGravity,&Settings::qpVolatile,&Settings::qpDoubleHole,&Settings::qpInvisible,
            &Settings::qpAllSpin,&Settings::qpBackfire,&Settings::qpSnakeEyes,&Settings::qpSnowball,
            &Settings::qpFool,&Settings::qpDefaultFloor}};
        for (std::size_t i=0;i<fields.size();++i) s.*fields[i]=values[i];
        s.quickPlayModBackup.reset();
    }
}
namespace {

constexpr NumericRange no_range() { return {}; }
constexpr NumericRange range(double minimum, double maximum) {
    return {minimum, maximum, true};
}

#define INT_SETTING(ID, SECTION, DISK, LABEL, MEMBER, CHOICE, MIN, MAX, CYCLE) \
    SettingDescriptor{SettingId::ID, Section::SECTION, DISK, LABEL, &Settings::MEMBER, ChoiceKind::CHOICE, range(MIN, MAX), CYCLE}
#define INT_SETTING_NR(ID, SECTION, DISK, LABEL, MEMBER, CHOICE, CYCLE) \
    SettingDescriptor{SettingId::ID, Section::SECTION, DISK, LABEL, &Settings::MEMBER, ChoiceKind::CHOICE, no_range(), CYCLE}
#define REAL_SETTING(ID, SECTION, DISK, LABEL, MEMBER, MIN, MAX) \
    SettingDescriptor{SettingId::ID, Section::SECTION, DISK, LABEL, &Settings::MEMBER, ChoiceKind::None, range(MIN, MAX)}
#define REAL_SETTING_NR(ID, SECTION, DISK, LABEL, MEMBER) \
    SettingDescriptor{SettingId::ID, Section::SECTION, DISK, LABEL, &Settings::MEMBER}
#define TEXT_SETTING(ID, SECTION, DISK, LABEL, MEMBER) \
    SettingDescriptor{SettingId::ID, Section::SECTION, DISK, LABEL, &Settings::MEMBER}

SettingDescriptor control(SettingId id, std::string_view label, std::size_t index) {
    SettingDescriptor descriptor{id, Section::Controls, {}, label, std::monostate{}};
    descriptor.keyBinding = true;
    descriptor.controlIndex = index;
    return descriptor;
}

std::vector<int> sequential_values(std::size_t count) {
    std::vector<int> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) values.push_back(static_cast<int>(index));
    return values;
}

} // namespace

const std::vector<SettingDescriptor>& setting_schema() {
    static const std::vector<SettingDescriptor> schema = {
        INT_SETTING(GameMode, Model, "Model", "Model", gameMode, GameMode, -6, 6, false),
        INT_SETTING(ChallengeRules, Game, "Challenge rules", "Challenge rules", challengeRules, ChallengeRules, 0, 15, false),
        INT_SETTING(RacingDistance, Model, "Racing Distance", "Line count", racingDistance, None, 1, 1000000000, false),
        REAL_SETTING(BlitzTime, Model, "Blitz Time", "Time limit", blitzTime, 0.0, 1e18),
        INT_SETTING(Goal, Model, "Goal", "Goal", goal, Goal, 0, 7, false),
        INT_SETTING(GarbageTarget, Model, "Garbage Target", "Garbage lines to clear", garbageTarget, None, 1, 1000000000, false),
        INT_SETTING(PieceTarget, Model, "Piece Target", "Piece count", pieceTarget, None, 1, 1000000000, false),
        INT_SETTING(AttackTarget, Model, "Attack Target", "Attack count", attackTarget, None, 1, 1000000000, false),
        INT_SETTING(AllClearTarget, Model, "All Clear Target", "All clear count", allClearTarget, None, 1, 1000000000, false),
        TEXT_SETTING(ReplayPath, Model, "Replay", "Replay", replayPath),
        REAL_SETTING(ReplayTimeMultiplier, Model, "Time Multiplier", "Time Multiplier", replayTimeMultiplier, 0.0, 1e18),

        INT_SETTING(GarbageMode, Garbage, "Garbage Model", "Garbage Model", garbageMode, GarbageMode, 0, 5, false),
        INT_SETTING(CheesePattern, Garbage, "Cheese Model", "Garbage shape", cheesePattern, None, -3, 3, false),
        INT_SETTING_NR(HoleCount, Garbage, "Num of Hole", "Num of Hole", holeCount, None, false),
        INT_SETTING_NR(LayerHeight, Garbage, "Layer Height", "Layer Height", layerHeight, None, false),
        REAL_SETTING(GarbageInterval, Garbage, "Time Interval", "Time Interval", garbageInterval, 0.0, 1e18),
        REAL_SETTING(CheeseMessiness, Garbage, "Cheese Messiness", "Cheese Messiness", cheeseMessiness, 0.0, 1.0),
        REAL_SETTING(CheeseDensity, Garbage, "Cheese Density", "Density", cheeseDensity, 0.0, 1.0),
        REAL_SETTING(HitMultiplier, Garbage, "Hit Multiplier", "Hit multiplier", hitMultiplier, 0.0, 1000.0),
        REAL_SETTING(SurvivalApm, Garbage, "APM for Survival", "APM for Survival", survivalApm, 0.0, 1800.0),
        REAL_SETTING(GarbageInTime, Garbage, "GarbageInTime", "Buffer time (seconds)", garbageInTime, 0.0, 1e18),
        INT_SETTING(GarbageEntryMode, Garbage, "Garbage Entry Mode", "Garbage entry mode", garbageEntryMode, GarbageEntryMode, 0, 2, true),
        REAL_SETTING(GarbageRiseInterval, Garbage, "Garbage Rise Interval", "Garbage rise speed (seconds)", garbageRiseInterval, 0.0, 1e18),

        INT_SETTING(QpExpert, QuickPlay, "Expert", "Expert", qpExpert, Modifier, 0, 2, true),
        INT_SETTING(QpNoHold, QuickPlay, "Nohold", "Nohold", qpNoHold, Modifier, 0, 2, true),
        INT_SETTING(QpMessy, QuickPlay, "Messy", "Messy", qpMessy, Modifier, 0, 2, true),
        INT_SETTING(QpGravity, QuickPlay, "Gravity", "Gravity", qpGravity, Modifier, 0, 2, true),
        INT_SETTING(QpVolatile, QuickPlay, "Volatile", "Volatile", qpVolatile, Modifier, 0, 2, true),
        INT_SETTING(QpDoubleHole, QuickPlay, "Double hole", "Double hole", qpDoubleHole, Modifier, 0, 2, true),
        INT_SETTING(QpInvisible, QuickPlay, "Invisible", "Invisible", qpInvisible, Modifier, 0, 2, true),
        INT_SETTING(QpAllSpin, QuickPlay, "Allspin", "Allspin", qpAllSpin, AllSpin, 0, 3, true),
        INT_SETTING(QpBackfire, QuickPlay, "Backfire", "Backfire", qpBackfire, Modifier, 0, 2, true),
        INT_SETTING(QpSnakeEyes, QuickPlay, "Snake eyes", "Snake eyes", qpSnakeEyes, Modifier, 0, 2, true),
        INT_SETTING(QpSnowball, QuickPlay, "Snowball", "Snowball", qpSnowball, Modifier, 0, 2, true),
        INT_SETTING(QpFool, QuickPlay, "Fool", "Fool", qpFool, Fool, 0, 4, true),
        INT_SETTING(QpDefaultFloor, QuickPlay, "Default floor", "Default floor", qpDefaultFloor, None, 1, 10, false),

        INT_SETTING(ForceSdf, Game, "Force SDF", "Force SDF", forceSdf, Boolean, 0, 1, true),
        REAL_SETTING(ForcedSdf, Game, "Forced SDF", "SDF", forcedSdf, 0.0, 1e18),
        INT_SETTING(ForcedSdfFixed, Game, "Forced SDF Fixed", "SDF Fixed", forcedSdfFixed, Boolean, 0, 1, false),
        INT_SETTING(ForceDas, Game, "Force DAS", "Force DAS", forceDas, Boolean, 0, 1, true),
        REAL_SETTING(ForcedDas, Game, "Forced DAS", "DAS", forcedDas, 0.0, 1e18),
        INT_SETTING(ForceArr, Game, "Force ARR", "Force ARR", forceArr, Boolean, 0, 1, true),
        REAL_SETTING(ForcedArr, Game, "Forced ARR", "ARR", forcedArr, 0.0, 1e18),
        INT_SETTING(ForceDcd, Game, "Force DCD", "Force DCD", forceDcd, Boolean, 0, 1, true),
        REAL_SETTING(ForcedDcd, Game, "Forced DCD", "DCD", forcedDcd, 0.0, 1e18),
        INT_SETTING(ForcePreMove, Game, "Force Pre-move", "Force Pre-move", forcePreMove, Boolean, 0, 1, true),
        INT_SETTING(ForcedPreMove, Game, "Forced Pre-move", "Pre-move", forcedPreMove, Boolean, 0, 1, true),
        INT_SETTING(ForceDasPrecharge, Game, "Force DAS Precharge", "Force DAS Precharge", forceDasPrecharge, Boolean, 0, 1, true),
        INT_SETTING(ForcedDasPrecharge, Game, "Forced DAS Precharge", "DAS Precharge", forcedDasPrecharge, Boolean, 0, 1, true),

        REAL_SETTING_NR(GravityInterval, Handling, "Speed", "Speed", gravityInterval),
        REAL_SETTING(SoftDropFactor, Handling, "SDF", "SDF", softDropFactor, 0.0, 1e18),
        INT_SETTING(SoftDropFixed, Handling, "SDF Fixed", "SDF Fixed", softDropFixed, Boolean, 0, 1, false),
        REAL_SETTING(Das, Handling, "DAS", "DAS", das, 0.0, 1e18),
        REAL_SETTING(Arr, Handling, "ARR", "ARR", arr, 0.0, 1e18),
        REAL_SETTING(Dcd, Handling, "DCD", "DCD", dcd, 0.0, 1e18),
        INT_SETTING(PreMove, Handling, "Pre-move", "Pre-move", preMove, Boolean, 0, 1, true),
        INT_SETTING(DasPrecharge, Handling, "DAS Precharge", "DAS Precharge", dasPrecharge, Boolean, 0, 1, true),

        INT_SETTING(FieldWidth, Game, "Width", "Width", fieldWidth, None, 4, 100, false),
        INT_SETTING(FieldHeight, Game, "Height", "Height", fieldHeight, None, 0, 100, false),
        INT_SETTING(NextCount, Game, "Num of Next", "Num of Next", nextCount, None, 0, 100, false),
        INT_SETTING(HoldEnabled, Game, "Open Hold", "Open Hold", holdEnabled, Boolean, 0, 1, true),
        INT_SETTING(FieldInvisible, Game, "Invisible", "Invisible", fieldInvisible, Invisibility, 0, 2, true),
        REAL_SETTING(InvisibilityDelay, Game, "Invisibility Delay", "Invisibility delay (seconds)", invisibilityDelay, 0.0, 1e18),
        REAL_SETTING(InvisibilityInterval, Game, "Invisibility Interval", "Flash interval (seconds)", invisibilityInterval, 0.0, 1e18),
        REAL_SETTING(InvisibilityDuration, Game, "Invisibility Duration", "Flash duration (seconds)", invisibilityDuration, 0.0, 1e18),
        INT_SETTING(AntiGravity, Game, "Anti Gravity", "Anti Gravity", antiGravity, Boolean, 0, 1, true),
        INT_SETTING(Ghost, Game, "Ghost", "Ghost", ghost, Boolean, 0, 1, true),
        REAL_SETTING(LockDelay, Game, "EPLD", "EPLD", lockDelay, 0.0, 1e18),
        INT_SETTING(LockResetLimit, Game, "Limit of EPLD", "Limit of EPLD", lockResetLimit, None, 0, 1000000000, false),
        INT_SETTING(RotationSystem, Game, "Rotation System", "Rotation System", rotationSystem, RotationSystem, 0, 11, false),
        INT_SETTING(SpinBonuses, Game, "Spin Bonuses", "Spin bonuses", spinBonuses, SpinBonuses, 0, 9, false),
        REAL_SETTING(AttackMultiplier, Game, "Attack Multiplier", "Attack multiplier", attackMultiplier, 0.0, 1000.0),
        INT_SETTING(RandomPieces, Game, "Random", "Random rules", randomPieces, BagRandomizer, 0, 9, false),
        INT_SETTING(InstantGravity, Game, "20G", "20G", instantGravity, Boolean, 0, 1, true),
        REAL_SETTING(LineClearDelay, Game, "Line Clear Delay", "Line Clear Delay", lineClearDelay, 0.0, 1e18),
        INT_SETTING(InfiniteMovement, Game, "Infinite Movement", "Infinite movement", infiniteMovement, Boolean, 0, 1, true),
        REAL_SETTING(EntryDelay, Game, "Entry Delay", "Entry delay", entryDelay, 0.0, 1e18),
        REAL_SETTING(CancellationMultiplier, Game, "Cancellation Multiplier", "Cancellation multiplier", cancelMultiplier, 0.0, 1000.0),
        REAL_SETTING(MessinessWithinAttack, Game, "Messiness Within Attack", "Messiness within attack", messinessWithinAttack, 0.0, 1.0),
        INT_SETTING(AvoidSameColumn, Game, "Avoid Same Column RNG", "Avoid same column RNG", avoidSameColumn, Boolean, 0, 1, true),
        INT_SETTING(GarbageCap, Game, "Garbage Cap", "Garbage cap", garbageCap, None, 0, 1000000, false),
        INT_SETTING(OpenerPhase, Game, "Opener Phase", "Opener phase", openerPhase, None, 0, 1000000, false),
        REAL_SETTING(SimulationPps, Game, "Simulation PPS", "Simulation PPS", simulationPps, 0.0, 1000000.0),
        REAL_SETTING(TurnPieces, Game, "Turn Pieces", "Turn Pieces", turnPieces, 0.0, 1000000.0),
        INT_SETTING(DisableLeft, Game, "Disable Left", "Move Piece Left", disableLeft, Boolean, 0, 1, true),
        INT_SETTING(DisableRight, Game, "Disable Right", "Move Piece Right", disableRight, Boolean, 0, 1, true),
        INT_SETTING(DisableClockwise, Game, "Disable Clockwise", "Rotate Clockwise", disableClockwise, Boolean, 0, 1, true),
        INT_SETTING(DisableCounterclockwise, Game, "Disable Counterclockwise", "Rotate Counterclockwise", disableCounterclockwise, Boolean, 0, 1, true),
        INT_SETTING(Disable180, Game, "Disable 180", "Rotate 180", disable180, Boolean, 0, 1, true),
        INT_SETTING(DisableSoftDrop, Game, "Disable Soft Drop", "Soft Drop", disableSoftDrop, Boolean, 0, 1, true),
        INT_SETTING(DisableHardDrop, Game, "Disable Hard Drop", "Hard Drop", disableHardDrop, Boolean, 0, 1, true),
        INT_SETTING(DisableUndo, Game, "Disable Undo", "Disable Undo", disableUndo, Boolean, 0, 1, true),
        INT_SETTING(DisablePause, Game, "Disable Pause", "Disable Pause", disablePause, Boolean, 0, 1, true),
        INT_SETTING(InfiniteHold, Game, "Infinite Hold", "Infinite Hold", infiniteHold, Boolean, 0, 1, true),
        INT_SETTING(HoldCount, Game, "Hold Count", "Hold count", holdCount, None, 0, 1000000000, false),

        // Keep the historical mode key so existing profiles retain their choice.
        INT_SETTING(B2BReward, Game, "B2B Reward", "B2B reward mode", b2bReward, B2BReward, 0, 2, true),
        INT_SETTING(B2BRewardAmount, Game, "B2B Reward Amount", "B2B attack bonus", b2bRewardAmount, None, 0, 1000000, false),
        INT_SETTING(B2BCharging, Game, "B2B Charging", "B2B charging", b2bCharging, Boolean, 0, 1, true),
        INT_SETTING(B2BChargeThreshold, Game, "B2B Charge Threshold", "B2B charge threshold", b2bChargeThreshold, None, 0, 1000000, false),
        INT_SETTING(B2BChargeReduction, Game, "B2B Charge Reduction", "B2B charge reduction", b2bChargeReduction, None, -1000000, 1000000, false),
        INT_SETTING(PerfectClearEnabled, Game, "Enable Perfect Clear", "Enable perfect clear", perfectClearEnabled, Boolean, 0, 1, true),
        INT_SETTING(PerfectClearAttack, Game, "Perfect Clear Attack", "Perfect clear attack", perfectClearAttack, None, 0, 1000000, false),
        INT_SETTING(PerfectClearCharge, Game, "Perfect Clear Charge", "Perfect clear B2B charge", perfectClearCharge, None, 0, 1000000, false),
        INT_SETTING(HalfClearEnabled, Game, "Enable Half Clear", "Enable half clear", halfClearEnabled, Boolean, 0, 1, true),
        INT_SETTING(HalfClearAttack, Game, "Half Clear Attack", "Half clear attack", halfClearAttack, None, 0, 1000000, false),
        INT_SETTING(HalfClearCharge, Game, "Half Clear Charge", "Half clear B2B charge", halfClearCharge, None, 0, 1000000, false),
        INT_SETTING(GarbageSpecialAttack, Game, "Garbage Special Attack", "Garbage special attack", garbageSpecialAttack, None, 0, 1000000, false),

        INT_SETTING(AutoplayMode, Autoplay, "Autoplay", "Bot", autoplayMode, Autoplay, 0, 10, false),
        REAL_SETTING(AutoplayPps, Autoplay, "PPS of Autoplay", "Bot PPS", autoplayPps, 0.0, 100.0),
        INT_SETTING(AutoplayWidth, Autoplay, "Width of Autoplay", "Search width", autoplayWidth, None, 0, 1000, false),
        INT_SETTING(AutoplayHandlingSimulation, Autoplay, "Handling Simulation", "Handling simulation", autoplayHandlingSimulation, Boolean, 0, 1, true),

        control(SettingId::ControlLeft, "Move Piece Left", 0),
        control(SettingId::ControlRight, "Move Piece Right", 1),
        control(SettingId::ControlSoftDrop, "Soft Drop", 2),
        control(SettingId::ControlHardDrop, "Hard Drop", 3),
        control(SettingId::ControlRotateClockwise, "Rotate Clockwise", 4),
        control(SettingId::ControlRotateCounterclockwise, "Rotate Counterclockwise", 5),
        control(SettingId::ControlRotate180, "Rotate 180", 6),
        control(SettingId::ControlHold, "Swap Hold Piece", 7),
        control(SettingId::ControlRetry, "Retry Game", 8),
        control(SettingId::ControlOpenSettings, "Open Config", 9),
        control(SettingId::ControlReplay, "Replay", 10),
        control(SettingId::ControlUndo, "Undo", 11),
        control(SettingId::ControlPause, "Pause game", 12),

        TEXT_SETTING(ServerIp, Network, "Server IP", "Server IP", serverIp),
        TEXT_SETTING(Username, Network, "Username", "Username", username),
        [] {
            SettingDescriptor descriptor = INT_SETTING(SquareStyle, Others, "Square Version", "Square Version", squareStyle, SquareStyle, 0, 1, true);
            descriptor.squarePreview = true;
            return descriptor;
        }(),
        INT_SETTING(ObscuredColors, Others, "ObscuredColors", "ObscuredColors", obscuredColors, Boolean, 0, 1, true),
        INT_SETTING(UiLanguage, Others, "UI Language", "Interface language", uiLanguage, Language, 0, 1, false),
        INT_SETTING(ShowSpawnPosition, Others, "Show Spawn Position", "Show spawn position", showSpawnPosition, Boolean, 0, 1, true),
        INT_SETTING(ContinueInputUnfocused, Others, "Continue Input Unfocused", "Continue input when unfocused", continueInputUnfocused, Boolean, 0, 1, true),
        INT_SETTING(EffectsIntensity, Others, "Effects Intensity", "Effects intensity (%)", effectsIntensity, None, 0, 200, false),
        INT_SETTING(BoardBounciness, Others, "Board Bounciness", "Board bounciness (%)", boardBounciness, None, 0, 200, false),
        INT_SETTING(FieldShake, Others, "Field Shake", "Field shake (%)", fieldShake, None, 0, 200, false),
        INT_SETTING(ParticleIntensity, Others, "Particle Intensity", "Particles (%)", particleIntensity, None, 0, 200, false),
        INT_SETTING(LandingEffect, Others, "Landing Effect", "Landing impact (%)", landingEffect, None, 0, 200, false),
        INT_SETTING(LineClearEffect, Others, "Line Clear Effect", "Line clear glow (%)", lineClearEffect, None, 0, 200, false),
        INT_SETTING(HardDropTrail, Others, "Hard Drop Trail", "Hard drop trail (%)", hardDropTrail, None, 0, 200, false),
        INT_SETTING(GravityProgression, Game, "Gravity Progression", "Gravity progression", gravityProgression, GravityProgression, 0, 3, true),
        INT_SETTING(GravityLevelLines, Game, "Gravity Level Lines", "Lines per level", gravityLevelLines, None, 1, 1000000, false),
        REAL_SETTING(GravityAcceleration, Game, "Gravity Acceleration", "Acceleration factor", gravityAcceleration, 0.01, 100.0),
        INT_SETTING(MapId, Maps, "Selected", "Selected map", mapId, None, 0, 2147483646, false),
        INT_SETTING(MapRotationSystem, Maps, "Rotation System", "Rotation System", mapRotationSystem, None, -1, 11, false),
        INT_SETTING(MapGoal, Model, "Map Mode", "Map mode", mapGoal, MapGoal, 0, 2, false)
    };
    return schema;
}

#undef INT_SETTING
#undef INT_SETTING_NR
#undef REAL_SETTING
#undef REAL_SETTING_NR
#undef TEXT_SETTING

const SettingDescriptor& setting_descriptor(SettingId id) {
    const auto& schema = setting_schema();
    const auto found = std::find_if(schema.begin(), schema.end(), [id](const SettingDescriptor& descriptor) {
        return descriptor.id == id;
    });
    if (found == schema.end()) throw std::logic_error("unknown setting id");
    return *found;
}

const SettingDescriptor* find_setting(Section section, std::string_view diskName) {
    const auto& schema = setting_schema();
    for (const auto& descriptor : schema) {
        if (!descriptor.diskName.empty() && descriptor.section == section && descriptor.diskName == diskName) {
            return &descriptor;
        }
    }

    const SettingDescriptor* first = nullptr;
    int matches = 0;
    for (const auto& descriptor : schema) {
        if (descriptor.diskName.empty() || descriptor.diskName != diskName) continue;
        if (!first) first = &descriptor;
        ++matches;
    }
    return matches == 1 || section == Section::Global ? first : nullptr;
}

std::string_view section_name(Section section) noexcept {
    switch (section) {
        case Section::Global: return {};
        case Section::Model: return "MODEL";
        case Section::Garbage: return "GARBAGE";
        case Section::QuickPlay: return "QUICK PLAY";
        case Section::Handling: return "HANDLING";
        case Section::Game: return "GAME";
        case Section::Autoplay: return "AUTOPLAY";
        case Section::Controls: return "CONTROLS";
        case Section::Network: return "NETWORK";
        case Section::Others: return "OTHERS";
        case Section::Maps: return "MAPS";
    }
    return {};
}

Section parse_section_name(std::string_view name, Section fallback) noexcept {
    for (Section section : {Section::Model, Section::Garbage, Section::QuickPlay, Section::Handling,
                            Section::Game, Section::Autoplay, Section::Controls, Section::Network, Section::Others, Section::Maps}) {
        if (section_name(section) == name) return section;
    }
    return fallback;
}

SettingReference bind_setting(Settings& settings, const SettingDescriptor& descriptor) {
    if (descriptor.keyBinding) {
        if (descriptor.controlIndex >= Settings::kControlCount) throw std::logic_error("invalid control index");
        return std::ref(settings.keyCodes[descriptor.controlIndex]);
    }
    return std::visit([&](auto member) -> SettingReference {
        using Member = decltype(member);
        if constexpr (std::is_same_v<Member, int Settings::*>) {
            return std::ref(settings.*member);
        } else if constexpr (std::is_same_v<Member, double Settings::*>) {
            return std::ref(settings.*member);
        } else if constexpr (std::is_same_v<Member, std::string Settings::*>) {
            return std::ref(settings.*member);
        } else {
            throw std::logic_error("setting has no value member");
        }
    }, descriptor.member);
}

bool assign_setting_text(Settings& settings, const SettingDescriptor& descriptor, std::string_view text) {
    if (descriptor.keyBinding || std::holds_alternative<std::monostate>(descriptor.member)) return false;
    const auto personalId=personal_handling_id(descriptor.id);
    if (personalId == SettingId::Das || personalId == SettingId::Arr ||
        personalId == SettingId::Dcd || personalId == SettingId::SoftDropFactor) {
        const auto trim = [](std::string_view value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            return first == std::string_view::npos ? std::string_view{}
                : value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
        };
        auto number = trim(text);
        char unit = 0;
        if (!number.empty() && (number.back()=='s' || number.back()=='S' ||
            number.back()=='f' || number.back()=='F')) {
            unit = number.back(); number = trim(number.substr(0,number.size()-1));
        }
        if (!number.empty() && number.front()=='+') number.remove_prefix(1);
        const bool clock=number.find(':')!=std::string_view::npos;
        if(clock && (unit=='f' || unit=='F')) return false;
        const auto parsed = clock?Util::parse_time(number):Util::Text::parse_finite_real(number);
        if (!parsed || *parsed < 0) return false;
        const double value = unit=='f' || unit=='F' ? *parsed/60.0 : *parsed;
        if (value > descriptor.range.maximum) return false;
        settings.*std::get<double Settings::*>(descriptor.member) = value;
        if (descriptor.id == SettingId::SoftDropFactor) settings.softDropFixed = unit != 0 || clock;
        if (descriptor.id == SettingId::ForcedSdf) settings.forcedSdfFixed = unit != 0 || clock;
        return true;
    }
    if (descriptor.id == SettingId::SimulationPps || descriptor.id == SettingId::TurnPieces ||
        descriptor.id == SettingId::HitMultiplier || descriptor.id == SettingId::CancellationMultiplier ||
        descriptor.id == SettingId::AttackMultiplier) {
        const auto value = Util::Text::parse_finite_real(text);
        if (!value) return false;
        settings.*std::get<double Settings::*>(descriptor.member) = *value;
        return true;
    }
    if (descriptor.id == SettingId::Username && (text.empty() || text.size() > 48 ||
        std::any_of(text.begin(),text.end(),[](unsigned char c) {return c < 32 || c == 127;}))) return false;
    SettingReference reference = bind_setting(settings, descriptor);
    std::visit([&](auto target) {
        using Value = std::decay_t<decltype(target.get())>;
        if constexpr (std::is_same_v<Value, int>) {
            target.get() = static_cast<int>(Compat::LegacyText::parse_int(text));
        } else if constexpr (std::is_same_v<Value, double>) {
            target.get() = Compat::LegacyText::parse_real(text);
        } else {
            target.get().assign(text);
        }
    }, reference);
    return true;
}

bool assign_setting_text(Settings& settings, SettingId id, std::string_view text) {
    return assign_setting_text(settings, setting_descriptor(id), text);
}

ChoiceOptions choice_options(ChoiceKind kind) {
    ChoiceOptions options;
    switch (kind) {
        case ChoiceKind::None:
            break;
        case ChoiceKind::Goal:
            options.labels={"None","Time","Lines","Clear garbage","Placed pieces","Attack","All clear","Clear initial cells"};
            break;
        case ChoiceKind::GameMode:
            options.labels = {
                "Zen", "QuickPlay", "Multiplayer",
                "Local Versus", "Replay"};
            // Display order is independent from the stable persisted enum
            // values: Replay remains 5 and Local Versus remains 6.
            options.values = {0, 3, 4, 6, 5};
            break;
        case ChoiceKind::GarbageMode:
            options.labels = {"None", "Backfire", "Cheese-layer", "Cheese-timer", "Survival"};
            options.values = {0, 1, 3, 4, 5};
            break;
        case ChoiceKind::GarbageEntryMode:
            options.labels = {"After non-clear", "After lock", "Immediate"};
            break;
        case ChoiceKind::Modifier:
            options.labels = {"Close", "Open", "Reverse"};
            break;
        case ChoiceKind::AllSpin:
            options.labels = {"Close", "Open", "Reverse"};
            break;
        case ChoiceKind::Fool:
            options.labels = {"Close", "Pentr", "Hexo", "20260401", "r20260401"};
            break;
        case ChoiceKind::Boolean:
            options.labels = {"OFF", "ON"};
            break;
        case ChoiceKind::Invisibility:
            options.labels = {"OFF", "Delayed invisibility", "Periodic flashing"};
            break;
        case ChoiceKind::MapGoal:
            options.labels={"OFF","Clear map","All clear field"};
            break;
        case ChoiceKind::RotationSystem:
            for (const auto& system:Runtime::kRotationSystems) {
                options.labels.emplace_back(system.label);
                options.values.push_back(static_cast<int>(system.value));
            }
            options.labels.insert(options.labels.end(),{"M123","PENTOMINO","HEXOMINO","20260401"});
            options.values.insert(options.values.end(),{4,3,5,6});
            break;
        case ChoiceKind::Autoplay:
            for (const auto& bot : Runtime::kAutoplayModes) {
                options.labels.emplace_back(bot.name);
                options.values.push_back(static_cast<int>(bot.mode));
            }
            break;
        case ChoiceKind::SquareStyle:
            options.labels = {"Filled", "Spaced"};
            break;
        case ChoiceKind::Language:
            // Native names remain recognizable in either interface language.
            options.labels = {"中文", "English"};
            break;
        case ChoiceKind::SpinBonuses:
            for (const auto& mode : Runtime::kSpinBonuses) {
                options.labels.emplace_back(mode.label);
                options.values.push_back(static_cast<int>(mode.value));
            }
            break;
        case ChoiceKind::ChallengeRules:
            options.labels={"None","Instinct (active piece hidden)","Blind play","Audio play","PC puzzle","PC puzzle series","Stage master","Delayed clear stack","Finesse sprint","Finesse research","49-player Bot royale","99-player Bot royale","Secret grade construction","Checkerboard construction","Map construction","Symmetry construction"};
            options.values=sequential_values(options.labels.size());break;
        case ChoiceKind::BagRandomizer:
            for(const auto& mode:Runtime::kBagRandomizers) {
                options.labels.emplace_back(mode.label);
                options.values.push_back(static_cast<int>(mode.value));
            }
            break;
        case ChoiceKind::B2BReward:
            options.labels = {"Fixed attack", "B2B chaining", "None"};
            break;
        case ChoiceKind::GravityProgression:
            options.labels = {"Fixed", "NES progression", "Marathon progression", "Increasing"};
            break;
    }
    if (!options.labels.empty() && options.values.empty()) options.values = sequential_values(options.labels.size());
    return options;
}

int clamped_int(const Settings& settings, SettingId id) {
    const auto& descriptor = setting_descriptor(id);
    const auto member = std::get_if<int Settings::*>(&descriptor.member);
    if (!member) throw std::logic_error("setting is not integer");
    int value = settings.*(*member);
    if (descriptor.range.enabled) {
        value = std::clamp(value,
            static_cast<int>(descriptor.range.minimum),
            static_cast<int>(descriptor.range.maximum));
    }
    return value;
}

double clamped_real(const Settings& settings, SettingId id) {
    const auto& descriptor = setting_descriptor(id);
    const auto member = std::get_if<double Settings::*>(&descriptor.member);
    if (!member) throw std::logic_error("setting is not real");
    double value = settings.*(*member);
    if (descriptor.range.enabled) value = std::clamp(value, descriptor.range.minimum, descriptor.range.maximum);
    return value;
}

void reset_settings(Settings& settings, bool playerTwo) {
    Challenges::restore_replay(settings);Challenges::restore_adjustment(settings);
    (void)playerTwo; // Bindings are preserved, including Player 2 customizations.
    const bool localVersus=settings.gameMode==static_cast<int>(Runtime::GameMode::LocalVersus);
    const bool multiplayer=settings.gameMode==static_cast<int>(Runtime::GameMode::Multiplayer) ||
        settings.gameMode==-static_cast<int>(Runtime::GameMode::Multiplayer);
    const auto serverIp=settings.serverIp, username=settings.username;
    Presets::clear(settings);
    Settings defaults;
    // Root reset changes player configuration without ending the local match,
    // even if removing preset layers restores a different base mode.
    if(localVersus) defaults.gameMode=static_cast<int>(Runtime::GameMode::LocalVersus);
    if(multiplayer) defaults.gameMode=static_cast<int>(Runtime::GameMode::Multiplayer);
    defaults.serverIp=serverIp; defaults.username=username;
    defaults.persistencePath = settings.persistencePath;
    defaults.persistencePlayer=settings.persistencePlayer;defaults.persistencePlatform=settings.persistencePlatform;
    defaults.persistenceEnabled = settings.persistenceEnabled;
    defaults.keybindingPath = settings.keybindingPath;
    defaults.obscuredColors = settings.obscuredColors;
    defaults.squareStyle = settings.squareStyle;
    defaults.uiLanguage = settings.uiLanguage;
    defaults.showSpawnPosition = settings.showSpawnPosition;
    defaults.continueInputUnfocused = settings.continueInputUnfocused;
    defaults.effectsIntensity = settings.effectsIntensity;
    defaults.fieldShake = settings.fieldShake;
    defaults.particleIntensity = settings.particleIntensity;
    defaults.landingEffect = settings.landingEffect;
    defaults.lineClearEffect = settings.lineClearEffect;
    defaults.hardDropTrail = settings.hardDropTrail;
    defaults.boardBounciness = settings.boardBounciness;
    defaults.keyCodes = settings.keyCodes;
    defaults.additionalKeyCodes = settings.additionalKeyCodes;
    // Global/Player reset preserves the complete movement-handling category.
    defaults.softDropFactor = settings.softDropFactor;
    defaults.softDropFixed = settings.softDropFixed;
    defaults.das = settings.das;
    defaults.arr = settings.arr;
    defaults.dcd = settings.dcd;
    defaults.preMove = settings.preMove;
    defaults.dasPrecharge = settings.dasPrecharge;
    // A full reset also discards every preset layer and its recovery values.
    settings = std::move(defaults);
}

void reset_section(Settings& settings, Section section, bool playerTwo) {
    Settings defaults;
    defaults.keyCodes = playerTwo ? default_player_two_key_codes() : default_key_codes();
    if (section == Section::Controls) settings.additionalKeyCodes = {};
    for (const auto& descriptor : setting_schema()) {
        // Gravity keeps its historical HANDLING/Speed storage key, while its
        // controls and reset now belong to GAME.
        const auto resetSection = descriptor.id == SettingId::GravityInterval
            ? Section::Game : descriptor.section;
        if (resetSection != section) continue;
        if (descriptor.id==SettingId::GameMode && (settings.gameMode==static_cast<int>(Runtime::GameMode::Multiplayer) ||
            settings.gameMode==-static_cast<int>(Runtime::GameMode::Multiplayer))) continue;
        auto source = bind_setting(defaults, descriptor);
        std::visit([&](auto target) {
            using Ref = decltype(target);
            target.get() = std::get<Ref>(source).get();
        }, bind_setting(settings, descriptor));
    }
    Presets::record_edits(settings);
}

} // namespace UserConfig
