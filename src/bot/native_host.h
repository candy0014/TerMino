#ifndef TETR_BOT_NATIVE_HOST_H
#define TETR_BOT_NATIVE_HOST_H

// Shared geometry/reachability bridge. Strategy evaluation stays in each BOT.
#include "tetr/bot/environment.h"
#include "tetr/runtime/piece.h"
#include "tetris_core.h"

namespace BotNativeHost {
bool supported(const Bot::Environment& environment);
m_tetris::TetrisMap make_map(const Runtime::Playfield& field);
bool reachable(const m_tetris::TetrisNode* target, const Piece::Definition& piece,
               const Runtime::Playfield& field, Piece::Pose start,
               bool spin, bool mini, Piece::Pose& answer, int& kick,
               const Bot::Environment* environment = nullptr);
int fallback(const Runtime::Playfield& field, const std::vector<int>& next,
             int row, int col, int rotation, int& answerRow, int& answerCol,
             int& answerRotation, int& answerKick);
}

#endif
