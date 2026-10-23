#include "tetr/network/board_view.h"
#include "tetr/network/network_services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/transport.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/runtime/game_context.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace Network;
using namespace std::chrono_literals;
struct Member {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};NetworkServices network;
    Member() {
        context.attach_network(network);context.config=Runtime::normalize_user_config(UserConfig::Settings{},Runtime::GameMode::Multiplayer);
        auto& p=session->player;p.gameplay.playfield.width=10;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
        p.gameplay.playfield.field.initialize(10,20);p.pieces.bag.push_back({Piece::Type{0},false});
        p.pieces.nextPreview={Piece::Type{1},Piece::Type{2}};
        p.gameplay.playfield.field.set_cell(19,1,Runtime::BoardCell::piece(0));
        p.gameplay.playfield.field.set_cell(-1,1,Runtime::BoardCell::garbage());
        session->roomScene.selfAlive=true;
    }
    ~Member(){disconnect_online(network);}
};
int main(int argc,char** argv) {
    Member a,b,c;std::vector<Member*> members{&a,&b,&c};
    {
        Member large;auto& f=large.session->player.gameplay.playfield;
        for(int width:{20,40,80})for(int hole:{0,1,7,19}) {
            f.width=width;f.field.initialize(width,20);
            for(int row=12;row<20;++row)for(int col=0;col<width;++col)
                if(col!=hole)f.field.set_cell(row,col,Runtime::BoardCell::garbage());
            const auto original=capture_mini_board(large.session->player,16,14,1);
            const auto projected=project_mini_board(original,8,7);
            for(const auto* frame:{&original,&projected}) {
                int gaps=0,filled=0;for(int col=0;col<frame->columns;++col)
                    frame->at(frame->rows-1,col).occupied()?++filled:++gaps;
                assert(gaps>0 && filled>0);
                const auto decoded=decode_mini_board(encode_mini_board(*frame));assert(decoded && decoded->cells==frame->cells);
            }
        }
    }
    auto wide=capture_mini_board(a.session->player,20,28,1),small=capture_mini_board(a.session->player,8,12,2);
    assert(wide.at(27,2).occupied() && wide.at(7,2).occupied());
    for(const auto* frame:{&wide,&small}) {
        const auto wire=encode_mini_board(*frame);const auto decoded=decode_mini_board(wire);
        assert(decoded && decoded->cells==frame->cells && decoded->sequence==frame->sequence);
        assert(validated_mini_board_sequence(wire)==frame->sequence);
        for(std::size_t i=0;i<wire.size();++i) {
            assert(!decode_mini_board(std::string_view(wire).substr(0,i)));
            assert(!validated_mini_board_sequence(std::string_view(wire).substr(0,i)));
            auto corrupt=wire;corrupt[i]^=char(0x80);
            assert(validated_mini_board_sequence(corrupt).has_value()==decode_mini_board(corrupt).has_value());
        }
        assert(!decode_mini_board(wire+"x"));
    }
    assert(project_mini_board(wide,8,12).cells==small.cells);
    for(double rate:{0.,1.,2.5}) {
        wide.clock=RemoteClockSample{123.456,rate};
        const auto wire=encode_mini_board(wide);const auto decoded=decode_mini_board(wire);
        assert(decoded && decoded->clock && decoded->clock->elapsedSeconds==123.456 && decoded->clock->rate==rate);
        assert(validated_mini_board_sequence(wire)==wide.sequence);
        assert(project_mini_board(*decoded,8,12).clock->rate==rate);
        for(std::size_t i=0;i<wire.size();++i)assert(!decode_mini_board(std::string_view(wire).substr(0,i)));
    }
    wide.clock=RemoteClockSample{-1.,1.};assert(!decode_mini_board(encode_mini_board(wide)));
    wide.clock.reset();
    assert(encode_mini_board(small).size()<encode_board_state(a.context,a.session->player).size()/2);
    assert(!decode_board_views("!view-v1;3;1,0,10,20,0"));
    assert(!decode_board_views("!view-v1;3;1,0,10,20,8;1,1,0,0,60"));
    assert(!decode_board_views("!view-v1;3;1,0,201,20,8"));
    assert(!decode_board_budget("!budget-v1;0;10;0;5"));
    assert(!decode_board_views("!view-v1;3;1,0,8,12,60"));
    assert(decode_board_views("!view-v2;3;1,0,8,12,60")->highRate);
    assert(decode_board_views("!view-v2;2;1,1,0,0,120")->highRate);
    assert(!decode_board_views("!view-v2;3;1,0,8,12,61"));
    assert(!decode_board_budget("!budget-v1;0;8;12;60"));
    assert(decode_board_budget("!budget-v2;0;8;12;60")->highRate);
    std::unique_ptr<RelayServer> server;if(argc<2){server=std::make_unique<RelayServer>(555);server->bind(3026);}
    const auto pump=[&] {
        if(server)server->poll(0ms);
        for(auto* p:members){poll_connection(p->network);while(auto frame=try_receive_multiplayer_frame(p->context,false))
            if(frame->opponentState)p->network.peerSnapshots[frame->sourceSlot]=*frame->opponentState;}
        std::this_thread::sleep_for(1ms);
    };
    int waitNumber=0;
    const auto wait=[&](auto done){++waitNumber;const auto end=std::chrono::steady_clock::now()+10s;
        while(!done() && std::chrono::steady_clock::now()<end)pump();
        if(!done())std::cerr<<"adaptive board wait failed: "<<waitNumber<<'\n';assert(done());};
    const auto request=[&](Member& p,RoomOperation op,const std::string& name="",int value=-1){
        request_room(p.network,op,name,false,false,value);wait([&]{return !p.network.pendingRoomRequest;});};
    for(auto* p:members)initialize_network_services(p->context,argc>1?argv[1]:"127.0.0.1:3026");
    wait([&]{for(auto* p:members)if(p->network.connection!=ConnectionState::Connected)return false;return true;});
    const auto room="Adaptive-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    request(a,RoomOperation::Create,room);request(b,RoomOperation::Join,room);request(c,RoomOperation::Join,room);
    set_room_policy(a.network,{false});wait([&]{return !a.network.pendingRoomRequest;});request(a,RoomOperation::StartMatch);
    wait([&]{for(auto* p:members)if(p->network.matchEpoch!=a.network.matchEpoch)return false;return true;});
    LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=2;setup.hybridBoardSync=true;
    setup.blockMaxWidth=3;setup.blockMaxHeight=2;setup.holdEnabled=true;setup.nextPreview={Piece::Type{1},Piece::Type{2}};
    for(auto* p:members){send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));}
    wait([&]{for(auto* p:members)if(p->network.playerSetups.size()!=2)return false;return true;});
    for(auto* p:members)request(*p,RoomOperation::SetReady,"",1);
    for(auto* p:members)send(p->network,"start6");
    wait([&]{for(auto* p:members)if(p->network.matchPhase!=RoomMatchPhase::Playing)return false;return true;});
    for(auto* p:members)configure_board_sync(p->context,true,static_cast<std::uint32_t>(a.network.matchEpoch));
    a.context.config.field.invisible=true;a.context.config.field.periodicInvisible=true;
    a.context.config.field.invisibilityDuration=Runtime::Duration::from_seconds(3);
    send_field_visibility(a.context);
    wait([&]{return b.network.peerFieldVisibility.count(0) && c.network.peerFieldVisibility.count(0);});
    assert(b.network.peerFieldVisibility.at(0).periodic && b.network.peerFieldVisibility.at(0).duration==Runtime::Duration::from_seconds(3));
    assert(!b.context.config.field.invisible);
    a.context.config.field.invisible=false;send_field_visibility(a.context);
    wait([&]{return !b.network.peerFieldVisibility.at(0).invisible;});
    // All three viewers request thumbnails. No actor needs to serialize a
    // full field on movement, even when a caller requests a forced checkpoint.
    for(auto* p:members) {
        BoardViews views{3,{}};for(auto* peer:members)if(peer!=p)views.players.push_back({peer->network.roomPlayerIndex,false,8,12,5});
        publish_board_views(p->context,views);
    }
    wait([&]{for(auto* p:members)if(!p->network.boardBudget || p->network.boardBudget->full || p->network.boardBudget->fps!=5)return false;return true;});
    const auto framesBefore=a.network.miniFramesSent;
    const auto end=std::chrono::steady_clock::now()+650ms;int attempts=0;
    while(std::chrono::steady_clock::now()<end) {
        for(auto* p:members) {
            // Keep a distinct next color until the rate limiter accepts it;
            // a fast caller must not accidentally alias the sample cadence.
            p->session->player.gameplay.playfield.field.set_cell(19,1,Runtime::BoardCell::piece(p->network.miniFramesSent%7));
            send_board_state_frame(p->context,p->session->player,ActivePieceState::active({{5,3},Runtime::Rotation{}}),{},true);
        }
        ++attempts;pump();
    }
    assert(attempts>20 && a.network.boardSync.snapshotsSent==0 && a.network.boardSync.motionsSent==0);
    assert(a.network.miniFramesSent-framesBefore>=3 && a.network.miniFramesSent-framesBefore<=4);
    wait([&]{return b.network.peerMiniBoards.count(0) && c.network.peerMiniBoards.count(0);});
    assert(b.network.peerMiniBoards.at(0).columns==8 && b.network.peerMiniBoards.at(0).rows==12);
    assert(b.network.peerMiniBoards.at(0).clock.has_value());
    // A viewer removes A from its visible set. Other actors and control
    // packets remain available, but A's future visual frames are not forwarded.
    publish_board_views(b.context,{3,{{2,false,4,10,3}}});
    const auto beforeUnsubscribed=b.network.peerMiniBoards.at(0).sequence;
    auto delayed=b.network.peerMiniBoards.at(0);delayed.sequence+=100;
    assert(handle_multiplayer_message(b.network,encode_player_packet({b.network.matchEpoch,0,encode_mini_board(delayed)})));
    assert(b.network.peerMiniBoards.at(0).sequence==beforeUnsubscribed);
    for(int i=0;i<50;++i)pump();const auto frozen=b.network.peerMiniBoards.at(0).sequence;
    for(int i=0;i<250;++i){send_board_visual_tick(a.context);pump();}
    assert(b.network.peerMiniBoards.at(0).sequence==frozen);
    publish_board_views(c.context,{3,{{1,false,8,12,5}}});
    wait([&]{return a.network.boardBudget && !a.network.boardBudget->full && a.network.boardBudget->fps==0;});
    const auto disabledFrames=a.network.miniFramesSent;
    for(int i=0;i<30;++i){send_board_state_frame(a.context,a.session->player,ActivePieceState::spawn(),{},true);pump();}
    assert(a.network.miniFramesSent==disabledFrames && a.network.boardSync.snapshotsSent==0);
    const int target=a.network.targetSlot;assert(target==1 || target==2);
    send_attack_frame(a.context,{Runtime::MultiplayerSequenceId::from_wire(1),{},7});
    wait([&]{return a.network.deliveredAttack==1;});
    publish_board_views(c.context,{3,{{0,false,8,12,5},{1,false,8,12,5}}});
    wait([&]{return a.network.boardBudget && a.network.boardBudget->fps==5;});
    // Lose one delivered miniature. An unchanged frame's one-second heartbeat
    // is forwarded again and reconstructs the cache without a full snapshot.
    c.network.peerMiniBoards.erase(0);a.network.lastMiniSample=a.context.real_now()-Runtime::Duration::from_seconds(2);
    a.network.lastMiniSend=a.network.lastMiniSample;send_board_visual_tick(a.context);
    wait([&]{return c.network.peerMiniBoards.count(0)>0;});
    // Explicitly emulate dropped budget negotiation. The periodic interest
    // heartbeat repairs it without blocking input or requiring a reconnect.
    c.network.boardBudget.reset();c.network.lastBoardViewSend=c.context.real_now()-Runtime::Duration::from_seconds(2);
    publish_board_views(c.context,c.network.boardViews);wait([&]{return c.network.boardBudget.has_value();});
    // Promote a thumbnail to a full spectator target. The sender immediately
    // provides a checkpoint and resumes the original motion protocol.
    publish_board_views(b.context,{3,{{0,true,0,0,60},{2,false,4,10,3}}});
    wait([&]{return a.network.boardBudget && a.network.boardBudget->full && a.network.forceFullCheckpoint;});
    send_board_visual_tick(a.context);assert(a.network.boardSync.snapshotsSent==1);
    wait([&]{return b.network.peerTimelines.count(0) && b.network.peerTimelines.at(0).applied_sequence()>0;});
    // Sampling a thumbnail must not create a full checkpoint for every
    // sample when the actor also has a full spectator and a prior placement.
    publish_board_views(c.context,{3,{{0,false,8,12,12},{1,false,8,12,5}}});
    wait([&]{return a.network.boardBudget->fps==12;});
    a.network.boardSync.lastPlacement=RemotePlacementSnapshot{Piece::Type{0}};
    a.session->player.presentation.activePose=Piece::Pose{{2,3},Runtime::Rotation{}};
    const auto fullBefore=a.network.boardSync.snapshotsSent,miniBefore=a.network.miniFramesSent;
    const auto bytesBefore=a.network.boardSync.snapshotBytes;
    const auto mixedEnd=std::chrono::steady_clock::now()+1100ms;
    while(std::chrono::steady_clock::now()<mixedEnd) {
        a.session->player.gameplay.playfield.field.set_cell(19,1,Runtime::BoardCell::piece(a.network.miniFramesSent%7));
        send_board_visual_tick(a.context);pump();
    }
    const auto mixedFull=a.network.boardSync.snapshotsSent-fullBefore,mixedMini=a.network.miniFramesSent-miniBefore;
    assert(mixedMini>=10 && mixedMini<=14 && mixedFull==2);
    std::cout<<"mixed full/12Hz thumbnail, 1.1s: full="<<mixedFull<<" mini="<<mixedMini
        <<" full bytes="<<a.network.boardSync.snapshotBytes-bytesBefore<<'\n';
    wait([&]{return c.network.peerMiniBoards.at(0).sequence==a.network.lastMiniFrame->sequence;});
    assert(c.network.peerMiniBoards.at(0).cells==project_mini_board(*a.network.lastMiniFrame,8,12).cells);
    if(server) {
        // Resizing another thumbnail keeps an existing full scope intact;
        // the relay must not resend A's old anchor for this unrelated edit.
        b.network.playerInbox.clear();
        publish_board_views(b.context,{3,{{0,true,0,0,60},{2,false,5,10,3}}});
        for(int i=0;i<50;++i){server->poll(0ms);poll_connection(b.network);std::this_thread::sleep_for(1ms);}
        assert(std::none_of(b.network.playerInbox.begin(),b.network.playerInbox.end(),[](const auto& p){return p.slot==0;}));
    }
    // Delayed full packets after a downgrade do not rebuild an unneeded
    // timeline. Attacks still enter their independent reliable path.
    const auto state=encode_board_state(a.context,a.session->player);
    const auto delayedFull=encode_board_checkpoint({a.network.boardSync.epoch,999,999,1,board_state_fingerprint(state)},state);
    const auto queued=b.network.playerInbox.size();
    b.network.playerInbox.push_back({b.network.matchEpoch,0,delayedFull});
    publish_board_views(b.context,{3,{{0,false,8,12,12},{2,false,5,10,3}}});
    assert(!b.network.peerTimelines.count(0) && b.network.playerInbox.size()==queued);
    const auto pending=b.network.playerInbox.size();
    handle_multiplayer_message(b.network,encode_player_packet({b.network.matchEpoch,0,delayedFull}));
    assert(b.network.playerInbox.size()==pending);
    handle_multiplayer_message(b.network,encode_player_packet({b.network.matchEpoch,0,encode_attack_frame({Runtime::MultiplayerSequenceId::from_wire(42),{},3})}));
    assert(b.network.playerInbox.size()==pending+1);b.network.playerInbox.pop_back();
    // Re-entering full mode gets a usable checkpoint even after its old
    // timeline has been discarded.
    publish_board_views(b.context,{3,{{0,true,0,0,60},{2,false,5,10,3}}});
    wait([&]{return a.network.boardBudget->full && a.network.forceFullCheckpoint;});
    send_board_visual_tick(a.context);
    wait([&]{return b.network.peerTimelines.count(0) && b.network.peerTimelines.at(0).applied_sequence()>0;});
    // More than four requested sizes exercise the bounded projection cache,
    // including regeneration after eviction without changing the source.
    for(int columns:{2,3,4,5,6,8}) {
        publish_board_views(c.context,{3,{{0,false,columns,12,12},{1,false,8,12,5}}});
        wait([&]{return c.network.peerMiniBoards.at(0).columns==columns;});
        assert(c.network.peerMiniBoards.at(0).cells==project_mini_board(*a.network.lastMiniFrame,columns,12).cells);
    }
    // An unchanged cached repair frame must carry a current wall-clock
    // sample; turn-based/frozen clocks must remain exactly constant.
    auto timed=*a.network.lastMiniFrame;timed.sequence=++a.network.miniSequence;timed.clock=RemoteClockSample{100.,2.};
    send(a.network,encode_mini_board(timed));
    wait([&]{return c.network.peerMiniBoards.at(0).sequence==timed.sequence;});
    auto marker=c.network.peerMiniBoards.at(0).receivedRevision;
    const double initialClock=c.network.peerMiniBoards.at(0).clock->elapsedSeconds;
    wait([&]{return c.network.peerMiniBoards.at(0).receivedRevision>marker;});
    assert(c.network.peerMiniBoards.at(0).clock->elapsedSeconds>initialClock+1.8);
    timed.sequence=++a.network.miniSequence;timed.clock=RemoteClockSample{123.,0.};send(a.network,encode_mini_board(timed));
    wait([&]{return c.network.peerMiniBoards.at(0).sequence==timed.sequence;});
    marker=c.network.peerMiniBoards.at(0).receivedRevision;
    wait([&]{return c.network.peerMiniBoards.at(0).receivedRevision>marker;});
    assert(c.network.peerMiniBoards.at(0).clock->elapsedSeconds==123.);
    // Death removes the third actor; normal two-player interests restore full
    // fields in both directions, with no effect on combat acknowledgements.
    send_round_signal(c.context,OutgoingRoundSignal::LocalLoss);
    wait([&]{for(const auto& m:a.network.roomMembers)if(m.slot==2)return !m.alive;return false;});
    publish_board_views(a.context,{2,{{1,true,0,0,60}}});publish_board_views(b.context,{2,{{0,true,0,0,60}}});
    wait([&]{return a.network.boardBudget && b.network.boardBudget && a.network.boardBudget->full && b.network.boardBudget->full;});
    // A legacy sender has no view capability. Its full snapshots must still
    // reach newer clients requesting thumbnails, which project locally.
    const auto measuredFrames=a.network.miniFramesSent,measuredBytes=a.network.miniBytesSent;
    request(a,RoomOperation::StartMatch);
    wait([&]{for(auto* p:members)if(p->network.matchEpoch!=a.network.matchEpoch)return false;return true;});
    for(auto* p:members){send(p->network,"start2");send(p->network,encode_lobby_player_setup(setup));}
    wait([&]{for(auto* p:members)if(p->network.playerSetups.size()!=2)return false;return true;});
    for(auto* p:members)request(*p,RoomOperation::SetReady,"",1);
    for(auto* p:members)send(p->network,"start6");
    wait([&]{for(auto* p:members)if(p->network.matchPhase!=RoomMatchPhase::Playing)return false;return true;});
    for(auto* p:members)configure_board_sync(p->context,true,static_cast<std::uint32_t>(a.network.matchEpoch));
    for(auto* p:{&a,&b}) {
        BoardViews views{3,{}};for(auto* peer:members)if(peer!=p)views.players.push_back({peer->network.roomPlayerIndex,false,8,12,5});
        publish_board_views(p->context,views);
    }
    wait([&]{return c.network.boardBudget.has_value();});
    c.network.boardBudget.reset(); // Emulate an older binary that ignores !budget-v1.
    send_board_state_frame(c.context,c.session->player,ActivePieceState::spawn(),{},true);
    wait([&]{return a.network.peerSnapshots.count(2) && b.network.peerSnapshots.count(2);});
    assert(!a.network.peerMiniBoards.count(2));
    // High-rate capability is confirmed by the sender's v2 budget. Legacy
    // actors retain v1 budgets capped at 12 Hz in the same room.
    for(auto* p:{&a,&b}) {
        BoardViews views{3,{},true};for(auto* peer:members)if(peer!=p)views.players.push_back({peer->network.roomPlayerIndex,false,8,12,60});
        publish_board_views(p->context,views);
    }
    wait([&]{return a.network.boardViewsV2Confirmed && b.network.boardViewsV2Confirmed &&
        a.network.boardBudget->fps==60 && b.network.boardBudget->fps==60 && c.network.boardBudget && c.network.boardBudget->fps==12;});
    assert(!c.network.boardBudget->highRate);
    const auto highRateBefore=a.network.miniFramesSent;
    const auto highRateEnd=std::chrono::steady_clock::now()+500ms;
    while(std::chrono::steady_clock::now()<highRateEnd) {
        a.session->player.gameplay.playfield.field.set_cell(19,1,Runtime::BoardCell::piece(a.network.miniFramesSent%7));
        send_mini_board_frame(a.context,a.session->player);pump();
    }
    const auto highRateFrames=a.network.miniFramesSent-highRateBefore;
    assert(highRateFrames>=20 && highRateFrames<=31);
    wait([&]{return b.network.peerMiniBoards.count(0) && b.network.peerMiniBoards.at(0).sequence==a.network.lastMiniFrame->sequence;});
    std::cout<<"negotiated 60Hz miniature: "<<highRateFrames<<" frames in 0.5s; legacy sender budget=12Hz\n";
    // Sustained in-game echo probes reduce aggregate visual demand under
    // queueing, recover slowly when stable, and leave the start clock alone.
    const auto deadlineBefore=a.network.countdownDeadline;
    const auto probeReply=[&](std::chrono::milliseconds rtt) {
        const auto probe=++a.network.countdownProbe;
        a.network.countdownProbes[probe]=std::chrono::steady_clock::now()-rtt;
        handle_multiplayer_message(a.network,encode_match_packet(a.network.matchEpoch,encode_countdown_clock({probe,0})));
    };
    a.network.countdownBestRtt=30ms;a.network.miniUpdateBudget=240;
    a.network.visualGoodSince={};
    a.network.lastVisualRateChange=std::chrono::steady_clock::now()-1s;
    a.network.visualRtts={};a.network.visualRttSamples=0;
    probeReply(30ms);probeReply(90ms);probeReply(30ms);
    assert(a.network.miniUpdateBudget==240); // A single route spike is ignored.
    probeReply(90ms);assert(a.network.miniUpdateBudget==180); // Two high samples reduce demand promptly.
    probeReply(90ms);assert(a.network.miniUpdateBudget==180); // Rate changes stay bounded in time.
    probeReply(30ms);probeReply(30ms);
    a.network.visualGoodSince=std::chrono::steady_clock::now()-3s;
    a.network.lastVisualRateChange=std::chrono::steady_clock::now()-2s;
    probeReply(30ms);assert(a.network.miniUpdateBudget==210 && a.network.countdownDeadline==deadlineBefore);
    a.network.lastVisualRateChange=std::chrono::steady_clock::now()-1s;
    probeReply(600ms);assert(a.network.miniUpdateBudget==60); // A severe queue is cut on its first echo.
    assert(a.network.countdownDeadline==deadlineBefore);
    std::cout<<"adaptive board view passed; attempted="<<attempts<<" mini frames="<<measuredFrames<<" mini bytes="<<measuredBytes<<"; legacy sender received\n";
}
