#ifndef TETR_TEST_NETWORK_GAMEPLAY_FRAMES_H
#define TETR_TEST_NETWORK_GAMEPLAY_FRAMES_H

#include "tetr/compat/board_encoding.h"
#include "tetr/compat/piece_encoding.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/runtime/game_context.h"
#include <cassert>

namespace Test {
// Encode real gameplay snapshots, including each possible leading cell class.
// Plain text relay probes do not exercise the historical !board-state format.
inline Network::ActivePieceState prepare_online_board(Runtime::GameContext& context, int variant) {
    auto& player = context.session.player;
    auto& field = player.gameplay.playfield;
    field.width = 10; field.visibleHeight = field.activeHeight = 20; field.nextCount = 2;
    field.field.initialize(10,20); field.field.clear();
    field.field.set_cell(0,0,Compat::board_cell_from_legacy(3));
    if (variant) field.field.set_cell(19,0,Compat::board_cell_from_legacy(variant == 1 ? 7 : 101));
    player.pieces.bag.clear();
    player.pieces.bag.push_back({Compat::piece_type_from_legacy(1),false});
    player.pieces.nextPreview = {Compat::piece_type_from_legacy(2),Compat::piece_type_from_legacy(3)};
    player.gameplay.hold.piece = Compat::piece_type_from_legacy(4);
    player.gameplay.hold.used = 1;
    context.config.field.holdEnabled = true; context.config.field.holdCount = 1;
    player.gameplay.scoring.wins = 2;
    player.gameplay.scoring.placedBlocks = 42 + variant;
    player.gameplay.scoring.totalAttack = 7;
    return Network::ActivePieceState::active({{2 + variant,3 + variant},Runtime::Rotation::from_index(variant)});
}
inline void check_online_board(const Network::FramePacket& packet, int variant) {
    assert(packet.type == Network::FramePacketType::OpponentState && packet.opponentState);
    const auto& board = *packet.opponentState;
    assert(Compat::to_legacy(board.field.cell(0,0)) == 3);
    assert(Compat::to_legacy(board.field.cell(19,0)) == (variant == 0 ? -1 : variant == 1 ? 7 : 101));
    assert(board.currentPiece == Compat::piece_type_from_legacy(1));
    assert(board.activePose == (Piece::Pose{{2 + variant,3 + variant},Runtime::Rotation::from_index(variant)}));
    assert(board.nextPreview == std::vector<Piece::Type>({Compat::piece_type_from_legacy(2),Compat::piece_type_from_legacy(3)}));
    assert(board.hold == Compat::piece_type_from_legacy(4) && board.holdAvailable == false);
    assert(board.score.wins == 2 && board.score.placedBlocks == 42 + variant && board.score.totalAttack == 7);
}
inline Network::RemotePlacementSnapshot online_placement() {
    Network::RemotePlacementSnapshot placement;
    placement.piece = Compat::piece_type_from_legacy(1);
    placement.spin = true; placement.clearedLines = 2; placement.b2b = 3; placement.combo = 4;
    return placement;
}
inline void check_online_placement(const Network::FramePacket& packet) {
    assert(packet.type == Network::FramePacketType::OpponentState && packet.opponentState && packet.opponentState->placement);
    const auto& placement = *packet.opponentState->placement;
    assert(placement.piece == Compat::piece_type_from_legacy(1) && placement.spin);
    assert(placement.clearedLines == 2 && placement.b2b == 3 && placement.combo == 4);
}
} // namespace Test
#endif
