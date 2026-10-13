#include "tetr/input/services.h"
#include "init_multiplayer.h"

#include "tetr/app/settings_menu.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/input/action.h"
#include "tetr/config/user_config.h"
#include "tetr/config/multiplayer_config.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/util/terminal_text.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>

namespace App::InitDetail {
namespace {

void apply_remote_setup(
    Runtime::PlayerSessionState& local,
    Runtime::PlayerSessionState& remote,
    const Network::LobbyPlayerSetup& setup,
    Runtime::GameMode mode) {
    remote.presentation.margin = local.presentation.margin +
        std::max(40, (local.gameplay.playfield.width + local.presentation.blockMaxWidth * 2) * 2 + 20);
    remote.gameplay.playfield.visibleHeight = setup.visibleHeight;
    remote.gameplay.playfield.width = setup.width;
    remote.gameplay.playfield.nextCount = setup.nextCount;
    remote.gameplay.playfield.holdEnabled = setup.holdEnabled;
    remote.gameplay.scoring.wins = setup.wins;
    remote.presentation.blockMaxHeight = setup.blockMaxHeight;
    remote.presentation.blockMaxWidth = setup.blockMaxWidth;
    remote.presentation.spike = {};
    remote.pieces.bag.clear();
    for (Piece::Type piece : setup.nextPreview) {
        remote.pieces.bag.push_back(Runtime::QueuedPiece{piece, false});
    }
    remote.gameplay.hold = {};
    remote.gameplay.playfield.field.clear();
    remote.gameplay.scoring.combo = 0;
    remote.gameplay.scoring.b2b = 0;
    remote.gameplay.scoring.placedBlocks = 0;
    remote.gameplay.scoring.totalAttack = 0;
    remote.gameplay.scoring.totalLines = 0;
    if (mode != Runtime::GameMode::Zen) remote.gameplay.scoring.score = 0;
}

} // namespace

LobbyWaitResult wait_for_multiplayer_lobby(Runtime::GameContext& context) {
    auto& network = Network::network_services(context);
    network.roomConfig={};
    network.localRoomConfig=UserConfig::encode_multiplayer_config(context.user_settings(),context.config);
    std::map<int,bool> heldKeys;
    for (const auto& [key, action] : context.input_services().bindings.entries())
        if (action == Input::Action::OpenSettings || action == Input::Action::Retry) heldKeys[key] = true;
    std::string shown;
    bool pairReady = false;
    auto lastReady = std::chrono::steady_clock::time_point{};
    for (;;) {
        Network::poll_connection(network);
        if (network.roomConfig.incoming.payload && !network.roomConfig.applied) {
            const auto host=UserConfig::decode_multiplayer_config(*network.roomConfig.incoming.payload);
            if (!host) {
                Network::send_lobby_signal(context,Network::ClientLobbySignal::Cancel);
                throw Network::LobbyInterrupted("Invalid host configuration. Return to the room and start again.");
            }
            context.config=UserConfig::apply_multiplayer_config(context.user_settings(),context.config,*host,
                network.roomConfig.policy.forceRules,network.roomConfig.policy.forceHandling);
            if (Runtime::autoplay_enabled(context.config.autoplay.mode) && !Engine::initialize_autoplay(context)) {
                Network::send_lobby_signal(context,Network::ClientLobbySignal::Cancel);
                throw Network::LobbyInterrupted("The host's bot is unavailable on this client.");
            }
            if (Runtime::autoplay_enabled(context.config.autoplay.mode) && context.config.autoplay.mode!=Runtime::AutoplayMode::ChatGPT &&
                !context.config.autoplay.handlingSimulation && context.config.handling.gravityInterval.enabled() &&
                !context.config.handling.gravityInterval.is_instant()) context.config.handling.gravityInterval=Runtime::GravityInterval::disabled();
            Runtime::reset_dynamic_rules(context);
            Replay::session(context).configPrefix=Replay::encode_config_prefix(context.config);
            Network::accept_room_config(network);
        }
        if (network.connection == Network::ConnectionState::Connected && network.room.name.empty()) {
            Settings::run_network_menu(context);
            return LobbyWaitResult::Reconfigure;
        }
        const std::string status = network.connectionMessage + " / Room: " + network.room.name +
            (network.connection == Network::ConnectionState::Connected ? " / Waiting for another player..." : "");
        if (shown != status) {
            shown = status;
            Render::clear_screen();
            Render::set_ui_color(Render::ColorRole::Text, context.config.display.obscuredColors);
            int row=2;
            for (const auto& line:Util::TerminalText::wrap_words(status,44)) {
                Render::move_cursor(row++,4);
                std::cout<<line;
            }
            Render::move_cursor(row+1,4);
            std::cout << "Settings: edit server / disconnect.";
            Render::move_cursor(row+2,4);
            std::cout << "Retry: reconnect.";
            Render::flush();
        }
        for (const auto& [key, action] : context.input_services().bindings.entries()) {
            const bool down = context.input_services().key_down(key);
            if (action == Input::Action::OpenSettings) {
                if (down && !heldKeys[key]) {
                    if (network.connection == Network::ConnectionState::Connected)
                        Network::send_lobby_signal(context, Network::ClientLobbySignal::Cancel);
                    Settings::run_menu(context);
                    return LobbyWaitResult::Reconfigure;
                }
                heldKeys[key] = down;
            }
            if (action == Input::Action::Retry) {
                if (down && !heldKeys[key]) {
                    Network::initialize_network_services(context, context.user_settings().serverIp);
                    Settings::run_network_menu(context);
                    return LobbyWaitResult::Reconfigure;
                }
                heldKeys[key] = down;
            }
        }
        if (network.connection == Network::ConnectionState::Connected) {
            if (const auto signal = Network::receive_lobby_signal_for(context, std::chrono::milliseconds(10))) {
                if (*signal == Network::ServerLobbySignal::PairReady) pairReady = true;
                if (*signal == Network::ServerLobbySignal::ConfigExchangeReady && pairReady)
                    return LobbyWaitResult::Ready;
            }
            const auto now = std::chrono::steady_clock::now();
            if (pairReady && now-lastReady >= std::chrono::milliseconds(250)) {
                Network::send_lobby_signal(context, Network::ClientLobbySignal::ReadyForConfig);
                lastReady = now;
            }
        } else context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.01));
    }
}

