#include "tetr/network/network_services.h"
#include <algorithm>
namespace Network {
bool handle_room_config_message(NetworkServices& services,std::string_view wire) {
    if (wire.rfind("!config-",0)!=0) return false;
    const auto message=decode_config_message(wire);
    if (!message || message->token!=services.connectionToken || services.room.name.empty()) return true;
    auto& state=services.roomConfig;
    const auto& m=*message;
    if (m.kind==ConfigMessageKind::Begin) {
        if (m.roomInstance!=services.roomInstance) return true;
        if (m.epoch<=services.lastAppliedConfigEpoch || m.epoch<state.epoch) return true;
        const auto host=std::find_if(services.roomMembers.begin(),services.roomMembers.end(),[&](const auto& member) {
            return member.slot==m.hostSlot && member.host;
        });
        if (host==services.roomMembers.end()) return true; // Wait for a current roster.
        if (m.epoch!=state.epoch) {
            state={}; state.epoch=m.epoch; state.policy=m.policy; state.hostSlot=m.hostSlot;
            state.started=std::chrono::steady_clock::now();
        }
        return true;
    }
    if (!state.epoch || m.epoch!=state.epoch) return true;
    if (m.kind==ConfigMessageKind::Stored && state.hostSlot==services.roomPlayerIndex &&
        m.digest==state.uploadDigest && m.index<state.uploaded.size())
        state.uploaded[m.index]=true;
    if (m.kind==ConfigMessageKind::Part && state.incoming.insert(m)) {
        auto ack=m; ack.kind=ConfigMessageKind::Received; ack.data.clear();
        send_online(services,encode_config_message(ack));
    }
    return true;
}
void poll_room_config_transfer(NetworkServices& services) {
    auto& state=services.roomConfig;
    if (!state.epoch || state.finished || services.connection!=ConnectionState::Connected) return;
    const auto now=std::chrono::steady_clock::now();
    if (!state.applied && now-state.started>std::chrono::seconds(60)) {
        send_online(services,"end_"); services.roomMessage="Host configuration transfer timed out. Start again.";
        ++services.roomRevision; state={}; services.onlineInbox.clear(); services.onlineInbox.push_back("end2"); return;
    }
    if (state.hostSlot==services.roomPlayerIndex && !services.localRoomConfig.empty() &&
        !state.incoming.payload && now-state.lastUpload>=std::chrono::milliseconds(25)) {
        const auto count=room_config_chunk_count(services.localRoomConfig.size());
        if (services.localRoomConfig.size()>kMaxRoomConfigBytes) throw UdpException("Host configuration is too large.");
        if (state.uploaded.size()!=count) {state.uploaded.assign(count,false); state.uploadDigest=room_config_digest(services.localRoomConfig);}
        const auto digest=state.uploadDigest;
        std::size_t scanned=0,sent=0;
        while (scanned++<count && sent<32) {
            const auto index=state.uploadCursor++%count;
            if (!state.uploaded[index]) {
                send_online(services,encode_config_message(room_config_part(services.localRoomConfig,services.connectionToken,state.epoch,digest,index)));
                ++sent;
            }
        }
        state.lastUpload=now;
    }
    if (state.applied && state.incoming.payload && now-state.lastApplied>=std::chrono::milliseconds(250)) {
        ConfigMessage ack; ack.kind=ConfigMessageKind::Applied; ack.token=services.connectionToken;
        ack.epoch=state.epoch; ack.digest=state.incoming.digest;
        send_online(services,encode_config_message(ack)); state.lastApplied=now;
    }
}
void accept_room_config(NetworkServices& services) {
    if (!services.roomConfig.incoming.payload) throw ConnectionError("Host configuration is incomplete.");
    services.roomConfig.applied=true; services.lastAppliedConfigEpoch=services.roomConfig.epoch;
    services.roomConfig.lastApplied={}; poll_room_config_transfer(services);
}
}
