#ifndef TETR_RULES_GAME_RESULT_H
#define TETR_RULES_GAME_RESULT_H

namespace Rules {

enum class PlayResult {
    Locked = 0,
    Hold = 1,
    Dead = 2,
    OpenSettings = 3,
    OpponentLeft = 4,
    Won = 5,
    Retry = 6,
    ContinueFrame = 7
};

} // namespace Rules

#endif
