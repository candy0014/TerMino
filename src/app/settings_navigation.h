#ifndef TETR_APP_SETTINGS_NAVIGATION_H
#define TETR_APP_SETTINGS_NAVIGATION_H

#include "tetr/runtime/time.h"

#include <array>
#include <map>

namespace Runtime { class GameContext; }

namespace App::Settings {

enum class NavigationKey { Up, Down, Confirm, Left, Right, Back, Backspace, PageUp, PageDown, Tab };

class NavigationInput {
public:
    explicit NavigationInput(Runtime::GameContext& context);
    bool update(NavigationKey key, bool allowRepeat);
    NavigationKey active_vertical_key() const;
    void suppress_held_keys();
    bool is_down(NavigationKey key) const;
    static int key_code(NavigationKey key);

private:
    enum class KeyPhase { Released, Suppressed, InitialDelay, Repeating };
    struct KeyRepeat {
        KeyPhase phase = KeyPhase::Released;
        Runtime::TimePoint lastEvent{};
        Runtime::TimePoint pressedAt{};
    };

    Runtime::GameContext& context_;
    std::array<NavigationKey,10> trackedKeys_ = {
        NavigationKey::Up, NavigationKey::Down, NavigationKey::Confirm,
        NavigationKey::Left, NavigationKey::Right, NavigationKey::Back,
        NavigationKey::Backspace, NavigationKey::PageUp, NavigationKey::PageDown, NavigationKey::Tab};
    std::map<NavigationKey,KeyRepeat> states_;
};

} // namespace App::Settings

#endif
