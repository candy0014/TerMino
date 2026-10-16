#include "tetr/input/services.h"
#include "init_multiplayer.h"

#include "tetr/app/settings_menu.h"
#include "tetr/app/start_screen_navigation.h"
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
#include "tetr/engine/multiplayer_scene.h"
#include "settings_navigation.h"
#include "multiplayer_battle.h"
#include "terminal_canvas.h"
#include "tetr/app/application_state.h"
#include "ui_text.h"

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
    network.matchCanceled=false;
    if(!network.multiplayerRoom) {network.roomConfig={};network.lastAppliedConfigEpoch=0;}
    network.localRoomConfig=UserConfig::encode_multiplayer_config(context.user_settings(),context.config);
    std::map<int,bool> heldKeys;
    for (const auto& [key, action] : context.input_services().bindings.entries())
        if (action == Input::Action::OpenSettings || action == Input::Action::Retry) heldKeys[key] = true;
    std::string shown;
    TerminalCanvas waitingSurface;
    auto shownSize=Render::terminal_size();
    bool pairReady = false;
    bool configLoaded = false;
    bool preparationSent = false;
    auto configMatchEpoch=network.matchEpoch;
    Settings::NavigationInput navigation(context);
    std::size_t rosterOffset=0,hostChoice=0;
    std::uint64_t shownRevision=~std::uint64_t{};
    auto lastReady = std::chrono::steady_clock::time_point{};
    Network::poll_connection(network);
    network.startFromMenu=false;
    for (;;) {
        Network::poll_connection(network);
        const bool matchInProgress=network.multiplayerRoom &&
            (network.matchPhase==Network::RoomMatchPhase::Playing || network.matchPhase==Network::RoomMatchPhase::Countdown);
        if(network.multiplayerRoom && !preparationSent && network.connection==Network::ConnectionState::Connected &&
           !matchInProgress) {
            Network::request_room(network,Network::RoomOperation::Prepare);
            preparationSent=true;
        }
        if(matchInProgress && network.connection==Network::ConnectionState::Connected &&
           std::chrono::steady_clock::now()-lastReady>=std::chrono::milliseconds(250)) {
            // A late observer joins the current round's configuration/data
            // stream; it does not enter or restart match preparation.
            Network::send_lobby_signal(context,Network::ClientLobbySignal::ReadyForConfig);
            lastReady=std::chrono::steady_clock::now();
        }
        if(configMatchEpoch!=network.matchEpoch) {configMatchEpoch=network.matchEpoch;configLoaded=false;}
        const bool activePreparation=!network.multiplayerRoom ||
            (network.matchPhase!=Network::RoomMatchPhase::Idle && network.matchPhase!=Network::RoomMatchPhase::Finished);
        const bool waitingStart=network.multiplayerRoom && (network.startMatchQueued || network.queuedMatchControl==Network::RoomOperation::Prepare ||
            network.queuedMatchControl==Network::RoomOperation::EndMatch ||
            (network.pendingRoomRequest && (network.pendingRoomRequest->operation==Network::RoomOperation::StartMatch ||
                                           network.pendingRoomRequest->operation==Network::RoomOperation::Prepare ||
                                           network.pendingRoomRequest->operation==Network::RoomOperation::EndMatch)));
        if (!waitingStart && activePreparation && network.roomConfig.incoming.payload && !configLoaded) {
            const auto host=UserConfig::decode_multiplayer_config(*network.roomConfig.incoming.payload);
            if (!host) {
                Network::send_lobby_signal(context,Network::ClientLobbySignal::Cancel);
                throw Network::LobbyInterrupted("Invalid host configuration. Return to the room and start again.");
            }
            context.config=UserConfig::apply_multiplayer_config(context.user_settings(),context.config,*host,
                network.roomConfig.policy.forceRules);
            if (Runtime::autoplay_enabled(context.config.autoplay.mode) && !Engine::initialize_autoplay(context)) {
                Network::send_lobby_signal(context,Network::ClientLobbySignal::Cancel);
                throw Network::LobbyInterrupted("The host's bot is unavailable on this client.");
            }
            if (Runtime::autoplay_enabled(context.config.autoplay.mode) && context.config.autoplay.mode!=Runtime::AutoplayMode::ChatGPT &&
                !context.config.autoplay.handlingSimulation && context.config.handling.gravityInterval.enabled() &&
                !context.config.handling.gravityInterval.is_instant()) context.config.handling.gravityInterval=Runtime::GravityInterval::disabled();
            Runtime::reset_dynamic_rules(context);
            Replay::session(context).configPrefix=Replay::encode_config_prefix(context.config);
            if(!network.roomConfig.applied) Network::accept_room_config(network);
            configLoaded=true;
        }
        if (network.connection == Network::ConnectionState::Connected && network.room.name.empty()) {
            Settings::run_network_menu(context);
            return LobbyWaitResult::Reconfigure;
        }
        if(network.connection==Network::ConnectionState::Failed) throw Network::ConnectionError(network.connectionMessage);
        const std::string status = matchInProgress ? UiText::tr("Joining current match...")+" / "+network.room.name :
            network.multiplayerRoom ? UiText::tr("Match start waiting")+" / "+network.room.name+" / "+
            UiText::tr(network.matchPhase==Network::RoomMatchPhase::Idle || network.matchPhase==Network::RoomMatchPhase::Finished?
                "Waiting for the host to start the match...":"Preparing player configuration...") :
            network.connectionMessage+" / Room: "+network.room.name+" / Waiting for another player...";
        if (shown != status || shownRevision!=network.roomRevision || shownSize!=Render::terminal_size()) {
            shown = status;
            shownRevision=network.roomRevision;
            shownSize=Render::terminal_size();
            if(network.multiplayerRoom && !matchInProgress) {
                paint_room_waiting(context,waitingSurface,shownSize.value_or(Render::TerminalSize{80,40}),hostChoice,rosterOffset,true);
            } else {
                Render::clear_screen();Render::set_ui_color(Render::ColorRole::Text,context.config.display.obscuredColors);
                Render::move_cursor(2,4);std::cout<<status;
                if(!network.multiplayerRoom) {
                    Render::move_cursor(4,4);std::cout<<"Settings: edit server / disconnect.";
                    Render::move_cursor(5,4);std::cout<<UiText::tr("Retry: reconnect.");
                }
                Render::flush();
            }
        }
        if(network.multiplayerRoom) {
            using Settings::NavigationKey;
            if(navigation.update(NavigationKey::PageUp,true)) {rosterOffset=rosterOffset>8?rosterOffset-8:0;shownRevision=~std::uint64_t{};}
            if(navigation.update(NavigationKey::PageDown,true)) {rosterOffset+=8;shownRevision=~std::uint64_t{};}
            if(Network::is_room_host(network) && network.matchPhase==Network::RoomMatchPhase::Preparing && navigation.update(NavigationKey::Confirm,false))
                Network::request_room(network,Network::RoomOperation::ForceStart);
            if(network.matchCanceled && network.matchPhase==Network::RoomMatchPhase::Idle) throw RoomSettingsRequested{};
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
                    if(network.multiplayerRoom) {
                        Network::send_lobby_signal(context,Network::ClientLobbySignal::Cancel);
                        return LobbyWaitResult::Reconfigure;
                    }
                    Network::initialize_network_services(context, context.user_settings().serverIp);
                    Settings::run_network_menu(context);
                    return LobbyWaitResult::Reconfigure;
                }
                heldKeys[key] = down;
            }
        }
        if (network.connection == Network::ConnectionState::Connected) {
            if (const auto signal = waitingStart || !activePreparation?std::optional<Network::ServerLobbySignal>{}:Network::receive_lobby_signal_for(context, std::chrono::milliseconds(10))) {
                if (*signal == Network::ServerLobbySignal::PairReady) pairReady = true;
                if (*signal == Network::ServerLobbySignal::ConfigExchangeReady && (pairReady || network.multiplayerRoom))
                    return LobbyWaitResult::Ready;
            }
            const auto now = std::chrono::steady_clock::now();
            if ((pairReady || network.multiplayerRoom) && now-lastReady >= std::chrono::milliseconds(250)) {
                Network::send_lobby_signal(context, Network::ClientLobbySignal::ReadyForConfig);
                lastReady = now;
            }
        } else context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.01));
        if(waitingStart || !activePreparation) context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.005));
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
    auto& network=Network::network_services(context);
    for(const auto& member:network.roomMembers) if(member.slot!=network.roomPlayerIndex)
        remote.presentation.username=member.username;
    if(network.multiplayerRoom) {
        network.countdownClockRequested=true;
        network.localPlayerSetup=Network::encode_lobby_player_setup(localSetup);
        Network::send_lobby_signal(context,Network::ClientLobbySignal::ReadyForConfig);
        Network::send_player_setup(context,localSetup);
        Network::configure_board_sync(context,true,network.pieceTurns.epoch);
        network.pieceTurns.enabled=context.config.turnPieces>0;
        network.pieceTurns.playerIndex=network.roomPlayerIndex;network.pieceTurns.piecesPerTurn=context.config.turnPieces;
        const auto fallback=network.playerSetups.empty()?localSetup:network.playerSetups.begin()->second;
        apply_remote_setup(local,remote,fallback,context.config.mode.mode);
        return;
    }
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
