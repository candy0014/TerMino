#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/input/services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "support/network_gameplay_frames.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif
#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

namespace {
using namespace std::chrono_literals;
void countdown_clock_rejects_stale_and_delayed_samples() {
    Network::NetworkServices n;n.multiplayerRoom=true;n.matchEpoch=7;n.matchPhase=Network::RoomMatchPhase::Countdown;
    const auto now=std::chrono::steady_clock::now();n.countdownProbes[1]=now-100ms;
    const auto reply=Network::encode_match_packet(7,Network::encode_countdown_clock({1,2000000}));
    assert(Network::handle_multiplayer_message(n,reply) && n.countdownDeadline);
    assert(*n.countdownDeadline>now+1940ms && *n.countdownDeadline<now+1960ms);
    const auto deadline=*n.countdownDeadline;
    // A queued push or a worse RTT must not restart/extend the countdown.
    Network::handle_multiplayer_message(n,Network::encode_match_packet(7,Network::encode_countdown_clock({0,3000000})));
    n.countdownProbes[2]=now-500ms;
    Network::handle_multiplayer_message(n,Network::encode_match_packet(7,Network::encode_countdown_clock({2,3000000})));
    assert(n.countdownDeadline==deadline);
    n.countdownProbes[3]=now;
    Network::handle_multiplayer_message(n,Network::encode_match_packet(6,Network::encode_countdown_clock({3,0})));
    assert(n.countdownDeadline==deadline && n.countdownProbes.count(3));
    assert(!Network::decode_countdown_clock("!countdown-clock-v1;0"));
    assert(!Network::decode_countdown_clock("!countdown-clock-v1;1;3000001"));
    assert(!Network::decode_countdown_clock("!countdown-clock-v1;1;-2"));
}
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    UserConfig::Settings user;
    Network::NetworkServices network;
    Input::Services input;
    Replay::ReplaySessionState replay;
    App::ApplicationState app;
    std::atomic<bool> started=false,moved=false,guestRetry=false,drop=false;
    std::atomic<bool> allowConfirm=false,readySeen=false,stopMatch=false,finishedSeen=false,retryReleased=false,leaveResults=false;
    bool confirmSent=false;int confirmReads=0;
    std::atomic<std::int64_t> scheduledStart=0,actualStart=0;
    std::atomic<int> phaseSeen=-1,aliveSeen=-1;
    Player() {
        context.attach_user_settings(user);context.attach_network(network);context.attach_input(input);
        context.attach_replay(replay);context.attach_app(app);app.primaryContext=&context;app.headlessLocalMatch=true;
        user.gameMode=4;user.gravityInterval=-1;user.keyCodes=UserConfig::default_key_codes();
        user.serverIp="127.0.0.1:3017";input.initialized=true;
        input.keyStateProvider=[this](int key) {
            phaseSeen=static_cast<int>(network.matchPhase);aliveSeen=session->roomScene.selfAlive;
#ifdef _WIN32
            constexpr int confirm=VK_RETURN,ctrl=VK_LCONTROL,shift=VK_LSHIFT,stop=VK_F12;
#else
            constexpr int confirm=KEY_ENTER,ctrl=KEY_LEFTCTRL,shift=KEY_LEFTSHIFT,stop=KEY_F12;
#endif
            int ready=0;for(const auto& m:network.roomMembers)if(m.role==Network::PlayerRole::Battle && m.ready)++ready;
            if(network.matchPhase==Network::RoomMatchPhase::Preparing && ready>=2 && network.countdownClockAvailable)readySeen=true;
            if(key==confirm && allowConfirm && readySeen && !confirmSent && ++confirmReads>=3) {confirmSent=true;return true;}
            if(stopMatch && (key==ctrl || key==shift || key==stop))return true;
            if(network.countdownDeadline) scheduledStart=std::chrono::duration_cast<std::chrono::microseconds>(
                network.countdownDeadline->time_since_epoch()).count();
            const bool playing=session->gameStartTime.has_value();
            if(playing && session->player.presentation.activePose && !started) {
                actualStart=std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();started=true;
            }
            if(session->player.gameplay.scoring.placedBlocks>0) moved=true;
            if(session->roomScene.finished) {
                finishedSeen=true;
                if(key==user.keyCodes[8] && !guestRetry && !leaveResults)retryReleased=true;
                if(leaveResults && key==user.keyCodes[8])return true;
            }
            if(guestRetry && key==user.keyCodes[8]) return true;
            return drop && playing && !moved && key==user.keyCodes[3];
        };
        Input::refresh_bindings(input,user.keyCodes);
        App::install_start_screen_navigation(context);
    }
    ~Player() {Network::disconnect_online(network);}
};
}
int main(int argc,char** argv) {
    countdown_clock_rejects_stale_and_delayed_samples();
    Network::RelayServer server(421);server.bind(3017);
    Player host,guest;
    if(argc>1) host.user.serverIp=guest.user.serverIp=argv[1];
    const auto pump=[&] {
        server.poll(1ms);Network::poll_connection(host.network);Network::poll_connection(guest.network);
    };
    int waitNumber=0;
    const auto wait=[&](const auto& predicate,const auto& poll) {
        ++waitNumber;
        const auto end=std::chrono::steady_clock::now()+10s;
        while(!predicate() && std::chrono::steady_clock::now()<end) poll();
        if(!predicate()) std::cerr<<"round lifecycle timed out at wait "<<waitNumber
            <<" phases="<<host.phaseSeen<<','<<guest.phaseSeen<<" alive="<<host.aliveSeen<<','<<guest.aliveSeen
            <<" finished="<<host.finishedSeen<<','<<guest.finishedSeen<<" retry="<<guest.guestRetry<<'\n';
        assert(predicate());
    };
    Network::initialize_network_services(host.context,host.user.serverIp);
    Network::initialize_network_services(guest.context,guest.user.serverIp);
    wait([&]{return host.network.connection==Network::ConnectionState::Connected && guest.network.connection==Network::ConnectionState::Connected;},pump);
    Network::request_room(host.network,Network::RoomOperation::Create,"Round-lifecycle",false);
    wait([&]{return !host.network.pendingRoomRequest;},pump);
    Network::request_room(guest.network,Network::RoomOperation::Join,"Round-lifecycle",false);
    wait([&]{return !guest.network.pendingRoomRequest;},pump);
    Network::request_room(host.network,Network::RoomOperation::StartMatch);
    wait([&]{return !host.network.pendingRoomRequest;},pump);
    Render::begin_output_suppression();
    std::uint64_t previous=0;
    for(int round=0;round<4;++round) {
        for(auto* p:{&host,&guest}) {
            p->allowConfirm=false;p->readySeen=false;p->stopMatch=false;p->finishedSeen=false;p->retryReleased=false;p->leaveResults=false;
            p->guestRetry=false;p->drop=false;p->confirmSent=false;p->confirmReads=0;
        }
        auto initialize=[&](Player& p) {return std::async(std::launch::async,[&p]{App::initialize_game(p.context);});};
        auto initializedHost=initialize(host),initializedGuest=initialize(guest);
        wait([&]{return initializedHost.wait_for(0ms)==std::future_status::ready && initializedGuest.wait_for(0ms)==std::future_status::ready;},[&]{server.poll(1ms);});
        initializedHost.get();initializedGuest.get();
        assert(host.network.matchEpoch==guest.network.matchEpoch && host.network.matchEpoch!=previous);
        previous=host.network.matchEpoch;
        assert(!host.session->player.timing.completedGameTime && !guest.session->player.timing.completedGameTime);
        host.started=false;guest.started=false;host.scheduledStart=0;guest.scheduledStart=0;
        host.readySeen=false;guest.readySeen=false;guest.moved=false;
        const auto play=[&](Player& p) {return std::async(std::launch::async,[&p] {
            try {App::run(p.context);} catch(const Network::LobbyInterrupted&) {return true;}
            return false;
        });};
        auto first=play(host),second=play(guest);
        const auto done=[&]{return first.wait_for(0ms)==std::future_status::ready && second.wait_for(0ms)==std::future_status::ready;};
        wait([&]{return host.readySeen && guest.readySeen;},[&]{server.poll(1ms);});
        const auto holdUntil=std::chrono::steady_clock::now()+100ms;
        while(std::chrono::steady_clock::now()<holdUntil)server.poll(1ms);
        assert(!host.started && !guest.started);
        host.allowConfirm=true;
        wait([&]{return host.started.load() && guest.started.load();},[&]{
            const auto now=std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto deadline=host.scheduledStart.load();
            // Stall the relay across the start deadline: neither RoundReady
            // nor an end-of-countdown acknowledgement can arrive in time.
            if(round==0 && deadline && now>=deadline-100000 && now<deadline+300000)
                std::this_thread::sleep_for(1ms);
            else server.poll(1ms);
        });
        for(auto* p:{&host,&guest}) {
            assert(p->scheduledStart>0);
            const auto lateness=p->actualStart.load()-p->scheduledStart.load();
            assert(lateness>=0 && lateness<80000);
            std::cerr<<"round "<<round<<" start lateness "<<lateness<<" us\n";
        }
        guest.drop=true;wait([&]{return guest.moved.load();},[&]{server.poll(1ms);});guest.drop=false;
        if(round%2==0) {
            host.stopMatch=true;
            wait(done,[&]{server.poll(1ms);});assert(first.get() && second.get());host.stopMatch=false;
            wait([&]{return guest.network.matchEpoch!=previous && host.network.matchEpoch!=previous && !host.network.pendingRoomRequest;},pump);
            assert(host.network.matchPhase==Network::RoomMatchPhase::Preparing && guest.network.matchPhase==Network::RoomMatchPhase::Preparing);
        } else {
            guest.guestRetry=true;
            wait([&]{return host.finishedSeen && guest.finishedSeen;},[&]{server.poll(1ms);});
            guest.guestRetry=false;
            wait([&]{return host.retryReleased && guest.retryReleased;},[&]{server.poll(1ms);});
            // The completed round remains visible and nobody is ready until
            // a fresh Retry enters preparation, including the winner/host.
            host.leaveResults=true;guest.leaveResults=true;
            wait(done,[&]{server.poll(1ms);});assert(!first.get() && !second.get());
            assert(host.network.matchEpoch==previous && guest.network.matchEpoch==previous);
            for(const auto& m:host.network.roomMembers)assert(!m.ready);
            host.leaveResults=false;guest.leaveResults=false;
        }
    }
    // Enter one more scheduled round without running the gameplay loop. The
    // relay's receive wait starts before the deadline; its ONLY incoming
    // gameplay packet arrives just after it. No subsequent poll may rescue it.
    auto first=std::async(std::launch::async,[&]{App::initialize_game(host.context);});
    auto second=std::async(std::launch::async,[&]{App::initialize_game(guest.context);});
    wait([&]{return first.wait_for(0ms)==std::future_status::ready && second.wait_for(0ms)==std::future_status::ready;},[&]{server.poll(1ms);});
    first.get();second.get();
    Network::request_room(host.network,Network::RoomOperation::SetReady,{},true,false,1);
    wait([&]{return !host.network.pendingRoomRequest;},pump);
    Network::request_room(guest.network,Network::RoomOperation::SetReady,{},true,false,1);
    wait([&]{return !guest.network.pendingRoomRequest && host.network.countdownClockAvailable && guest.network.countdownClockAvailable;},pump);
    Network::request_room(host.network,Network::RoomOperation::ForceStart);
    wait([&]{return host.network.countdownDeadline && guest.network.countdownDeadline &&
        host.network.matchPhase==Network::RoomMatchPhase::Countdown;},pump);
    host.network.countdownClockRequested=guest.network.countdownClockRequested=false;
    for(int i=0;i<100;++i) server.poll(0ms);
    Network::configure_board_sync(host.context,true,321);Network::configure_board_sync(guest.context,true,321);
    guest.network.playerInbox.clear();guest.network.peerTimelines.clear();
    const auto active=Test::prepare_online_board(host.context,0);
    const auto deadline=*host.network.countdownDeadline;
    auto sender=std::thread([&]{
        std::this_thread::sleep_until(deadline+20ms);
        Network::send_board_state_frame(host.context,host.session->player,active,std::nullopt,true);
    });
    std::this_thread::sleep_until(deadline-20ms);
    server.poll(100ms);sender.join();
    std::optional<Network::FramePacket> received;
    const auto by=std::chrono::steady_clock::now()+2s;
    do {
        received=Network::try_receive_multiplayer_frame(guest.context);
        if(!received) std::this_thread::sleep_for(1ms);
    } while(!received && std::chrono::steady_clock::now()<by);
    assert(received);Test::check_online_board(*received,0);
    Render::end_output_suppression();
    std::cout<<"four manual online countdowns, live gameplay, host stop, guest forfeit, unready results and first-packet deadline boundary passed\n";
}
