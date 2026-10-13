#ifndef TETR_LEGACY_BOT_ADAPTER_H
#define TETR_LEGACY_BOT_ADAPTER_H
#include "tetr/autoplay/strategy.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/playfield.h"
#include <string>
#include <vector>
namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace Bot {
using HoldInput = Autoplay::HoldInput;

bool init(Runtime::GameContext& context);
Piece::Type getpos(Runtime::GameContext& context,Runtime::PlayerSessionState& player,Runtime::Playfield field,
                   const std::vector<Piece::Type>& next,const std::vector<Piece::Type>& unseenBagPieces,
                   Piece::Pose start,HoldInput hold,
                   Piece::Pose& answer,Runtime::RotationRecord& answerRotation);
std::string getop(Runtime::GameContext& context,Runtime::Playfield field,Piece::Definition piece,
                  Piece::Pose start,Piece::Pose answer,Runtime::RotationRecord answerRotation);
}
#endif
