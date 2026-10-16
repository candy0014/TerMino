#ifndef TETR_NETWORK_LOBBY_CHANNEL_H
#define TETR_NETWORK_LOBBY_CHANNEL_H

#include "tetr/network/lobby_protocol.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace Runtime { class GameContext; }

namespace Network {
void send_player_setup(Runtime::GameContext& context,const LobbyPlayerSetup& setup);

void send_lobby_signal(Runtime::GameContext& context, ClientLobbySignal signal);
std::optional<ServerLobbySignal> receive_lobby_signal_for(
    Runtime::GameContext& context, std::chrono::milliseconds timeout);
void wait_for_lobby_signal(Runtime::GameContext& context, ServerLobbySignal expected);
std::uint32_t receive_lobby_seed(Runtime::GameContext& context);
LobbyPlayerSetup exchange_lobby_player_setup(
    Runtime::GameContext& context,
    const LobbyPlayerSetup& localSetup);

} // namespace Network

#endif
