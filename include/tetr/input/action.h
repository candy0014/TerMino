#ifndef TETR_INPUT_ACTION_H
#define TETR_INPUT_ACTION_H

#include <array>
#include <cstddef>
#include <map>
#include <vector>

namespace Input {

enum class KeyPlatform { Windows, Evdev };
#ifdef _WIN32
inline constexpr KeyPlatform kNativeKeyPlatform = KeyPlatform::Windows;
#else
inline constexpr KeyPlatform kNativeKeyPlatform = KeyPlatform::Evdev;
#endif
constexpr bool capture_key_code(int key, KeyPlatform platform = kNativeKeyPlatform) noexcept {
    if (key <= 0) return false;
    // Generic modifier states mirror the side-specific keys in Windows.
    return platform != KeyPlatform::Windows ||
        (key < 0xff && key != 0x10 && key != 0x11 && key != 0x12);
}

enum class Action {
    Unknown,
    MoveLeft,
    MoveRight,
    SoftDrop,
    HardDrop,
    Rotate180,
    RotateClockwise,
    RotateCounterclockwise,
    Hold,
    Retry,
    OpenSettings,
    SaveReplay,
    Undo,
    Pause
};

constexpr bool reserved_binding_key(int key, Action action, KeyPlatform platform = kNativeKeyPlatform) noexcept {
    // 0xff is not a bindable Windows virtual key, even if polling reports it.
    if (platform == KeyPlatform::Windows && key == 0xff) return true;
    if (action != Action::Retry && action != Action::OpenSettings) return false;
    // Windows shares VK_RETURN for main/keypad Enter; evdev has two codes.
    return platform == KeyPlatform::Windows
        ? key == 0x0d || key == 0x1b || key == 0x09 || (key >= 0x25 && key <= 0x28)
        : key == 1 || key == 28 || key == 96 || key == 15 || key == 103 || key == 105 || key == 106 || key == 108;
}

// Runtime key bindings are derived directly from the configured key-code
// slots. Historical short action strings are no longer part of the runtime
// binding model.
inline constexpr std::size_t kBindableActionCount = 13;
inline constexpr std::array<Action, kBindableActionCount> kBindableActions = {
    Action::MoveLeft, Action::MoveRight, Action::SoftDrop, Action::HardDrop,
    Action::RotateClockwise, Action::RotateCounterclockwise, Action::Rotate180,
    Action::Hold, Action::Retry, Action::OpenSettings, Action::SaveReplay, Action::Undo, Action::Pause
};

class BindingSet {
public:
    using Map = std::map<int, Action>;

    static BindingSet from_key_codes(const std::array<int, kBindableActionCount>& keyCodes,
        const std::array<std::vector<int>, kBindableActionCount>& additional = {}) {
        BindingSet result;
        for (std::size_t index = 0; index < kBindableActionCount; ++index) {
            const int key = keyCodes[index];
            const auto action = kBindableActions[index];
            if (key >= 0 && !reserved_binding_key(key,action)) result.bindings_[key] = action;
            for (const int extra : additional[index])
                if (extra >= 0 && !reserved_binding_key(extra,action)) result.bindings_[extra] = action;
        }
        return result;
    }

    Action action_for(int key) const noexcept {
        const auto it = bindings_.find(key);
        return it == bindings_.end() ? Action::Unknown : it->second;
    }

    const Map& entries() const noexcept { return bindings_; }
    bool empty() const noexcept { return bindings_.empty(); }

private:
    Map bindings_;
};

constexpr bool is_rotation(Action action) noexcept {
    return action == Action::Rotate180 || action == Action::RotateClockwise ||
           action == Action::RotateCounterclockwise;
}

constexpr bool is_horizontal(Action action) noexcept {
    return action == Action::MoveLeft || action == Action::MoveRight;
}

constexpr int rotation_delta(Action action) noexcept {
    return action == Action::Rotate180 ? 2 :
           action == Action::RotateCounterclockwise ? -1 :
           action == Action::RotateClockwise ? 1 : 0;
}

constexpr int horizontal_delta(Action action) noexcept {
    return action == Action::MoveLeft ? -1 : action == Action::MoveRight ? 1 : 0;
}

} // namespace Input

#endif
