#include "tetr/network/lobby_protocol.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include "tetr/compat/piece_encoding.h"
#include "tetr/util/text_codec.h"
#include "tetr/runtime/playfield.h"
#include "tetr/runtime/piece.h"

namespace Network {
namespace {

bool valid_nonnegative(int value) noexcept { return value >= 0; }
bool valid_piece_id(int value) {
    return value >= 0 && value < static_cast<int>(Piece::catalog().size());
}

} // namespace

namespace {
std::string room_hex(std::string_view text) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::optional<std::string> room_unhex(std::string_view text) {
    if (text.size() % 2 || text.size() > kMaxRoomNameBytes * 2) return std::nullopt;
    const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int a = digit(text[i]), b = digit(text[i+1]);
        if (a < 0 || b < 0) return std::nullopt;
        out += static_cast<char>((a << 4) | b);
    }
    return out;
}
bool room_token(std::string_view token) {
    return !token.empty() && token.size() <= 64 && token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") == std::string_view::npos;
}
}
std::string normalize_room_name(std::string_view name) {
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1));
}
bool valid_room_name(std::string_view name, bool allowEmpty) {
    if (name.empty()) return allowEmpty;
    if (name.size() > kMaxRoomNameBytes || normalize_room_name(name) != name) return false;
    // Require valid UTF-8 and reject terminal control characters.
    for (std::size_t i = 0; i < name.size();) {
        const unsigned char c = name[i++];
        if (c < 0x20 || c == 0x7f) return false;
        if (c < 0x80) continue;
        const int extra = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : -1;
        if (extra < 0 || i + extra > name.size()) return false;
        const unsigned char next = name[i];
        if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) ||
            (c == 0xf0 && next < 0x90) || (c == 0xf4 && next >= 0x90)) return false;
        for (int j = 0; j < extra; ++j) {
            const unsigned char tail = name[i++];
            if ((tail & 0xc0) != 0x80) return false;
        }
    }
    return true;
}
std::string encode_room_request(const RoomRequest& r) {
    auto wire = "!room;" + r.token + ";" + std::to_string(r.id) + ";" +
        std::to_string(static_cast<int>(r.operation)) + ";" + room_hex(r.name) + ";" + (r.isPublic ? "1" : "0");
    if (r.operation==RoomOperation::TransferHost || r.operation==RoomOperation::SetPolicy)
        wire += ";host-v1;"+std::to_string(r.targetSlot)+";"+(r.policy.forceRules?"1":"0")+";"+(r.policy.forceHandling?"1":"0");
    return wire;
}
std::optional<RoomRequest> decode_room_request(std::string_view wire) {
    const auto f = Util::Text::split(wire, ';');
    if ((f.size()!=6 && f.size()!=10) || f[0]!="!room" || !room_token(f[1])) return std::nullopt;
    const auto id=Util::Text::parse_integer<std::uint64_t>(f[2]);
    const auto op=Util::Text::parse_integer<int>(f[3]);
    const auto name=room_unhex(f[4]);
    if (!id || !*id || !op || *op<0 || *op>5 || !name || (f[5]!="0" && f[5]!="1") ||
        ((*op>=4)!=(f.size()==10))) return std::nullopt;
    RoomRequest r{std::string(f[1]),*id,static_cast<RoomOperation>(*op),*name,f[5]=="1"};
    if (f.size()==10) {
        const auto target=Util::Text::parse_integer<int>(f[7]);
        if (f[6]!="host-v1" || !target || *target<-1 || *target>255 ||
            (f[8]!="0" && f[8]!="1") || (f[9]!="0" && f[9]!="1")) return std::nullopt;
        r.targetSlot=*target; r.policy={f[8]=="1",f[9]=="1"};
    }
    return r;
}
std::string encode_username_update(const UsernameUpdate& update, bool reply) {
    return std::string(reply ? "!username-ok;" : "!username;") + update.token + ";" +
        std::to_string(update.revision) + ";" + room_hex(update.username);
}
std::optional<UsernameUpdate> decode_username_update(std::string_view wire, bool reply) {
    const auto fields = Util::Text::split_exact<4>(wire, ';');
    if (!fields || (*fields)[0] != (reply ? "!username-ok" : "!username") || !room_token((*fields)[1])) return std::nullopt;
    const auto revision = Util::Text::parse_integer<std::uint64_t>((*fields)[2]);
    const auto name = room_unhex((*fields)[3]);
    if (!revision || !*revision || !name || !valid_room_name(*name)) return std::nullopt;
    return UsernameUpdate{std::string((*fields)[1]), *name, *revision};
}
std::string encode_room_response(const RoomResponse& r) {
    std::string wire = "!room-reply;" + r.token + ";" + std::to_string(r.id) + ";" +
        std::to_string(static_cast<int>(r.result)) + ";" + room_hex(r.room.name) + ";" +
        (r.room.isPublic ? "1" : "0") + ";" + std::to_string(r.room.players) + ";" + std::to_string(r.playerIndex) + ";" +
        std::to_string(r.stateRevision) + ";" + std::to_string(r.room.capacity) + ";" + std::to_string(r.rooms.size());
    for (const auto& room : r.rooms) wire += ";" + room_hex(room.name) + ";" + std::to_string(room.players) + ";" + std::to_string(room.capacity);
    wire += ";" + std::to_string(r.members.size());
    for (const auto& member : r.members) wire += ";" + std::to_string(member.slot) + ";" + room_hex(member.username) + ";" + (member.host ? "1" : "0");
    if (r.hasPolicy) wire += std::string(";host-v1;")+(r.policy.forceRules?"1":"0")+";"+(r.policy.forceHandling?"1":"0")+";"+std::to_string(r.roomInstance);
    return wire;
}
std::optional<RoomResponse> decode_room_response(std::string_view wire) {
    if (wire.size() > 60000) return std::nullopt;
    const auto f = Util::Text::split(wire, ';');
    if (f.size() < 12 || f[0] != "!room-reply" || !room_token(f[1])) return std::nullopt;
    const auto integer = [&](std::size_t i, int minimum, int maximum) -> std::optional<int> {
        if (i >= f.size()) return std::nullopt;
        const auto value = Util::Text::parse_integer<int>(f[i]);
        return value && *value >= minimum && *value <= maximum ? value : std::nullopt;
    };
    const auto id = Util::Text::parse_integer<std::uint64_t>(f[2]);
    const auto result = integer(3,0,14), players = integer(6,0,256), index = integer(7,-1,255), capacity = integer(9,1,256);
    const auto name = room_unhex(f[4]);
    const auto revision = Util::Text::parse_integer<std::uint64_t>(f[8]);
    const auto roomCount = integer(10,0,256);
    if (!id || !result || !name || !valid_room_name(*name,true) || (f[5] != "0" && f[5] != "1") ||
        !players || !index || !revision || !capacity || *players > *capacity || !roomCount) return std::nullopt;
    RoomResponse r{std::string(f[1]),*id,static_cast<RoomResult>(*result),{*name,f[5]=="1",*players,*capacity},*index,{},*revision,{}};
    std::size_t at = 11;
    for (int i = 0; i < *roomCount; ++i, at += 3) {
        if (at + 2 >= f.size()) return std::nullopt;
        const auto entry = room_unhex(f[at]);
        const auto count = integer(at+1,1,256), limit = integer(at+2,1,256);
        if (!entry || !valid_room_name(*entry) || !count || !limit || *count > *limit) return std::nullopt;
        r.rooms.push_back({*entry,true,*count,*limit});
    }
    const auto memberCount = integer(at++,0,256);
    if (!memberCount) return std::nullopt;
    const auto end=at+*memberCount*3;
    if (f.size()!=end && f.size()!=end+3 && f.size()!=end+4) return std::nullopt;
    if (f.size()>=end+3) {
        if (f[end]!="host-v1" || (f[end+1]!="0" && f[end+1]!="1") || (f[end+2]!="0" && f[end+2]!="1")) return std::nullopt;
        r.hasPolicy=true; r.policy={f[end+1]=="1",f[end+2]=="1"};
        if (f.size()==end+4) {
            const auto instance=Util::Text::parse_integer<std::uint64_t>(f[end+3]);
            if (!instance) return std::nullopt;
            r.roomInstance=*instance;
        }
    }
    int hosts = 0;
    for (int i = 0; i < *memberCount; ++i, at += 3) {
        const auto slot = integer(at,0,*capacity-1);
        const auto username = room_unhex(f[at+1]);
        if (!slot || !username || !valid_room_name(*username) || (f[at+2] != "0" && f[at+2] != "1")) return std::nullopt;
        for (const auto& member : r.members) if (member.slot == *slot) return std::nullopt;
        const bool host = f[at+2] == "1"; hosts += host;
        r.members.push_back({*slot,*username,host});
    }
    if (r.result == RoomResult::Created || r.result == RoomResult::Joined ||
        r.result==RoomResult::HostTransferred || r.result==RoomResult::PolicyChanged) {
        if (r.room.name.empty() || !r.room.players || r.playerIndex < 0 || r.playerIndex >= r.room.capacity ||
            *memberCount != r.room.players || hosts != 1) return std::nullopt;
        bool self = false;
        for (const auto& member : r.members) if (member.slot == r.playerIndex) self = true;
        if (!self) return std::nullopt;
    } else if (*memberCount) return std::nullopt;
    if (r.result != RoomResult::Listed && *roomCount) return std::nullopt;
    return r;
}
std::string_view room_result_message(RoomResult result) {
    switch (result) {
        case RoomResult::Created: return "Room created. Choose Start game when ready.";
        case RoomResult::Joined: return "Joined room. Choose Start game when ready.";
        case RoomResult::Listed: return "Public rooms: select a room to join.";
        case RoomResult::Left: return "Left room.";
        case RoomResult::NameTaken: return "Room name already exists. Enter a different name.";
        case RoomResult::NotFound: return "Room does not exist. Enter the name again.";
        case RoomResult::Full: return "Room is full (2 players). Choose another room.";
        case RoomResult::InvalidName: return "Invalid room name. Use 1-48 UTF-8 bytes without control characters.";
        case RoomResult::AlreadyInRoom: return "Leave the current room before creating or joining another.";
        case RoomResult::Busy: return "Server busy. Try again later.";
        case RoomResult::HostTransferred: return "Host transferred.";
        case RoomResult::PolicyChanged: return "Host configuration policy updated.";
        case RoomResult::HostOnly: return "Only the room host can change this option.";
        case RoomResult::InvalidPlayer: return "Player is no longer in this room. Select another player.";
        case RoomResult::RoundBusy: return "Return to the room before changing the host or configuration policy.";
    }
    return {};
}

