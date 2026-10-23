#include "tetr/frontend/graphical/session.h"
#include "tetr/config/normalization.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/round_statistics.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/multiplayer_codec.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/runtime/game_context.h"
#include "input.h"
#include "support/network_gameplay_frames.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <cmath>
namespace TetrTests::room_replay_graphical_test {
namespace {
using namespace std::chrono_literals;
template<class Predicate> Graphical::SessionView wait(Graphical::Session& s,Predicate ready) {
    const auto deadline=std::chrono::steady_clock::now()+6s;
    for(;;) {
        const auto view=s.view();if(ready(view))return view;
        if(view.status==Graphical::SessionStatus::Error)std::cerr<<view.message<<'\n';
        assert(view.status!=Graphical::SessionStatus::Error && std::chrono::steady_clock::now()<deadline);
        std::this_thread::sleep_for(5ms);
    }
}
void tap(Graphical::Session& s,SDL_Scancode code) {
    const auto key=Graphical::native_key(code);s.input().update(key,true);std::this_thread::sleep_for(25ms);
    s.input().update(key,false);std::this_thread::sleep_for(25ms);
}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int run(int,char**) {
    namespace fs=std::filesystem;const auto original=fs::current_path();
    const auto directory=fs::temp_directory_path()/("termino-room-replay-graphical-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);fs::current_path(directory);
    struct Restore {fs::path cwd,directory;~Restore(){fs::current_path(cwd);std::error_code error;fs::remove_all(directory,error);}} restore{original,directory};
    UserConfig::Settings user;user.keyCodes=UserConfig::default_key_codes();user.persistenceEnabled=false;user.uiLanguage=1;
    auto state=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;Runtime::GameContext c(*state,services);
    c.config=Runtime::normalize_user_config(user,Runtime::GameMode::Multiplayer);
    const auto config=Replay::encode_config_prefix(c.config)+"{block_seed,1}{garbage_seed,2}}";
    Replay::MultiplayerReplay replay;replay.room=4;replay.epoch=7;replay.viewer=102;replay.duration=30000000;replay.winner=0;
    for(int slot=0;slot<2;++slot) {
        Replay::MultiplayerReplayTrack track;track.slot=slot;track.actor=101+slot;track.name="Same name";track.complete=true;track.endedAt=replay.duration;
        track.journal=Replay::journal_record('C',0,config);
        Test::prepare_online_board(c,0);
        track.journal+=Replay::journal_record('B',0,"10;20;2;"+Network::encode_board_state(c,state->player,Network::ActivePieceState::active({{2,3},Runtime::Rotation{}})));
        Test::prepare_online_board(c,1);
        track.journal+=Replay::journal_record('B',15000000,"10;20;2;"+Network::encode_board_state(c,state->player,Network::ActivePieceState::locked()));
        track.journal+=Replay::journal_record('O',replay.duration,"{wall_time,30}{end_all,30}");
        Network::RoundStatistics result;result.gameSeconds=result.realSeconds=30;result.score.placedBlocks=50+slot;
        track.journal+=Replay::journal_record('F',replay.duration,Network::encode_round_statistics(result));replay.tracks.push_back(std::move(track));
    }
    const auto recording=Replay::encode_multiplayer_replay(replay),file=recording+Replay::replay_hash_suffix(recording);
    {std::ofstream out("fixture.txt",std::ios::binary);out.write(file.data(),file.size());}
    user.gameMode=5;user.replayPath="./fixture.txt";
    {
        Graphical::Session session(user,user,{false});
        auto view=wait(session,[](const auto& v){return !v.ui && v.replay && v.room && v.peers.size()==2 && v.replayPosition>.02;});
        assert(view.spectator && view.focusSlot==1 && view.peers.front().slot==1);
        assert(view.peers.front().board.username.find("You")!=std::string::npos);
        tap(session,SDL_SCANCODE_SPACE);view=wait(session,[](const auto& v){return v.replayPaused;});
        const auto paused=view.replayPosition;std::this_thread::sleep_for(50ms);assert(std::abs(session.view().replayPosition-paused)<.02);
        tap(session,SDL_SCANCODE_EQUALS);wait(session,[](const auto& v){return v.replaySpeed==2;});
        tap(session,SDL_SCANCODE_TAB);wait(session,[](const auto& v){return v.focusSlot==0;});
        tap(session,SDL_SCANCODE_RIGHT);view=wait(session,[](const auto& v){return v.replayPosition>=10;});assert(view.replayPaused);
        tap(session,SDL_SCANCODE_LEFT);wait(session,[](const auto& v){return v.replayPosition<1;});
        for(int i=0;i<3;++i)tap(session,SDL_SCANCODE_RIGHT);
        view=wait(session,[](const auto& v){return v.status==Graphical::SessionStatus::Finished;});
        assert(view.peers.size()==2);
        for(const auto& peer:view.peers)assert(peer.board.pieces==50+peer.slot);
        tap(session,SDL_SCANCODE_TAB);wait(session,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Statistics;});
        tap(session,SDL_SCANCODE_S);wait(session,[&](const auto&){return session.text_input_active();});
        session.text_event({Frontend::TextEditKind::Insert,"多人录像副本.txt"});session.text_event({Frontend::TextEditKind::Confirm});
        wait(session,[&](const auto&){return !session.text_input_active() && fs::exists(".tetr/replay/多人录像副本.txt");});
        assert(read("fixture.txt")==file);
        auto copy=read(".tetr/replay/多人录像副本.txt");assert(copy.size()>17);copy.resize(copy.size()-17);
        const auto decoded=Replay::decode_multiplayer_replay(copy);assert(decoded && decoded->viewer==102 && decoded->tracks.size()==2 && decoded->complete());
        tap(session,SDL_SCANCODE_ESCAPE);wait(session,[](const auto& v){return !v.ui && v.replay;});
        tap(session,SDL_SCANCODE_ESCAPE);wait(session,[](const auto& v){return v.ui && v.ui->metadata.id=="root";});
    }
    std::cerr<<"room_replay_graphical_test: same-name focus, pause/speed/seek, results, keyboard Save As and return OK\n";return 0;
}
}
