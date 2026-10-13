#include "tetr/input/services.h"
#include "settings_navigation.h"

#include "tetr/runtime/game_context.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif

namespace App::Settings {

int NavigationInput::key_code(NavigationKey key) {
#ifdef _WIN32
    switch (key) {
        case NavigationKey::Up: return VK_UP;
        case NavigationKey::Down: return VK_DOWN;
        case NavigationKey::Confirm: return VK_RETURN;
        case NavigationKey::Left: return VK_LEFT;
        case NavigationKey::Right: return VK_RIGHT;
        case NavigationKey::Back: return VK_ESCAPE;
        case NavigationKey::Backspace: return VK_BACK;
        case NavigationKey::PageUp: return VK_PRIOR;
        case NavigationKey::PageDown: return VK_NEXT;
        case NavigationKey::Tab: return VK_TAB;
    }
    return VK_RETURN;
#else
    switch (key) {
        case NavigationKey::Up: return KEY_UP;
        case NavigationKey::Down: return KEY_DOWN;
        case NavigationKey::Confirm: return KEY_ENTER;
        case NavigationKey::Left: return KEY_LEFT;
        case NavigationKey::Right: return KEY_RIGHT;
        case NavigationKey::Back: return KEY_ESC;
        case NavigationKey::Backspace: return KEY_BACKSPACE;
        case NavigationKey::PageUp: return KEY_PAGEUP;
        case NavigationKey::PageDown: return KEY_PAGEDOWN;
        case NavigationKey::Tab: return KEY_TAB;
    }
    return KEY_ENTER;
#endif
}

NavigationInput::NavigationInput(Runtime::GameContext& context) : context_(context) {
    for (NavigationKey key : trackedKeys_) {
        KeyRepeat state;
        if (context_.input_services().key_down(key_code(key))) state.phase = KeyPhase::Suppressed;
        states_.emplace(key, state);
    }
}
bool NavigationInput::is_down(NavigationKey key) const {
    return context_.input_services().key_down(key_code(key));
}

void NavigationInput::suppress_held_keys() {
    for (NavigationKey key : trackedKeys_)
        states_.at(key).phase = context_.input_services().key_down(key_code(key))
            ? KeyPhase::Suppressed : KeyPhase::Released;
}

bool NavigationInput::update(NavigationKey key, bool allowRepeat) {
    KeyRepeat& state = states_.at(key);
    const bool down = context_.input_services().key_down(key_code(key));
    const Runtime::TimePoint now = context_.real_now();
    if (!down) {
        state.phase = KeyPhase::Released;
        return false;
    }
    if (state.phase == KeyPhase::Suppressed) return false;
    if (state.phase == KeyPhase::Released) {
        state.phase = KeyPhase::InitialDelay;
        state.lastEvent = now;
        state.pressedAt = now;
        return true;
    }
    if (!allowRepeat) return false;
    const Runtime::Duration delay = state.phase == KeyPhase::InitialDelay
        ? Runtime::Duration::from_seconds(0.2)
        : Runtime::Duration::from_seconds(0.05);
    if (now - state.lastEvent <= delay) return false;
    state.lastEvent = now;
    state.phase = KeyPhase::Repeating;
    return true;
}

NavigationKey NavigationInput::active_vertical_key() const {
    const KeyRepeat& up = states_.at(NavigationKey::Up);
    const KeyRepeat& down = states_.at(NavigationKey::Down);
    const bool upHeld = up.phase != KeyPhase::Released && up.phase != KeyPhase::Suppressed;
    const bool downHeld = down.phase != KeyPhase::Released && down.phase != KeyPhase::Suppressed;
    if (!upHeld) return NavigationKey::Down;
    if (!downHeld) return NavigationKey::Up;
    return up.pressedAt >= down.pressedAt ? NavigationKey::Up : NavigationKey::Down;
}

} // namespace App::Settings
