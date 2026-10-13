#include "tetr/network/hybrid_sync.h"
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
    auto damaged=encode_board_checkpoint(*initial.checkpoint,state);
    damaged.back()=damaged.back()=='8' ? '9' : '8';
    assert(!decode_frame_packet(damaged,10,20));
    assert(!decode_piece_motion("!motion-v1;77;0;0;1;1;1;0;0;1;0;2;3;0"));
    assert(!decode_piece_motion("!motion-v1;77;1;0;1;1;1;0;0;1;0;999999;3;0"));
    assert(!decode_piece_motion("!motion-v1;77;1;0;1;1;1;1;0;1;0;2;3;0"));
    assert(!decode_board_checkpoint("!snapshot-v1;77;1;4294967296;1;1;!bad"));
    assert(decode_snapshot_request(encode_snapshot_request(77))==77);
    assert(is_gameplay_extension(encode_snapshot_request(77)) && is_gameplay_extension(encode_board_checkpoint(*initial.checkpoint,state)));

    RemoteBoardTimeline timeline; timeline.reset(77);
    timeline.insert(initial,time(10));
    assert(!timeline.next(time(10.034)));
    auto first=timeline.next(time(10.036)); assert(first && first->checkpoint);
    timeline.insert(motion(3,6,hash,{{2,5},Runtime::Rotation{}}),time(10.05));
    timeline.insert(motion(2,4,hash,{{2,4},Runtime::Rotation{}}),time(10.055)); // Arrived out of order.
    assert(!timeline.next(time(10.060)));
    auto second=timeline.next(time(10.070)); assert(second && second->motion->stamp.sequence==2);
    assert(!timeline.next(time(10.080)));
    auto third=timeline.next(time(10.090)); assert(third && third->motion->stamp.sequence==3);
    timeline.insert(initial,time(10.091));
    timeline.insert(*third,time(10.091));
    assert(timeline.pending_count()==0); // Duplicate and stale snapshots cannot rewind the view.

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
