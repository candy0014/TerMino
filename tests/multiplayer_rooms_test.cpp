#include "tetr/network/relay_server.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/round_statistics.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/app/init.h"
#include "tetr/app/application_state.h"
#include "tetr/input/services.h"
#include "tetr/replay/session_state.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/ui_text.h"
#include "../src/app/settings_navigation.h"
#include "tetr/config/multiplayer_config.h"
#include "tetr/runtime/game_context.h"
#include "support/network_gameplay_frames.h"
#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <future>
#include <sstream>
#include <atomic>
#include <mutex>

namespace TetrTests::multiplayer_rooms_test {
namespace {std::recursive_mutex roomRenderMutex;}

struct Member {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    UserConfig::Settings user;
    Network::NetworkServices network;
    Input::Services input;
    Replay::ReplaySessionState replay;
    App::ApplicationState app;
    Member() {context.attach_output_mutex(roomRenderMutex);context.attach_network(network);context.attach_user_settings(user);context.attach_input(input);context.attach_replay(replay);context.attach_app(app);app.primaryContext=&context;app.headlessLocalMatch=true;}
    ~Member() {Network::disconnect_online(network);}
};
namespace {
thread_local std::string* roomOutput=nullptr;
struct RoomOutputCapture : std::streambuf {
    std::streambuf* previous=std::cout.rdbuf(this);
    ~RoomOutputCapture(){std::cout.rdbuf(previous);}
    std::streamsize xsputn(const char* text,std::streamsize size) override {
        if(roomOutput)roomOutput->append(text,std::size_t(size));return size;
    }
    int overflow(int value) override {
        if(value!=EOF){const char ch=static_cast<char>(value);xsputn(&ch,1);}return value;
    }
};
}
int run(int argc,char** argv) {
    using namespace Network;using namespace std::chrono_literals;
    std::unique_ptr<RelayServer> server;
    if(argc<2) {server=std::make_unique<RelayServer>(903);server->bind(3005);}
    // Keep relay dispatch independent of the clients, as in the deployed
    // server. One packet per manual client tick starves 32-client heartbeats
    // under Wine's socket-call overhead and measures the fixture, not rooms.
    std::atomic<bool> relayRunning=true;std::thread relayWorker;
    if(server)relayWorker=std::thread([&]{while(relayRunning)server->poll(1ms);});
    struct RelayScope {std::atomic<bool>& running;std::thread& worker;~RelayScope(){running=false;if(worker.joinable())worker.join();}} relayScope{relayRunning,relayWorker};
    Member a,b,c,d,e;std::vector<Member*> clients{&a,&b,&c,&d,&e};
    const auto pump=[&] {
        // Sleep(1) can take a full Windows timer tick. Bound the elapsed wait
        // instead of multiplying that tick by 45 on every room operation.
        const auto until=std::chrono::steady_clock::now()+45ms;
        do {for(auto* p:clients) poll_connection(p->network);std::this_thread::sleep_for(1ms);}
        while(std::chrono::steady_clock::now()<until);
    };
    int waitNumber=0;
    const auto wait=[&](const auto& predicate) {
        ++waitNumber;
        const auto deadline=std::chrono::steady_clock::now()+10s;
        do {pump();if(predicate()) return;} while(std::chrono::steady_clock::now()<deadline);
        std::cerr<<"multiplayer state update timed out: wait "<<waitNumber<<" phases=";
        for(const auto* p:clients) std::cerr<<static_cast<int>(p->network.matchPhase)<<',';
        std::cerr<<'\n';assert(false && "multiplayer state update timed out");
    };
    const auto request=[&](Member& p,RoomOperation op,int value=-1,int battle=32,const std::string& name="") {
        request_room(p.network,op,name,false,false,value,{},battle);
        const auto deadline=std::chrono::steady_clock::now()+15s;
        do {pump();} while(p.network.pendingRoomRequest && std::chrono::steady_clock::now()<deadline);
        assert(!p.network.pendingRoomRequest);
    };
    for(auto* p:clients) initialize_network_services(p->context,argc>1?argv[1]:"127.0.0.1:3005");pump();
    const auto joinedBy=std::chrono::steady_clock::now()+10s;
    while(std::any_of(clients.begin(),clients.end(),[](const auto* p){return p->network.connection!=ConnectionState::Connected;}) && std::chrono::steady_clock::now()<joinedBy) pump();
    for(auto* p:clients) assert(p->network.connection==ConnectionState::Connected);
    const auto name="Multi-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    request(a,RoomOperation::Create,-1,32,name);
    assert(a.network.multiplayerRoom && a.network.room.capacity==32 && a.network.battleLimit==32);
    for(auto* p:{&b,&c,&d}) request(*p,RoomOperation::Join,-1,32,name);
    request(b,RoomOperation::SetLimits,4,2);assert(b.network.roomMessage==room_result_message(RoomResult::HostOnly));
    request(a,RoomOperation::SetLimits,3,3);assert(a.network.roomMessage==room_result_message(RoomResult::LimitBelowMembers));
    request(a,RoomOperation::SetLimits,4,2);
    assert(a.network.room.capacity==4 && a.network.battleLimit==2 && a.network.roomMembers[2].role==PlayerRole::Spectator);
    request(c,RoomOperation::SetRole,0);assert(c.network.roomMessage==room_result_message(RoomResult::BattleFull));
    request(e,RoomOperation::Join,-1,32,name);assert(e.network.roomMessage==room_result_message(RoomResult::Full));
    request(a,RoomOperation::SetLimits,4,4);request(c,RoomOperation::SetRole,0);
    set_room_policy(a.network,{false});pump();
    request(b,RoomOperation::StartMatch);assert(b.network.roomMessage==room_result_message(RoomResult::HostOnly));
    request(a,RoomOperation::StartMatch);
    assert(a.network.matchPhase==RoomMatchPhase::Preparing && b.network.matchNoticeEpoch==a.network.matchEpoch);
    request(a,RoomOperation::ForceStart);assert(a.network.roomMessage==room_result_message(RoomResult::NeedReadyPlayers));
    LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=2;setup.hybridBoardSync=true;setup.holdEnabled=true;setup.blockMaxWidth=3;setup.blockMaxHeight=2;
    setup.nextPreview={Piece::Type{0},Piece::Type{1}};
    const auto enter=[&] {
        wait([&]{for(auto* p:{&a,&b,&c,&d}) if(p->network.matchEpoch!=a.network.matchEpoch) return false;return true;});
        for(auto* p:{&a,&b,&c,&d}) {
            p->session->player.multiplayer={}; // Normal board initialization resets per-round combat state.
            send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));
        }
        wait([&]{return a.network.playerSetups.size()==3;});
    };
    enter();
    request(a,RoomOperation::SetReady,1);assert(a.network.matchPhase==RoomMatchPhase::Preparing && a.network.roomMembers[0].ready);
    request(b,RoomOperation::SetReady,1);assert(a.network.matchPhase==RoomMatchPhase::Preparing && b.network.roomMembers[1].ready);
    request(c,RoomOperation::SetReady,1);assert(c.network.roomMembers[2].ready);
    wait([&]{return a.network.matchPhase==RoomMatchPhase::Countdown;});
    send(a.network,"start6");send(b.network,"start6");pump();assert(a.network.matchPhase==RoomMatchPhase::Countdown);
    send(c.network,"start6");wait([&]{return a.network.matchPhase==RoomMatchPhase::Playing;});
    for(auto* p:{&a,&b,&c,&d}) {configure_board_sync(p->context,true,99);p->network.onlineInbox.clear();}
    for(int variant=0;variant<3;++variant) {
        send_board_state_frame(a.context,a.session->player,Test::prepare_online_board(a.context,variant));pump();
        for(auto* p:{&b,&c,&d}) {
            auto packet=try_receive_multiplayer_frame(p->context);
            const auto deadline=std::chrono::steady_clock::now()+1s;
            while(!packet && std::chrono::steady_clock::now()<deadline) {pump();packet=try_receive_multiplayer_frame(p->context);}
            assert(packet && packet->sourceSlot==0);Test::check_online_board(*packet,variant);
        }
    }
    if(argc>2 && std::string(argv[2])=="--soak") {
        // Keep several real WebSockets exchanging 60 Hz boards beyond the
        // gateway's ping interval; count deliveries rather than relying on a probe.
        for(auto* p:{&a,&b,&c}) Test::prepare_online_board(p->context,0);
        const auto end=std::chrono::steady_clock::now()+30s;
        auto next=std::chrono::steady_clock::now();int frames=0;
        std::map<Member*,int> received;
        while(std::chrono::steady_clock::now()<end) {
            const auto now=std::chrono::steady_clock::now();
            if(now>=next) {
                for(auto* p:{&a,&b,&c}) send_board_state_frame(p->context,p->session->player,
                    ActivePieceState::active({{4,3+frames%2},Runtime::Rotation{}}));
                ++frames;next=now+std::chrono::microseconds(16667);
            }
            for(auto* p:clients) {
                poll_connection(p->network);assert(p->network.connection==ConnectionState::Connected);
                while(auto packet=try_receive_multiplayer_frame(p->context))
                    if(packet->motion || packet->opponentState) ++received[p];
            }
            std::this_thread::sleep_for(1ms);
        }
        for(auto* p:{&a,&b,&c,&d}) assert(received[p]>=frames*(p==&d?3:2)*9/10);
        std::cout<<"30-second board sync: sent "<<frames<<" frames per actor; received "<<
            received[&a]<<", "<<received[&b]<<", "<<received[&c]<<", "<<received[&d]<<" frames; all connected\n";
    }
    // Every recipient has an independent attack stream; a duplicate source packet cannot add garbage twice.
    const int target=a.network.targetSlot;assert(target==1 || target==2);
    send_attack_frame(a.context,{Runtime::MultiplayerSequenceId::from_wire(1),{},7});pump();
    auto* attacked=clients[target];std::optional<FramePacket> first;
    wait([&]{
        while(auto packet=try_receive_multiplayer_frame(attacked->context)) if(packet->type==FramePacketType::Attack) {first=std::move(packet);return true;}
        return false;
    });
    assert(first && first->type==FramePacketType::Attack && first->attack==7 && first->sourceSlot==0);
    send(a.network,encode_attack_frame({Runtime::MultiplayerSequenceId::from_wire(1),{},7}));pump();
    while(auto packet=try_receive_multiplayer_frame(attacked->context)) assert(packet->type!=FramePacketType::Attack);
    wait([&]{return a.network.deliveredAttack==1;});
    while(auto packet=try_receive_multiplayer_frame(d.context)) assert(packet->type!=FramePacketType::Attack);
    // Spectators cannot attack or eliminate a fighter.
    send_attack_frame(d.context,{Runtime::MultiplayerSequenceId::from_wire(1),{},100});pump();
    while(auto packet=try_receive_multiplayer_frame(a.context)) assert(packet->type!=FramePacketType::Attack);
    attacked->session->player.multiplayer.lastDamageSource=0;
    send_round_signal(attacked->context,OutgoingRoundSignal::LocalLoss);
    wait([&]{return std::any_of(a.network.roomMembers.begin(),a.network.roomMembers.end(),[&](const auto& m){return m.slot==target && !m.alive;});});
    assert(a.network.matchPhase==RoomMatchPhase::Playing);
    const auto dead=std::find_if(a.network.roomMembers.begin(),a.network.roomMembers.end(),[&](const auto& m){return m.slot==target;});
    assert(dead!=a.network.roomMembers.end() && !dead->alive && dead->killer==0);
    wait([&]{return std::any_of(attacked->network.roomMembers.begin(),attacked->network.roomMembers.end(),[&](const auto& m){return m.slot==target && !m.alive && m.killer==0;});});
    Engine::initialize_multiplayer_scene(attacked->context);attacked->session->roomScene.focusSlot=target;attacked->session->roomScene.selfAlive=true;
    Engine::update_multiplayer_scene(attacked->context);assert(attacked->session->roomScene.focusSlot==0);
    const int last=target==1?2:1;send_round_signal(clients[last]->context,OutgoingRoundSignal::LocalLoss);
    wait([&]{return a.network.matchPhase==RoomMatchPhase::Finished;});
    assert(a.network.matchPhase==RoomMatchPhase::Finished && a.network.winnerSlot==0);
    assert(Engine::compact_player_name("abcdefghijklmnopqrstuvwxyz",11)=="abcd...wxyz");
    assert(Util::TerminalText::width(Engine::compact_player_name("用户名非常非常非常长",9))<=9);
    // Force start selects only ready fighters and does not wait for an unready member.
    // A host restart leaves the guest's final board on screen. The guest
    // joins the next preparation only after pressing Retry, without a lobby
    // exception or a request to open the start screen.
    const auto check_guest_restart=[&](Member& guest) {
        wait([&]{return guest.network.matchPhase==RoomMatchPhase::Finished;});
        Engine::update_multiplayer_scene(guest.context);
        const auto finishedEpoch=guest.session->roomScene.epoch;
        const auto finalSurvivors=guest.session->roomScene.survivors;
        const auto finalFocus=guest.session->roomScene.focusSlot;
        assert(!guest.session->roomScene.selfAlive);
        request(a,RoomOperation::StartMatch);
        wait([&]{return guest.network.matchEpoch!=finishedEpoch && guest.network.matchPhase==RoomMatchPhase::Preparing;});
        guest.context.config.mode.mode=Runtime::GameMode::Multiplayer;
        guest.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
        guest.input.initialized=true;
        guest.user.keyCodes=UserConfig::default_key_codes();
        Input::refresh_bindings(guest.input,guest.user.keyCodes,guest.user.additionalKeyCodes);
        const int retry=guest.user.keyCodes[8];int retryReads=0;
        const auto waitingBy=std::chrono::steady_clock::now()+2s;
        guest.input.keyStateProvider=[&](int key) {
            assert(std::chrono::steady_clock::now()<waitingBy);
            if(key!=retry) return false;
            assert(guest.session->roomScene.epoch==finishedEpoch && !guest.session->roomScene.selfAlive);
            assert(guest.session->roomScene.survivors==finalSurvivors && guest.session->roomScene.focusSlot==finalFocus);
            for(const auto& m:guest.network.roomMembers) if(m.slot==guest.network.roomPlayerIndex) assert(!m.ready);
            return ++retryReads>=4;
        };
        std::ostringstream waitingOutput;auto* output=std::cout.rdbuf(waitingOutput.rdbuf());
        try {App::watch_room_match(guest.context,true);} catch(...) {std::cout.rdbuf(output);throw;}
        std::cout.rdbuf(output);guest.input.keyStateProvider={};
        assert(retryReads>=4 && !guest.app.startScreenRequested);
        assert(guest.network.connection==ConnectionState::Connected && guest.network.room.name==name);
        assert(waitingOutput.str().find(App::UiText::tr("The host has started the match."))!=std::string::npos);
        assert(waitingOutput.str().find(App::UiText::tr("Retry: Prepare match."))!=std::string::npos);
    };
    check_guest_restart(*attacked);
    enter();
    request(a,RoomOperation::SetReady,1);request(b,RoomOperation::SetReady,1);request(a,RoomOperation::ForceStart);
    assert(a.network.matchPhase==RoomMatchPhase::Countdown && !a.network.roomMembers[2].participating);
    Engine::initialize_multiplayer_scene(c.context);
    assert(c.session->roomScene.spectator && !c.session->roomScene.selfAlive);
    send(a.network,"start6");send(b.network,"start6");
    wait([&]{return b.network.matchPhase==RoomMatchPhase::Playing;});
    b.context.config.mode.mode=Runtime::GameMode::Multiplayer;
    b.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    Test::prepare_online_board(b.context,0);b.session->player.gameplay.playfield.holdEnabled=true;
    Engine::initialize_multiplayer_scene(b.context);
    assert(b.session->roomScene.finalists==std::vector<int>({0,1}));
    send_round_signal(b.context,OutgoingRoundSignal::LocalLoss);
    wait([&]{return b.network.matchPhase==RoomMatchPhase::Finished;});
    check_guest_restart(b); // The exact two-player finish -> host Retry trigger.
    const auto previous=a.network.matchEpoch;
    request(a,RoomOperation::CancelMatch);assert(a.network.matchPhase==RoomMatchPhase::Idle);
    send_online(b.network,encode_match_packet(previous,"end1"));pump();assert(a.network.matchPhase==RoomMatchPhase::Idle);
    // Force host configuration is distributed independently to all ready-screen entrants.
    set_room_policy(a.network,{true});pump();
    a.user.fieldWidth=12;a.user.fieldHeight=24;
    a.network.localRoomConfig=UserConfig::encode_multiplayer_config(a.user,a.context.config);
    request(a,RoomOperation::StartMatch);
    wait([&]{for(auto* p:{&a,&b,&c,&d}) if(p->network.matchEpoch!=a.network.matchEpoch) return false;return true;});
    for(auto* p:{&a,&b,&c,&d}) send(p->network,"start2");
    const auto deadline=std::chrono::steady_clock::now()+10s;
    do {pump();} while((!a.network.roomConfig.incoming.payload || !b.network.roomConfig.incoming.payload || !c.network.roomConfig.incoming.payload || !d.network.roomConfig.incoming.payload) && std::chrono::steady_clock::now()<deadline);
    for(auto* p:{&a,&b,&c,&d}) {assert(p->network.roomConfig.incoming.payload==a.network.localRoomConfig);accept_room_config(p->network);}pump();
    request(a,RoomOperation::CancelMatch);
    if(server) {
        // The real App must build every normal game board before declaring it ready.
        request(a,RoomOperation::StartMatch);
        for(auto* p:{&a,&b,&c,&d}) {p->user.gameMode=4;p->user.serverIp=p->network.serverIp;}
        RoomOutputCapture capture;std::vector<std::string> screens(4);
        std::vector<std::future<void>> initialized;
        int index=0;
        for(auto* p:{&a,&b,&c,&d}) {
            auto* screen=&screens[index++];
            initialized.push_back(std::async(std::launch::async,[p,screen]{roomOutput=screen;App::initialize_game(p->context);roomOutput=nullptr;}));
        }
        const auto timeout=std::chrono::steady_clock::now()+10s;
        const auto finished=[&] {
            return std::all_of(initialized.begin(),initialized.end(),[](auto& future){return future.wait_for(0ms)==std::future_status::ready;});
        };
        while(!finished() && std::chrono::steady_clock::now()<timeout) {std::this_thread::sleep_for(1ms);}
        assert(finished());for(auto& future:initialized) future.get();
        assert(a.network.matchPhase==RoomMatchPhase::Preparing);
        for(const auto& screen:screens) {
            assert(!screen.empty());
            assert(screen.find("HOLD")==std::string::npos && screen.find("NEXT")==std::string::npos && screen.find("PPS")==std::string::npos);
        }
        for(auto* p:{&a,&b,&c,&d}) assert(p->session->player.gameplay.playfield.width==12);
        int confirmPolls=0;
        a.input.initialized=true;
        a.input.keyStateProvider=[&](int key) {
            if(key!=App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Confirm))return false;
            const int readyCount=static_cast<int>(std::count_if(a.network.roomMembers.begin(),a.network.roomMembers.end(),[](const auto& m){return m.role==PlayerRole::Battle && m.ready;}));
            return readyCount>=3 && ++confirmPolls>=3;
        };
        std::vector<std::future<bool>> ready;
        index=0;
        for(auto* p:{&a,&b,&c,&d}) {
            auto* screen=&screens[index++];
            ready.push_back(std::async(std::launch::async,[p,screen]{roomOutput=screen;const auto result=App::wait_for_room_ready(p->context);roomOutput=nullptr;return result;}));
        }
        while(std::any_of(ready.begin(),ready.end(),[](auto& future){return future.wait_for(0ms)!=std::future_status::ready;}) && std::chrono::steady_clock::now()<timeout)
            {std::this_thread::sleep_for(1ms);}
        for(std::size_t i=0;i<ready.size();++i) {assert(ready[i].wait_for(0ms)==std::future_status::ready);assert(ready[i].get()==(i<3));}
        a.input.keyStateProvider={};
        for(std::size_t i=0;i<screens.size();++i) {
            const auto& screen=screens[i];
            if(screen.find("HOLD")==std::string::npos || screen.find("NEXT")==std::string::npos)
                std::cerr<<"Missing ready board: client="<<i<<" hold="<<clients[i]->session->player.gameplay.playfield.holdEnabled
                    <<" next="<<clients[i]->session->player.gameplay.playfield.nextCount<<" bytes="<<screen.size()<<'\n';
            assert(screen.find("HOLD")!=std::string::npos && screen.find("NEXT")!=std::string::npos);
        }
        request(a,RoomOperation::CancelMatch);
    }
    request(a,RoomOperation::TransferHost,2);wait([&]{return is_room_host(c.network);});
    request(c,RoomOperation::Leave);wait([&]{return a.network.room.players==3;});
    int hosts=0;for(const auto& m:a.network.roomMembers) hosts+=m.host;assert(hosts==1);
    if(server) {
        Member* host=nullptr;for(auto* p:{&a,&b,&d}) if(is_room_host(p->network)) host=p;
        assert(host);request(*host,RoomOperation::SetLimits,32,32);
        request(e,RoomOperation::Join,-1,32,name);
        std::vector<std::unique_ptr<Member>> extra;
        for(int index=0;index<29;++index) {
            extra.push_back(std::make_unique<Member>());auto* p=extra.back().get();clients.push_back(p);
            initialize_network_services(p->context,"127.0.0.1:3005");pump();request(*p,RoomOperation::Join,-1,32,name);
            if(index<28) assert(p->network.room.name==name);else assert(p->network.roomMessage==room_result_message(RoomResult::Full));
        }
        assert(a.network.room.players==32 && a.network.roomMembers.size()==32);
        // Restore the vector before destroying clients so the pump cannot access dangling pointers.
        clients={&a,&b,&c,&d,&e};extra.clear();pump();
    }
    // Offline membership and spectator UI changes are independent of the
    // authoritative roster for this epoch, even for the host.
    for(auto* p:clients) if(!p->network.room.name.empty()) request(*p,RoomOperation::Leave);
    const auto statusRoom=name+"-state";
    request(a,RoomOperation::Create,-1,32,statusRoom);
    for(auto* p:{&b,&c,&d}) request(*p,RoomOperation::Join,-1,32,statusRoom);
    set_room_policy(a.network,{false});pump();
    // Menu polling repeats synchronization after an in-flight policy request.
    // Public RTT can exceed one pump; a one-shot call would skip SetRole.
    wait([&]{return !a.network.pendingRoomRequest;});
    a.user.gameMode=0;synchronize_room_player_mode(a.network,false);
    wait([&]{return a.network.roomMembers[0].role==PlayerRole::Offline;});
    assert(a.network.room.players==4 && a.network.room.name==statusRoom);
    if(server) {
        Render::begin_output_suppression();App::initialize_game(a.context);Render::end_output_suppression();
        assert(a.context.config.mode.mode!=Runtime::GameMode::Multiplayer);
        assert(a.network.connection==ConnectionState::Connected && a.network.room.name==statusRoom);
    }
    request(a,RoomOperation::StartMatch);
    assert(a.network.roomMembers[0].role==PlayerRole::Spectator && !a.network.roomMembers[0].participating);
    const auto spectatorEpoch=a.network.matchEpoch;
    for(auto* p:{&a,&b,&c,&d}) {send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));}
    pump();
    request(a,RoomOperation::SetRole,0); // Reserve Battle for the next epoch.
    assert(a.network.roomMembers[0].role==PlayerRole::Battle && !a.network.roomMembers[0].participating);
    request(b,RoomOperation::SetReady,1);request(c,RoomOperation::SetReady,1);
    assert(a.network.matchPhase==RoomMatchPhase::Preparing);
    request(a,RoomOperation::ForceStart);
    // The host's acknowledgement can arrive before another WebSocket's roster.
    wait([&]{for(auto* p:{&a,&b,&c,&d}) if(p->network.matchEpoch!=spectatorEpoch || p->network.matchPhase!=RoomMatchPhase::Countdown)return false;return true;});
    for(auto* p:{&a,&d}) {Engine::initialize_multiplayer_scene(p->context);assert(p->session->roomScene.spectator);}
    assert(d.network.roomMembers[3].role==PlayerRole::Battle && !d.network.roomMembers[3].participating);
    send(b.network,"start6");send(c.network,"start6");wait([&]{return a.network.matchPhase==RoomMatchPhase::Playing;});
    for(auto* p:{&b,&c}) Engine::initialize_multiplayer_scene(p->context);
    send(a.network,"start7");send(d.network,"start7");pump();
    assert(a.network.matchEpoch==spectatorEpoch && a.network.matchPhase==RoomMatchPhase::Playing);
    assert(a.network.roomMembers[1].alive && a.network.roomMembers[2].alive);
    assert(handle_room_game_control(a.context,false));
    assert(!a.network.pendingRoomRequest && !a.network.startMatchQueued);
    request(d,RoomOperation::SetRole,2);send(d.network,"start7");pump();
    assert(a.network.matchEpoch==spectatorEpoch && a.network.roomMembers[1].alive && a.network.roomMembers[2].alive);
    if(server) {
        a.user.gameMode=4;
        for(int attempt=0;attempt<2;++attempt) {
            a.user.fieldWidth=13+attempt;
            Render::begin_output_suppression();
            auto viewed=std::async(std::launch::async,[&]{App::initialize_game(a.context);});
            const auto until=std::chrono::steady_clock::now()+10s;
            while(viewed.wait_for(0ms)!=std::future_status::ready && std::chrono::steady_clock::now()<until)
                {std::this_thread::sleep_for(1ms);}
            assert(viewed.wait_for(0ms)==std::future_status::ready);viewed.get();Render::end_output_suppression();
            assert(a.session->roomScene.spectator && !a.session->roomScene.selfAlive);
            assert(a.network.matchEpoch==spectatorEpoch && a.network.matchPhase==RoomMatchPhase::Playing);
            send(a.network,"start7");pump();
        }
    }
    // Complete results are uploaded independently of combat snapshots and
    // remain retrievable by spectators after the host starts the next round.
    auto& score=b.session->player.gameplay.scoring;score.placedBlocks=25;score.totalOutgoingAttack=17;score.totalReceivedAttack=13;
    score.roundPieceTypes={0};for(int lines=0;lines<2048;++lines)score.clearStatistics[{0,lines}]={1000,11,3};
    b.session->player.timing.completedGameTime=Runtime::Duration::from_seconds(65.125);
    b.session->player.timing.completedRealTime=Runtime::Duration::from_seconds(70.5);
    send_round_statistics(b.context,b.session->player);
    wait([&]{return a.network.roundResultWires[spectatorEpoch].count(1) && d.network.roundResultWires[spectatorEpoch].count(1) && !b.network.pendingResultEpoch;});
    const auto saved=b.network.roundResultWires[spectatorEpoch].at(1);
    assert(a.network.roundResultWires[spectatorEpoch].at(1)==saved && decode_round_statistics(saved)->score.clearStatistics==score.clearStatistics);
    // An unready/offline spectator cannot add a fabricated participant result.
    for(const auto& part:round_statistics_parts(saved))send_online(d.network,encode_match_packet(spectatorEpoch,part));pump();
    assert(!a.network.roundResultWires[spectatorEpoch].count(3));
    // A late upload can finish after the epoch changes, using the archived roster.
    request(a,RoomOperation::StartMatch);
    assert(a.network.matchEpoch!=spectatorEpoch && a.network.roomMembers[0].participating);
    for(const auto& part:round_statistics_parts(saved))send_online(c.network,encode_match_packet(spectatorEpoch,part));
    wait([&]{return a.network.roundResultWires[spectatorEpoch].count(2);});
    d.network.roundResultWires[spectatorEpoch].erase(1);d.network.resultIncoming[spectatorEpoch].erase(1);
    const auto retrievedBy=std::chrono::steady_clock::now()+10s;
    while(!d.network.roundResultWires[spectatorEpoch].count(1) && std::chrono::steady_clock::now()<retrievedBy) {
        request_round_statistics(d.network,spectatorEpoch,1);pump();
    }
    assert(d.network.roundResultWires[spectatorEpoch].at(1)==saved);
    Engine::initialize_multiplayer_scene(a.context);assert(!a.session->roomScene.spectator);
    assert(a.network.roomMembers[3].role==PlayerRole::Offline && !a.network.roomMembers[3].participating);
    request(a,RoomOperation::CancelMatch);
    std::cout<<"multiplayer rooms passed\n";
    return 0;
}
} // namespace TetrTests::multiplayer_rooms_test
