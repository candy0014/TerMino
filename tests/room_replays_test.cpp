#include "tetr/app/application_state.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/config/normalization.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/input/services.h"
#include "tetr/frontend/ui_surface.h"
#include "tetr/frontend/text_input.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/room_replay.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/round_statistics.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/recent.h"
#include "tetr/replay/replay_hash.h"
#include "../src/app/online_replay.h"
#include "../src/app/room_replay_playback.h"
#include "../src/app/init_replay.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/settings_navigation.h"
#include "../src/app/ui_text.h"
#include "support/network_gameplay_frames.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <deque>
namespace TetrTests::room_replays_test {
using namespace Network;using namespace std::chrono_literals;
namespace {
struct TextInput : Frontend::TextInputSession {
    std::deque<Frontend::TextEditEvent> events;
    void begin_text_input() override {}
    void end_text_input() override {}
    bool owns_text_events() const override {return true;}
    std::optional<Frontend::TextEditEvent> poll_text_event() override {
        if(events.empty())return {};auto next=events.front();events.pop_front();return next;
    }
};
struct Member {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext c{*session,services};UserConfig::Settings user;
    Input::Services input;NetworkServices network;Replay::ReplaySessionState replay;App::ApplicationState app;
    TextInput text;
    Member() {
        c.attach_network(network);c.attach_user_settings(user);c.attach_input(input);c.attach_replay(replay);c.attach_app(app);
        c.attach_output_mutex(app.terminalOutputMutex);app.primaryContext=&c;user.gameMode=4;user.keyCodes=UserConfig::default_key_codes();
        app.textInputSession=&text;
        user.persistencePath=".tetr/profiles/"+user.username+".txt";
        input.initialized=true;input.keyStateProvider=[](int){return false;};Input::refresh_bindings(input,user.keyCodes);
        c.config=Runtime::normalize_user_config(user,Runtime::GameMode::Multiplayer);
        Test::prepare_online_board(c,0);
    }
    ~Member(){disconnect_online(network);}
};
std::string config(Member& p) {return Replay::encode_config_prefix(p.c.config)+"{block_seed,42}{garbage_seed,73}}";}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void codec_and_playback() {
    Member source;Replay::MultiplayerReplay archive;archive.room=11;archive.epoch=12;archive.startedAt=1000;archive.duration=3000000;archive.viewer=102;archive.winner=0;
    archive.targets={{0,0,1},{0,1,0},{1500000,0,-1}};
    const auto rawConfig=config(source);
    for(int slot=0;slot<2;++slot) {
        Replay::MultiplayerReplayTrack track;track.actor=101+slot;track.slot=slot;track.name="同 名;player\n";
        track.complete=true;track.endedAt=3000000;if(slot==1){track.eliminatedAt=2000000;track.killer=0;}
        track.journal=Replay::journal_record('C',0,rawConfig);
        Test::prepare_online_board(source.c,0);
        track.journal+=Replay::journal_record('B',0,"10;20;2;"+encode_board_state(source.c,source.session->player,ActivePieceState::active({{2,3},Runtime::Rotation{}})));
        PieceMotionFrame motion;motion.stamp={1,1,1,1,1};motion.piece=source.session->player.pieces.bag.front().type;
        motion.result={{4,4},Runtime::Rotation{}};motion.clock=RemoteClockSample{1,1};
        track.journal+=Replay::journal_record('M',1000000,encode_piece_motion(motion));
        Test::prepare_online_board(source.c,1);
        track.journal+=Replay::journal_record('B',1500000,"10;20;2;"+encode_board_state(source.c,source.session->player,ActivePieceState::locked()));
        track.journal+=Replay::journal_record('O',3000000,"{end_all,3}");
        RoundStatistics result;result.gameSeconds=3;result.realSeconds=3;result.score.placedBlocks=50+slot;
        track.journal+=Replay::journal_record('F',3000000,encode_round_statistics(result));archive.tracks.push_back(std::move(track));
    }
    const auto bytes=Replay::encode_multiplayer_replay(archive);auto decoded=Replay::decode_multiplayer_replay(bytes);
    assert(decoded && decoded->complete() && decoded->tracks[1].name==archive.tracks[1].name && decoded->viewer==102);
    assert(!Replay::decode_multiplayer_replay(bytes.substr(0,bytes.size()-1)) && !Replay::decode_multiplayer_replay(bytes+"x"));
    auto invalid=archive;invalid.tracks[1].actor=101;assert(!Replay::decode_multiplayer_replay(Replay::encode_multiplayer_replay(invalid)));
    invalid=archive;invalid.viewer=999;assert(!Replay::decode_multiplayer_replay(Replay::encode_multiplayer_replay(invalid)));
    assert(Replay::replay_unhex(Replay::replay_hex(bytes),bytes.size())==bytes);
    assert(!Replay::replay_unhex("0x",10) && !Replay::replay_unhex("0",10));
    Member viewer;App::RoomReplayPlayback playback(viewer.c,*decoded);assert(playback.valid() && playback.advance(0));
    assert(viewer.session->roomScene.focusSlot==1 && viewer.session->roomScene.usernames[1].find("You")!=std::string::npos);
    assert(playback.advance(1100000));
    for(const auto& entry:viewer.session->roomScene.players)assert(entry.second.presentation.activePose==(Piece::Pose{{4,4},Runtime::Rotation{}}));
    assert(playback.advance(2100000));assert(viewer.session->roomScene.focusSlot==0 && viewer.session->roomScene.survivors==std::vector<int>{0});
    for(const auto& entry:viewer.session->roomScene.players)assert(entry.second.gameplay.playfield.field.cell(19,0).occupied());
    assert(playback.seek(500000));assert(viewer.session->roomScene.survivors.size()==2);
    for(const auto& entry:viewer.session->roomScene.players)assert(!entry.second.gameplay.playfield.field.cell(19,0).occupied());
    assert(playback.advance(3000000));for(const auto& entry:viewer.session->roomScene.players)assert(entry.second.gameplay.scoring.placedBlocks==50+entry.first);
    assert(viewer.session->roomScene.finished && viewer.session->roomScene.winnerSlot==0);
    invalid=archive;invalid.reason=Replay::RoundEndReason::HostStopped;invalid.winner=-1;
    Member stopped;App::RoomReplayPlayback stoppedPlayback(stopped.c,invalid);assert(stoppedPlayback.advance(3000000));
    assert(stopped.session->roomScene.aborted && stopped.session->roomScene.winnerSlot==-1);
    // Zero-height boards and an exhausted finite queue can end before the
    // first lock. Their final semantic board must still decode and replay.
    source.c.config.field.height=0;auto& empty=source.session->player;
    empty.gameplay.playfield.visibleHeight=empty.gameplay.playfield.activeHeight=0;
    empty.gameplay.playfield.field.initialize(10,0);empty.pieces.bag.clear();empty.pieces.nextPreview.clear();
    auto tiny=archive;tiny.tracks.resize(1);tiny.viewer=tiny.tracks[0].actor;tiny.winner=0;tiny.targets.clear();
    auto& track=tiny.tracks[0];track.journal=Replay::journal_record('C',0,config(source));
    const auto emptyBoard=encode_board_state(source.c,empty,ActivePieceState::locked());assert(decode_board_state(emptyBoard,10,0));
    track.journal+=Replay::journal_record('B',0,"10;0;0;"+emptyBoard);
    track.journal+=Replay::journal_record('O',3000000,"{end_all,3}");
    track.journal+=Replay::journal_record('F',3000000,encode_round_statistics(RoundStatistics{}));
    Member shortViewer;App::RoomReplayPlayback shortPlayback(shortViewer.c,tiny);assert(shortPlayback.valid() && shortPlayback.advance(3000000));
    assert(shortViewer.session->roomScene.players.at(0).gameplay.playfield.visibleHeight==0);
    const auto personal=Replay::save_recent("{{Model,0}}{end_all,1}","personal");assert(personal);
    const auto upgraded=Replay::replace_recent(*personal,bytes,"online");assert(upgraded && !std::filesystem::exists(*personal));
    Member loader;loader.user.replayPath=upgraded->generic_u8string();auto mode=Runtime::GameMode::Replay;
    assert(App::InitDetail::configure_replay_or_local_game(loader.c,mode,nullptr));
    assert(loader.replay.multiplayerReplay && loader.replay.multiplayerReplay->viewer==102 && loader.replay.loadedRecording==bytes);
    Member wireClient;wireClient.network.connectionToken="range-client";
    request_room_replay_download(wireClient.network,archive.room,archive.epoch);const auto request=wireClient.network.replays->download->id;
    for(std::size_t last=(bytes.size()-1)/kReplayTransferChunk+1;last>0;--last) {
        const auto at=(last-1)*kReplayTransferChunk;const auto part=bytes.substr(at,kReplayTransferChunk);
        handle_room_replay_message(wireClient.network,replay_message("data","range-client",{"11","12",std::to_string(request),std::to_string(bytes.size()),std::to_string(Replay::multiplayer_digest(bytes)),std::to_string(at),"102",Replay::replay_hex(part)}));
    }
    assert(wireClient.network.replays->download->ready && wireClient.network.replays->download->bytes==bytes);
    const auto before=read(*upgraded);{std::ofstream f(*upgraded);f<<"user edit";}
    assert(!Replay::replace_recent(*upgraded,bytes,"must preserve edits") && read(*upgraded)=="user edit");
    assert(Replay::clear_recent());
}
}
int run(int argc,char** argv) {
    struct Discard : std::streambuf {int overflow(int value) override{return value;}std::streamsize xsputn(const char*,std::streamsize count) override{return count;}} discard;
    auto* previousOutput=std::cout.rdbuf(&discard);
    struct RestoreOutput {std::streambuf* previous;~RestoreOutput(){std::cout.rdbuf(previous);}} restoreOutput{previousOutput};
    const std::string address=argc>1?argv[1]:"127.0.0.1:3045";
    namespace fs=std::filesystem;const auto original=fs::current_path();
    const auto directory=fs::temp_directory_path()/("termino_room_replays_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);fs::current_path(directory);
    struct Restore {fs::path original,directory;~Restore(){fs::current_path(original);std::error_code ec;fs::remove_all(directory,ec);}} restore{original,directory};
    App::UiText::set_language(App::UiText::Language::English);codec_and_playback();
    auto server=std::make_unique<RelayServer>(917);server->bind(3045);std::atomic<bool> running=true;
    std::thread worker([&]{while(running)server->poll(1ms);});
    struct Stop {std::atomic<bool>& running;std::thread& worker;~Stop(){running=false;worker.join();}} stop{running,worker};
    Member host,guest,observer;std::vector<Member*> members{&host,&guest,&observer};host.user.username=guest.user.username="Same Name";observer.user.username="Watcher";
    const auto pump=[&]{for(auto* p:members)poll_connection(p->network);std::this_thread::sleep_for(1ms);};
    int step=0;const auto wait=[&](auto ready) {
        ++step;const auto deadline=std::chrono::steady_clock::now()+8s;while(!ready() && std::chrono::steady_clock::now()<deadline)pump();
        if(!ready())std::cerr<<"Room replay wait timed out at "<<step<<'\n';assert(ready());
    };
    const auto request=[&](Member& p,RoomOperation operation,int value=-1,const std::string& name="") {
        request_room(p.network,operation,name,false,false,value);wait([&]{return !p.network.pendingRoomRequest;});
    };
    for(auto* p:members)initialize_network_services(p->c,address);
    wait([&]{for(const auto* p:members)if(p->network.connection!=ConnectionState::Connected || !p->network.replays || !p->network.replays->supported)return false;return true;});
    request(host,RoomOperation::Create,-1,"Replay-room");request(guest,RoomOperation::Join,-1,"Replay-room");request(observer,RoomOperation::Join,-1,"Replay-room");
    request(observer,RoomOperation::SetRole,1);set_room_policy(host.network,{false});wait([&]{return !host.network.pendingRoomRequest;});
    const auto room=host.network.roomInstance;
    const auto start=[&] {
        request(host,RoomOperation::StartMatch);wait([&]{return guest.network.matchEpoch==host.network.matchEpoch && observer.network.matchEpoch==host.network.matchEpoch;});
        LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=2;setup.holdEnabled=true;setup.blockMaxHeight=setup.blockMaxWidth=4;setup.nextPreview={Piece::Type{1},Piece::Type{2}};
        for(auto* p:members){send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));}
        wait([&]{return host.network.playerSetups.size()>=1;});
        request(host,RoomOperation::SetReady,1);request(guest,RoomOperation::SetReady,1);
        wait([&]{return host.network.matchPhase==RoomMatchPhase::Countdown && guest.network.matchPhase==RoomMatchPhase::Countdown;});
        send(host.network,"start6");send(guest.network,"start6");
        wait([&]{return host.network.matchPhase==RoomMatchPhase::Playing && guest.network.matchPhase==RoomMatchPhase::Playing && observer.network.matchPhase==RoomMatchPhase::Playing;});
        for(auto* p:members){p->session->roomScene={};Engine::initialize_multiplayer_scene(p->c);}
        for(auto* p:{&host,&guest}) {
            p->replay=Replay::ReplaySessionState{};p->replay.configPrefix=config(*p);p->replay.data=p->replay.configPrefix;
            Test::prepare_online_board(p->c,0);p->session->gameStartTime=p->c.now();p->session->player.timing.completedGameTime.reset();p->session->player.timing.completedRealTime.reset();
            begin_room_replay_recording(p->c);Replay::add_replay(p->c,Replay::OperationKind::Move,2,3,0);
            record_room_replay_motion(p->c,p->session->player,{{4,4},Runtime::Rotation{}});
        }
        return host.network.matchEpoch;
    };
    const auto first=start();
    auto upload=host.network.replays->active;assert(upload && upload->file->flush());
    upload->lastSend=std::chrono::steady_clock::now()+10s;
    wait([&]{return upload->file->size==upload->file->queued_size();});
    const auto put=[&](std::uint64_t offset,const std::string& token) {
        const auto part=upload->file->read(offset,kReplayTransferChunk);
        send_online(host.network,replay_message("put",token,{std::to_string(room),std::to_string(first),std::to_string(offset),std::to_string(Replay::multiplayer_digest(part)),Replay::replay_hex(part)}));
    };
    put(kReplayTransferChunk,host.network.connectionToken); // gap must not advance the committed prefix
    put(0,"invalid-token");put(0,host.network.connectionToken);put(0,host.network.connectionToken);
    wait([&]{return upload->acknowledged==kReplayTransferChunk;});upload->lastSend={};
    // Stop switches epochs before either tail is submitted. Old uploads must
    // remain authorized independently of the new preparation roster.
    request(host,RoomOperation::EndMatch);assert(host.network.matchEpoch!=first);
    App::finish_online_replay(host.c);App::finish_online_replay(guest.c);
    wait([&]{return host.network.replays->uploads.empty() && guest.network.replays->uploads.empty();});
    request_room_replay_list(observer.network);wait([&]{return !observer.network.replays->listPending;});
    assert(observer.network.replays->list.size()==1 && observer.network.replays->list[0].complete && observer.network.replays->list[0].reason==Replay::RoundEndReason::HostStopped);
    const auto download=[&](Member& p,std::uint64_t epoch) {
        request_room_replay_download(p.network,room,epoch);wait([&]{return p.network.replays->download->ready || p.network.replays->download->failed;});
        assert(p.network.replays->download->ready);return *Replay::decode_multiplayer_replay(p.network.replays->download->bytes);
    };
    const auto own=download(host,first),other=download(guest,first),watched=download(observer,first);
    assert(own.viewer && other.viewer && own.viewer!=other.viewer && watched.viewer==0 && own.tracks.size()==2 && own.complete());
    assert(own.tracks[0].name==own.tracks[1].name && own.winner==-1);
    assert(!own.targets.empty());
    // A malformed/out-of-order or duplicate packet cannot rewrite a committed
    // track. Invalid token uploads must not alter another player's archive.
    const auto originalDigest=Replay::multiplayer_digest(Replay::encode_multiplayer_replay(watched));
    send_online(observer.network,replay_message("put",observer.network.connectionToken,{std::to_string(room),std::to_string(first),"0","0","00"}));
    assert(Replay::multiplayer_digest(Replay::encode_multiplayer_replay(download(observer,first)))==originalDigest);

