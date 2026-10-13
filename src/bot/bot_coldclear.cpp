// C++ adaptation of MinusKelvin/cold-clear. SPDX-License-Identifier: MPL-2.0
#include "tetr/bot/bot_coldclear.h"
#include "coldclear/core.h"
#include <algorithm>
#include <chrono>

namespace Bot_coldclear {
namespace { thread_local Diagnostics diagnostics; }
Diagnostics last_diagnostics() { return diagnostics; }
bool init(const Bot::Environment& environment) { diagnostics = {}; return bool(availability(environment)); }

int getpos(const Bot::Environment& environment, const Runtime::PlayerSessionState& player,
           Runtime::Playfield field, std::vector<int> next, const std::vector<int>& unseenBagPieces,
           int startRow, int startCol, int startRotation, int hold,
           int& answerRow, int& answerCol, int& answerRotation, int& answerKick) {
    diagnostics = {};
    const auto began = std::chrono::steady_clock::now();
    if (!availability(environment) || next.empty()) return -1;
    for (int piece : next) if (piece < 0 || piece >= 7) return -1;
    if (hold >= 7) return -1;
    ColdClear::Input input;
    input.environment = environment;
    input.next = std::move(next);
    input.start = {{startRow,startCol},Runtime::Rotation::from_index(startRotation)};
    input.canHold = environment.holdEnabled && hold != -2;
    for (int piece = 0; piece < 7; ++piece) {
        input.pieces[piece] = Piece::get_definition(environment.pieceTypes[piece]);
        input.metadata[piece] = Piece::metadata(environment.pieceTypes[piece]);
    }
    for (int row = -Runtime::kHiddenRowCapacity; row < ColdClear::Height; ++row)
        for (int col = 0; col < ColdClear::Width; ++col)
            if (field.cell(row,col).occupied()) {
                input.root.board.rows[ColdClear::Height-1-row] |= 1u<<col;
                if (field.cell(row,col).is_piece()) input.root.board.pieceRows[ColdClear::Height-1-row] |= 1u<<col;
            }
    input.root.hold = hold >= 0 ? hold : -1;
    input.root.b2b = player.gameplay.scoring.b2b;
    input.root.combo = player.gameplay.scoring.combo;
    input.root.garbageHeight = player.gameplay.playfield.garbageHeight;
    input.root.previousCleared = player.clutch.previousPieceCleared;
    input.root.placed = player.gameplay.scoring.placedBlocks;
    input.root.sent = environment.sentAttack;
    input.root.time = environment.nowSeconds;
    input.root.incoming = player.garbage.buffer;
    input.root.cancellation = player.garbage.cancellation;
    input.root.bag = 0;
    for (int piece : unseenBagPieces) if (piece >= 0 && piece < 7) input.root.bag |= 1u<<piece;
    if (!input.root.bag || environment.randomPieces) input.root.bag = 127;
    const auto chosen = ColdClear::search(input,diagnostics);
    if (chosen.move.piece < 0) {
        diagnostics.fallback = true;
        // A grounded reachable answer is still returned when deeper search
        // proves every branch dead. The host applies its own last legality check.
        const auto moves = ColdClear::find_moves(input,input.root,input.next.front(),input.start,false);
        if (moves.empty()) return -1;
        answerRow = moves.front().pose.position.row; answerCol = moves.front().pose.position.col;
        answerRotation = moves.front().pose.rotation.index(); answerKick = 0;
    } else {
        answerRow = chosen.move.pose.position.row; answerCol = chosen.move.pose.position.col;
        answerRotation = chosen.move.pose.rotation.index();
        answerKick = chosen.move.rotation.rotated() ? std::min(chosen.move.rotation.kick().index()+1,6) : 0;
        diagnostics.attack = chosen.lock.attack.attack; diagnostics.clearedLines = chosen.lock.clear.clearedLines;
        diagnostics.spin = chosen.lock.spin.spin; diagnostics.mini = chosen.lock.attack.effectiveMini;
        diagnostics.usedHold = chosen.move.hold; diagnostics.projectedRise = chosen.state.rise;
        diagnostics.target = chosen.move.pose; diagnostics.rotation = chosen.move.rotation; diagnostics.selected = chosen.move.piece;
    }
    diagnostics.milliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    return chosen.move.piece >= 0 ? chosen.move.piece : input.next.front();
}
}
