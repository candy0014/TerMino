#ifndef TETR_BOT_ZZZTOJ_H
#define TETR_BOT_ZZZTOJ_H

#include "tetr/bot/environment.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/playfield.h"

#include <vector>

namespace Bot_zzztoj {

bool init(const Bot::Environment& environment);
Autoplay::Availability availability(const Bot::Environment& environment);
int getpos(const Bot::Environment& environment, const Runtime::PlayerSessionState& player,
           Runtime::Playfield field, std::vector<int> next,
           const std::vector<int>& unseenBagPieces,
           int startRow, int startCol, int startRotation, int hold,
           int& answerRow, int& answerCol, int& answerRotation, int& answerKick);

} // namespace Bot_zzztoj

#endif
