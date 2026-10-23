#include "tetr/network/board_view.h"
#include "tetr/network/event_stream.h"
#include "tetr/network/network_services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/network/udp_socket.h"
#include "tetr/runtime/game_context.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
namespace TetrTests::board_view_stagger_test {
using namespace Network;
using namespace std::chrono_literals;
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};NetworkServices network;
    Player() {
        context.attach_network(network);auto& p=session->player;
        p.gameplay.playfield.width=10;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
        p.gameplay.playfield.field.initialize(10,20);p.pieces.bag.push_back({Piece::Type{0},false});
        p.pieces.nextPreview={Piece::Type{1}};p.gameplay.playfield.field.set_cell(19,0,Runtime::BoardCell::piece(0));
    }
    ~Player(){disconnect_online(network);}
};
int run(int argc,char** argv) {
    const int playerCount=argc>1?std::stoi(argv[1]):10;assert(playerCount>=4 && playerCount<=32);
    const bool steady=argc>2 && std::string(argv[2])=="--steady";
    const int fps=steady?30:6;
    RelayServer server(773);server.bind(3027);std::atomic<bool> running=true;
    std::thread worker([&]{while(running)server.poll(1ms);});
    struct Scope {std::atomic<bool>& run;std::thread& worker;~Scope(){run=false;worker.join();}} scope{running,worker};
    std::vector<std::unique_ptr<Player>> players;for(int i=0;i<playerCount;++i)players.push_back(std::make_unique<Player>());
    const auto pump=[&]{for(auto& p:players)poll_connection(p->network);std::this_thread::sleep_for(1ms);};
    const auto wait=[&](auto done){const auto end=std::chrono::steady_clock::now()+15s;while(!done() && std::chrono::steady_clock::now()<end)pump();assert(done());};
    const auto request=[&](Player& p,RoomOperation op,const std::string& name="",int value=-1){request_room(p.network,op,name,false,false,value);wait([&]{return !p.network.pendingRoomRequest;});};
    for(auto& p:players)initialize_network_services(p->context,"127.0.0.1:3027");
    wait([&]{for(auto& p:players)if(p->network.connection!=ConnectionState::Connected)return false;return true;});
    auto& host=*players.front();request(host,RoomOperation::Create,"Staggered-miniatures");
    for(std::size_t i=1;i<players.size();++i)request(*players[i],RoomOperation::Join,host.network.room.name);
    set_room_policy(host.network,{false});wait([&]{return !host.network.pendingRoomRequest;});request(host,RoomOperation::StartMatch);
    wait([&]{for(auto& p:players)if(p->network.matchEpoch!=host.network.matchEpoch)return false;return true;});
    LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=1;setup.hybridBoardSync=true;setup.nextPreview={Piece::Type{1}};
    for(auto& p:players){send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));}
    wait([&]{for(auto& p:players)if(p->network.playerSetups.size()!=players.size()-1)return false;return true;});
    // Hold in Preparing and count actual wire retries, independently of the
    // client's deduplicated setup map. Config polling must not flood setups.
    while(host.network.socket->try_receive()){}
    std::size_t setupRetries=0;
    const auto preparingUntil=std::chrono::steady_clock::now()+550ms;
    while(std::chrono::steady_clock::now()<preparingUntil) {
        for(std::size_t i=1;i<players.size();++i)poll_connection(players[i]->network);
        while(auto packet=host.network.socket->try_receive())
            if(auto routed=decode_player_packet(packet->data);routed && decode_lobby_player_setup(routed->data))++setupRetries;
        std::this_thread::sleep_for(1ms);
    }
    assert(setupRetries>=players.size()-1 && setupRetries<=3*(players.size()-1));
    std::cout<<players.size()<<" players preparing: "<<setupRetries<<" setup retries in 550ms\n";
    for(auto& p:players)request(*p,RoomOperation::SetReady,"",1);
    for(auto& p:players)send(p->network,"start6");
    wait([&]{for(auto& p:players)if(p->network.matchPhase!=RoomMatchPhase::Playing)return false;return true;});
    for(auto& p:players) {
        configure_board_sync(p->context,true,static_cast<std::uint32_t>(host.network.matchEpoch));BoardViews views{playerCount,{},steady};
        for(auto& peer:players)if(peer!=p)views.players.push_back({peer->network.roomPlayerIndex,false,8,12,fps});
        publish_board_views(p->context,views);
    }
    wait([&]{for(auto& p:players)if(!p->network.boardBudget || p->network.boardBudget->full)return false;return true;});
    for(std::size_t i=1;i<players.size();++i)send_board_state_frame(players[i]->context,players[i]->session->player,ActivePieceState::spawn(),{},true);
    std::size_t prior=0,maxBurst=0;int polls=0;std::optional<std::chrono::steady_clock::time_point> first;
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(host.network.peerMiniBoards.size()<players.size()-1 && std::chrono::steady_clock::now()<deadline) {
        poll_connection(host.network);const auto count=host.network.peerMiniBoards.size();
        if(count>prior){if(!first)first=std::chrono::steady_clock::now();maxBurst=std::max(maxBurst,count-prior);prior=count;}
        if(++polls%10==0)for(std::size_t i=1;i<players.size();++i)poll_connection(players[i]->network);
        std::this_thread::sleep_for(1ms);
    }
    assert(prior==players.size()-1 && first && maxBurst<=3);
    const double span=std::chrono::duration<double>(std::chrono::steady_clock::now()-*first).count();
    assert(span>=(steady?.008:.08));
    std::cout<<players.size()-1<<" miniature delivery: max batch="<<maxBurst<<"; spread="<<span<<" seconds\n";
    if(steady) {
        std::vector<std::uint64_t> before;
        for(std::size_t i=1;i<players.size();++i)before.push_back(host.network.peerMiniBoards.at(players[i]->network.roomPlayerIndex).receivedRevision);
        const auto start=std::chrono::steady_clock::now(),until=start+1100ms;
        auto nextSource=start;
        while(std::chrono::steady_clock::now()<until) {
            const auto now=std::chrono::steady_clock::now();
            if(now>=nextSource) {
                // Supply 60Hz to isolate the relay's requested 30Hz cadence
                // from a second, independently sampled sender rate limiter.
                nextSource=now+std::chrono::microseconds(16667);
                for(std::size_t i=1;i<players.size();++i) {
                    auto& p=*players[i];
                    p.session->player.gameplay.playfield.field.set_cell(19,9,Runtime::BoardCell::piece(p.network.miniSequence%7));
                    send(p.network,encode_mini_board(capture_mini_board(p.session->player,8,12,++p.network.miniSequence)));
                }
            }
            pump();
        }
        for(std::size_t i=1;i<players.size();++i) {
            const auto updates=host.network.peerMiniBoards.at(players[i]->network.roomPlayerIndex).receivedRevision-before[i-1];
            std::cout<<"source="<<i<<" steady updates="<<updates<<" / 1.1s at "<<fps<<"Hz"<<std::endl;
            assert(updates>=27 && updates<=35);
        }
        // Emulate the gateway on the host's UDP endpoint. Ordinary clients
        // must keep receiving raw packets; authenticated opt-in bundles only
        // the images already selected by one <=3-image scheduler batch.
        while(host.network.socket->try_receive()){}
        const auto capture=[&](bool enabled) {
            send_online(host.network,"!relay-batch-v1;"+(enabled?host.network.connectionToken:"wrong-token"));
            int batches=0,frames=0;auto next=std::chrono::steady_clock::now();
            const auto until=next+350ms;
            while(std::chrono::steady_clock::now()<until) {
                const auto now=std::chrono::steady_clock::now();
                if(now>=next) {
                    next=now+16ms;
                    for(std::size_t i=1;i<players.size();++i) {
                        auto& p=*players[i];
                        send(p.network,encode_mini_board(capture_mini_board(p.session->player,8,12,++p.network.miniSequence)));
                    }
                }
                while(auto packet=host.network.socket->try_receive()) {
                    if(packet->data.substr(0,4)!="TES1")continue;
                    const auto events=decode_event_stream(packet->data);
                    assert(enabled && events && events->size()>=2 && events->size()<=3);
                    ++batches;
                    for(const auto wire:*events) {
                        const auto routed=decode_player_packet(wire);
                        assert(routed && decode_mini_board(routed->data));++frames;
                    }
                }
                for(std::size_t i=1;i<players.size();++i)poll_connection(players[i]->network);
                std::this_thread::sleep_for(1ms);
            }
            assert(enabled?batches>0:batches==0);
            std::cout<<"relay batching="<<enabled<<"; batches="<<batches<<"; frames="<<frames<<std::endl;
        };
        capture(false);capture(true);
    }
    return 0;
}
} // namespace TetrTests::board_view_stagger_test
