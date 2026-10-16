#include "tetr/network/relay_server.h"

#include <cstdint>
#include <ctime>
#include <exception>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace {
std::optional<std::filesystem::path> environment_path(const char* name) {
#ifdef _WIN32
    const std::wstring key(name,name+std::char_traits<char>::length(name));
    if(const auto* value=::_wgetenv(key.c_str()))return std::filesystem::path(value);
#else
    if(const auto* value=std::getenv(name))return std::filesystem::u8path(value);
#endif
    return std::nullopt;
}
}

int main() {
    try {
        std::filesystem::path history;
        if(const auto configured=environment_path("TETR_ROOM_CODE_HISTORY"))history=*configured;
        else if(const auto state=environment_path("XDG_STATE_HOME"))history=*state/"termino/room-codes.txt";
        else if(const auto profile=environment_path("HOME"))history=*profile/".local/state/termino/room-codes.txt";
        else if(const auto profile=environment_path("USERPROFILE"))history=*profile/".termino/room-codes.txt";
        else history="room-codes.txt";
        Network::RelayServer server(static_cast<std::uint32_t>(std::time(nullptr)),history.u8string());
        server.bind(3000);
        std::cerr << "TerMino relay listening on 0.0.0.0:3000/UDP (rooms: up to 32 players; legacy rooms: 2)\n";
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "TerMino relay failed: " << error.what() << '\n';
        return 1;
    }
}