    // Pure spectators have no completed local gameplay timer. Save must work
    // from the statistics modal as well as from the finished boards.
    assert(!observer.session->player.timing.completedGameTime);
    int stage=0;bool sawStatistics=false,sawSave=false;const int tab=App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Tab);
    const auto uiDeadline=std::chrono::steady_clock::now()+8s;auto stageTime=std::chrono::steady_clock::now();
    Frontend::UiSurfaceScope surface([&](Frontend::UiFrame frame) {
        if(frame.metadata.kind==Frontend::UiKind::Statistics && stage==0){sawStatistics=true;stage=1;stageTime=std::chrono::steady_clock::now();}
        if(frame.metadata.id=="replay-save" || (stage==1 && frame.metadata.kind==Frontend::UiKind::Text)) {sawSave=true;stage=2;stageTime=std::chrono::steady_clock::now();}
        if(sawSave && (frame.metadata.hint.find("Replay saved")!=std::string::npos || frame.metadata.notice.find("Replay saved")!=std::string::npos)) {stage=3;stageTime=std::chrono::steady_clock::now();}
        if(stage==3 && frame.metadata.kind==Frontend::UiKind::Canvas){stage=4;stageTime=std::chrono::steady_clock::now();}
    });
    observer.input.keyStateProvider=[&](int key) {
        if(std::chrono::steady_clock::now()>=uiDeadline)std::cerr<<"Spectator UI stage="<<stage<<" statistics="<<sawStatistics<<" save="<<sawSave<<" participants="<<observer.network.roundParticipants[first].size()<<'\n';
        assert(std::chrono::steady_clock::now()<uiDeadline);
        if(std::chrono::steady_clock::now()-stageTime<20ms)return false;
        if(stage==0)return key==tab;
        if(stage==1)return key==observer.user.keyCodes[10];
        if(stage==3)return key==App::Settings::NavigationInput::key_code(App::Settings::NavigationKey::Back);
        if(stage==4)return key==observer.user.keyCodes[8];
        return false;
    };
    observer.text.events={{Frontend::TextEditKind::Confirm,{}}};
    App::watch_room_match(observer.c);observer.input.keyStateProvider=[](int){return false;};
    assert(sawStatistics && sawSave && !observer.session->player.timing.completedGameTime);
    bool foundObserver=false;for(const auto& entry:Replay::recent_replays()) {
        auto saved=read(entry.path);if(!Replay::is_multiplayer_replay(saved))continue;saved.resize(saved.size()-17);
        const auto archive=Replay::decode_multiplayer_replay(saved);if(archive && archive->viewer==0 && archive->complete())foundObserver=true;
    }assert(foundObserver);
    // Reusing a vacant slot and nickname must not identify the newcomer as
    // the original actor. Reconnecting the original profile must recover it.
    request(guest,RoomOperation::Leave);
    Member replacement;replacement.user.username="Same Name";members.push_back(&replacement);
    initialize_network_services(replacement.c,address);wait([&]{return replacement.network.connection==ConnectionState::Connected && replacement.network.replays && replacement.network.replays->supported;});
    request(replacement,RoomOperation::Join,-1,"Replay-room");assert(download(replacement,first).viewer==0);
    request(replacement,RoomOperation::Leave);
    disconnect_online(guest.network);initialize_network_services(guest.c,address);wait([&]{return guest.network.connection==ConnectionState::Connected && guest.network.replays && guest.network.replays->supported;});
    request(guest,RoomOperation::Join,-1,"Replay-room");request(guest,RoomOperation::SetRole,0);
    assert(download(guest,first).viewer==other.viewer);
    // One natural finish plus another 30 rounds verifies exact retention, and
    // immutable old actors despite later renames or room slot reuse.
    std::uint64_t latest=first;
    for(int round=0;round<31;++round) {
        latest=start();App::finish_online_replay(guest.c);send_round_signal(guest.c,OutgoingRoundSignal::LocalLoss);
        wait([&]{return host.network.matchPhase==RoomMatchPhase::Finished;});App::finish_online_replay(host.c);
        wait([&]{return host.network.replays->uploads.empty() && guest.network.replays->uploads.empty();});
    }
    request_room_replay_list(observer.network);wait([&]{return !observer.network.replays->listPending;});
    assert(observer.network.replays->list.size()==30 && observer.network.replays->list.front().epoch==latest);
    assert(std::none_of(observer.network.replays->list.begin(),observer.network.replays->list.end(),[&](const auto& r){return r.epoch==first;}));
    request_room_replay_download(observer.network,room,first);wait([&]{return observer.network.replays->download->failed;});
    assert(observer.network.replays->download->status=="gone");
    // An abrupt disconnect retains the received prefix and is explicitly
    // incomplete after collection expires; explicit Save As preserves the
    // host's automatically saved personal recording.
    const auto broken=start();pump();disconnect_online(guest.network);App::finish_online_replay(guest.c);
    wait([&]{return host.network.matchPhase==RoomMatchPhase::Finished;});App::finish_online_replay(host.c);
    wait([&]{return host.network.replays->uploads.empty();});
    request_room_replay_download(observer.network,room,broken);
    const auto collectBy=std::chrono::steady_clock::now()+18s;
    while(!observer.network.replays->download->ready && !observer.network.replays->download->failed && std::chrono::steady_clock::now()<collectBy)pump();
    assert(observer.network.replays->download->ready);
    const auto partial=Replay::decode_multiplayer_replay(observer.network.replays->download->bytes);assert(partial && !partial->complete());
    const auto personalPath=host.replay.recentPath,personalBytes=read(fs::u8path(personalPath));
    host.text.events={{Frontend::TextEditKind::Insert,"partial-copy.txt"},{Frontend::TextEditKind::Confirm,{}}};
    const bool savedPartial=App::save_online_room_replay(host.c,room,broken);
    assert(savedPartial && host.replay.recentPath==personalPath && read(fs::u8path(personalPath))==personalBytes);
    assert(fs::exists(".tetr/replay/partial-copy.txt"));latest=broken;
    members.erase(std::find(members.begin(),members.end(),&replacement));
    request(host,RoomOperation::Leave);request(guest,RoomOperation::Leave);request(observer,RoomOperation::Leave);
    request_room_replay_download(observer.network,room,latest);wait([&]{return observer.network.replays->download->failed;});
    assert(observer.network.replays->download->status=="gone");
    request(host,RoomOperation::Create,-1,"Replay-room");assert(host.network.roomInstance!=room);
    request_room_replay_list(host.network);wait([&]{return !host.network.replays->listPending;});assert(host.network.replays->list.empty());
    std::cerr<<"room_replays_test: stop/tails, identities, spectator save, playback/seek, retention and room deletion OK\n";return 0;
}
}
