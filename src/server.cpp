#include "tetr/network/relay_server.h"

#include <cstdint>
#include <ctime>
#include <exception>
#include <iostream>

int main() {
    try {
        Network::RelayServer server(static_cast<std::uint32_t>(std::time(nullptr)));
        server.bind(3000);
        std::cerr << "TerMino relay listening on 0.0.0.0:3000/UDP (rooms: 2 players per room)\n";
        server.run();
    } catch (const std::exception& error) {
        std::cerr << "TerMino relay failed: " << error.what() << '\n';
        return 1;
    }
}
