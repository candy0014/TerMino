#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/frame_board_adapter.h"
#include "tetr/replay/recorder.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/player_session_state.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace TetrTests::game_context_test {
int run(int argc,char** argv) {
    Runtime::SessionState baselineSession;
    Runtime::RuntimeServices baselineServices;
    Runtime::GameContext baseline(baselineSession, baselineServices);
    Replay::ReplaySessionState baselineReplay;
    baseline.attach_replay(baselineReplay);

    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext custom(session, services);
    Replay::ReplaySessionState customReplay;
    custom.attach_replay(customReplay);
    custom.config.garbage.survivalApm = 77;
    custom.config.garbage.incomingDelay = Runtime::Duration::from_seconds(2.5);
    custom.config.handling.gravityInterval = Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.4));
    custom.config.handling.epld = Runtime::Duration::from_seconds(.55);
    custom.session.quickPlay.speed = 4;
    custom.session.quickPlay.altitude = 13;
    custom.session.quickPlay.floor = 3;

    Runtime::PlayerSessionState localPlayer;
    localPlayer.pieces.qpBagBreather = true;
    auto state = Engine::capture_frame_rule_state(localPlayer, custom);
    assert(state.qpSpeed == 4 && state.qpAltitude == 13 && state.qpFloor == 3);
    assert(state.survivalApm == 77 && state.garbageInTime.seconds() == 2.5);
    assert(state.gravitySpeed.interval().seconds() == .4 && state.epld.seconds() == .55 && state.qpBagBreather);

    const Runtime::GravityInterval baselineGravity = baseline.config.handling.gravityInterval;
    state.qpSpeed = 8;
    state.qpAltitude = 21;
    state.qpFloor = 6;
    state.survivalApm = 123;
    state.gravitySpeed = Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.125));
    state.epld = Runtime::Duration::from_seconds(.33);
    Engine::store_frame_rule_state(localPlayer, state, custom);
    assert(custom.session.quickPlay.speed == 8);
    assert(custom.session.quickPlay.altitude == 21);
    assert(custom.session.quickPlay.floor == 6);
    assert(custom.config.garbage.survivalApm == 77);
    assert(custom.config.handling.gravityInterval.interval().seconds() == .4);
    assert(custom.config.handling.epld.seconds() == .55);
    assert(Runtime::current_survival_apm(custom) == 123);
    assert(Runtime::current_gravity_interval(custom).interval().seconds() == .125);
    assert(Runtime::current_epld(custom).seconds() == .33);
    assert(baseline.config.handling.gravityInterval == baselineGravity);

    custom.session.gameStartTime = custom.now() - Runtime::Duration::from_seconds(1.0);
    const std::string baselineReplayBefore = Replay::session(baseline).data;
    Replay::add_replay(custom, "ctx", 1, 2, 3);
    assert(Replay::session(custom).data.find("{ctx,") == 0);
    assert(Replay::session(baseline).data == baselineReplayBefore);

    const auto baselineStart = baseline.session.gameStartTime;
    Engine::initialize_board_timers(custom, localPlayer);
    assert(custom.session.gameStartTime == localPlayer.timing.frameUpdate);
    assert(baseline.session.gameStartTime == baselineStart);

    const int baselineFloor = baseline.session.quickPlay.floor;
    custom.config.randomPieces = false;
    custom.config.quickPlay.fool = Runtime::FoolMode::Disabled;
    custom.config.quickPlay.defaultFloor = 4;
    custom.config.garbage.mode = Runtime::GarbageMode::None;
    localPlayer.gameplay.playfield.width = 10;
    localPlayer.gameplay.playfield.visibleHeight = 20;
    localPlayer.gameplay.playfield.nextCount = 1;
    localPlayer.pieces.allowedPieces = {
        Piece::Type{0}, Piece::Type{1}, Piece::Type{2}, Piece::Type{3},
        Piece::Type{4}, Piece::Type{5}, Piece::Type{6}};
    localPlayer.pieces.random.seed(123);
    localPlayer.multiplayer.lastDamageSource=23;
    localPlayer.multiplayer.sendId=Runtime::MultiplayerSequenceId::from_wire(9);
    localPlayer.multiplayer.receiveId=Runtime::MultiplayerSequenceId::from_wire(4);
    localPlayer.multiplayer.pendingSends.push_back({Runtime::MultiplayerSequenceId::from_wire(7),3});
    Engine::initialize_board(custom, localPlayer);
    assert(custom.session.quickPlay.floor == 4);
    assert(baseline.session.quickPlay.floor == baselineFloor);
    assert(localPlayer.pieces.bag.size() >= 2);
    assert(localPlayer.multiplayer.lastDamageSource==-1);
    assert(localPlayer.multiplayer.sendId==Runtime::MultiplayerSequenceId{} &&
        localPlayer.multiplayer.receiveId==Runtime::MultiplayerSequenceId{} && localPlayer.multiplayer.pendingSends.empty());

    // Multiplayer setup supplies the remote NEXT queue over the wire and asks
    // board initialization not to generate a local bag.  Such a remote player
    // intentionally has no allowed-piece set, so only the preloaded queue is
    // required by the initializeQueue=false contract.
    Runtime::PlayerSessionState remotePlayer;
    remotePlayer.gameplay.playfield.width = 10;
    remotePlayer.gameplay.playfield.visibleHeight = 20;
    remotePlayer.gameplay.playfield.nextCount = 2;
    remotePlayer.pieces.bag = {
        {Piece::Type{2}, false},
        {Piece::Type{5}, false},
    };
    assert(remotePlayer.pieces.allowedPieces.empty());
    Engine::initialize_board(custom, remotePlayer, false);
    assert((remotePlayer.pieces.nextPreview ==
            std::vector<Piece::Type>{Piece::Type{2}, Piece::Type{5}}));
    assert(remotePlayer.gameplay.playfield.field.width() == 10);
    assert(remotePlayer.gameplay.playfield.field.height() == 20);

    // Keep the local bag-generation invariant that originally made an empty
    // piece catalog fail clearly instead of dividing by zero.
    Runtime::PlayerSessionState invalidLocalPlayer;
    invalidLocalPlayer.gameplay.playfield.width = 10;
    invalidLocalPlayer.gameplay.playfield.visibleHeight = 20;
    invalidLocalPlayer.gameplay.playfield.nextCount = 1;
    bool rejectedEmptyLocalSet = false;
    try {
        Engine::initialize_board(custom, invalidLocalPlayer, true);
    } catch (const std::logic_error&) {
        rejectedEmptyLocalSet = true;
    }
    assert(rejectedEmptyLocalSet);

    Runtime::PlayerSessionState truncatedRemotePlayer;
    truncatedRemotePlayer.gameplay.playfield.width = 10;
    truncatedRemotePlayer.gameplay.playfield.visibleHeight = 20;
    truncatedRemotePlayer.gameplay.playfield.nextCount = 2;
    truncatedRemotePlayer.pieces.bag = {{Piece::Type{2}, false}};
    bool rejectedTruncatedRemoteQueue = false;
    try {
        Engine::initialize_board(custom, truncatedRemotePlayer, false);
    } catch (const std::logic_error&) {
        rejectedTruncatedRemoteQueue = true;
    }
    assert(rejectedTruncatedRemoteQueue);

    const int baselineWidth = baseline.config.field.width;
    custom.config.field.width = 37;
    assert(baseline.config.field.width == baselineWidth);
    std::cout << "game_context_test: OK\n";
    return 0;
}
} // namespace TetrTests::game_context_test
