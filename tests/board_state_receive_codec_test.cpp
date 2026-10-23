#include "tetr/compat/legacy_encoding.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_services.h"
#include "tetr/runtime/session_state.h"

#include <cassert>
#include <random>
#include <string>

namespace TetrTests::board_state_receive_codec_test {
namespace {

void assert_field_equal(const Runtime::Playfield& expected,const Runtime::Playfield& actual,int width,int visibleHeight){
    for(int row=visibleHeight-1;row>=-Runtime::kDisplayHiddenRows;--row){
        for(int col=0;col<width;++col) assert(expected.cell(row,col)==actual.cell(row,col));
    }
}

void random_roundtrip(){
    std::mt19937 rng(0x25B04D5u);
    for(int tc=0;tc<10000;++tc){
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        auto& player=session.player;
        auto& playfield=player.gameplay.playfield;
        const int width=4+static_cast<int>(rng()%7);
        const int height=static_cast<int>(rng()%21);
        playfield.width=width;
        playfield.visibleHeight=height;
        playfield.activeHeight=height;
        playfield.field.initialize(width,height);
        playfield.field.clear();
        for(int row=height-1;row>=-Runtime::kDisplayHiddenRows;--row){
            for(int col=0;col<width;++col){
                const unsigned r=rng()%24;
                int cell=-1;
                if(r<10) cell=static_cast<int>(rng()%10);
                else if(r==10) cell=100+static_cast<int>(rng()%26);
                playfield.field.set_cell(row,col,Compat::board_cell_from_legacy(cell));
            }
        }

        player.pieces.bag.clear();
        const Piece::Type currentPiece=Compat::piece_type_from_legacy(static_cast<int>(rng()%20));
        player.pieces.bag.push_back({currentPiece,false});
        player.pieces.nextPreview.clear();
        const int previewCount=static_cast<int>(rng()%6);
        for(int i=0;i<previewCount;++i) player.pieces.nextPreview.emplace_back(Compat::piece_type_from_legacy(static_cast<int>(rng()%20)));
        player.gameplay.hold.piece=(rng()%3==0)?Piece::Type::none():Compat::piece_type_from_legacy(static_cast<int>(rng()%20));
        player.gameplay.scoring.wins=static_cast<int>(rng()%50);
        player.gameplay.scoring.placedBlocks=static_cast<int>(rng()%10000);
        player.gameplay.scoring.totalAttack=static_cast<int>(rng()%30000);
        context.config.garbage.incomingDelay=Runtime::Duration::from_seconds(1.0);

        const Runtime::TimePoint now=context.now();
        const int packetCount=static_cast<int>(rng()%5);
        for(int i=0;i<packetCount;++i){
            Rules::GarbagePacket packet{
                1+static_cast<int>(rng()%8),
                now-Runtime::Duration::from_seconds((rng()%2)?0.1:10.0),
                static_cast<bool>(rng()%2)};
            player.garbage.buffer.push_back(packet);
        }

        const Piece::Pose pose{{static_cast<int>(rng()%10)-3,static_cast<int>(rng()%width)},Compat::rotation_from_legacy(static_cast<int>(rng()%4))};
        const bool placementFrame=(rng()%2)!=0;
        const Piece::Type placedPiece=placementFrame?Compat::piece_type_from_legacy(static_cast<int>(rng()%7)):Piece::Type::none();
        const int spin=static_cast<int>(rng()%2);
        const int mini=static_cast<int>(rng()%2);
        const int lines=static_cast<int>(rng()%7);
        const int b2b=static_cast<int>(rng()%30);
        const int combo=static_cast<int>(rng()%30);
        const int pc=static_cast<int>(rng()%2);

        const std::optional<Network::RemotePlacementSnapshot> placement = placementFrame
            ? std::optional<Network::RemotePlacementSnapshot>{Network::RemotePlacementSnapshot{
                placedPiece, spin != 0, mini != 0, lines, b2b, combo, pc != 0}}
            : std::nullopt;
        const std::string wire=Network::encode_board_state(
            context,player,Network::ActivePieceState::active(pose),placement);
        const auto decoded=Network::decode_board_state(wire,width,height);
        assert(decoded);
        assert_field_equal(playfield.field,decoded->field,width,height);
        assert(decoded->currentPiece==currentPiece);
        assert(decoded->nextPreview==player.pieces.nextPreview);
        assert(decoded->hold==player.gameplay.hold.piece);
        assert(decoded->score.wins==player.gameplay.scoring.wins);
        assert(decoded->score.placedBlocks==player.gameplay.scoring.placedBlocks);
        assert(decoded->score.totalAttack==player.gameplay.scoring.totalAttack);
        assert(decoded->garbage.size()==static_cast<std::size_t>(height+Runtime::kDisplayHiddenRows));
        if(placementFrame){
            assert(decoded->placement);
            assert(decoded->placement->piece==placedPiece);
            assert(decoded->placement->spin==(spin!=0));
            assert(decoded->placement->mini==(mini!=0));
            assert(decoded->placement->clearedLines==lines);
            assert(decoded->placement->b2b==b2b);
            assert(decoded->placement->combo==combo);
            assert(decoded->placement->perfectClear==(pc!=0));
        }else{
            assert(!decoded->placement);
            assert(decoded->activePose==pose);
        }
    }
}

void malformed_inputs(){
    assert(!Network::decode_board_state("",10,20));
    assert(!Network::decode_board_state("not-board-state",10,20));
    assert(!Network::decode_board_state("!;0;0,;-;0;0;0;0;0;0",10,20));

    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    auto& player=session.player;
    auto& field=player.gameplay.playfield;
    field.width=4; field.visibleHeight=4; field.activeHeight=4;
    field.field.initialize(4,4); field.field.clear();
    player.pieces.bag.push_back({Compat::piece_type_from_legacy(0),false});
    player.pieces.nextPreview.clear();
    const std::string valid=Network::encode_board_state(context,player);
    assert(Network::decode_board_state(valid,4,4));

    std::string bad=valid;
    bad[1]='?';
    assert(!Network::decode_board_state(bad,4,4));

    bad=valid;
    const std::size_t firstSemicolon=bad.find(';');
    bad[firstSemicolon+1]='9';
    assert(!Network::decode_board_state(bad,4,4));

    bad=valid;
    const std::size_t previewStart=bad.find(';',firstSemicolon+1)+1;
    const std::size_t previewEnd=bad.find(';',previewStart);
    bad.erase(previewEnd-1,1); // remove the required trailing comma
    assert(!Network::decode_board_state(bad,4,4));
    bad=valid;
    const std::size_t previewStart2=bad.find(';',firstSemicolon+1)+1;
    const std::size_t previewEnd2=bad.find(';',previewStart2);
    bad.replace(previewStart2,previewEnd2-previewStart2,"999999,");
    assert(!Network::decode_board_state(bad,4,4));
    assert(!Network::decode_board_state(valid,Runtime::Playfield::kStorageColumns+1,4));

    bad=valid;
    // Replace the active rotation field with a negative value on a non-placement frame.
    std::size_t pos=0;
    for(int i=0;i<6;++i) pos=bad.find(';',pos)+1;
    const std::size_t rotationEnd=bad.find(';',pos);
    bad.replace(pos,rotationEnd-pos,"-2");
    assert(!Network::decode_board_state(bad,4,4));

    for (int fieldIndex:{4,5}) for (const std::string value:{"2147483647","-2147483648"}) {
        bad=valid;
        pos=0;
        for(int i=0;i<fieldIndex;++i) pos=bad.find(';',pos)+1;
        const auto end=bad.find(';',pos);
        bad.replace(pos,end-pos,value);
        assert(!Network::decode_board_state(bad,4,4));
    }
}

} // namespace

int run(int argc,char** argv){
    random_roundtrip();
    malformed_inputs();
    return 0;
}
} // namespace TetrTests::board_state_receive_codec_test
