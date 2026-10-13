#include "tetr/network/board_state_codec.h"
#include "tetr/compat/board_encoding.h"
#include "tetr/compat/piece_encoding.h"
#include "tetr/runtime/constants.h"

#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/util/text_codec.h"

#include <array>

namespace Network {
namespace {

bool valid_piece_id(int value) {
    return value >= 0 && value < static_cast<int>(Piece::catalog().size());
}

std::vector<std::string_view> split_sections(std::string_view state){
    if(state.empty()||state.front()!='!') return {};
    const std::string_view payload = state.substr(1);
    if (const auto compact = Util::Text::split_exact<10>(payload, ';'))
        return {compact->begin(), compact->end()};
    if (const auto placement = Util::Text::split_exact<17>(payload, ';'))
        return {placement->begin(), placement->end()};
    if (const auto compact = Util::Text::split_exact<12>(payload, ';'))
        return {compact->begin(), compact->end()};
    if (const auto placement = Util::Text::split_exact<19>(payload, ';'))
        return {placement->begin(), placement->end()};
    if (const auto compact = Util::Text::split_exact<15>(payload, ';'))
        return {compact->begin(), compact->end()};
    if (const auto placement = Util::Text::split_exact<22>(payload, ';'))
        return {placement->begin(), placement->end()};
    if (const auto placement = Util::Text::split_exact<24>(payload, ';'))
        return {placement->begin(), placement->end()};
    return {};
}

bool decode_piece_list(std::string_view text,std::vector<Piece::Type>& pieces){
    if(text.empty()) return true;
    const auto tokens=Util::Text::split(text,',');
    if(tokens.empty()||!tokens.back().empty()||tokens.size()>102) return false;
    pieces.reserve(pieces.size()+tokens.size()-1);
    for(std::size_t index=0;index+1<tokens.size();++index){
        const auto value=Util::Text::parse_integer<int>(tokens[index]);
        if(!value||!valid_piece_id(*value)) return false;
        pieces.emplace_back(Compat::piece_type_from_legacy(*value));
    }
    return true;
}

bool decode_hold(std::string_view text,Piece::Type& hold){
    if(text=="-"){
        hold=Piece::Type::none();
        return true;
    }
    std::vector<Piece::Type> pieces;
    if(!decode_piece_list(text,pieces)||pieces.size()!=1) return false;
    hold=pieces.front();
    return true;
}

bool decode_board_cells(std::string_view text,int width,int visibleHeight,Runtime::Playfield& field){
    if(width<=0||width>Runtime::Playfield::kStorageColumns ||
       visibleHeight<=0||visibleHeight>Runtime::Playfield::kStorageRows) return false;
    const int rows=visibleHeight+Runtime::kDisplayHiddenRows;
    if(rows<=0) return false;
    const std::size_t expected=static_cast<std::size_t>(width)*static_cast<std::size_t>(rows);
    if(text.size()!=expected) return false;
    field.initialize(width,visibleHeight);
    std::size_t cells=0;
    for(char ch:text){
        int cell=-1;
        if(ch=='-') cell=-1;
        else if(ch>='0'&&ch<='9') cell=ch-'0';
        else if(ch>='A'&&ch<='Z') cell=ch-'A'+100;
        else return false;
        const int row=visibleHeight-static_cast<int>(cells/static_cast<std::size_t>(width))-1;
        const int col=static_cast<int>(cells%static_cast<std::size_t>(width));
        field.set_cell(row,col,Compat::board_cell_from_legacy(cell));
        ++cells;
    }
    return true;
}

bool decode_garbage(std::string_view text,int visibleHeight,std::vector<RemoteGarbageCell>& garbage){
    const std::size_t expected=static_cast<std::size_t>(visibleHeight+Runtime::kDisplayHiddenRows);
    if(text.size()!=expected) return false;
    garbage.reserve(text.size());
    for(char ch:text){
        switch(ch){
            case '0': garbage.push_back(RemoteGarbageCell::Empty); break;
            case '1': garbage.push_back(RemoteGarbageCell::Incoming); break;
            case '2': garbage.push_back(RemoteGarbageCell::Ready); break;
            case '3': garbage.push_back(RemoteGarbageCell::EarlyBuffer); break;
            default: return false;
        }
    }
    return true;
}

} // namespace

std::uint64_t board_state_fingerprint(std::string_view state) {
    const auto fields=split_sections(state);
    if (fields.empty()) return 0;
    auto core=fields.size();
    if (core>=15 && fields[core-5]=="spawn-v1") core-=5;
    if (core>=12 && fields[core-2]=="hold-v1") core-=2;
    const bool placement=core==17;
    std::uint64_t hash=14695981039346656037ULL;
    for (std::size_t i=0;i<fields.size();++i) {
        if ((i>=4 && i<=6) || (placement && i>=10 && i<17)) continue;
        for (unsigned char byte:fields[i]) {hash^=byte; hash*=1099511628211ULL;}
        hash^=';'; hash*=1099511628211ULL;
    }
    return hash;
}

std::string encode_board_state(
    const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,
    ActivePieceState activePiece,
    std::optional<RemotePlacementSnapshot> placement){
    const auto& gameplay=player.gameplay;
    const auto& field=gameplay.playfield;
    const auto& scoring=gameplay.scoring;
    const auto& hold=gameplay.hold;

    int row=-2;
    int col=(field.width-1)/2;
    int rotation=0;
    if(activePiece.kind==ActivePieceStateKind::Active){
        row=activePiece.pose.position.row;
        col=activePiece.pose.position.col;
        rotation=Compat::to_legacy(activePiece.pose.rotation);
    }else if(activePiece.kind==ActivePieceStateKind::Locked){
        row=-1;
        col=-1;
        rotation=-2;
    }

    std::string state="!";
    for(int i=field.visibleHeight-1;i>=-Runtime::kDisplayHiddenRows;i--){
        for(int j=0;j<field.width;j++){
            const int legacyCell=Compat::to_legacy(field.field.cell(i,j));
            if(legacyCell<0) state+='-';
            else if(legacyCell<=9) state+=char(legacyCell+'0');
            else state+=char(legacyCell-100+'A');
        }
    }
    state+=';';
    const auto buffer=Rules::compute_garbage_buffer_display(player.garbage.buffer,context.now(),
        context.config.timeMultiplier,Runtime::current_incoming_garbage_delay(context),
        Rules::GarbageBufferDisplayMode::ThreeStage,field.visibleHeight+Runtime::kDisplayHiddenRows+1);
    // Preserve the historical dark-red/ready wire codes and add yellow as 3.
    constexpr int wireColors[]={0,3,1,2};
    for(int i=1;i<=field.visibleHeight+Runtime::kDisplayHiddenRows;i++) state+=char(wireColors[buffer[i]]+'0');
    state+=';';
    if(!player.pieces.bag.empty()) state+=std::to_string(Compat::to_legacy(player.pieces.bag.front().type))+",";
    for(Piece::Type piece:player.pieces.nextPreview) state+=std::to_string(Compat::to_legacy(piece))+",";
    state+=';';
    if(!hold.piece.valid()) state+='-'; else state+=std::to_string(Compat::to_legacy(hold.piece))+",";
    state+=";"+std::to_string(row)+";"+std::to_string(col)+";"+std::to_string(rotation);
    state+=";"+std::to_string(scoring.wins)+";"+std::to_string(scoring.placedBlocks)+";"+std::to_string(scoring.totalAttack);
    if(placement){
        state+=";"+std::to_string(Compat::to_legacy(placement->piece))+";"+std::to_string(placement->spin?1:0)+";"+std::to_string(placement->mini?1:0);
        state+=";"+std::to_string(placement->clearedLines)+";"+std::to_string(placement->b2b)+";"+std::to_string(placement->combo)+";"+std::to_string(placement->perfectClear?1:placement->halfClear?2:0);
    }
    if(hold.used || context.config.field.infiniteHold || context.config.field.holdCount!=1){
        const int used=activePiece.kind==ActivePieceStateKind::Locked ? 0 : hold.used;
        state+=";hold-v1;"+std::to_string(Runtime::hold_available(context.config.field,used)?1:0);
    }
    // Ordinary previews already identify their spawn. Disguised event shapes
    // and Snowball wall fitting need the actual type/pose on the remote board.
    if (!player.pieces.nextPreview.empty() && player.presentation.nextSpawnType &&
        player.presentation.nextSpawnType->valid()) {
        const auto type=*player.presentation.nextSpawnType;
        auto pose=Piece::spawn_pose(field.width,type);
        if (Runtime::enabled(context.config.quickPlay.snowball))
            pose=Piece::fit_spawn_inside_walls(pose,Piece::catalog_entry(type).shapes[pose.rotation.index()],field.width);
        if (type!=player.pieces.nextPreview.front() ||
            pose!=Piece::spawn_pose(field.width,player.pieces.nextPreview.front())) {
            state+=";spawn-v1;"+std::to_string(Compat::to_legacy(type))+";"+
                std::to_string(pose.position.row)+";"+std::to_string(pose.position.col)+";"+
                std::to_string(pose.rotation.index());
        }
    }
    return state;
}

std::optional<BoardStateSnapshot> decode_board_state(std::string_view state,int width,int visibleHeight){
    std::vector<std::string_view> sections = split_sections(state);
    if(sections.empty()) return std::nullopt;

    BoardStateSnapshot snapshot;
    if (sections.size()>=15 && sections[sections.size()-5]=="spawn-v1") {
        const auto begin=sections.size()-4;
        const auto type=Util::Text::parse_integer<int>(sections[begin]);
        const auto row=Util::Text::parse_integer<int>(sections[begin+1]);
        const auto col=Util::Text::parse_integer<int>(sections[begin+2]);
        const auto rotation=Util::Text::parse_integer<int>(sections[begin+3]);
        if (!type || !valid_piece_id(*type) || !row || !col || !rotation || *rotation<0 || *rotation>3 ||
            *row < -Runtime::kHiddenRowCapacity-Runtime::Playfield::kStorageRows ||
            *row > Runtime::Playfield::kStorageRows ||
            *col < -Runtime::Playfield::kStorageColumns || *col > Runtime::Playfield::kStorageColumns)
            return std::nullopt;
        snapshot.nextSpawnType=Compat::piece_type_from_legacy(*type);
        snapshot.nextSpawnPose=Piece::Pose{{*row,*col},Runtime::Rotation::from_index(*rotation)};
        sections.resize(sections.size()-5);
    }
    if (sections.size()!=10 && sections.size()!=12 && sections.size()!=17 && sections.size()!=19)
        return std::nullopt;
    if(sections.size()==12 || sections.size()==19){
        const auto available=Util::Text::parse_integer<int>(sections.back());
        if(sections[sections.size()-2]!="hold-v1" || !available || (*available!=0 && *available!=1))
            return std::nullopt;
        snapshot.holdAvailable=*available!=0;
    }
    if(!decode_board_cells(sections[0],width,visibleHeight,snapshot.field)) return std::nullopt;
    if(!decode_garbage(sections[1],visibleHeight,snapshot.garbage)) return std::nullopt;
    std::vector<Piece::Type> wirePreview;
    if(!decode_piece_list(sections[2],wirePreview)||wirePreview.empty()) return std::nullopt;
    snapshot.currentPiece=wirePreview.front();
    snapshot.nextPreview.assign(wirePreview.begin()+1,wirePreview.end());
    if (snapshot.nextSpawnType && snapshot.nextPreview.empty()) return std::nullopt;
    if(!decode_hold(sections[3],snapshot.hold)) return std::nullopt;

    int row=0,col=0,rotation=0;
    const auto rowValue=Util::Text::parse_integer<int>(sections[4]);
    const auto colValue=Util::Text::parse_integer<int>(sections[5]);
    const auto rotationValue=Util::Text::parse_integer<int>(sections[6]);
    const auto wins=Util::Text::parse_integer<int>(sections[7]);
    const auto placedBlocks=Util::Text::parse_integer<int>(sections[8]);
    const auto totalAttack=Util::Text::parse_integer<int>(sections[9]);
    if(!rowValue||!colValue||!rotationValue||!wins||!placedBlocks||!totalAttack) return std::nullopt;
    row=*rowValue; col=*colValue; rotation=*rotationValue;
    snapshot.score.wins=*wins;
    snapshot.score.placedBlocks=*placedBlocks;
    snapshot.score.totalAttack=*totalAttack;

    if(sections.size()==17 || sections.size()==19){
        RemotePlacementSnapshot placement;
        const auto piece=Util::Text::parse_integer<int>(sections[10]);
        const auto spin=Util::Text::parse_integer<int>(sections[11]);
        const auto mini=Util::Text::parse_integer<int>(sections[12]);
        const auto clearedLines=Util::Text::parse_integer<int>(sections[13]);
        const auto b2b=Util::Text::parse_integer<int>(sections[14]);
        const auto combo=Util::Text::parse_integer<int>(sections[15]);
        const auto perfectClear=Util::Text::parse_integer<int>(sections[16]);
        if(!piece||!valid_piece_id(*piece)||!spin||!mini||!clearedLines||!b2b||!combo||!perfectClear) return std::nullopt;
        placement.piece=Compat::piece_type_from_legacy(*piece);
        placement.spin=*spin!=0;
        placement.mini=*mini!=0;
        placement.clearedLines=*clearedLines;
        placement.b2b=*b2b;
        placement.combo=*combo;
        placement.perfectClear=*perfectClear!=0 && *perfectClear!=2;
        placement.halfClear=*perfectClear==2;
        snapshot.placement=placement;
    }else{
        if(rotation<0||rotation>3) return std::nullopt;
        // Leave room for off-board shape pivots, but keep wire coordinates
        // bounded before piece offsets and terminal positions use int arithmetic.
        if(row < -Runtime::kHiddenRowCapacity-Runtime::Playfield::kStorageRows ||
           row > visibleHeight+Runtime::Playfield::kStorageRows ||
           col < -Runtime::Playfield::kStorageColumns ||
           col > width+Runtime::Playfield::kStorageColumns) return std::nullopt;
        snapshot.activePose=Piece::Pose{{row,col},Compat::rotation_from_legacy(rotation)};
    }
    return snapshot;
}

} // namespace Network
