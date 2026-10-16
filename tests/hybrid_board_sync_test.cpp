#include "tetr/network/hybrid_sync.h"
#include "tetr/network/event_stream.h"
#include "tetr/network/frame_packet.h"
#include "tetr/network/lobby_protocol.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/render/terminal_renderer.h"
#include "support/network_gameplay_frames.h"
#include <cassert>
#include <iostream>
#include <memory>

struct Fixture {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
};
Network::FramePacket checkpoint(Fixture& f,std::uint64_t sequence,std::uint64_t frame=0,std::uint64_t revision=1) {
    const auto state=Network::encode_board_state(f.context,f.session->player,Network::ActivePieceState::active({{2,3},Runtime::Rotation{}}));
    const auto wire=Network::encode_board_checkpoint({77,sequence,frame,revision,Network::board_state_fingerprint(state)},state);
    const auto packet=Network::decode_frame_packet(wire,10,20); assert(packet); return *packet;
}
Network::FramePacket motion(std::uint64_t sequence,std::uint64_t frame,std::uint64_t hash,
                           Piece::Pose result,int columns=1,std::uint64_t revision=1) {
    Network::PieceMotionFrame move{{77,sequence,frame,revision,hash},Compat::piece_type_from_legacy(1),
        Network::PieceMotionKind::Translate,0,columns,0,result};
    const auto packet=Network::decode_frame_packet(Network::encode_piece_motion(move),10,20); assert(packet); return *packet;
}
int main() {
    // Binary envelopes must be bounded and reject a damaged batch atomically.
    const std::vector<std::string> streamEvents{"!probe",std::string("\0;TES1",6),""};
    const auto streamWire=Network::encode_event_stream(streamEvents);assert(streamWire);
    const auto streamDecoded=Network::decode_event_stream(*streamWire);assert(streamDecoded && streamDecoded->size()==streamEvents.size());
    for(std::size_t i=0;i<streamEvents.size();++i)assert((*streamDecoded)[i]==streamEvents[i]);
    for(std::size_t n=0;n<streamWire->size();++n)assert(!Network::decode_event_stream(std::string_view(*streamWire).substr(0,n)));
    assert(!Network::decode_event_stream(*streamWire+"x"));
    assert(!Network::encode_event_stream({}));
    assert(!Network::encode_event_stream(std::vector<std::string>(65,"x")));
    assert(Network::encode_event_stream({std::string(65507,'\0')}));
    assert(!Network::encode_event_stream({std::string(65508,'x')}));
    assert(!Network::encode_event_stream({std::string(40000,'x'),std::string(40000,'y')}));
    assert(!Network::decode_event_stream(std::string("TES1\0\1\xff\xff\xff\xff",10)));
    using StreamClock=Network::EventStreamQueue::Clock;
    using namespace std::chrono_literals;
    const auto queuedAt=StreamClock::time_point{}+1s;
    Network::EventStreamQueue streamQueue;
    auto visualWire=[](std::uint64_t sequence,std::uint64_t revision=1,int slot=0) {
        Network::PieceMotionFrame frame{{77,sequence,sequence,revision,123},Piece::Type::from_index(0),
            Network::PieceMotionKind::Translate,0,1,0,{{2,4},Runtime::Rotation{}}};
        return Network::encode_player_packet({77,slot,Network::encode_piece_motion(frame)});
    };
    for(std::uint64_t seq=1;seq<=1000;++seq) assert(streamQueue.push(visualWire(seq),queuedAt+1ms,true));
    assert(streamQueue.size()==1 && streamQueue.oldest_age(queuedAt+2ms)==1ms);
    assert(streamQueue.push(visualWire(3),queuedAt+2ms,true)); // Late UDP cannot replace newer visual state.
    assert(streamQueue.push(visualWire(1,1,1),queuedAt+3ms,true)); // Another player's independent stream.
    assert(streamQueue.size()==2 && streamQueue.oldest_age(queuedAt+9ms)==8ms);
    assert(streamQueue.push("critical-attack",queuedAt+3ms,true));
    assert(streamQueue.push(visualWire(1001),queuedAt+4ms,true));
    assert(streamQueue.push("critical-checkpoint",queuedAt+4ms,true));
    assert(streamQueue.push(visualWire(1002,2),queuedAt+4ms,true));
    const auto ordered=streamQueue.take(true);
    assert(ordered==std::vector<std::string>({visualWire(1000),visualWire(1,1,1),"critical-attack",visualWire(1001),"critical-checkpoint",visualWire(1002,2)}));
    assert(streamQueue.empty() && streamQueue.bytes()==0);
    assert(!streamQueue.push(std::string(65508,'x'),queuedAt,true));
    Network::MiniBoardFrame mini;mini.sequence=1;mini.logicalWidth=10;mini.logicalHeight=20;
    mini.columns=mini.rows=1;mini.cells.resize(1);mini.clock=Network::RemoteClockSample{1,1};
    assert(streamQueue.push(Network::encode_mini_board(mini),queuedAt,true));
    mini.clock->elapsedSeconds=2;
    const auto updatedMini=Network::encode_mini_board(mini);
    assert(streamQueue.push(updatedMini,queuedAt+1ms,true));
    assert(streamQueue.size()==1 && streamQueue.pop()==updatedMini && streamQueue.bytes()==0);
    assert(streamQueue.push(updatedMini,queuedAt,true));
    assert(streamQueue.push("attack",queuedAt,true));
    assert(streamQueue.push("checkpoint",queuedAt,true));
    mini.sequence=2;const auto newestMini=Network::encode_mini_board(mini);
    assert(streamQueue.push(newestMini,queuedAt+1ms,true));
    assert(streamQueue.push(updatedMini,queuedAt+2ms,true));
    assert(streamQueue.take(true)==std::vector<std::string>({"attack","checkpoint",newestMini}));
    assert(streamQueue.push("x",queuedAt,false,1,1));
    assert(!streamQueue.push("y",queuedAt,false,1,1));
    assert(streamQueue.take(false)==std::vector<std::string>{"x"});
    using namespace Network;
    const auto time=[](double seconds) {return Runtime::TimePoint::from_seconds(seconds);};
    Fixture source,remote;
    Test::prepare_online_board(source.context,0); Test::prepare_online_board(remote.context,0);
    auto initial=checkpoint(source,1);
    const auto hash=initial.checkpoint->fingerprint;
    const auto state=encode_board_state(source.context,source.session->player,ActivePieceState::active({{2,3},Runtime::Rotation{}}));
    const auto packed=encode_board_checkpoint(*initial.checkpoint,state);
    const auto unpacked=decode_board_checkpoint(packed);
    assert(unpacked && unpacked->state==state && packed.size()<state.size());
    for (std::size_t n=0;n<packed.size();++n) assert(!decode_board_checkpoint(std::string_view(packed).substr(0,n)));
    assert(!decode_board_checkpoint("!snapshot-v1;77;1;0;1;1;~"));
    const auto moved=encode_board_state(source.context,source.session->player,ActivePieceState::active({{6,7},Runtime::Rotation::from_index(1)}));
    assert(board_state_fingerprint(state)==board_state_fingerprint(moved));
    const auto core=state.substr(0,state.rfind(";clock-v1;"));
    const auto timed=decode_board_state(core+";clock-v1;61.125;2",10,20);
    assert(timed && timed->clock && timed->clock->elapsedSeconds==61.125);
    assert(board_state_fingerprint(core)==board_state_fingerprint(core+";clock-v1;72.25;0"));
    assert(!decode_board_state(core+";clock-v1;nan;1",10,20));
    assert(!decode_board_state(core+";clock-v1;1;-1",10,20));
    Engine::apply_remote_board_state(remote.context,remote.session->player,*timed,false);
    auto& clock=remote.session->player.timing;
    assert(clock.remote_elapsed(*clock.remoteTimeUpdated+Runtime::Duration::from_seconds(2)).seconds()==65.125);
    PieceMotionFrame clockMotion{{77,1,0,1,hash},timed->currentPiece,PieceMotionKind::Translate,0,0,0,timed->activePose,RemoteClockSample{68.5,2}};
    const auto clockWire=encode_piece_motion(clockMotion);const auto decodedClock=decode_piece_motion(clockWire);
    assert(decodedClock && decodedClock->clock && decodedClock->clock->elapsedSeconds==68.5);
    assert(Engine::apply_remote_piece_motion(remote.context,remote.session->player,*decodedClock,false));
    assert(clock.remote_elapsed(*clock.remoteTimeUpdated).seconds()==68.5);
    clockMotion.clock=RemoteClockSample{7.5,0};
    assert(Engine::apply_remote_piece_motion(remote.context,remote.session->player,clockMotion,false));
    assert(clock.remote_elapsed(*clock.remoteTimeUpdated+Runtime::Duration::from_seconds(10)).seconds()==7.5);
    source.context.config.simulationPps=2;source.session->gameStartTime=time(0);source.session->simulationTime=time(3.25);
    assert(capture_player_clock(source.context,source.session->player).elapsedSeconds==3.25);
    assert(capture_player_clock(source.context,source.session->player).rate==0);
    source.context.config.simulationPps=0;
    auto damagedState=state;damagedState[1]=damagedState[1]=='-'?'0':'-';
    auto damaged=encode_board_checkpoint(*initial.checkpoint,damagedState);
    assert(!decode_frame_packet(damaged,10,20));
    assert(!decode_piece_motion("!motion-v1;77;0;0;1;1;1;0;0;1;0;2;3;0"));
    assert(!decode_piece_motion("!motion-v1;77;1;0;1;1;1;0;0;1;0;999999;3;0"));
    assert(!decode_piece_motion("!motion-v1;77;1;0;1;1;1;1;0;1;0;2;3;0"));
    assert(!decode_board_checkpoint("!snapshot-v1;77;1;4294967296;1;1;!bad"));
    assert(decode_snapshot_request(encode_snapshot_request(77))==77);
    assert(is_gameplay_extension(encode_snapshot_request(77)) && is_gameplay_extension(encode_board_checkpoint(*initial.checkpoint,state)));

    RemoteBoardTimeline timeline; timeline.reset(77);
    timeline.insert(initial,time(10));
    assert(!timeline.next(time(10.008)));
    auto first=timeline.next(time(10.009)); assert(first && first->checkpoint);
    timeline.insert(motion(3,6,hash,{{2,5},Runtime::Rotation{}}),time(10.05));
    timeline.insert(motion(2,4,hash,{{2,4},Runtime::Rotation{}}),time(10.055)); // Arrived out of order.
    auto second=timeline.next(time(10.055)); assert(second && second->motion->stamp.sequence==2);
    assert(!timeline.next(time(10.056)));
    auto third=timeline.next(time(10.060)); assert(third && third->motion->stamp.sequence==3);
    timeline.insert(initial,time(10.091));
    timeline.insert(*third,time(10.091));
    assert(timeline.pending_count()==0); // Duplicate and stale snapshots cannot rewind the view.

    // Stable playback adds one motion frame, rather than a fixed 35ms. A late
    // arrival grows a bounded jitter cushion; stable traffic then reduces it.
    RemoteBoardTimeline jitter; jitter.reset(77);
    jitter.insert(initial,time(50));assert(jitter.next(time(50.009)));
    jitter.insert(motion(2,6,hash,{{2,4},Runtime::Rotation{}}),time(50.075));
    assert(jitter.buffer_seconds()>0.033 && jitter.buffer_seconds()<0.034);
    assert(!jitter.next(time(50.080)) && jitter.next(time(50.084)));
    for(std::uint64_t seq=3;seq<=5;++seq) {
        const auto frame=6+(seq-2)*120;
        const auto at=time(50.0+frame/kMotionFramesPerSecond);
        jitter.insert(motion(seq,frame,hash,{{2,4},Runtime::Rotation{}}),at);
        assert(jitter.next(at+Runtime::Duration::from_seconds(jitter.buffer_seconds()+0.001)));
    }
    assert(jitter.buffer_seconds()<=RemoteBoardTimeline::kMinBufferSeconds+1e-9);

    // A stall never rebases old packets into the future. Skip only replaceable
    // due motion in the same revision; the final absolute result is sufficient.
    RemoteBoardTimeline catchup;catchup.reset(77);
    catchup.insert(initial,time(60));assert(catchup.next(time(60.009)));
    for(std::uint64_t seq=2;seq<=20;++seq)
        catchup.insert(motion(seq,(seq-1)*2,hash,{{2,5},Runtime::Rotation{}}),time(61));
    assert(catchup.buffer_seconds()<=RemoteBoardTimeline::kMaxBufferSeconds);
    const auto caughtUp=catchup.next(time(61));
    assert(caughtUp && caughtUp->motion->stamp.sequence==20 && catchup.coalesced_motions()==18);
    assert(catchup.pending_count()==0 && !catchup.needs_snapshot());

    // A placement/checkpoint is an ordering barrier even during catch-up.
    RemoteBoardTimeline barriers;barriers.reset(77);
    barriers.insert(initial,time(70));assert(barriers.next(time(70.009)));
    barriers.insert(motion(2,2,hash,{{2,4},Runtime::Rotation{}}),time(71));
    barriers.insert(checkpoint(source,3,4,2),time(71));
    barriers.insert(motion(4,5,hash,{{2,5},Runtime::Rotation{}},1,2),time(71));
    barriers.insert(motion(5,6,hash,{{2,6},Runtime::Rotation{}},1,2),time(71));
    assert(barriers.next(time(71))->motion->stamp.sequence==2);
    assert(barriers.next(time(71))->checkpoint->sequence==3);
    assert(barriers.next(time(71))->motion->stamp.sequence==5);
    assert(barriers.coalesced_motions()==1 && !barriers.needs_snapshot());

    // Congestion must not evict the only usable anchor, causing an extra
    // snapshot request/response round trip despite having the full state.
    RemoteBoardTimeline anchorBurst;anchorBurst.reset(77);
    anchorBurst.insert(initial,time(80));
    for(std::uint64_t seq=2;seq<=100;++seq)
        anchorBurst.insert(motion(seq,seq,hash,{{2,5},Runtime::Rotation{}}),time(81));
    assert(anchorBurst.pending_count()==RemoteBoardTimeline::kMaxPending && !anchorBurst.needs_snapshot());
    assert(anchorBurst.next(time(81))->checkpoint);
    assert(anchorBurst.next(time(81))->motion->stamp.sequence==100);
    assert(anchorBurst.pending_count()==0 && !anchorBurst.needs_snapshot());

    Render::begin_output_suppression();
    // Opponent motion carries the resolved pose, so the viewer's handling,
    // gravity and kick system cannot change the reconstructed trajectory.
    remote.context.config.handling.das=Runtime::Duration::from_seconds(0.7);
    remote.context.config.handling.arr=Runtime::Duration::from_seconds(0.2);
    remote.context.config.handling.gravityInterval=Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(0.001));
    remote.context.config.rotationSystem=Runtime::RotationSystem::Srs;
    Engine::apply_remote_board_state(remote.context,remote.session->player,*first->opponentState);
    // Lose sequence 2. Sequence 3's independent result pose still restores the
    // correct active piece without affecting the authoritative locked field.
    assert(Engine::apply_remote_piece_motion(remote.context,remote.session->player,*third->motion));
    assert(remote.session->player.presentation.activePose==third->motion->result);
    auto differentRotation=*third->motion;
    differentRotation.kind=PieceMotionKind::Rotate;
    differentRotation.rotations=1;
    differentRotation.rows=differentRotation.columns=0;
    differentRotation.result.rotation=Runtime::Rotation::from_index(1);
    assert(Engine::apply_remote_piece_motion(remote.context,remote.session->player,differentRotation));
    assert(remote.session->player.presentation.activePose==differentRotation.result);
    assert(Compat::to_legacy(remote.session->player.gameplay.playfield.field.cell(0,0))==3);
    for (int row=0;row<20;++row) for (int col=0;col<10;++col)
        remote.session->player.gameplay.playfield.field.set_cell(row,col,Runtime::BoardCell::garbage());
    assert(!Engine::apply_remote_piece_motion(remote.context,remote.session->player,*third->motion));
    Engine::apply_remote_board_state(remote.context,remote.session->player,*first->opponentState);
    assert(Engine::apply_remote_piece_motion(remote.context,remote.session->player,*third->motion));

    RemoteBoardTimeline missing; missing.reset(77);
    missing.insert(motion(4,0,hash,{{2,6},Runtime::Rotation{}}),time(20));
    assert(!missing.next(time(20.040)) && missing.needs_snapshot());
    missing.insert(checkpoint(source,5,1),time(20.041));
    auto repaired=missing.next(time(20.050)); assert(repaired && repaired->checkpoint && !missing.needs_snapshot());
    auto wrong=motion(6,2,hash+1,{{2,7},Runtime::Rotation{}});
    missing.insert(wrong,time(20.051));
    assert(!missing.next(time(20.070)) && missing.needs_snapshot());
    auto otherRound=checkpoint(source,100,1); otherRound.checkpoint->epoch=78;
    missing.insert(otherRound,time(20.071)); assert(missing.pending_count()==0);
    missing.insert(checkpoint(source,7,3,2),time(20.072));
    assert(missing.next(time(20.080)) && !missing.needs_snapshot());
    missing.invalidate_anchor(); assert(missing.needs_snapshot());

    const auto placement=encode_board_state(source.context,source.session->player,ActivePieceState::locked(),Test::online_placement());
    const auto locked=decode_board_state(placement,10,20); assert(locked);
    remote.session->player.gameplay.playfield.field.set_cell(0,0,Runtime::BoardCell::empty());
    Engine::apply_remote_board_state(remote.context,remote.session->player,*locked);
    assert(Compat::to_legacy(remote.session->player.gameplay.playfield.field.cell(0,0))==3);
    assert(!remote.session->player.presentation.activePose);
    Render::end_output_suppression();

    RemoteBoardTimeline bounded; bounded.reset(77);
    for (std::uint64_t seq=1;seq<1000;++seq) bounded.insert(motion(seq,1,hash,{{2,4},Runtime::Rotation{}}),time(30));
    assert(bounded.pending_count()==RemoteBoardTimeline::kMaxPending && bounded.needs_snapshot());
    // A movement frame carries no locked cells, NEXT or HOLD. On the standard
    // field it is substantially smaller than even one legacy full snapshot.
    const auto lightweight=encode_piece_motion(*third->motion);
    assert(lightweight.size()*4<state.size());
    assert(decode_piece_motion(lightweight));
    for (std::size_t n=0;n<lightweight.size();++n) assert(!decode_piece_motion(std::string_view(lightweight).substr(0,n)));
    auto rotating=*third->motion; rotating.kind=PieceMotionKind::Rotate; rotating.rotations=1; rotating.result.rotation=Runtime::Rotation::from_index(1);
    const auto rotation=decode_piece_motion(encode_piece_motion(rotating));
    assert(rotation && rotation->kind==PieceMotionKind::Rotate && rotation->rotations==1 && rotation->result==rotating.result);
    std::cerr << "hybrid_board_sync_test: OK; snapshot=" << state.size() << " bytes; compressed checkpoint=" << packed.size() << " bytes; motion=" << lightweight.size() << " bytes\n";
}