std::string_view encode_server_lobby_signal(ServerLobbySignal signal) noexcept {
    switch (signal) {
        case ServerLobbySignal::PairReady: return "start1";
        case ServerLobbySignal::ConfigExchangeReady: return "start3";
        case ServerLobbySignal::CountdownReady: return "start5";
        case ServerLobbySignal::RoundReady: return "start7";
    }
    return {};
}

std::optional<ServerLobbySignal> decode_server_lobby_signal(std::string_view wire) noexcept {
    if (wire == "start1") return ServerLobbySignal::PairReady;
    if (wire == "start3") return ServerLobbySignal::ConfigExchangeReady;
    if (wire == "start5") return ServerLobbySignal::CountdownReady;
    if (wire == "start7") return ServerLobbySignal::RoundReady;
    return std::nullopt;
}

std::string_view encode_client_lobby_signal(ClientLobbySignal signal) noexcept {
    switch (signal) {
        case ClientLobbySignal::ReadyForConfig: return "start2";
        case ClientLobbySignal::SetupReceived: return "start4";
        case ClientLobbySignal::CountdownComplete: return "start6";
        case ClientLobbySignal::Cancel: return "end_";
    }
    return {};
}

std::optional<ClientLobbySignal> decode_client_lobby_signal(std::string_view wire) noexcept {
    if (wire == "start2") return ClientLobbySignal::ReadyForConfig;
    if (wire == "start4") return ClientLobbySignal::SetupReceived;
    if (wire == "start6") return ClientLobbySignal::CountdownComplete;
    if (wire == "end_") return ClientLobbySignal::Cancel;
    return std::nullopt;
}

