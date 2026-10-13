#ifndef TETR_APP_INIT_H
#define TETR_APP_INIT_H

#include <stdexcept>

namespace Runtime { class GameContext; }

namespace App {
struct MapUnavailable : std::runtime_error {
    Runtime::GameContext& context;
    explicit MapUnavailable(Runtime::GameContext& owner)
        :std::runtime_error("Selected map is unavailable or the map collection is invalid."),context(owner) {}
};
void initialize_game(Runtime::GameContext& context);
}

#endif
