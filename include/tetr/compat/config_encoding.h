#ifndef TETR_COMPAT_CONFIG_ENCODING_H
#define TETR_COMPAT_CONFIG_ENCODING_H

#include "tetr/runtime/game_modes.h"

namespace Compat {

constexpr int to_legacy(Runtime::GameMode value) noexcept { return static_cast<int>(value); }
constexpr int to_legacy(Runtime::GarbageMode value) noexcept { return static_cast<int>(value); }
constexpr int to_legacy(Runtime::RotationSystem value) noexcept { return static_cast<int>(value); }
constexpr int to_legacy(Runtime::AutoplayMode value) noexcept {
    switch (value) {
        case Runtime::AutoplayMode::Disabled: return 0;
        case Runtime::AutoplayMode::While1: return -1;   // historical bot_while2
        case Runtime::AutoplayMode::Candy0014: return 2;
        case Runtime::AutoplayMode::Mech: return 4;     // historical bot_mech_all
        case Runtime::AutoplayMode::ZzztojBotris: return 7;
        case Runtime::AutoplayMode::ZzztojToj: return 8;
        case Runtime::AutoplayMode::ZzztojDig: return 9;
        case Runtime::AutoplayMode::ZzztojQq: return 10;
        case Runtime::AutoplayMode::ZzztojC2: return 11;
        case Runtime::AutoplayMode::ChatGPT: return 12;
        case Runtime::AutoplayMode::ColdClear: return 13;
    }
    return 0;
}
constexpr int to_legacy(Runtime::ModifierMode value) noexcept { return static_cast<int>(value); }
constexpr int to_legacy(Runtime::AllSpinMode value) noexcept { return static_cast<int>(value); }
constexpr int to_legacy(Runtime::FoolMode value) noexcept { return static_cast<int>(value); }

constexpr Runtime::GameMode game_mode_from_legacy(int value) noexcept {
    return static_cast<Runtime::GameMode>(value);
}
constexpr Runtime::GarbageMode garbage_mode_from_legacy(int value) noexcept {
    return static_cast<Runtime::GarbageMode>(value);
}
constexpr Runtime::RotationSystem rotation_system_from_legacy(int value) noexcept {
    return Runtime::rotation_system_from_index(value);
}
constexpr Runtime::AutoplayMode autoplay_mode_from_legacy(int value) noexcept {
    switch (value) {
        case -1: return Runtime::AutoplayMode::While1;
        case 0: return Runtime::AutoplayMode::Disabled;
        case 1: return Runtime::AutoplayMode::While1;
        case 2: return Runtime::AutoplayMode::Candy0014;
        case 3: return Runtime::AutoplayMode::Mech;
        case 4: return Runtime::AutoplayMode::Mech;
        case 5: return Runtime::AutoplayMode::Disabled; // retired historical BOT
        case 6: return Runtime::AutoplayMode::Disabled; // retired bot ID
        case 7: return Runtime::AutoplayMode::ZzztojBotris;
        case 8: return Runtime::AutoplayMode::ZzztojToj;
        case 9: return Runtime::AutoplayMode::ZzztojDig;
        case 10: return Runtime::AutoplayMode::ZzztojQq;
        case 11: return Runtime::AutoplayMode::ZzztojC2;
        case 12: return Runtime::AutoplayMode::ChatGPT;
        case 13: return Runtime::AutoplayMode::ColdClear;
        default: return Runtime::AutoplayMode::Disabled;
    }
}
constexpr Runtime::ModifierMode modifier_mode_from_legacy(int value) noexcept {
    return static_cast<Runtime::ModifierMode>(value);
}
constexpr Runtime::AllSpinMode all_spin_mode_from_legacy(int value) noexcept {
    return static_cast<Runtime::AllSpinMode>(value);
}
constexpr Runtime::FoolMode fool_mode_from_legacy(int value) noexcept {
    return static_cast<Runtime::FoolMode>(value);
}

} // namespace Compat

#endif
