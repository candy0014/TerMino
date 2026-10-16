#ifndef TETR_RUNTIME_GAME_MODES_H
#define TETR_RUNTIME_GAME_MODES_H

#include <array>
#include <string_view>

namespace Runtime {

enum class GameMode : int {
    Zen = 0,
    // Reserved IDs for importing historical profiles and recordings.
    Racing = 1,
    Blitz = 2,
    QuickPlay = 3,
    Multiplayer = 4,
    Replay = 5,
    LocalVersus = 6,
};

enum class GameGoal : int {
    None, Time, Lines, Garbage, Pieces, Attack, AllClear, InitialCells
};

enum class GarbageMode : int {
    None = 0,
    Backfire = 1,
    Unclear = 2,
    CheeseLayer = 3,
    CheeseTimer = 4,
    Survival = 5,
    QuickPlay = 10,
    Multiplayer = 11,
};

enum class GarbageEntryMode : int {
    AfterNonClear = 0,
    AfterLock = 1,
    Immediate = 2,
};

enum class RotationSystem : int {
    SrsPlus = 0,
    Srs = 1,
    Ars = 2,
    Pentomino = 3,
    M123 = 4,
    Hexomino = 5,
    AprilFools2026 = 6,
    SrsX = 7,
    TetraX = 8,
    Nrs = 9,
    Asc = 10,
    None = 11,
};

constexpr bool standard_rotation_system(RotationSystem system) noexcept {
    return system == RotationSystem::SrsPlus || system == RotationSystem::Srs ||
        system == RotationSystem::Ars || system == RotationSystem::SrsX ||
        system == RotationSystem::TetraX || system == RotationSystem::Nrs ||
        system == RotationSystem::Asc || system == RotationSystem::None;
}

enum class AutoplayMode : int {
    Disabled = 0,
    While1 = 1,
    Candy0014 = 2,
    Mech = 3,
    ZzztojBotris = 4,
    Zzztoj = ZzztojBotris,
    ZzztojToj = 5,
    ZzztojDig = 6,
    ZzztojQq = 7,
    ZzztojC2 = 8,
    ChatGPT = 9,
    ColdClear = 10,
};

struct AutoplayModeInfo {
    AutoplayMode mode;
    std::string_view name;
};

inline constexpr std::array<AutoplayModeInfo, 11> kAutoplayModes = {{
    {AutoplayMode::Disabled, "Disabled"},
    {AutoplayMode::While1, "bot_while1"},
    {AutoplayMode::Candy0014, "bot_candy0014"},
    {AutoplayMode::Mech, "bot_mech"},
    {AutoplayMode::ZzztojBotris, "zzztoj_botris"},
    {AutoplayMode::ZzztojToj, "zzztoj_toj"},
    {AutoplayMode::ZzztojDig, "zzztoj_dig"},
    {AutoplayMode::ZzztojQq, "zzztoj_qq"},
    {AutoplayMode::ZzztojC2, "zzztoj_c2"},
    {AutoplayMode::ChatGPT, "bot_chatgpt"},
    {AutoplayMode::ColdClear, "bot_coldclear"},
}};

constexpr AutoplayMode autoplay_mode_from_index(int value) noexcept {
    return value >= 0 && value < static_cast<int>(kAutoplayModes.size())
        ? static_cast<AutoplayMode>(value)
        : AutoplayMode::Disabled;
}

enum class ModifierMode : int {
    Disabled = 0,
    Enabled = 1,
    Reverse = 2,
};

enum class AllSpinMode : int {
    Disabled = 0,
    Enabled = 1,
    Reverse = 2,
    Extended = 3,
};

enum class FoolMode : int {
    Disabled = 0,
    Pentomino = 1,
    Hexomino = 2,
    AprilFools2026 = 3,
    ReverseAprilFools2026 = 4,
};

constexpr RotationSystem rotation_system_from_index(int value) noexcept {
    return static_cast<RotationSystem>(value);
}

constexpr bool enabled(ModifierMode value) noexcept {
    return value != ModifierMode::Disabled;
}

constexpr bool reverse(ModifierMode value) noexcept {
    return value == ModifierMode::Reverse;
}

constexpr bool enabled(AllSpinMode value) noexcept {
    return value != AllSpinMode::Disabled;
}

constexpr bool autoplay_enabled(AutoplayMode value) noexcept {
    return value != AutoplayMode::Disabled;
}

constexpr bool is_cheese_mode(GarbageMode value) noexcept {
    return value == GarbageMode::CheeseLayer || value == GarbageMode::CheeseTimer;
}

constexpr bool uses_garbage_buffer(GarbageMode value) noexcept {
    return value == GarbageMode::Backfire || value == GarbageMode::Survival ||
           value == GarbageMode::QuickPlay || value == GarbageMode::Multiplayer;
}

} // namespace Runtime

#endif
