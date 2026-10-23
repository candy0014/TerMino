#include "../src/frontend/opengl/session_scene.h"
#include "../src/frontend/opengl/board_layout.h"
#include "../src/frontend/opengl/field_effects.h"
#include "tetr/presentation/context_view.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/network/network_services.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/runtime/game_context.h"
#include "tetr/rules/quick_play.h"
#include "tetr/replay/session_state.h"
#include <cassert>
#include <memory>
namespace TetrTests::graphical_scene_test {
namespace {
void control_rail_has_its_own_geometry() {
    const float end=Graphical::controlsLeft+Graphical::controlsWidth;
    for(int width:{1,4,10,20,100})for(int height:{0,4,20,40,100}) {
        const auto solo=Graphical::solo_geometry(width,height);
        assert(solo.left-26-solo.rail>end+8);
        const auto scene=Graphical::multiplayer_geometry({{width,height},{10,20}},{});
        for(const auto& board:scene.full)assert(board.left-26-board.rail>end+8);
    }
}
void windup_projection_tracks_game_time() {
    auto state=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
    Runtime::GameContext c(*state,services);Network::NetworkServices network;c.attach_network(network);
    c.config.garbage.mode=Runtime::GarbageMode::QuickPlay;c.config.simulationPps=1;
    state->gameStartTime=Runtime::TimePoint{};
    auto& field=state->player.gameplay.playfield;
    field.field.initialize(10,20);field.width=10;field.visibleHeight=field.activeHeight=20;
    auto& progress=state->quickPlay.progress;
    assert(Rules::qp_queue_windup(progress,16,500,false,10));
    assert(Rules::qp_queue_windup(progress,16,500,false,10));
    assert(Rules::qp_queue_windup(progress,8,500,false,10));
    const auto at=[&](double time) {
        state->simulationTime=Runtime::TimePoint::from_seconds(time);
        return Presentation::capture_board_view(c,state->player);
    };
    for(double multiplier:{1.,4.})for(double time:{9.99,10.,10.5,11.5,13.99,14.,14.25,17.99,18.,20.99,21.,10.25}) {
        c.config.timeMultiplier=multiplier;
        const auto view=at(time);
        const int segments=time<10 || time>=21?0:time<18?4:2;
        const double age=segments?time-(time<14?10:time<18?14:18):0;
        assert(view.windup==segments && std::abs(view.windupAge.seconds()-age)<1e-9);
        assert(std::abs(view.elapsed.seconds()-time*multiplier)<1e-9);
    }
    // Pausing freezes the semantic age, independently of rendering/wall time.
    c.config.simulationPps=0;c.config.timeMultiplier=1;c.pause.paused=true;
    c.pause.started=Runtime::TimePoint::from_seconds(10.5);
    const auto frozen=Presentation::capture_board_view(c,state->player);
    assert(frozen.windup==4 && frozen.windupAge.seconds()==.5);
    assert(Presentation::capture_board_view(c,state->player).windupAge==frozen.windupAge);
    // Replay age follows the applied game clock, with no extra speed factor.
    Replay::ReplaySessionState replay;c.attach_replay(replay);
    c.config.mode.replayMode=true;c.config.timeMultiplier=4;
    replay.separateGameTime=true;replay.gameClockPaused=true;replay.playback.paused=true;
    replay.lastGameTimestamp=Runtime::Duration::from_seconds(18.25);
    auto view=Presentation::capture_board_view(c,state->player);
    assert(view.windup==2 && view.windupAge.seconds()==.25);
    replay.lastGameTimestamp=Runtime::Duration::from_seconds(10.75);
    view=Presentation::capture_board_view(c,state->player);
    assert(view.windup==4 && view.windupAge.seconds()==.75 && frozen.windupAge.seconds()==.5);
    assert(progress.windupWarnings.size()==3 && progress.windupWarnings[0].start==10 &&
        progress.windupWarnings[1].start==14 && progress.windupWarnings[2].start==18);
    c.config.garbage.mode=Runtime::GarbageMode::None;
    view=Presentation::capture_board_view(c,state->player);
    assert(!view.windup && view.windupAge==Runtime::Duration{});
}
void remote_finalists_effects_without_ghost() {
    auto source=std::make_unique<Runtime::SessionState>(),state=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices sourceServices,services;
    Runtime::GameContext sender(*source,sourceServices),c(*state,services);
    Network::NetworkServices network;c.attach_network(network);
    c.config.mode.mode=Runtime::GameMode::Multiplayer;c.config.field.ghost=false;
    c.config.display.effects.bounciness=135;
    auto& p=source->player;auto& field=p.gameplay.playfield;
    field.field.initialize(10,20);field.width=10;field.visibleHeight=field.activeHeight=20;
    const auto type=Piece::Type::from_index(0);const auto piece=Piece::get_definition(type);
    auto pose=Piece::spawn_pose(10,type);pose.rotation=Runtime::Rotation(1);
    auto landing=pose;while(piece.can_place(landing.translated(1,0),field.field))++landing.position.row;
    for(int row=16;row<20;++row)for(int col=0;col<10;++col) {
        bool gap=false;for(const auto& cell:piece.cells(landing.rotation))
            gap=gap || (row==landing.position.row+cell[0] && col==landing.position.col+cell[1]);
        if(!gap)field.field.set_cell(row,col,Runtime::BoardCell::piece(3));
    }
    p.pieces.bag.push_back({type,false});
    const auto checkpoint=[&](Network::ActivePieceState active,std::optional<Network::RemotePlacementSnapshot> event={}) {
        const auto wire=Network::encode_board_state(sender,p,active,event);
        auto snapshot=Network::decode_board_state(wire,10,20);assert(snapshot);return *snapshot;
    };
    auto& scene=state->roomScene;scene.enabled=true;scene.ownSlot=0;scene.selfAlive=true;scene.focusSlot=0;
    scene.survivors={0,2};scene.finalistLayout={2,0};network.matchPhase=Network::RoomMatchPhase::Playing;
    for(int slot:{0,2,5}) {
        Network::RoomMember member;member.slot=slot;member.participating=true;member.alive=slot!=5;network.roomMembers.push_back(member);
    }
    state->player=p;
    auto& remote=scene.players[2];remote.presentation.username="REMOTE";
    const auto initial=checkpoint(Network::ActivePieceState::active(pose));
    Engine::apply_remote_board_state(c,remote,initial,false);
    auto boards=Graphical::capture_room_boards(c);
    assert(boards.ownBoardVisible && boards.peers.size()==1);
    const auto find=[&](int slot)->const Presentation::BoardView& {
        for(const auto& peer:boards.peers)if(peer.slot==slot){assert(!peer.miniature);return peer.board;}
        assert(false);return boards.ownBoard;
    };
    auto before=find(2);assert(before.active && !before.ghost && before.effects.bounciness==135);
    Graphical::FieldEffects effect;effect.update(before,0);
    p.gameplay.scoring.placedBlocks=1;field.field.clear();
    Network::RemotePlacementSnapshot event;event.piece=type;event.clearedLines=4;
    const auto locked=checkpoint(Network::ActivePieceState::locked(),event);
    Engine::apply_remote_board_state(c,remote,locked,false);
    boards=Graphical::capture_room_boards(c);auto after=find(2);
    assert(after.lines==0 && after.placementLines==4 && after.placement);
    assert(after.placement->clearedRows==std::vector<int>({16,17,18,19}));
    assert(after.placement->piece.pose==landing && !after.ghost);
    effect.update(after,.1);effect.update(after,.16);
    assert(effect.pulses().back().lines==4 && effect.pulses().back().rows.size()==4);
    assert(!effect.particles().empty() && effect.transform().y>.5f);
    Engine::apply_remote_board_state(c,remote,locked,false);
    assert(remote.presentation.placement.clearedRows.size()==4); // Repair does not lose the prior field.
    // The local player is eliminated. Both remaining remote boards stay full
    // and animate while the local completed clock has stopped.
    state->player.timing.completedGameTime=Runtime::Duration::from_seconds(5);
    scene.selfAlive=false;scene.survivors={2,5};scene.finalistLayout={5,2};scene.focusSlot=5;
    network.roomMembers[0].alive=false;
    network.roomMembers[2].alive=true;
    Engine::apply_remote_board_state(c,scene.players[5],initial,false);
    boards=Graphical::capture_room_boards(c);
    assert(!boards.ownBoardVisible && boards.peers.size()==2);
    for(const auto& peer:boards.peers)assert(!peer.miniature && !peer.board.finished && peer.board.result==Presentation::RoundResult::None);
    Graphical::FieldEffects other;other.update(find(5),.16);
    Engine::apply_remote_board_state(c,scene.players[5],locked,false);
    boards=Graphical::capture_room_boards(c);other.update(find(5),.2);other.update(find(5),.26);
    assert(!other.particles().empty() && other.pulses().back().lines==4 && other.transform().y>.5f);
    assert(effect.time()!=other.time()); // Independent board clocks/pools.
    scene.spectator=true;boards=Graphical::capture_room_boards(c);
    assert(!boards.ownBoardVisible && boards.peers.size()==2);
    // An ordinary next placement cannot reuse the previous QUAD's count.
    p.gameplay.scoring.placedBlocks=2;event.clearedLines=0;
    Engine::apply_remote_board_state(c,remote,checkpoint(Network::ActivePieceState::locked(),event),false);
    assert(Presentation::capture_board_view(c,remote).placementLines==0);
    remote.presentation.remoteFieldVisibility=Runtime::FieldVisibility{true,false,{},{},{}};
    const auto hidden=Presentation::capture_board_view(c,remote);
    assert(!hidden.spatialEffects && !hidden.placement);
}
}
int run(int argc,char** argv) {
    control_rail_has_its_own_geometry();
    windup_projection_tracks_game_time();
    remote_finalists_effects_without_ghost();
    {
        auto s=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
        Runtime::GameContext c(*s,services);Network::NetworkServices network;c.attach_network(network);
        auto& p=s->player;p.gameplay.playfield.field.initialize(100,20);
        p.gameplay.playfield.width=100;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
        p.gameplay.playfield.field.set_cell(-51,3,Runtime::BoardCell::piece(0));
        const auto wide=Graphical::capture_graphical_board(c,p,Graphical::solo_geometry(100,20));
        assert(wide.firstRow==-100 && wide.at(-51,3).value.occupied());
        auto narrow=Graphical::solo_geometry(100,20);narrow.next=20;
        const auto thin=Graphical::capture_graphical_board(c,p,narrow);
        assert(thin.firstRow==-100);
        p.gameplay.playfield.field.initialize(4,0);p.gameplay.playfield.width=4;
        p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=0;
        assert(Graphical::capture_graphical_board(c,p,Graphical::solo_geometry(4,0)).firstRow==-11);
    }
    {
        auto s=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
        Runtime::GameContext c(*s,services);Network::NetworkServices network;c.attach_network(network);
        auto& scene=s->roomScene;scene.enabled=true;scene.ownSlot=scene.focusSlot=0;scene.selfAlive=true;
        scene.survivors={0,1};scene.finalistLayout={1,0};
        auto& own=s->player;own.gameplay.playfield.field.initialize(10,100);
        own.gameplay.playfield.width=10;own.gameplay.playfield.visibleHeight=own.gameplay.playfield.activeHeight=100;
        own.gameplay.playfield.field.set_cell(-32,3,Runtime::BoardCell::piece(0));
        auto& peer=scene.players[1];peer.gameplay.playfield.field.initialize(10,20);
        peer.gameplay.playfield.width=10;peer.gameplay.playfield.visibleHeight=peer.gameplay.playfield.activeHeight=20;
        for(int slot:{0,1}) {
            Network::RoomMember member;member.slot=slot;member.participating=member.alive=true;
            network.roomMembers.push_back(member);
        }
        network.matchPhase=Network::RoomMatchPhase::Playing;
        const auto boards=Graphical::capture_room_boards(c);
        assert(boards.ownBoardVisible && boards.ownBoard.firstRow==-67);
        assert(boards.ownBoard.at(-32,3).value.occupied());
    }
    auto state=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
    Runtime::GameContext c(*state,services);Network::NetworkServices network;c.attach_network(network);
    c.config.mode.mode=Runtime::GameMode::Multiplayer;
    auto& scene=state->roomScene;scene.enabled=true;scene.ownSlot=0;scene.selfAlive=false;
    state->player.gameplay.playfield.field.initialize(10,20);
    state->player.gameplay.playfield.width=10;
    state->player.gameplay.playfield.visibleHeight=state->player.gameplay.playfield.activeHeight=20;
    for(int slot=0;slot<4;++slot) {
        Network::RoomMember member;member.slot=slot;member.username="Player "+std::to_string(slot);member.participating=true;
        network.roomMembers.push_back(member);scene.usernames[slot]=member.username;
        if(slot) {scene.players[slot]=state->player;scene.players[slot].presentation.username=member.username;}
    }
    network.matchPhase=Network::RoomMatchPhase::Countdown;
    auto boards=Graphical::capture_room_boards(c);assert(boards.ownBoardVisible && boards.peers.size()==3);
    for(const auto& peer:boards.peers)assert(peer.board.width==10 && peer.board.bottom==20 && !peer.board.cells.empty());
    network.matchPhase=Network::RoomMatchPhase::Playing;
    for(auto& member:network.roomMembers)member.alive=member.slot!=0;
    scene.survivors={1,2,3};scene.focusSlot=1;state->player.timing.completedGameTime=Runtime::Duration::from_seconds(10);
    boards=Graphical::capture_room_boards(c);assert(!boards.ownBoardVisible && boards.peers.size()==3);
    for(const auto& peer:boards.peers)assert(peer.board.result==Presentation::RoundResult::None && !peer.board.finished);
    assert(Presentation::capture_board_view(c,state->player).result==Presentation::RoundResult::Defeat);
    scene.finished=true;scene.finalists={1,2};scene.finalistLayout={1,2};scene.survivors={2};
    network.matchPhase=Network::RoomMatchPhase::Finished;network.winnerSlot=2;network.roomMembers[1].alive=false;
    assert(Presentation::capture_board_view(c,scene.players[1]).result==Presentation::RoundResult::Defeat);
    assert(Presentation::capture_board_view(c,scene.players[2]).result==Presentation::RoundResult::Win);
    auto& remote=scene.players[1];remote.timing.remoteGameTime=Runtime::Duration{};
    remote.presentation.garbageBufferDisplay={3,3,1,1,2,2,0,0};
    auto view=Presentation::capture_board_view(c,remote);
    assert((view.garbage==std::vector<int>{2,2,2}));
    assert((view.garbagePhases==std::vector<Presentation::GarbagePhase>{Presentation::GarbagePhase::Early,Presentation::GarbagePhase::Incoming,Presentation::GarbagePhase::Ready}));
    state->player.garbage.buffer.emplace_back(4,c.now()-Runtime::Duration::from_seconds(10));
    view=Presentation::capture_board_view(c,state->player);
    assert(view.garbagePhases[0]==Presentation::GarbagePhase::Ready && view.garbageReadyIn[0]<0);
    Network::BoardStateSnapshot snapshot;snapshot.field.initialize(10,20);
    Engine::apply_remote_board_state(c,remote,snapshot,false); // Establish a fresh match baseline.
    snapshot.score.totalAttack=8;snapshot.score.placedBlocks=1;
    Engine::apply_remote_board_state(c,remote,snapshot,false);assert(remote.presentation.spike.attack==8);
    Engine::apply_remote_board_state(c,remote,snapshot,false);assert(remote.presentation.spike.attack==8);
    snapshot.score.totalAttack=11;snapshot.score.placedBlocks=2;
    Engine::apply_remote_board_state(c,remote,snapshot,false);assert(remote.presentation.spike.attack==11);
    remote.presentation.spike.lastAttackAt=c.now()-Runtime::Duration::from_seconds(2);
    snapshot.score.totalAttack=19;snapshot.score.placedBlocks=3;
    Engine::apply_remote_board_state(c,remote,snapshot,false);assert(remote.presentation.spike.attack==8);
    remote.presentation.remoteAttackBaselineKnown=false;remote.presentation.spike={};
    snapshot.score.totalAttack=500;snapshot.score.placedBlocks=100;
    Engine::apply_remote_board_state(c,remote,snapshot,false);assert(remote.presentation.spike.attack==0);
    network.connection=Network::ConnectionState::Connected;network.multiplayerRoom=true;
    network.localTransport=std::make_shared<Network::LocalDuplexTransport>();network.localEndpoint=0;
    network.matchPhase=Network::RoomMatchPhase::Playing;
    network.boardViews={3,{{1,false,8,12,5},{2,false,8,12,5}}};
    network.lastBoardViews=Network::encode_board_views(network.boardViews);
    remote.presentation.remoteAttackBaselineKnown=true;remote.gameplay.scoring.totalAttack=0;
    remote.presentation.spike.attack=32;
    Network::publish_board_views(c,{2,{{1,true,0,0,60},{2,true,0,0,60}}});
    assert(!remote.presentation.remoteAttackBaselineKnown && remote.presentation.spike.attack==0);
    Engine::apply_remote_board_state(c,remote,snapshot,false);assert(remote.presentation.spike.attack==0);
    snapshot.score.totalAttack+=8;++snapshot.score.placedBlocks;
    Engine::apply_remote_board_state(c,remote,snapshot,false);assert(remote.presentation.spike.attack==8);
    Network::publish_board_views(c,{2,{{1,true,0,0,60},{2,true,0,0,60}}});
    assert(remote.presentation.spike.attack==8); // Identical scope/heartbeat keeps a real spike.
    scene.finished=false;scene.spectator=true;scene.focusSlot=1;scene.survivors={1,2,3};
    auto& invisible=scene.players[2];
    invisible.presentation.remoteFieldVisibility=Runtime::FieldVisibility{true,true,{},Runtime::Duration::from_seconds(5),Runtime::Duration::from_seconds(3)};
    invisible.gameplay.playfield.field.set_cell(19,0,Runtime::BoardCell::piece(3));
    invisible.timing.remoteGameTime=Runtime::Duration::from_seconds(5.5);invisible.timing.remoteTimeRate=0;
    invisible.presentation.activePieceType=Piece::Type::from_index(5);invisible.presentation.activePose=Piece::spawn_pose(10,*invisible.presentation.activePieceType);
    network.peerMiniBoards[2]=Network::capture_mini_board(invisible,8,12,1);
    c.config.field.invisible=false;boards=Graphical::capture_room_boards(c);
    bool found=false;for(const auto& peer:boards.peers)if(peer.slot==2) {
        found=true;assert(peer.miniature && !peer.board.active && !peer.board.ghost);
        assert(peer.logicalWidth==0 && peer.board.at(19,0).opacity==.5f);
    }
    assert(found);
    c.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    for(const auto mod:{Runtime::ModifierMode::Enabled,Runtime::ModifierMode::Reverse}) {
        c.config.quickPlay.snowball=mod;
        for(int lines:{0,1,19,20,21,39,40,299,300,319,320,321,10000,19}) {
            state->player.gameplay.scoring.totalLines=lines;
            const auto growth=Presentation::capture_board_view(c,state->player).growth;
            if(lines>=320)assert(growth.nextSize==0 && growth.remaining==0);
            else {
                assert(growth.nextSize==5+lines/20 && growth.remaining==20-lines%20);
                assert(std::abs(growth.progress-(lines%20)/20.)<1e-9);
            }
        }
    }
    c.config.quickPlay.snowball=Runtime::ModifierMode::Disabled;
    assert(Presentation::capture_board_view(c,state->player).growth.nextSize==0);
    c.config.quickPlay.snowball=Runtime::ModifierMode::Enabled;c.config.garbage.mode=Runtime::GarbageMode::None;
    assert(Presentation::capture_board_view(c,state->player).growth.nextSize==0);
    return 0;
}
} // namespace TetrTests::graphical_scene_test
