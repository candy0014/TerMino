#include "tetr/compat/legacy_encoding.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/runtime_services.h"
#include "tetr/runtime/session_state.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/constants.h"

#include <cassert>
#include <random>
#include <string>
#include <vector>

static std::string legacy_oracle(Runtime::GameContext& context,Runtime::PlayerSessionState& player,int row,int col,int rotation,int lastPiece,int spin,int mini,int cleared,int b2b,int combo,int pc){
    auto& game=player.gameplay;
    auto& field=game.playfield;
    if(rotation==-1) row=-2,col=(field.width-1)/2,rotation=0;
    std::string state="!";
    for(int i=field.visibleHeight-1;i>=-Runtime::kDisplayHiddenRows;i--){
        for(int j=0;j<field.width;j++){
            const int cell=Compat::to_legacy(field.field.cell(i,j));
            if(cell==-1) state+='-';
            else if(0<=cell&&cell<=9) state+=char(cell+'0');
            else state+=char(cell-100+'A');
        }
    }
    state+=';';
    std::vector<int> buffer(static_cast<std::size_t>(field.visibleHeight + Runtime::kDisplayHiddenRows + 1), 0);
    int total=0;const double now=context.now().seconds();
    for(auto packet:player.garbage.buffer){
        for(int i=total+1;i<=total+packet.line_count()&&i<=field.visibleHeight+Runtime::kDisplayHiddenRows;i++){
            const double age=now-packet.arrival_time().seconds();
            const double delay=context.config.garbage.incomingDelay.seconds();
            if (delay>0.3 && age<delay/2) buffer[i]=3;
            else if (age<delay) buffer[i]=1; else buffer[i]=2;
        }
        total+=packet.line_count();
        if(total>field.visibleHeight+Runtime::kDisplayHiddenRows) break;
    }
    for(int i=1;i<=field.visibleHeight+Runtime::kDisplayHiddenRows;i++) state+=char(buffer[i]+'0');
    state+=';';
    state+=std::to_string(Compat::to_legacy(player.pieces.bag.front().type))+",";
    for(auto piece:player.pieces.nextPreview) state+=std::to_string(Compat::to_legacy(piece))+",";
    state+=';';
    if(!game.hold.piece.valid()) state+='-';else state+=std::to_string(Compat::to_legacy(game.hold.piece))+",";
    if(rotation!=-2) player.presentation.activePose=Piece::Pose{{row,col},Compat::rotation_from_legacy(rotation)};
    state+=";"+std::to_string(row)+";"+std::to_string(col)+";"+std::to_string(rotation);
    state+=";"+std::to_string(game.scoring.wins)+";"+std::to_string(game.scoring.placedBlocks)+";"+std::to_string(game.scoring.totalAttack);
    if(lastPiece!=-1){
        state+=";"+std::to_string(lastPiece)+";"+std::to_string(spin)+";"+std::to_string(mini);
        state+=";"+std::to_string(cleared)+";"+std::to_string(b2b)+";"+std::to_string(combo)+";"+std::to_string(pc);
    }
    return state;
}

int main(){
    std::mt19937 rng(20260901);
    for(int tc=0;tc<10000;tc++){
        Runtime::SessionState aSession,bSession;
        Runtime::RuntimeServices aServices,bServices;
        Runtime::GameContext a(aSession,aServices),b(bSession,bServices);
        auto& x=aSession.player; auto& y=bSession.player;
        const int width=4+rng()%7,height=10+rng()%11;
        for(auto* player:{&x,&y}){
            auto& field=player->gameplay.playfield;
            field.width=width;field.visibleHeight=height;field.activeHeight=height;
            field.field.initialize(width,height);field.field.clear();
            player->pieces.nextPreview.clear();
            player->pieces.bag.clear();
        }
        for(int row=height-1;row>=-Runtime::kDisplayHiddenRows;row--) for(int col=0;col<width;col++){
            int value=-1;const int r=rng()%20;
            if(r<8) value=rng()%10; else if(r==8) value=101+rng()%10;
            x.gameplay.playfield.field.set_cell(row,col,Compat::board_cell_from_legacy(value)); y.gameplay.playfield.field.set_cell(row,col,Compat::board_cell_from_legacy(value));
        }
        const Piece::Type current=Compat::piece_type_from_legacy(static_cast<int>(rng()%20));
        x.pieces.bag.push_back({current,false}); y.pieces.bag.push_back({current,false});
        for(int i=0,n=rng()%6;i<n;i++){const int v=rng()%20;x.pieces.nextPreview.push_back(Piece::Type{v});y.pieces.nextPreview.push_back(Piece::Type{v});}
        x.gameplay.hold.piece=y.gameplay.hold.piece=Compat::piece_type_from_legacy(rng()%3?int(rng()%20):-1);
        x.gameplay.scoring.wins=y.gameplay.scoring.wins=rng()%20;
        x.gameplay.scoring.placedBlocks=y.gameplay.scoring.placedBlocks=rng()%1000;
        x.gameplay.scoring.totalAttack=y.gameplay.scoring.totalAttack=rng()%5000;
        const double now=a.now().seconds();
        a.config.garbage.incomingDelay=b.config.garbage.incomingDelay=Runtime::Duration::from_seconds(1.0);
        for(int i=0,n=rng()%5;i<n;i++){
            Runtime::GarbagePacket p{
                1+static_cast<int>(rng()%8),
                Runtime::TimePoint::from_seconds(now-(rng()%2?0.1:10.0)),
                static_cast<bool>(rng()%2)};
            x.garbage.buffer.push_back(p); y.garbage.buffer.push_back(p);
        }
        const int row=int(rng()%10)-3,col=int(rng()%width),rotation=rng()%4;
        const int lastPiece=(rng()%2)?-1:int(rng()%7);
        const int spin=rng()%2,mini=rng()%2,cleared=rng()%7,b2b=rng()%20,combo=rng()%20,pc=rng()%2;
        const std::string expected=legacy_oracle(a,x,row,col,rotation,lastPiece,spin,mini,cleared,b2b,combo,pc);
        const std::optional<Network::RemotePlacementSnapshot> placement = lastPiece >= 0
            ? std::optional<Network::RemotePlacementSnapshot>{Network::RemotePlacementSnapshot{
                Compat::piece_type_from_legacy(lastPiece), spin != 0, mini != 0, cleared, b2b, combo, pc != 0}}
            : std::nullopt;
        const std::string actual=Network::encode_board_state(
            b,y,Network::ActivePieceState::active({{row,col},Runtime::Rotation{rotation}}),placement);
        const auto clock=actual.rfind(";clock-v1;");assert(clock!=std::string::npos);
        assert(actual.substr(0,clock)==expected);
        const auto withClock=Network::decode_board_state(actual,y.gameplay.playfield.width,y.gameplay.playfield.visibleHeight);
        assert(withClock && withClock->clock);
        // The Stage 24 encoder is pure: the historical oracle still mutates x,
        // while the real codec must leave y presentation state untouched.
        assert(!y.presentation.activePose);
    }
    return 0;
}
