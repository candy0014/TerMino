#include "tetr/network/relay_server.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

namespace TetrTests::room_names_test {
struct NameClient {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    Network::NetworkServices network;UserConfig::Settings user;
    NameClient(){context.attach_network(network);context.attach_user_settings(user);}
    ~NameClient(){Network::disconnect_online(network);}
};
int run(int argc,char** argv) {
    using namespace Network;using namespace std::chrono_literals;
    const auto history=std::filesystem::temp_directory_path()/std::filesystem::u8path("termino-房间编码-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt");
    struct Cleanup {std::filesystem::path file;~Cleanup(){std::error_code error;std::filesystem::remove(file,error);}}cleanup{history};
    std::unique_ptr<RelayServer> server;
    if(argc<2){server=std::make_unique<RelayServer>(441,history.u8string());server->bind(3018);}
    const auto endpoint=argc>1?argv[1]:"127.0.0.1:3018";
    NameClient a,b;
    const auto wait=[&](const auto& done){
        const auto deadline=std::chrono::steady_clock::now()+10s;
        do {if(server)server->poll(1ms);poll_connection(a.network);poll_connection(b.network);
            if(done())return;std::this_thread::sleep_for(1ms);
        }while(std::chrono::steady_clock::now()<deadline);
        assert(false && "Room name test timed out");
    };
    const auto request=[&](NameClient& client,RoomOperation op,const std::string& name="",bool isPublic=false){
        request_room(client.network,op,name,isPublic);wait([&]{return !client.network.pendingRoomRequest;});
    };
    const auto valid=[](const std::string& code){return code.size()==4 && code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")==std::string::npos;};
    for(auto* client:{&a,&b})initialize_network_services(client->context,endpoint);
    wait([&]{return a.network.connection==ConnectionState::Connected && b.network.connection==ConnectionState::Connected;});
    request(a,RoomOperation::SuggestName);const auto first=a.network.suggestedRoomName;
    request(b,RoomOperation::SuggestName);const auto second=b.network.suggestedRoomName;
    assert(valid(first) && valid(second) && first!=second);
    assert(a.network.room.name.empty() && a.network.roomMembers.empty());
    request(a,RoomOperation::SuggestName);assert(a.network.suggestedRoomName==first);
    request(b,RoomOperation::Create,first);assert(b.network.roomMessage==room_result_message(RoomResult::NameTaken));
    request(a,RoomOperation::Create,first);assert(a.network.room.name==first && !a.network.room.isPublic);
    request(a,RoomOperation::Leave);
    request(a,RoomOperation::SuggestName);assert(valid(a.network.suggestedRoomName) && a.network.suggestedRoomName!=first && a.network.suggestedRoomName!=second);
    request(b,RoomOperation::Create);assert(b.network.room.name==second);
    const RoomRequest duplicate{b.network.connectionToken,b.network.nextRoomRequest,RoomOperation::Create,"",false};
    send_online(b.network,encode_room_request(duplicate));wait([&]{return b.network.room.name==second;});
    request(b,RoomOperation::Leave);
    if(server) {
        disconnect_online(a.network);disconnect_online(b.network);server.reset();
        server=std::make_unique<RelayServer>(441,history.u8string());server->bind(3018);
        initialize_network_services(a.context,endpoint);
        wait([&]{return a.network.connection==ConnectionState::Connected;});
        request(a,RoomOperation::SuggestName);
        assert(valid(a.network.suggestedRoomName) && a.network.suggestedRoomName!=first && a.network.suggestedRoomName!=second);
        std::ifstream input(history);std::string contents((std::istreambuf_iterator<char>(input)),{});
        assert(contents.find(first+'\n')!=std::string::npos && contents.find(second+'\n')!=std::string::npos);
    }
    std::cout<<"room_names_test: four uppercase letters, distinct reservations, private rooms, reuse prevention, explicit defaults and retries passed; restart persistence "<<(server?"passed locally":"verified locally only")<<"\n";
    return 0;
}
} // namespace TetrTests::room_names_test
