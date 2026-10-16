#include "tetr/network/network_services.h"
#include <algorithm>
namespace Network {
void set_room_config_preview(NetworkServices& services,const std::string& payload) {
    if(!is_room_host(services) || !services.roomPolicy.forceRules || payload==services.localRoomPreview) return;
    if(payload.empty() || payload.size()>kMaxRoomConfigBytes) return;
    services.localRoomPreview=payload;
    auto& state=services.roomPreview;state={};state.epoch=++services.nextPreviewEpoch;
    state.hostSlot=services.roomPlayerIndex;state.policy=services.roomPolicy;
    state.uploadDigest=room_config_digest(payload);
    state.uploaded.assign(room_config_chunk_count(payload.size()),false);
    ++services.previewRevision;
}
static void poll_preview_transfer(NetworkServices& services) {
    auto& state=services.roomPreview;
    if(!state.epoch || !is_room_host(services) || !services.roomPolicy.forceRules || services.localRoomPreview.empty()) return;
    const auto now=std::chrono::steady_clock::now();
    const bool complete=std::all_of(state.uploaded.begin(),state.uploaded.end(),[](bool stored){return stored;});
    if(now-state.lastUpload<(complete?std::chrono::milliseconds(1000):std::chrono::milliseconds(25))) return;
    ConfigMessage begin;begin.preview=true;begin.token=services.connectionToken;begin.epoch=state.epoch;
    begin.roomInstance=services.roomInstance;begin.hostSlot=state.hostSlot;begin.policy=services.roomPolicy;
    send_online(services,encode_config_message(begin));
    const auto count=state.uploaded.size();std::size_t scanned=0,sent=0;
    while(scanned++<count && sent<32) {
        const auto index=state.uploadCursor++%count;
        if(state.uploaded[index]) continue;
        auto part=room_config_part(services.localRoomPreview,services.connectionToken,state.epoch,state.uploadDigest,index);
        part.preview=true;send_online(services,encode_config_message(part));++sent;
    }
    state.lastUpload=now;
}
bool handle_room_config_message(NetworkServices& services,std::string_view wire) {
    if (wire.rfind("!config-",0)!=0 && wire.rfind("!preview-",0)!=0) return false;
    const auto message=decode_config_message(wire);
    if (!message || message->token!=services.connectionToken || services.room.name.empty()) return true;
    const auto& m=*message;
    auto& state=m.preview?services.roomPreview:services.roomConfig;
    if (m.kind==ConfigMessageKind::Begin) {
        if (m.roomInstance!=services.roomInstance) return true;
        if ((!m.preview && m.epoch<=services.lastAppliedConfigEpoch) || m.epoch<state.epoch) return true;
        const auto host=std::find_if(services.roomMembers.begin(),services.roomMembers.end(),[&](const auto& member) {
            return member.slot==m.hostSlot && member.host;
        });
        if (host==services.roomMembers.end()) return true; // Wait for a current roster.
        if (m.epoch!=state.epoch) {
            state={}; state.epoch=m.epoch; state.policy=m.policy; state.hostSlot=m.hostSlot;
            state.started=std::chrono::steady_clock::now();
            if(m.preview) ++services.previewRevision;
        }
        return true;
    }
    if (!state.epoch || m.epoch!=state.epoch) return true;
    if (m.kind==ConfigMessageKind::Stored && state.hostSlot==services.roomPlayerIndex &&
        m.digest==state.uploadDigest && m.index<state.uploaded.size())
        state.uploaded[m.index]=true;
    const bool complete=state.incoming.payload.has_value();
    if (m.kind==ConfigMessageKind::Part && state.incoming.insert(m)) {
        auto ack=m; ack.kind=ConfigMessageKind::Received; ack.data.clear();
        send_online(services,encode_config_message(ack));
        if(m.preview && !complete && state.incoming.payload) ++services.previewRevision;
    }
    return true;
}
void poll_room_config_transfer(NetworkServices& services) {
    if(services.connection==ConnectionState::Connected) poll_preview_transfer(services);
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
