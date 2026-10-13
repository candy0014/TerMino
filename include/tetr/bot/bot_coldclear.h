#ifndef TETR_BOT_COLDCLEAR_H
#define TETR_BOT_COLDCLEAR_H

#include "tetr/bot/environment.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/playfield.h"
#include <cstddef>
#include <vector>

namespace Bot_coldclear {
struct Diagnostics {
    std::size_t nodes = 0;
    int depth = 0;
    int transpositions = 0;
    int speculations = 0;
    int attack = 0;
    int clearedLines = 0;
    int projectedRise = 0;
    bool spin = false;
    bool mini = false;
    bool usedHold = false;
    bool fallback = false;
    Piece::Pose target;
    Runtime::RotationRecord rotation;
    int selected = -1;
    double milliseconds = 0;
};
Diagnostics last_diagnostics();
bool init(const Bot::Environment& environment);
Autoplay::Availability availability(const Bot::Environment& environment);
int getpos(const Bot::Environment& environment, const Runtime::PlayerSessionState& player,
           Runtime::Playfield field, std::vector<int> next,
           const std::vector<int>& unseenBagPieces,
           int startRow, int startCol, int startRotation, int hold,
           int& answerRow, int& answerCol, int& answerRotation, int& answerKick);
}

#endif
