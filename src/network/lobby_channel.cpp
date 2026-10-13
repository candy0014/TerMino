#include "tetr/network/lobby_channel.h"

#include "tetr/network/transport.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#include <algorithm>

namespace Network {
namespace {
std::optional<std::string> receive_with_navigation(Runtime::GameContext& context,
                                                  std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (context.services.clock.waitInterrupt) context.services.clock.waitInterrupt();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (auto wire = receive_for(network_services(context),
                std::max(std::chrono::milliseconds(0), std::min(remaining, std::chrono::milliseconds(10)))))
            return wire;
    } while (std::chrono::steady_clock::now() < deadline);
    return std::nullopt;
}
void check_interrupted(const std::string& wire) {
    if (wire == "end2" || wire == "end1") throw LobbyInterrupted("Opponent left the lobby");
}
}

void send_lobby_signal(Runtime::GameContext& context, ClientLobbySignal signal) {
    send(network_services(context), std::string(encode_client_lobby_signal(signal)));
}

std::optional<ServerLobbySignal> receive_lobby_signal_for(
    Runtime::GameContext& context,
    std::chrono::milliseconds timeout) {
    auto wire = receive_with_navigation(context, timeout);
    if (!wire) return std::nullopt;
    check_interrupted(*wire);
    return decode_server_lobby_signal(*wire);
}

void wait_for_lobby_signal(Runtime::GameContext& context, ServerLobbySignal expected) {
    while (true) {
        send_lobby_signal(context, expected == ServerLobbySignal::CountdownReady
            ? ClientLobbySignal::SetupReceived : ClientLobbySignal::CountdownComplete);
        if (const auto wire = receive_with_navigation(context, std::chrono::milliseconds(250))) {
            check_interrupted(*wire);
            const auto signal = decode_server_lobby_signal(*wire);
            if (signal && *signal == expected) return;
        }
    }
}

std::uint32_t receive_lobby_seed(Runtime::GameContext& context) {
    while (true) {
        if (const auto wire = receive_with_navigation(context, std::chrono::milliseconds(250))) {
            check_interrupted(*wire);
            if (const auto seed = decode_lobby_seed(*wire)) return *seed;
        }
    }
}

LobbyPlayerSetup exchange_lobby_player_setup(
    Runtime::GameContext& context,
    const LobbyPlayerSetup& localSetup) {
    send(network_services(context), encode_lobby_player_setup(localSetup));
    while (true) {
        send(network_services(context), encode_lobby_player_setup(localSetup));
        if (auto wire = receive_with_navigation(context, std::chrono::milliseconds(250))) {
            check_interrupted(*wire);
            if (auto setup = decode_lobby_player_setup(*wire)) return *setup;
        }
    }
}

} // namespace Network
