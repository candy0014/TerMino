#include "tetr/network/relay_server.h"
#include "tetr/network/room_config_protocol.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace Network;
using namespace std::chrono_literals;

int main() {
    RelayServer server(843);server.bind(3038);
    struct Peer {
        UdpSocket socket;
        std::string token;
        RoomResponse room;
        std::uint64_t request=0,reply=0,configEpoch=0;
        int parts=0;
    };
    std::array<Peer,4> peers;
    const UdpAddress address{"127.0.0.1",3038};
    const auto pump=[&] {
        server.poll(0ms);
        for(auto& peer:peers)while(auto packet=peer.socket.try_receive()) {
            if(auto response=decode_room_response(packet->data)) {
                if(response->id)peer.reply=response->id;
                if(room_result_has_state(response->result))peer.room=*response;
            } else if(auto config=decode_config_message(packet->data)) {
                if(config->kind==ConfigMessageKind::Begin)peer.configEpoch=config->epoch;
                if(config->kind==ConfigMessageKind::Part)++peer.parts;
            }
        }
    };
    const auto wait=[&](auto done) {
        const auto until=std::chrono::steady_clock::now()+5s;
        while(!done() && std::chrono::steady_clock::now()<until) {pump();std::this_thread::sleep_for(1ms);}
        assert(done());
    };
    for(int i=0;i<4;++i) {
        auto& peer=peers[i];peer.socket.bind(0);peer.socket.set_non_blocking();peer.token="burst-peer-"+std::to_string(i);
    }
    for(auto& peer:peers) {
        peer.socket.send(address,"!join-v4;"+peer.token);
        for(int tick=0;tick<4;++tick)pump();
    }
    const auto request=[&](Peer& peer,RoomOperation operation) {
        RoomRequest value;value.token=peer.token;value.id=++peer.request;value.operation=operation;
        value.name="Config-burst";value.isPublic=false;value.matchEpoch=peer.room.matchEpoch;
        peer.socket.send(address,encode_room_request(value));
        wait([&]{return peer.reply==value.id;});
    };
    request(peers[0],RoomOperation::Create);
    for(int i=1;i<4;++i)request(peers[i],RoomOperation::Join);
    for(auto& peer:peers)request(peer,RoomOperation::Prepare);
    wait([&]{return peers[0].configEpoch!=0;});
    const std::string payload(3500,'x');const auto hash=room_config_digest(payload);
    for(std::size_t i=0;i<room_config_chunk_count(payload.size());++i) {
        peers[0].socket.send(address,encode_config_message(room_config_part(payload,peers[0].token,peers[0].configEpoch,hash,i)));
        pump();
    }
    wait([&]{return peers[1].parts>=4;});
    int before=0;for(const auto& peer:peers)before+=peer.parts;
    ConfigMessage ack;ack.kind=ConfigMessageKind::Received;ack.token=peers[1].token;
    ack.epoch=peers[0].configEpoch;ack.digest=hash;ack.index=0;
    const auto began=std::chrono::steady_clock::now();
    // A WAN can deliver repeated acknowledgements in a burst while other
    // viewers have not acknowledged their copies yet. This must not create
    // another whole-room upload for every received acknowledgement.
    for(int i=0;i<200;++i) {
        peers[1].socket.send(address,encode_config_message(ack));pump();
    }
    for(int i=0;i<64;++i)pump();
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
    int after=0;for(const auto& peer:peers)after+=peer.parts;
    const int pacedBursts=2+static_cast<int>(std::ceil(elapsed/.025));
    std::cout<<"200 duplicate config acknowledgements: "<<after-before<<" forwarded parts in "<<elapsed<<" seconds"<<std::endl;
    assert(after-before<=4*4*pacedBursts);
}
