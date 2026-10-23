#include "online_replay.h"
#include "settings_navigation.h"
#include "settings_view.h"
#include "terminal_canvas.h"
#include "post_game_statistics.h"
#include "ui_text.h"
#include "tetr/app/application_state.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/app/settings_model.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/network/room_replay.h"
#include "tetr/network/round_statistics.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/recent.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/text_codec.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
namespace App {
namespace {
using K=Settings::NavigationKey;
class ModalControls {
    Input::Services& input_;
    std::function<void()> saved_;
public:
    explicit ModalControls(Runtime::GameContext& c):input_(c.input_services()),saved_(std::move(input_.controlInterrupt)){input_.controlInterrupt=[]{};}
    ~ModalControls(){input_.discard_control_events();input_.suppress_control_key_until_release(Settings::NavigationInput::key_code(K::Back));input_.controlInterrupt=std::move(saved_);}
};
void notice(Runtime::GameContext& c,TerminalCanvas& canvas,const std::string& text) {
    const auto size=Render::terminal_size().value_or(Render::TerminalSize{100,35});
    canvas.columnsLimit=size.columns;canvas.rowsLimit=size.rows;canvas.begin_frame();
    const int width=std::max(20,std::min(90,size.columns-8));
    canvas.metadata={Frontend::UiKind::Notice,"room-replay-transfer",UiText::tr("Room replays")};
    canvas.metadata.hint=text;canvas.metadata.shortcuts={UiText::tr("Esc: Back")};
    canvas.box(5,3,width,5,UiText::tr("Room replays"),{{58,222,240}},{{230,240,255}},{{9,17,28}});
    const auto lines=Util::TerminalText::wrap_words(text,width-4);
    for(std::size_t i=0;i<lines.size() && i<4;++i)canvas.write(7+int(i),5,lines[i],width-4,{{230,240,255}},{{9,17,28}});
    canvas.write(12,5,UiText::tr("Esc: Back"),width-4,{{130,165,190}},{{9,17,28}});canvas.present({},c.config.display.obscuredColors);
}
std::string label(const Replay::RoomReplaySummary& r) {
    const auto time=static_cast<std::time_t>(r.startedAt/1000);std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&time);
#else
    localtime_r(&time,&local);
#endif
    std::ostringstream out;out<<std::put_time(&local,"%Y-%m-%d %H:%M:%S")<<"  "<<r.players<<UiText::tr(" players")<<"  "<<std::fixed<<std::setprecision(1)<<r.duration/1000000.<<"s";
    return out.str();
}
void recover_viewer(Runtime::GameContext& c,Network::RoomReplayClient& state,std::uint64_t room,std::uint64_t epoch) {
    const auto key=std::pair{room,epoch};if(state.ownActors.count(key) && state.ownActors.at(key))return;
    if(!c.user_settings_component() || c.user_settings().persistencePath.empty())return;
    for(const auto& entry:Replay::recent_replays()) {
        std::ifstream file(entry.path,std::ios::binary);if(!file)continue;
        std::string prefix(2*1024*1024,'\0');file.read(prefix.data(),prefix.size());prefix.resize(static_cast<std::size_t>(file.gcount()));
        std::optional<std::array<std::uint64_t,3>> identity=Replay::multiplayer_replay_identity(prefix);
        if(!identity && !prefix.empty() && prefix.front()=='{') {
            const auto end=Util::Text::matching_brace(prefix,0);if(end==std::string::npos)continue;
            const auto header=std::string_view(prefix).substr(0,end+1);
            const auto field=[&](std::string_view name)->std::optional<std::uint64_t> {
                const auto tag="{"+std::string(name)+",";const auto at=header.find(tag);if(at==std::string_view::npos)return {};
                const auto begin=at+tag.size(),finish=header.find('}',begin);if(finish==std::string_view::npos)return {};
                return Util::Text::parse_integer<std::uint64_t>(header.substr(begin,finish-begin));
            };
            const auto r=field("online_room"),e=field("online_epoch"),a=field("online_actor"),profile=field("online_profile");
            if(r&&e&&a&&profile && *profile==Replay::multiplayer_digest(c.user_settings().persistencePath))identity=std::array<std::uint64_t,3>{*r,*e,*a};
        }
        if(identity && (*identity)[0]==room && (*identity)[1]==epoch && (*identity)[2]) {
            state.ownActors[key]=(*identity)[2];state.recentPaths[key]=entry.path.generic_u8string();return;
        }
    }
}
}
void finish_online_replay(Runtime::GameContext& c) {
    if(!c.replay_component() || !Replay::session(c).onlineRecording)return;
    auto& state=Replay::session(c);state.onlineRecording=false;
    freeze_post_game_times(c,c.session.player);
    Replay::add_replay(c,Replay::OperationKind::EndAll);remember_finished_replay(c);
    if(c.network_component()) {
        auto& n=Network::network_services(c);
        if(n.replays && n.replays->active) {
            const auto key=std::pair{n.replays->active->room,n.replays->active->epoch};
            n.replays->recentPaths[key]=state.recentPath;
            if(state.data.size()>=state.recordingLimit)n.replays->active->limited=true;
            Network::finish_room_replay_recording(c,Network::encode_round_statistics(Network::capture_round_statistics(c,c.session.player)));
        }
    }
    state.recordObserver={};
}
std::optional<std::string> download_room_replay(Runtime::GameContext& c,std::uint64_t room,std::uint64_t epoch) {
    auto& n=Network::network_services(c);auto& r=Network::room_replays(n);
    recover_viewer(c,r,room,epoch);
    Network::request_room_replay_download(n,room,epoch);ModalControls controls(c);Settings::NavigationInput input(c);TerminalCanvas canvas;
    const auto start=std::chrono::steady_clock::now();std::string shown;
    for(;;) {
        Network::poll_connection(n);
        auto& d=*r.download;
        if(d.ready){auto bytes=std::move(d.bytes);r.download.reset();return bytes;}
        const auto command=Frontend::poll_ui_command();
        if(input.update(K::Back,false)||input.update(K::Backspace,false)||(command && command->kind==Frontend::UiCommandKind::Back)){r.download.reset();return {};}
        std::string text;
        if(n.connection!=Network::ConnectionState::Connected) {text=UiText::tr("Server unavailable. The local replay is kept.");d.failed=true;}
        else if(!r.supported && std::chrono::steady_clock::now()-start>std::chrono::seconds(2)) {text=UiText::tr("This server does not support room replays.");d.failed=true;}
        else if(d.failed)text=UiText::tr("Replay unavailable. The room or replay may have been deleted.");
        else if(d.status=="collecting")text=UiText::tr("Waiting for this match to finish and collect player recordings...");
        else if(d.total)text=UiText::tr("Downloading replay: ")+std::to_string(d.bytes.size()*100/d.total)+"%";
        else text=UiText::tr("Requesting the full room replay...");
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(60) && !d.failed){d.failed=true;text=UiText::tr("Replay download timed out. The local replay is kept.");}
        if(text!=shown){notice(c,canvas,text);shown=text;}
        c.services.clock.sleep_for(Runtime::Duration::from_seconds(.005));
    }
}
bool save_online_room_replay(Runtime::GameContext& c,std::uint64_t room,std::uint64_t epoch,PostGameStatisticsView* statistics) {
    auto recording=download_room_replay(c,room,epoch);if(!recording)return false;
    const auto archive=Replay::decode_multiplayer_replay(*recording);if(!archive)return false;
    auto& n=Network::network_services(c);auto& state=Replay::session(c);auto& paths=Network::room_replays(n).recentPaths;
    const auto key=std::pair{room,epoch};const auto source=paths.find(key);
    std::optional<std::filesystem::path> path;
    if(archive->complete()) {
        const auto title=label({epoch,archive->startedAt,archive->duration,static_cast<unsigned>(archive->tracks.size()),true,false,archive->reason})+" ONLINE";
        if(source!=paths.end() && !source->second.empty())path=Replay::replace_recent(std::filesystem::u8path(source->second),*recording,title);
        if(!path && (source==paths.end() || source->second.empty()))path=Replay::save_recent(*recording,title);
        if(path){state.recentPath=path->generic_u8string();paths[key]=state.recentPath;}
    }
    // A partial download is an explicit copy. It never replaces the local
    // personal recording, even if another participant cannot upload a tail.
    const bool saved=save_replay_copy(c,*recording,path.has_value(),statistics,!archive->complete());
    if(path)paths[key]=state.recentPath;
    return saved;
}
void run_room_replay_menu(Runtime::GameContext& c) {
    using Settings::Model::SettingNode;
    auto& n=Network::network_services(c);auto& r=Network::room_replays(n);const auto room=n.roomInstance;
    Network::request_room_replay_list(n);ModalControls controls(c);Settings::NavigationInput input(c);Settings::TerminalSettingsView view(c,false,0);
    std::size_t selected=0;std::optional<std::uint64_t> chosen;std::uint64_t revision=~std::uint64_t{};
    auto refreshed=std::chrono::steady_clock::now();const auto start=refreshed;bool dirty=true;
    SettingNode menu;
    for(;;) {
        Network::poll_connection(n);if(n.roomInstance!=room || n.connection!=Network::ConnectionState::Connected)return;
        if(n.matchPhase==Network::RoomMatchPhase::Preparing && !n.pendingRoomRequest)
            for(const auto& member:n.roomMembers)if(member.slot==n.roomPlayerIndex && member.ready) {
                Network::request_room(n,Network::RoomOperation::SetReady,{},true,true,0);break;
            }
        if(!chosen && !r.listPending && std::chrono::steady_clock::now()-refreshed>std::chrono::seconds(1)){Network::request_room_replay_list(n);refreshed=std::chrono::steady_clock::now();}
        if(revision!=r.listRevision){revision=r.listRevision;dirty=true;}
        if(dirty || view.terminal_size_changed()) {
            const auto previous=selected<menu.children.size()?menu.children[selected].key:std::string{};
            menu=SettingNode{"room-replays","Room replays","The latest 30 matches in this room. Saved copies remain after the room is deleted."};
            if(chosen) {
                menu.children={{"view","View replay"},{"save","Save As"},{"back","Back"}};
                if(const auto it=std::find_if(r.list.begin(),r.list.end(),[&](const auto& item){return item.epoch==*chosen;});it!=r.list.end())menu.description=label(*it)+" / "+UiText::tr(it->collecting?"Collecting":it->complete?"Complete":"Incomplete");
            } else {
                for(const auto& item:r.list){SettingNode row{"match:"+std::to_string(item.epoch),label(item)};row.literalTitle=true;
                    row.summary=UiText::tr(item.collecting?"Collecting":item.complete?"Complete":"Incomplete");
                    if(item.reason==Replay::RoundEndReason::HostStopped)row.summary+=" / "+UiText::tr("Host stopped");menu.children.push_back(std::move(row));}
                menu.children.push_back({"refresh","Refresh"});menu.children.push_back({"back","Back"});
            }
            selected=std::min(selected,menu.children.size()-1);
            if(const auto it=std::find_if(menu.children.begin(),menu.children.end(),[&](const auto& item){return item.key==previous;});it!=menu.children.end())selected=it-menu.children.begin();
            view.prepare_layout(menu.children.size(),"SETTINGS > NETWORK > Room replays");view.print_menu(menu,selected,"SETTINGS > NETWORK > Room replays");
            if(!r.supported && std::chrono::steady_clock::now()-start>std::chrono::seconds(2))view.print_message("This server does not support room replays.");
            else if(!chosen && r.list.empty())view.print_message(r.listPending?"Loading room replays...":"No room replays.");
            dirty=false;
        }
        const auto command=Frontend::poll_ui_command();
        const bool back=input.update(K::Back,false)||input.update(K::Backspace,false)||input.update(K::Left,false)||(command && command->kind==Frontend::UiCommandKind::Back);
        if(back){if(chosen){chosen.reset();selected=0;menu.children.clear();dirty=true;input.suppress_held_keys();continue;}return;}
        const auto previous=selected;
        if(input.update(K::Up,true))selected=(selected+menu.children.size()-1)%menu.children.size();
        if(input.update(K::Down,true))selected=(selected+1)%menu.children.size();
        if(input.update(K::PageUp,true))selected=selected>view.page_rows()?selected-view.page_rows():0;
        if(input.update(K::PageDown,true))selected=std::min(menu.children.size()-1,selected+view.page_rows());
        if(command && command->kind==Frontend::UiCommandKind::Activate && command->index<menu.children.size())selected=command->index;
        dirty|=previous!=selected;
        if((command && command->kind==Frontend::UiCommandKind::Activate && command->index<menu.children.size())||input.update(K::Confirm,false)||input.update(K::Right,false)) {
            const auto key=menu.children[selected].key;
            if(key=="back"){if(!chosen)return;chosen.reset();selected=0;menu.children.clear();dirty=true;}
            else if(key=="refresh")Network::request_room_replay_list(n);
            else if(key.substr(0,6)=="match:"){chosen=std::stoull(key.substr(6));selected=0;menu.children.clear();dirty=true;}
            else if(chosen && (key=="view" || key=="save")) {
                if(auto bytes=download_room_replay(c,room,*chosen)) {
                    if(key=="view")view_multiplayer_replay(c,*bytes);else save_replay_copy(c,*bytes);
                }
                view.invalidate();dirty=true;
            }
            input.suppress_held_keys();
        }
        c.services.clock.sleep_for(Runtime::Duration::from_seconds(.005));
    }
}
}
