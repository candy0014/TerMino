#ifndef TETR_ENGINE_PAUSE_H
#define TETR_ENGINE_PAUSE_H
#include "tetr/engine/movement_input_state.h"
#include <functional>
namespace Runtime { class GameContext; }
namespace Engine {
bool pause_enabled(const Runtime::GameContext& context) noexcept;
void poll_game_pause(Runtime::GameContext& context, const std::function<void()>& pollNavigation);
class GamePauseScope {
public:
    explicit GamePauseScope(Runtime::GameContext& context, MovementInputState* input = nullptr);
    ~GamePauseScope();
    void end();
    GamePauseScope(const GamePauseScope&) = delete;
    GamePauseScope& operator=(const GamePauseScope&) = delete;
private:
    Runtime::GameContext& context_;
    MovementInputState* input_;
    MovementInputState pausedInput_;
    bool charging_ = false;
    void precharge();
};
}
#endif
