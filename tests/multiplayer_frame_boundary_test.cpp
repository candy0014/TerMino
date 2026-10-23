#include "tetr/compat/legacy_encoding.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/frame_packet.h"
#include "tetr/network/frame_pump.h"
#include "tetr/network/transport.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/runtime_services.h"
#include "tetr/runtime/session_state.h"

#include <cassert>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace TetrTests::multiplayer_frame_boundary_test {
namespace {

static_assert(std::is_same_v<
    decltype(Network::try_receive(std::declval<Network::NetworkServices&>())),
    std::optional<std::string>>);
static_assert(std::is_same_v<
    decltype(Network::FramePumpCallbacks{}.receive),
    std::function<std::optional<Network::FramePacket>()>>);
static_assert(std::is_same_v<
    decltype(Network::FramePumpCallbacks{}.applyOpponentState),
    std::function<void(const Network::BoardStateSnapshot&)>>);

void round_signal_wire_is_hidden_at_codec_boundary() {
    assert(Network::encode_round_signal(Network::OutgoingRoundSignal::LocalLoss) == "end1");
    assert(Network::encode_round_signal(Network::OutgoingRoundSignal::LeaveRound) == "end2");

    assert(Network::decode_round_signal("end1") == Network::OutgoingRoundSignal::LocalLoss);
    assert(Network::decode_round_signal("end2") == Network::OutgoingRoundSignal::LeaveRound);
    assert(!Network::decode_round_signal("end"));
    assert(Network::encode_round_acknowledgement() == "end");
    assert(Network::is_round_acknowledgement("end"));
    assert(!Network::is_round_acknowledgement("end1"));

    const auto win = Network::decode_frame_packet("end1", 10, 20);
    const auto left = Network::decode_frame_packet("end2", 10, 20);
    assert(win && win->type == Network::FramePacketType::Win);
    assert(left && left->type == Network::FramePacketType::OpponentLeft);
}

void attack_frame_is_strictly_decoded() {
    const Network::OutgoingAttackFrame outgoing{Runtime::MultiplayerSequenceId::from_wire(17), Runtime::MultiplayerSequenceId::from_wire(9), 6};
    const auto decoded = Network::decode_frame_packet(
        Network::encode_attack_frame(outgoing), 10, 20);
    assert(decoded);
    assert(decoded->type == Network::FramePacketType::Attack);
    assert(decoded->receiveId.value() == 17);
    assert(decoded->sendId.value() == 9);
    assert(decoded->attack == 6);

    const std::vector<std::string> malformed{
        "", "1", "1;2", "1;2;3;4", ";2;3", "1;;3", "1;2;",
        "-1;2;3", "1;-2;3", "1;2;-3", "1;x;3", "1;2;3x",
        "999999999999999999999;2;3"
    };
    for (const auto& wire : malformed) {
        assert(!Network::decode_frame_packet(wire, 10, 20));
    }
}

void board_state_becomes_snapshot_before_frame_pump() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);
    auto& player = session.player;
    auto& playfield = player.gameplay.playfield;
    playfield.width = 4;
    playfield.visibleHeight = 4;
    playfield.activeHeight = 4;
    playfield.field.initialize(4, 4);
    playfield.field.clear();
    playfield.field.set_cell(0, 0, Compat::board_cell_from_legacy(3));
    player.pieces.bag.push_back({Compat::piece_type_from_legacy(1), false});
    player.pieces.nextPreview = {Compat::piece_type_from_legacy(2)};
    player.gameplay.scoring.wins = 7;

    const std::string wire = Network::encode_board_state(context, player);
    assert(Network::is_gameplay_extension(wire));
    assert(Network::is_gameplay_extension(Network::encode_piece_turn(77,2)));
    for (const auto* control : {"!probe","!join;token","!room;token;1;2;;1","!username;token;1;4142","!room-reply;token","!unknown-control;ignored"})
        assert(!Network::is_gameplay_extension(control));
    const auto packet = Network::decode_frame_packet(wire, 4, 4);
    assert(packet);
    assert(packet->type == Network::FramePacketType::OpponentState);
    assert(packet->opponentState);
    assert(Compat::to_legacy(packet->opponentState->field.cell(0, 0)) == 3);
    assert(packet->opponentState->currentPiece == Compat::piece_type_from_legacy(1));
    assert(packet->opponentState->nextPreview == player.pieces.nextPreview);
    assert(packet->opponentState->score.wins == 7);

    std::string malformed = wire;
    malformed[1] = '?';
    assert(!Network::decode_frame_packet(malformed, 4, 4));
}

void pump_consumes_only_typed_packets() {
    std::deque<Runtime::PendingAttack> pending;
    Runtime::MultiplayerSequenceId receiveId{};
    int wins = 0;

    Network::FramePacket attack;
    attack.type = Network::FramePacketType::Attack;
    attack.receiveId = Runtime::MultiplayerSequenceId::from_wire(4);
    attack.sendId = Runtime::MultiplayerSequenceId::from_wire(0);
    attack.attack = 3;

    Network::BoardStateSnapshot snapshot;
    snapshot.field.initialize(4, 4);
    snapshot.score.wins = 12;
    Network::FramePacket state;
    state.type = Network::FramePacketType::OpponentState;
    state.opponentState = snapshot;

    Network::FramePacket terminal;
    terminal.type = Network::FramePacketType::OpponentLeft;

    std::deque<Network::FramePacket> packets{attack, state, terminal};
    int incoming = 0;
    int remoteWins = 0;
    Network::FramePumpCallbacks callbacks;
    callbacks.receive = [&]() -> std::optional<Network::FramePacket> {
        if (packets.empty()) return std::nullopt;
        Network::FramePacket packet = packets.front();
        packets.pop_front();
        return packet;
    };
    callbacks.addIncomingAttack = [&](int value) { incoming += value; };
    callbacks.applyOpponentState = [&](const Network::BoardStateSnapshot& value) {
        remoteWins = value.score.wins;
    };

    const auto signal = Network::pump_multiplayer_frame(
        Network::MultiplayerFrameStateRef{pending, receiveId, wins}, callbacks);
    assert(signal == Network::MultiplayerFrameSignal::OpponentLeft);
    assert(incoming == 3);
    assert(remoteWins == 12);
    assert(receiveId.value() == 4);
    assert(wins == 1);
}

} // namespace

int run(int argc,char** argv) {
    round_signal_wire_is_hidden_at_codec_boundary();
    attack_frame_is_strictly_decoded();
    board_state_becomes_snapshot_before_frame_pump();
    pump_consumes_only_typed_packets();
    return 0;
}
} // namespace TetrTests::multiplayer_frame_boundary_test