std::string encode_lobby_player_setup(const LobbyPlayerSetup& setup) {
    std::string wire = std::to_string(setup.visibleHeight) + ";" +
                       std::to_string(setup.width) + ";" +
                       std::to_string(setup.nextCount) + ";" +
                       std::to_string(setup.holdEnabled ? 1 : 0) + ";";
    for (Piece::Type piece : setup.nextPreview) wire += std::to_string(Compat::to_legacy(piece)) + ",";
    wire += ";" + std::to_string(setup.wins) + ";" +
            std::to_string(setup.blockMaxHeight) + ";" +
            std::to_string(setup.blockMaxWidth) + ";";
    if (setup.simulationPps > 0.0 || setup.turnPieces > 0.0) {
        std::ostringstream extension;
        extension.imbue(std::locale::classic());
        extension << "piece-time-v2;" << std::setprecision(17) << setup.simulationPps
                  << ';' << setup.playerIndex << ';' << setup.turnPieces << ';';
        wire += extension.str();
    }
    if (setup.hybridBoardSync) wire += "hybrid-v1;";
    return wire;
}

std::optional<LobbyPlayerSetup> decode_lobby_player_setup(std::string_view wire) {
    constexpr std::string_view capability="hybrid-v1;";
    const bool hybrid=wire.size()>=capability.size() && wire.substr(wire.size()-capability.size())==capability;
    if (hybrid) wire.remove_suffix(capability.size());
    const auto fields = Util::Text::split(wire, ';');
    if ((fields.size() != 9 && fields.size() != 12 && fields.size() != 13) ||
        !fields.back().empty())
        return std::nullopt;

    LobbyPlayerSetup setup; setup.hybridBoardSync=hybrid;
    if (fields.size() >= 12) {
        if (fields[8] != (fields.size() == 12 ? "piece-time-v1" : "piece-time-v2"))
            return std::nullopt;
        std::istringstream number{std::string(fields[9])};
        number.imbue(std::locale::classic());
        number >> std::noskipws >> setup.simulationPps;
        const auto index = Util::Text::parse_integer<int>(fields[10]);
        if (!number || !number.eof() || !std::isfinite(setup.simulationPps) ||
            setup.simulationPps < 0.0 || setup.simulationPps > 1000000.0 ||
            !index || *index < -1 || *index > 1) return std::nullopt;
        setup.playerIndex = *index;
        // v1 used one simulated second per turn, equivalent to y = x.
        setup.turnPieces = setup.simulationPps;
        if (fields.size() == 13) {
            std::istringstream quota{std::string(fields[11])};
            quota.imbue(std::locale::classic());
            quota >> std::noskipws >> setup.turnPieces;
            if (!quota || !quota.eof() || !std::isfinite(setup.turnPieces) ||
                setup.turnPieces < 0.0 || setup.turnPieces > 1000000.0 ||
                (setup.turnPieces > 0.0 && setup.turnPieces < 1e-9))
                return std::nullopt;
        }
    }
    const auto visibleHeight = Util::Text::parse_integer<int>(fields[0]);
    const auto width = Util::Text::parse_integer<int>(fields[1]);
    const auto nextCount = Util::Text::parse_integer<int>(fields[2]);
    const auto holdEnabled = Util::Text::parse_integer<int>(fields[3]);
    const auto wins = Util::Text::parse_integer<int>(fields[5]);
    const auto blockMaxHeight = Util::Text::parse_integer<int>(fields[6]);
    const auto blockMaxWidth = Util::Text::parse_integer<int>(fields[7]);
    if (!visibleHeight || !width || !nextCount || !holdEnabled || !wins ||
        !blockMaxHeight || !blockMaxWidth) return std::nullopt;
    if (*visibleHeight <= 0 || *visibleHeight > Runtime::Playfield::kStorageRows ||
        *width <= 0 || *width > Runtime::Playfield::kStorageColumns ||
        *nextCount < 0 || *nextCount > 100 || (*holdEnabled != 0 && *holdEnabled != 1) ||
        !valid_nonnegative(*wins) || !valid_nonnegative(*blockMaxHeight) ||
        *blockMaxHeight > Runtime::Playfield::kStorageRows ||
        !valid_nonnegative(*blockMaxWidth) ||
        *blockMaxWidth > Runtime::Playfield::kStorageColumns) return std::nullopt;

    setup.visibleHeight = *visibleHeight;
    setup.width = *width;
    setup.nextCount = *nextCount;
    setup.holdEnabled = *holdEnabled != 0;
    setup.wins = *wins;
    setup.blockMaxHeight = *blockMaxHeight;
    setup.blockMaxWidth = *blockMaxWidth;

    if (!fields[4].empty()) {
        const auto pieces = Util::Text::split(fields[4], ',');
        if (pieces.empty() || !pieces.back().empty()) return std::nullopt;
        setup.nextPreview.reserve(pieces.size() - 1);
        for (std::size_t i = 0; i + 1 < pieces.size(); ++i) {
            const auto piece = Util::Text::parse_integer<int>(pieces[i]);
            if (!piece || !valid_piece_id(*piece)) return std::nullopt;
            setup.nextPreview.push_back(Compat::piece_type_from_legacy(*piece));
        }
    }
    if (static_cast<int>(setup.nextPreview.size()) != setup.nextCount) return std::nullopt;
    return setup;
}

std::string encode_lobby_seed(std::uint32_t seed) {
    return std::to_string(seed);
}

std::optional<std::uint32_t> decode_lobby_seed(std::string_view wire) noexcept {
    return Util::Text::parse_integer<std::uint32_t>(wire);
}

} // namespace Network
