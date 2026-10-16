#ifndef TETR_CONFIG_USER_CONFIG_H
#define TETR_CONFIG_USER_CONFIG_H

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <random>
#include "tetr/config/preset_state.h"

namespace UserConfig {

#ifdef _WIN32
inline constexpr const char* kDefaultKeybindingPath = "keybinding_windows.txt";
inline constexpr const char* kDefaultPlayerTwoKeybindingPath = "keybinding_player2_windows.txt";
inline constexpr const char* kDefaultUserConfigPath = ".tetr/player1_windows.ini";
inline constexpr const char* kDefaultPlayerTwoUserConfigPath = ".tetr/player2_windows.ini";
#else
inline constexpr const char* kDefaultKeybindingPath = "keybinding_linux.txt";
inline constexpr const char* kDefaultPlayerTwoKeybindingPath = "keybinding_player2_linux.txt";
inline constexpr const char* kDefaultUserConfigPath = ".tetr/player1_linux.ini";
inline constexpr const char* kDefaultPlayerTwoUserConfigPath = ".tetr/player2_linux.ini";
#endif

inline std::string make_guest_username() {
    std::random_device random;
    constexpr char symbols[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string name = "GUEST-";
    for (int i = 0; i < 8; ++i) name += symbols[random() % 36];
    return name;
}

// Raw user settings, automatically persisted after edits.
// These intentionally keep the historical integer/double representation so
// Init can perform the exact same clamping and mode transformations later.
struct Settings {
    std::optional<ChallengeAdjustment> challengeAdjustment;
    std::optional<ChallengeReplayRestore> challengeReplay;
    std::string serverIp="wss://termino.candy0014.icu/ws";
    std::string username = make_guest_username();
    int gameMode=0;
    int challengeRules=0;
    int mapId=0; // Stable record ID; map names may be duplicated.
    int mapRotationSystem=-1; // -1 keeps each custom-sequence piece's rotation.
    int mapGoal=0;
    int racingDistance=40;
    double blitzTime=120;
    int goal=0;
    int garbageTarget=40;
    int pieceTarget=100;
    int attackTarget=100;
    int allClearTarget=1;
    std::string replayPath;
    double replayTimeMultiplier=1;
    int garbageMode=0;
    int cheesePattern=1;
    int holeCount=1;
    int layerHeight=6;
    double garbageInterval=3;
    double cheeseMessiness=0.8;
    double cheeseDensity=0.8;
    double hitMultiplier=1;
    double survivalApm=60;
    double garbageInTime=0.3;
    int garbageEntryMode=0;
    double garbageRiseInterval=0;
    int qpExpert=0;
    int qpNoHold=0;
    int qpMessy=0;
    int qpGravity=0;
    int qpVolatile=0;
    int qpDoubleHole=0;
    int qpInvisible=0;
    int qpAllSpin=0;
    int qpBackfire=0;
    int qpSnakeEyes=0;
    int qpSnowball=0;
    int qpFool=0;
    int qpDefaultFloor=1;
    double gravityInterval=0.6;
    int gravityProgression=0;
    int gravityLevelLines=10;
    double gravityAcceleration=1.0;
    double softDropFactor=0;
    int softDropFixed=0; // SDF is seconds per cell when set; otherwise a multiplier.
    double das=0.1;
    double arr=0.01;
    double dcd=0;
    int preMove=1;
    int dasPrecharge=1;
    int forceSdf=0, forceDas=0, forceArr=0, forceDcd=0, forcePreMove=0, forceDasPrecharge=0;
    double forcedSdf=0, forcedDas=0.1, forcedArr=0.01, forcedDcd=0;
    int forcedSdfFixed=0, forcedPreMove=1, forcedDasPrecharge=1;
    int fieldWidth=10;
    int fieldHeight=20;
    int nextCount=5;
    int holdEnabled=1;
    int infiniteHold=0;
    int holdCount=1;
    int fieldInvisible=0;
    double invisibilityDelay=0;
    double invisibilityInterval=5;
    double invisibilityDuration=1;
    int antiGravity=0;
    int ghost=1;
    double lockDelay=0.5;
    int lockResetLimit=15;
    int infiniteMovement=0;
    int rotationSystem=0;
    int spinBonuses=5;
    int b2bReward=0;
    int b2bRewardAmount=1;
    int b2bCharging=1;
    int b2bChargeThreshold=4;
    int b2bChargeReduction=0;
    int perfectClearEnabled=1;
    int perfectClearAttack=5;
    int perfectClearCharge=1;
    int halfClearEnabled=0;
    int halfClearAttack=0;
    int halfClearCharge=0;
    int garbageSpecialAttack=1;
    int randomPieces=0; // Randomizer code; old 0/1 remain 7-BAG/TOTAL MAYHEM.
    int instantGravity=0;
    double lineClearDelay=0;
    double entryDelay=0;
    double cancelMultiplier=1;
    double attackMultiplier=1;
    double messinessWithinAttack=0;
    int avoidSameColumn=0;
    int garbageCap=8;
    int openerPhase=14;
    double simulationPps=0;
    double turnPieces=0;
    int disableLeft=0;
    int disableRight=0;
    int disableClockwise=0;
    int disableCounterclockwise=0;
    int disable180=0;
    int disableSoftDrop=0;
    int disableHardDrop=0;
    int disableUndo=0;
    int disablePause=0;
    int autoplayMode=0;
    double autoplayPps=2.5;
    int autoplayWidth=300;
    int autoplayHandlingSimulation=0;
    // Appearance is shared by both players; profile files keep these fields
    // for compatibility and are synchronized by the application.
    int squareStyle=0;
    int obscuredColors=0;
    int uiLanguage=0; // 0: Chinese, 1: English.
    int showSpawnPosition=1;
    int continueInputUnfocused=0;
    static constexpr std::size_t kControlCount = 13;
    std::array<int,kControlCount> keyCodes{};
    // The first binding stays in the historical slot; -1 means unbound.
    std::array<std::vector<int>,kControlCount> additionalKeyCodes;
    std::string keybindingPath=kDefaultKeybindingPath;
    // Empty for embedded/test settings; application players use separate files.
    std::string persistencePath;
    PresetState presets;
    // Transient authoring mask. Missing entries mean "undefined", regardless
    // of the concrete backing values used by the normal settings controls.
    std::optional<std::set<SettingId>> presetDefined;
    // Profile-local recovery for edits made during a Quick Play visit.
    std::optional<std::array<int,13>> quickPlayModBackup;
};

struct QuickPlayModeSnapshot {
    int mode;
    std::array<int,13> values;
    std::optional<std::array<int,13>> backup;
};
QuickPlayModeSnapshot snapshot_quick_play_mods(const Settings& settings);
void reconcile_quick_play_mods(Settings& settings,const QuickPlayModeSnapshot& before);
void migrate_legacy_modes(Settings& settings);

// Copy every user setting and preset recovery state, preserving the target's
// keyboard bindings and the paths identifying its own profile/binding files.
void copy_settings_except_bindings(Settings& target,const Settings& source);

const std::array<int,Settings::kControlCount>& default_key_codes();
const std::array<int,Settings::kControlCount>& default_player_two_key_codes();

// Load into an explicit settings object; no process-global configuration is
// mutated as a side effect.
void load(Settings& settings,const std::string& path=".tetr/player1.ini");
// Import the legacy shared profile once, preserving native platform bindings
// on Windows. Explicit imports retain their supplied controls.
void load_startup_settings(Settings& settings,bool playerTwo=false,std::string_view importPath={});
bool save(const Settings& settings);

// Each platform uses its own files. The historical numeric format and custom
// file paths remain supported by the centralized parser/writer.
void load_keybindings(Settings& settings,const std::string& path=kDefaultKeybindingPath);
void load_player_two_keybindings(Settings& settings,const std::string& path=kDefaultPlayerTwoKeybindingPath);
bool save_keybindings(Settings& settings);
std::vector<int> control_keys(const Settings& settings, std::size_t control);
void set_control_keys(Settings& settings, std::size_t control, const std::vector<int>& keys);
void toggle_control_key(Settings& settings, std::size_t control, int key);
std::optional<std::vector<int>> parse_control_keys(std::string_view text);
std::string control_keys_text(const Settings& settings, std::size_t control);

} // namespace UserConfig

#endif