std::uint32_t multiplayer_seed(Runtime::GameContext& context) {
    const auto seed = Network::receive_lobby_seed(context);
    Network::network_services(context).pieceTurns = {};
    Network::network_services(context).boardSync = {};
    Network::network_services(context).pieceTurns.epoch = seed;
    return seed;
}

void exchange_multiplayer_player_setup(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& local,
    Runtime::PlayerSessionState& remote) {
    Network::LobbyPlayerSetup localSetup;
    localSetup.hybridBoardSync=true;
    localSetup.visibleHeight = local.gameplay.playfield.activeHeight;
    localSetup.width = local.gameplay.playfield.width;
    // Finite maps can have fewer pieces than the configured NEXT count.
    // The setup protocol describes the actual available preview entries.
    localSetup.nextCount = static_cast<int>(local.pieces.nextPreview.size());
    localSetup.holdEnabled = local.gameplay.playfield.holdEnabled;
    localSetup.nextPreview = local.pieces.nextPreview;
    localSetup.wins = local.gameplay.scoring.wins;
    localSetup.blockMaxHeight = local.presentation.blockMaxHeight;
    localSetup.blockMaxWidth = local.presentation.blockMaxWidth;
    localSetup.simulationPps = context.config.simulationPps;
    localSetup.turnPieces = context.config.turnPieces;
    const auto remoteSetup = Network::exchange_lobby_player_setup(context, localSetup);
    Network::configure_piece_turns(context, remoteSetup.turnPieces,
        remoteSetup.playerIndex < 0 ? -1 : 1 - remoteSetup.playerIndex,
        Network::network_services(context).pieceTurns.epoch);
    Network::configure_board_sync(context,remoteSetup.hybridBoardSync,Network::network_services(context).pieceTurns.epoch);
    apply_remote_setup(
        local,
        remote,
        remoteSetup,
        context.config.mode.mode);
}

} // namespace App::InitDetail
