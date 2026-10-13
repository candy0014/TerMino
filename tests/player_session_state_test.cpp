#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/session_state.h"

#include <cassert>
#include <vector>

int main(){
    Runtime::SessionState session;
    assert(&session.player!=&session.opponent);

    session.player.pieces.nextPreview={Piece::Type{1},Piece::Type{2},Piece::Type{3}};
    assert(session.player.pieces.nextPreview==std::vector<Piece::Type>({Piece::Type{1},Piece::Type{2},Piece::Type{3}}));
    session.player.pieces.bag.push_back({Piece::Type{5},true});
    assert((session.player.pieces.bag.front()==Runtime::QueuedPiece{Piece::Type{5},true}));
    assert(session.opponent.pieces.nextPreview.empty());

    Runtime::GarbagePacket packet{7, Runtime::TimePoint::from_seconds(0.0)};
    session.player.garbage.buffer.push_back(packet);
    assert(session.player.garbage.buffer.size()==1);
    assert(session.player.garbage.buffer.front().line_count()==7);
    assert(session.opponent.garbage.buffer.empty());

    session.player.timing.frameUpdate=Runtime::TimePoint::from_seconds(12.5);
    session.player.presentation.margin=41;
    assert(session.player.timing.frameUpdate.seconds()==12.5);
    assert(session.player.presentation.margin==41);
    assert(session.opponent.timing.frameUpdate.seconds()==0.0);
    assert(session.opponent.presentation.margin==0);

    session.player.multiplayer.sendId=Runtime::MultiplayerSequenceId::from_wire(9);
    session.player.multiplayer.pendingSends.push_back({Runtime::MultiplayerSequenceId::from_wire(9),4});
    assert(session.player.multiplayer.sendId.value()==9);
    assert((session.player.multiplayer.pendingSends.front()==Runtime::PendingAttack{Runtime::MultiplayerSequenceId::from_wire(9),4}));
    assert(session.opponent.multiplayer.pendingSends.empty());

    Runtime::PlayerSessionState first;
    Runtime::PlayerSessionState second;
    first.pieces.nextPreview={Piece::Type{6}};
    first.garbage.buffer.push_back(packet);
    first.timing.autoplay=Runtime::TimePoint::from_seconds(3.0);
    first.gameplay.scoring.score=123;
    assert(second.pieces.nextPreview.empty());
    assert(second.garbage.buffer.empty());
    assert(second.timing.autoplay.seconds()==0.0);
    assert(second.gameplay.scoring.score==0);
    return 0;
}
