// Manual benchmark: real Win32 console input, gameplay loop, sockets, and
// console-buffer observation. Run under Windows or wineconsole, not CTest.
#include <windows.h>
#include "tetr/app/application_state.h"
#include "tetr/config/normalization.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/live_piece_controller.h"
#include "tetr/engine/autoplay_adapter.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/input/services.h"
#include "tetr/network/lobby_protocol.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/compat/piece_encoding.h"
#include "support/terminal_screen.h"
#include "support/autoplay_motion_strategy.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <streambuf>
#include <thread>
#include <mutex>

namespace {
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
std::int64_t stamp() {return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
void require(bool value,const char* what) {if(!value) throw std::runtime_error(std::string(what)+" (Win32="+std::to_string(GetLastError())+")");}
struct Console {
    HANDLE originalIn=GetStdHandle(STD_INPUT_HANDLE),originalOut=GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE input=INVALID_HANDLE_VALUE,output=INVALID_HANDLE_VALUE;
    bool allocated=false;
    explicit Console(bool allocate) {
        // Allocate an isolated console; injected F13/F14/F24 records cannot
        // reach another application or the user's ordinary keyboard bindings.
        if(allocate) {FreeConsole();require(AllocConsole()!=FALSE,"AllocConsole");allocated=true;}
        input=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        output=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        require(input!=INVALID_HANDLE_VALUE && output!=INVALID_HANDLE_VALUE,"Open console");
        SetStdHandle(STD_INPUT_HANDLE,input);SetStdHandle(STD_OUTPUT_HANDLE,output);
        require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8),"Console UTF-8");
        require(SetConsoleMode(output,ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING),"Console VT");
        require(SetConsoleMode(input,ENABLE_EXTENDED_FLAGS|ENABLE_WINDOW_INPUT),"Console records");
        require(SetConsoleScreenBufferSize(output,{140,45}),"Console buffer size");
        SMALL_RECT bounds{0,0,139,44};require(SetConsoleWindowInfo(output,TRUE,&bounds),"Console window size");
        SetConsoleTitleW(L"TerMino Windows multiplayer input latency test");
    }
    ~Console() {SetStdHandle(STD_INPUT_HANDLE,originalIn);SetStdHandle(STD_OUTPUT_HANDLE,originalOut);CloseHandle(input);CloseHandle(output);if(allocated) FreeConsole();}
    void tap(int key) {
        INPUT_RECORD records[2]{};
        for(int i=0;i<2;++i) {auto& r=records[i];r.EventType=KEY_EVENT;r.Event.KeyEvent.bKeyDown=i==0;
            r.Event.KeyEvent.wRepeatCount=1;r.Event.KeyEvent.wVirtualKeyCode=static_cast<WORD>(key);}
        DWORD count=0;require(WriteConsoleInputW(input,records,2,&count) && count==2,"Inject console key");
    }
    wchar_t cell(int row,int column) {
        CHAR_INFO value{};SMALL_RECT at{static_cast<SHORT>(column),static_cast<SHORT>(row),static_cast<SHORT>(column),static_cast<SHORT>(row)};
        require(ReadConsoleOutputW(output,&value,{1,1},{0,0},&at)!=FALSE,"Observe console cell");return value.Char.UnicodeChar;
    }
    void save(const std::string& path) {
        std::vector<CHAR_INFO> values(140*45);SMALL_RECT at{0,0,139,44};
        require(ReadConsoleOutputW(output,values.data(),{140,45},{0,0},&at)!=FALSE,"Console snapshot");
        std::ofstream file(path);file<<"{\"columns\":140,\"rows\":45,\"cells\":[";
        for(std::size_t i=0;i<values.size();++i) {if(i) file<<',';file<<'['<<int(values[i].Char.UnicodeChar)<<','<<values[i].Attributes<<']';}
        file<<"]}\n";
    }
};
thread_local bool peerBotOutput=false;
struct Sample {int column=0;std::int64_t queued=0,applied=0,displayed=0;};
struct Sink : std::streambuf {
    Console& console;Runtime::GameContext* context=nullptr;Piece::Definition* piece=nullptr;
    // Wine 11 accepts ENABLE_VIRTUAL_TERMINAL_PROCESSING but keeps the ANSI
    // bytes literal in ReadConsoleOutputW. Explicit stream observation still
    // exercises Win32 input and output; it does not measure terminal pixels.
    bool streamObservation=false;
    Test::TerminalScreen screen;
    std::vector<Sample>* samples=nullptr;std::string snapshot;
    std::atomic<int> pending{-1},completed{-1};std::atomic<bool> ready=false;
    std::uint64_t writes=0,bytes=0;std::vector<double> writeTimes;
    explicit Sink(Console& c,bool stream):console(c),streamObservation(stream) {writeTimes.reserve(10000);}
    bool block_at(int row,int column) {
        return streamObservation ? screen.at(row+1,column+1).glyph=="█" : console.cell(row,column)==L'█';
    }
    void save(const std::string& path) {
        if(!streamObservation) {console.save(path);return;}
        std::ofstream file(path);file<<"{\"observation\":\"vt_stream\",\"columns\":140,\"rows\":45,\"cells\":[";
        for(int row=0;row<45;++row) for(int col=0;col<140;++col) {
            if(row || col) file<<',';
            const auto value=screen.at(row+1,col+1);
            file<<'['<<col<<','<<row<<','<<std::quoted(value.glyph)<<",["<<value.foreground[0]<<','<<value.foreground[1]<<','<<value.foreground[2]
                <<"],["<<value.background[0]<<','<<value.background[1]<<','<<value.background[2]<<"]]";
        }
        file<<"]}\n";
    }
    std::streamsize xsputn(const char* text,std::streamsize size) override {
        if(peerBotOutput)return size; // Peer renders on its own simulated terminal.
        const auto begin=stamp();DWORD written=0;
        require(WriteFile(console.output,text,static_cast<DWORD>(size),&written,nullptr) && written==size,"Write console output");
        ++writes;bytes+=written;writeTimes.push_back((stamp()-begin)/1e6);
        if(streamObservation) screen.feed(std::string(text,static_cast<std::size_t>(size)));
        if(!context || !piece) return size;
        const auto& p=context->session.player;
        if(p.presentation.paintedActivePose && p.presentation.activePose) ready=true;
        const int index=pending.load(std::memory_order_acquire);
        if(!samples || index<0 || index<=completed || !p.presentation.paintedActivePose) return size;
        auto& sample=samples->at(index);const auto pose=*p.presentation.paintedActivePose;
        if(pose.position.row!=5 || pose.position.col!=sample.column) return size;
        // Verify newly covered and uncovered cells, independently of the
        // model pose, using the explicitly selected output observer.
        auto old=pose;old.position.col=sample.column==4?3:4;
        const auto cells=piece->cells(pose.rotation);
        const auto contains=[&](Piece::Pose at,int row,int col) {
            for(const auto& cell:cells) if(at.position.row+cell[0]==row && at.position.col+cell[1]==col) return true;
            return false;
        };
        bool correct=true;
        for(const auto& cell:cells) {
            const int row=pose.position.row+cell[0],col=pose.position.col+cell[1];
            if(!contains(old,row,col)) correct&=block_at(row+p.presentation.topMargin,col*2+p.presentation.margin);
            const int oldRow=old.position.row+cell[0],oldCol=old.position.col+cell[1];
            if(!contains(pose,oldRow,oldCol)) correct&=!block_at(oldRow+p.presentation.topMargin,oldCol*2+p.presentation.margin);
        }
        if(correct) {
            sample.displayed=stamp();
            if(index==20 && !snapshot.empty()) save(snapshot);
            completed.store(index,std::memory_order_release);
        }
        return size;
    }
    int_type overflow(int_type c) override {if(!traits_type::eq_int_type(c,traits_type::eof())) {const char v=traits_type::to_char_type(c);xsputn(&v,1);}return traits_type::not_eof(c);}
    int sync() override {return 0;}
};
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices runtime;Runtime::GameContext context{*session,runtime};
    UserConfig::Settings user;Input::Services input;Network::NetworkServices network;
    Replay::ReplaySessionState replay;App::ApplicationState app;
    explicit Player(bool online) {
        context.attach_input(input);context.attach_network(network);context.attach_user_settings(user);context.attach_replay(replay);context.attach_app(app);
        app.primaryContext=&context;input.initialized=true;input.set_continue_input_unfocused(true);
        user.gravityInterval=-1;user.lockDelay=600;user.keyCodes=UserConfig::default_key_codes();
        user.keyCodes[0]=VK_F13;user.keyCodes[1]=VK_F14;user.keyCodes[3]=VK_F24;
        Input::refresh_bindings(input,user.keyCodes);
        context.config=Runtime::normalize_user_config(user,online?Runtime::GameMode::Multiplayer:Runtime::GameMode::Zen);
        Runtime::set_current_gravity_interval(context,Runtime::GravityInterval::disabled());
        auto& p=session->player;auto& f=p.gameplay.playfield;
        f.width=10;f.visibleHeight=f.activeHeight=20;f.nextCount=2;f.holdEnabled=true;f.field.initialize(10,20);
        p.pieces.bag.push_back({Compat::piece_type_from_legacy(1),false});
        p.pieces.nextPreview={Compat::piece_type_from_legacy(2),Compat::piece_type_from_legacy(3)};
        p.gameplay.hold.piece=Compat::piece_type_from_legacy(4);
    }
    ~Player() {Network::disconnect_online(network);}
};
Network::LobbyPlayerSetup setup(const Player& p) {
    Network::LobbyPlayerSetup s;s.visibleHeight=20;s.width=10;s.nextCount=2;s.holdEnabled=true;
    s.nextPreview=p.session->player.pieces.nextPreview;s.blockMaxHeight=2;s.blockMaxWidth=3;s.hybridBoardSync=true;return s;
}
void connect_players(const std::vector<Player*>& players,const std::string& url) {
    auto& a=*players.front();
    int stage=0;
    const auto began=Clock::now();
    auto pump=[&]{
        for(std::size_t i=0;i<players.size();++i)try {Network::poll_connection(players[i]->network);}
        catch(const std::exception& error) {
            std::cerr<<"Network setup stage="<<stage<<" client="<<i<<" elapsed_ms="
                <<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count()<<" error="<<error.what()<<'\n';
            throw;
        }
        std::this_thread::sleep_for(1ms);
    };
    auto wait=[&](const auto& predicate) {
        std::cerr<<"Network setup entering stage="<<(stage+1)<<" elapsed_ms="
            <<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-began).count()<<'\n';
        ++stage;const auto end=Clock::now()+15s;while(!predicate() && Clock::now()<end)pump();
        if(!predicate()) {
            std::cerr<<"Network setup stage="<<stage<<'\n';
            for(std::size_t i=0;i<players.size();++i) {
                const auto& n=players[i]->network;
                std::cerr<<"client="<<i<<" connection="<<static_cast<int>(n.connection)<<" phase="<<static_cast<int>(n.matchPhase)
                    <<" setups="<<n.playerSetups.size()<<" pending="<<n.pendingRoomRequest.has_value()<<" status="<<n.connectionMessage<<'\n';
            }
        }
        require(predicate(),"Network setup timeout");
    };
    auto request=[&](Player& p,Network::RoomOperation op,const std::string& name={},int target=-1,Network::RoomPolicy policy={}) {
        Network::request_room(p.network,op,name,false,false,target,policy);wait([&]{return !p.network.pendingRoomRequest;});
    };
    for(auto* p:players)Network::initialize_network_services(p->context,url);
    wait([&]{for(auto* p:players)if(p->network.connection!=Network::ConnectionState::Connected)return false;return true;});
    const auto name="Latency-"+std::to_string(stamp());
    request(a,Network::RoomOperation::Create,name);for(auto* p:players)if(p!=&a)request(*p,Network::RoomOperation::Join,name);
    request(a,Network::RoomOperation::SetPolicy,{},-1,{false});request(a,Network::RoomOperation::StartMatch);
    wait([&]{for(auto* p:players)if(!a.network.matchEpoch || a.network.matchEpoch!=p->network.matchEpoch)return false;return true;});
    for(auto* p:players) {Network::send(p->network,"start2");Network::send(p->network,Network::encode_lobby_player_setup(setup(*p)));}
    wait([&]{for(auto* p:players)if(p->network.playerSetups.size()!=players.size()-1)return false;return true;});
    for(auto* p:players)request(*p,Network::RoomOperation::SetReady,{},1);
    for(auto* p:players)Network::send(p->network,"start6");
    wait([&]{for(auto* p:players)if(p->network.matchPhase!=Network::RoomMatchPhase::Playing)return false;return true;});
    for(auto* p:players) {Network::configure_board_sync(p->context,true,42);Engine::initialize_multiplayer_scene(p->context);}
}
struct JoinedThread {
    std::atomic<bool>& stop;std::thread thread;
    ~JoinedThread() {stop=true;if(thread.joinable()) thread.join();}
};
double percentile(std::vector<double> values,double fraction) {
    if(values.empty()) return 0;std::sort(values.begin(),values.end());return values[static_cast<std::size_t>(std::ceil((values.size()-1)*fraction))];
}
void run_case(Console& console,Sink& sink,const std::string& prefix,const std::string& url,const std::string& name,int rate,int count) {
    const bool online=rate>=0;Player a(online),b(true);
    const bool botPeer=name=="bot-slow" || name=="bot-handling";
    // Both simulated terminals share std::cout even though peer bytes are
    // discarded. Protect ostream formatting as well as the output buffer.
    std::recursive_mutex botOutputMutex;
    if(botPeer){a.context.attach_output_mutex(botOutputMutex);b.context.attach_output_mutex(botOutputMutex);}
    const int playerCount=name=="multi16"?16:(name=="multi8" || name=="multi8legacy"?8:2);
    std::vector<std::unique_ptr<Player>> extra;std::vector<Player*> players{&a,&b};
    for(int i=2;i<playerCount;++i){extra.push_back(std::make_unique<Player>(true));players.push_back(extra.back().get());}
    for(auto* p:players)p->network.boardViewsEnabled=name!="multi8legacy";
    if(online)connect_players(players,url);
    if(online && playerCount>2)for(auto* p:players) {
        Engine::position_multiplayer_scene(p->context,{140,45});
        const auto& scene=p->session->roomScene;Network::BoardViews views{playerCount,{},true};int pixels=0;
        for(const auto& [slot,g]:scene.miniLayouts) {
            const int rows=std::clamp(g.top+g.height+1-g.ceiling,1,160);
            views.players.push_back({slot,false,g.width,rows,1});pixels+=g.width*rows;
        }
        for(auto& view:views.players)view.fps=std::clamp(std::min(60000/std::max(1,pixels),p->network.miniUpdateBudget/std::max(1,int(views.players.size()))),4,60);
        Network::publish_board_views(p->context,views);
    }
    if(online && playerCount>2 && a.network.boardViewsEnabled) {
        const auto deadline=Clock::now()+10s;bool ready=false;
        while(Clock::now()<deadline) {
            ready=true;
            for(auto* p:players){Network::poll_connection(p->network);ready&=p->network.boardBudget && !p->network.boardBudget->full;}
            if(ready)break;
            std::this_thread::sleep_for(1ms);
        }
        require(ready,"Thumbnail interests did not settle before the load test");
    }
    FlushConsoleInputBuffer(console.input);
    // Both endpoints run in this process but traverse the selected public
    // server. A common steady clock measures capture-to-VT age without
    // estimating one-way delay from unrelated machine clocks.
    std::mutex sentMutex;
    std::map<std::pair<int,std::uint64_t>,std::int64_t> sentFull,sentMini;
    const auto remember_sent=[&](Player& peer) {
        auto now=stamp();
        if(botPeer)now-=static_cast<std::int64_t>((peer.context.real_now()-peer.network.boardSync.lastMotion).seconds()*1e9);
        std::lock_guard<std::mutex> lock(sentMutex);
        if(peer.network.boardSync.sentSequence)sentFull.emplace(std::make_pair(peer.network.roomPlayerIndex,peer.network.boardSync.sentSequence),now);
        if(peer.network.miniSequence)sentMini.emplace(std::make_pair(peer.network.roomPlayerIndex,peer.network.miniSequence),now);
    };
    std::atomic<bool> stop=false,peerFailed=false;
    JoinedThread peer{stop,{}};
    if(online) peer.thread=std::thread([&]{try {
        if(botPeer) {
            peerBotOutput=true;Test::ScriptMotionBot bot;b.context.attach_autoplay(bot);
            b.input.keyStateProvider=[](int){return false;};
            b.context.config.autoplay.mode=Runtime::AutoplayMode::ChatGPT;b.context.config.autoplay.pps=2.5;
            b.context.config.autoplay.handlingSimulation=name=="bot-handling";
            b.context.config.handling.das=Runtime::Duration::from_seconds(.04);b.context.config.handling.arr=Runtime::Duration::from_seconds(.02);
            b.context.config.handling.softDropFixed=true;b.context.config.handling.softDropFactor=.02;
            auto& p=b.session->player;Engine::AutoplayCarryState carry;Engine::MovementInputState keys;
            const auto pump=[&] {
                if(stop)return Rules::PlayResult::Retry;
                Network::poll_connection(b.network,0ms,250us);const auto until=Clock::now()+750us;
                while(Clock::now()<until && Network::try_receive_multiplayer_frame(b.context,false,250us)){}
                Network::send_board_visual_tick(b.context);remember_sent(b);p.timing.framePoll=b.context.real_now();
                return Rules::PlayResult::ContinueFrame;
            };
            while(!stop) {
                const auto start=Clock::now();auto piece=Piece::get_definition(p.pieces.bag.front().type);
                auto movement=Engine::make_spawn_movement_state(10,piece.type());
                p.presentation.activePose=movement.pose;p.presentation.activePieceType=piece.type();p.gameplay.scoring.score=0;
                Network::send_board_state_frame(b.context,p,Network::ActivePieceState::active(movement.pose),{},true);remember_sent(b);
                p.timing.autoplay=b.context.real_now();
                if(name=="bot-handling")Engine::run_handling_autoplay_step(b.context,p.gameplay.playfield.field,piece,p,movement,keys,pump,&carry);
                else Engine::run_autoplay_step(b.context,p.gameplay.playfield.field,piece,p,movement,carry,false,2.5,false,Runtime::FoolMode::Disabled,
                    Runtime::GravityInterval::disabled(),[&](auto pose){return piece.can_place(pose,p.gameplay.playfield.field);},
                    [&](auto& pose,auto turns,auto& kick){return piece.try_rotate(pose,turns,p.gameplay.playfield.field,kick);},pump);
                remember_sent(b);
                while(!stop && Clock::now()<start+400ms) {
                    if(b.context.real_now()-p.timing.framePoll>=Runtime::Duration::from_seconds(1./120))pump();
                    std::this_thread::sleep_for(1ms);
                }
            }
            peerBotOutput=false;return;
        }
        auto next=Clock::now();auto snapshot=next,interests=next;int step=0;
        for(auto* peer:players)if(peer!=&a) {
            auto& p=peer->session->player;
            for(int row=15;row<20;++row)for(int col=0;col<10;++col)if(col!=4)p.gameplay.playfield.field.set_cell(row,col,Runtime::BoardCell::piece((row+col)%7));
            Network::send_board_state_frame(peer->context,p,Network::ActivePieceState::active({{6,3},Runtime::Rotation{}}),{},true);
            remember_sent(*peer);
        }
        while(!stop) {
            for(auto* peer:players)if(peer!=&a) {
                // Give each synthetic peer the real client's bounded work.
                Network::poll_connection(peer->network,0ms,250us);
                const auto until=Clock::now()+750us;
                while(Clock::now()<until && Network::try_receive_multiplayer_frame(peer->context,false,250us)){}
                Network::send_board_visual_tick(peer->context);
                if(playerCount>2)Network::publish_board_views(peer->context,peer->network.boardViews);
            }
            const auto now=Clock::now();
            if(playerCount>2 && a.network.boardViewsEnabled && now>=interests) {
                for(auto* peer:players)if(peer!=&a) {
                    auto views=peer->network.boardViews;int pixels=0;
                    for(const auto& view:views.players)if(!view.full)pixels+=view.columns*view.rows;
                    const auto count=std::count_if(views.players.begin(),views.players.end(),[](const auto& v){return !v.full;});
                    for(auto& view:views.players)if(!view.full)view.fps=std::clamp(std::min(60000/std::max(1,pixels),peer->network.miniUpdateBudget/std::max(1,int(count))),4,60);
                    Network::publish_board_views(peer->context,views);
                }
                interests=now+100ms;
            }
            if(rate>0 && now>=next) {
                const Piece::Pose pose{{6+(step/16)%3,1+(step%5)},Runtime::Rotation{step%4}};
                for(auto* peer:players)if(peer!=&a){Network::send_piece_motion_frame(peer->context,peer->session->player,pose);remember_sent(*peer);}++step;
                next+=std::chrono::microseconds(1000000/rate);if(next<now-100ms) next=now;
                if(now>=snapshot) {
                    for(auto* peer:players)if(peer!=&a) {
                        auto& p=peer->session->player;p.gameplay.scoring.placedBlocks=step/12;p.gameplay.scoring.totalAttack=step/48;
                        if(playerCount>2)for(int col=0;col<10;++col)
                            p.gameplay.playfield.field.set_cell(19,col,Runtime::BoardCell::piece((step/2)%7));
                        else p.gameplay.playfield.field.set_cell(15,(step/8)%10,Runtime::BoardCell::piece(step%7));
                        Network::send_board_state_frame(peer->context,p,Network::ActivePieceState::active(pose),{},true);
                        remember_sent(*peer);
                    }
                    snapshot=now+(rate>120 || playerCount>2?16ms:100ms);
                }
            } else if(rate==0 && now>=snapshot) {
                Network::send_board_state_frame(b.context,b.session->player,Network::ActivePieceState::active({{6,3},Runtime::Rotation{}}),{},true);remember_sent(b);snapshot=now+500ms;
            }
            std::this_thread::sleep_for(1ms);
        }
    } catch(const std::exception& e) {std::cerr<<"Peer failed: "<<e.what()<<'\n';peerFailed=true;stop=true;}});
    auto& c=a.context;auto& p=a.session->player;
    if(online) Engine::draw_multiplayer_scene(c,true);
    else {Render::clear_screen();Engine::center_game_boards(c,{140,45});Render::redraw_player_board(c,p);}
    Engine::initialize_board_timers(c,p);
    Engine::MovementState movement;movement.pose={{5,3},Runtime::Rotation{}};
    Engine::MovementInputState input;Engine::AutoplayCarryState carry;
    auto piece=Piece::get_definition(p.pieces.bag.front().type);
    std::vector<Sample> samples(count);std::vector<double> frameTimes;frameTimes.reserve(10000);
    struct Seen {std::uint64_t sequence=0;std::int64_t first=0,last=0;int count=0;};
    std::map<int,Seen> seenFull,seenMini;
    std::vector<double> remoteAges,remoteIntervals,probeRtts;
    Runtime::TimePoint observedPaint{};
    const auto observe_remote=[&] {
        const auto now=stamp();const auto& scene=c.session.roomScene;
        if(scene.lastPaint==observedPaint)return;
        observedPaint=scene.lastPaint;
        std::lock_guard<std::mutex> lock(sentMutex);
        const auto observe=[&](int slot,std::uint64_t sequence,auto& seen,const auto& sent) {
            auto& prior=seen[slot];if(!sequence || sequence==prior.sequence)return;
            const auto at=sent.find({slot,sequence});if(at==sent.end())return;
            remoteAges.push_back((now-at->second)/1e6);
            if(prior.last)remoteIntervals.push_back((now-prior.last)/1e6);
            else prior.first=now;
            prior.last=now;prior.sequence=sequence;++prior.count;
        };
        for(const auto& [slot,p]:scene.paintedPlayers) {
            const auto it=a.network.peerTimelines.find(slot);
            if(it!=a.network.peerTimelines.end())observe(slot,it->second.applied_sequence(),seenFull,sentFull);
        }
        for(const auto& [slot,sequence]:scene.paintedMiniSequences)observe(slot,sequence,seenMini,sentMini);
    };
    sink.context=&c;sink.piece=&piece;sink.samples=&samples;sink.snapshot=prefix+"_"+name+"_console.json";
    sink.pending=-1;sink.completed=-1;sink.ready=false;sink.writes=sink.bytes=0;sink.writeTimes.clear();
    std::atomic<bool> failed=false;
    JoinedThread injector{stop,{}};
    injector.thread=std::thread([&]{try {
        const auto by=Clock::now()+3s;while(!sink.ready && !stop && Clock::now()<by) std::this_thread::sleep_for(1ms);
        require(sink.ready,"First piece did not paint");std::this_thread::sleep_for(100ms);
        std::mt19937 random(7102);
        for(int i=0;i<count && !stop;++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(31+random()%30));
            auto& sample=samples[i];sample.column=i%2==0?4:3;sample.queued=stamp();
            sink.pending.store(i,std::memory_order_release);console.tap(i%2==0?VK_F14:VK_F13);
            const auto end=Clock::now()+2s;while(sink.completed.load(std::memory_order_acquire)<i && !stop && Clock::now()<end) std::this_thread::sleep_for(1ms);
            require(sink.completed>=i,"Input did not appear in console");
        }
        console.tap(VK_F24);
    } catch(const std::exception& e) {std::cerr<<"Injection failed: "<<e.what()<<'\n';failed=true;stop=true;}});
    Input::GameplayInputScope capture(&a.input);
    auto canPlace=[&](Piece::Pose pose) {
        const bool okay=piece.can_place(pose,p.gameplay.playfield.field);const int index=sink.pending.load(std::memory_order_acquire);
        if(okay && index>=0 && index>sink.completed && pose.position.row==5 && pose.position.col==samples[index].column && !samples[index].applied)
            samples[index].applied=stamp();
        return okay;
    };
    const auto result=Engine::run_live_piece_movement(c,p.gameplay.playfield.field,piece,p,movement,input,carry,false,canPlace,
        [&](Piece::Pose& pose,Runtime::Rotation r,Piece::KickIndex& k){return piece.try_rotate(pose,r,p.gameplay.playfield.field,k);},
        [&] {if(stop) return Rules::PlayResult::Retry;const auto begin=stamp();
            const auto probes=a.network.countdownProbes;
            const auto r=Engine::run_frame(c,p.gameplay.playfield.field,p,true,true);frameTimes.push_back((stamp()-begin)/1e6);
            for(const auto& [id,sent]:probes)if(!a.network.countdownProbes.count(id))
                probeRtts.push_back(std::chrono::duration<double,std::milli>(Clock::now()-sent).count());
            observe_remote();return r;});
    stop=true;injector.thread.join();if(peer.thread.joinable()) peer.thread.join();
    if(failed || peerFailed) {
        const auto index=sink.pending.load();
        std::cerr<<"Failure index="<<index<<" completed="<<sink.completed<<" pose="<<movement.pose.position.row<<','<<movement.pose.position.col;
        if(index>=0) std::cerr<<" applied="<<samples[index].applied<<" displayed="<<samples[index].displayed;
        std::cerr<<'\n';sink.save(prefix+"_"+name+"_failed_console.json");
    }
    sink.context=nullptr;sink.piece=nullptr;sink.samples=nullptr;
    require(!failed && !peerFailed && result==Rules::PlayResult::ContinueFrame,"Benchmark stopped before completion");
    std::vector<double> applied,displayed;
    std::ofstream csv(prefix+"_"+name+".csv");csv<<"sample,input_to_model_ms,input_to_output_ms\n";
    for(int i=0;i<count;++i) {const auto& s=samples[i];require(s.applied>=s.queued && s.displayed>=s.applied,"Invalid latency sample");
        applied.push_back((s.applied-s.queued)/1e6);displayed.push_back((s.displayed-s.queued)/1e6);
        csv<<i<<','<<applied.back()<<','<<displayed.back()<<'\n';}
    std::uint64_t miniFrames=0,miniBytes=0,motionFrames=0,snapshotFrames=0;
    for(auto* peer:players)if(peer!=&a){miniFrames+=peer->network.miniFramesSent;miniBytes+=peer->network.miniBytesSent;
        motionFrames+=peer->network.boardSync.motionsSent;snapshotFrames+=peer->network.boardSync.snapshotsSent;}
    std::vector<double> remoteRates;
    for(const auto* group:{&seenFull,&seenMini})for(const auto& [slot,seen]:*group)
        if(seen.count>1 && seen.last>seen.first)remoteRates.push_back((seen.count-1)*1e9/(seen.last-seen.first));
    std::ofstream stats(prefix+"_"+name+".json");stats<<std::fixed<<std::setprecision(3)
        <<"{\"case\":\""<<name<<"\",\"samples\":"<<count<<",\"opponent_motion_hz\":"<<rate
        <<",\"observation\":\""<<(sink.streamObservation?"vt_stream":"win32_screen_buffer")<<'"'
        <<",\"input_model_p50_ms\":"<<percentile(applied,.5)<<",\"input_model_p95_ms\":"<<percentile(applied,.95)
        <<",\"output_p50_ms\":"<<percentile(displayed,.5)<<",\"output_p95_ms\":"<<percentile(displayed,.95)
        <<",\"output_p99_ms\":"<<percentile(displayed,.99)<<",\"output_max_ms\":"<<percentile(displayed,1)
        <<",\"frame_p95_ms\":"<<percentile(frameTimes,.95)<<",\"frame_max_ms\":"<<percentile(frameTimes,1)
        <<",\"write_p95_ms\":"<<percentile(sink.writeTimes,.95)<<",\"write_max_ms\":"<<percentile(sink.writeTimes,1)
        <<",\"console_writes\":"<<sink.writes<<",\"console_bytes\":"<<sink.bytes
        <<",\"players\":"<<(online?playerCount:1)<<",\"adaptive_views\":"<<(a.network.boardViewsEnabled?"true":"false")
        <<",\"opponent_mini_change_hz\":"<<(online && playerCount>2?60:0)
        <<",\"bot_peer\":"<<(botPeer?"true":"false")<<",\"bot_pps\":"<<(botPeer?2.5:0)
        <<",\"mini_update_budget_final\":"<<a.network.miniUpdateBudget
        <<",\"in_game_probe_rtt_p50_ms\":"<<percentile(probeRtts,.5)
        <<",\"in_game_probe_rtt_p95_ms\":"<<percentile(probeRtts,.95)
        <<",\"in_game_best_rtt_ms\":"<<std::chrono::duration<double,std::milli>(a.network.countdownBestRtt).count()
        <<",\"peer_motions_sent\":"<<motionFrames<<",\"peer_snapshots_sent\":"<<snapshotFrames
        <<",\"peer_mini_frames_sent\":"<<miniFrames<<",\"peer_mini_bytes_sent\":"<<miniBytes
        <<",\"remote_capture_to_vt_samples\":"<<remoteAges.size()
        <<",\"remote_capture_to_vt_p50_ms\":"<<percentile(remoteAges,.5)
        <<",\"remote_capture_to_vt_p95_ms\":"<<percentile(remoteAges,.95)
        <<",\"remote_capture_to_vt_max_ms\":"<<percentile(remoteAges,1)
        <<",\"remote_update_hz_p50\":"<<percentile(remoteRates,.5)
        <<",\"remote_update_gap_p95_ms\":"<<percentile(remoteIntervals,.95)<<"}\n";
    std::cerr<<name<<" samples="<<count<<" model p95="<<percentile(applied,.95)<<" ms output p95="<<percentile(displayed,.95)
        <<" ms max="<<percentile(displayed,1)<<" ms frames max="<<percentile(frameTimes,1)<<" ms\n";
}
}
int main(int argc,char** argv) {
    try {
        _putenv_s("TETR_TEST_CLOCK_SCALE","1");
        require(argc>=2,"Usage: benchmark output-prefix [url|udp] [samples] [case|all]");
        const std::string prefix=argv[1],endpoint=argc>2?argv[2]:"udp",which=argc>4?argv[4]:"all";
        require(which=="all" || which=="offline" || which=="idle" || which=="motion120" || which=="stress480" ||
            which=="multi8" || which=="multi8legacy" || which=="multi16" || which=="bot-slow" || which=="bot-handling","Unknown case");
        const std::string observation=argc>6?argv[6]:"console";
        require(observation=="console" || observation=="stream","Unknown output observation");
        const int samples=argc>3?std::stoi(argv[3]):200;require(samples>=8 && samples<=2000,"Sample count");
        std::unique_ptr<Network::RelayServer> relay;std::atomic<bool> stop=false;JoinedThread server{stop,{}};
        const auto url=endpoint=="udp"?std::string("127.0.0.1:3025"):endpoint;
        if(endpoint=="udp" || url.rfind("ws://127.0.0.1:",0)==0) {
            relay=std::make_unique<Network::RelayServer>(219);relay->bind(3025);
            server.thread=std::thread([&]{while(!stop) relay->poll(1ms);});
        }
        Console console(argc<6 || std::string(argv[5])!="inherit");Sink sink(console,observation=="stream");auto* previous=std::cout.rdbuf(&sink);
        struct Restore {std::streambuf* previous;~Restore(){std::cout.rdbuf(previous);}} restore{previous};
        Render::TerminalSession terminal;
        for(const auto& scenario:std::vector<std::pair<std::string,int>>{{"offline",-1},{"idle",0},{"motion120",120},{"stress480",480},
            {"multi8",120},{"multi8legacy",120},{"multi16",120},{"bot-slow",0},{"bot-handling",0}})
            if(which=="all" || which==scenario.first) run_case(console,sink,prefix,url,scenario.first,scenario.second,samples);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Benchmark error: "<<e.what()<<'\n';return 1;}
}
