#include "tetr/network/board_view.h"
#include "tetr/network/network_services.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/transport.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/text_codec.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <cstring>

namespace Network {
namespace {
int color_code(Runtime::BoardCell cell) {
    if(!cell.occupied())return 0;
    if(cell.is_piece())return 1+std::clamp(cell.color_id(),0,6);
    return cell.kind()==Runtime::BoardCellKind::Garbage?8:cell.kind()==Runtime::BoardCellKind::SolidGarbage?9:
        cell.kind()==Runtime::BoardCellKind::TimedRelease?11:10;
}
Runtime::BoardCell from_code(int code) {
    if(!code)return {};
    if(code<8)return Runtime::BoardCell::piece(code-1);
    return code==8?Runtime::BoardCell::garbage():code==9?Runtime::BoardCell::solid_garbage():
        code==11?Runtime::BoardCell::timed_release():Runtime::BoardCell::timed_solid();
}
template<class Sample,class GarbageRow> Runtime::BoardCell sample_region(int r0,int r1,int c0,int c1,Sample sample,GarbageRow garbage_row) {
    Runtime::BoardCell result;
    for(int row=r0;row<r1;++row) {
        const bool garbageRow=garbage_row(row);bool hole=false;Runtime::BoardCell cell,piece;
        for(int col=c0;col<c1;++col) {
            const auto next=sample(row,col);
            if(next.occupied()) {cell=next;if(next.is_piece())piece=next;}
            else hole=true;
        }
        // Preserve a one-cell garbage hole even when several source columns
        // share one output pixel. Apply this at both sender and relay scales.
        if(garbageRow && hole && !piece.occupied())result={};
        else if(cell.occupied())result=cell;
    }
    return result;
}
template<class Sample> MiniBoardFrame project(int width,int height,int columns,int rows,std::uint64_t sequence,Sample sample) {
    MiniBoardFrame result;result.sequence=sequence;result.logicalWidth=width;result.logicalHeight=height;
    result.columns=std::clamp(columns,1,width*2);
    const double scale=std::max(1.,double(width*2)/result.columns);
    result.rows=std::clamp(rows,1,std::min(160,int(std::ceil((height+Runtime::kDisplayHiddenRows)/scale))));
    result.cells.resize(result.columns*result.rows);
    for(int y=0;y<result.rows;++y)for(int x=0;x<result.columns;++x) {
        const int row0=height+int(std::floor((y-result.rows)*scale));
        const int row1=height+int(std::floor((y+1-result.rows)*scale));
        result.cells[y*result.columns+x]=sample(row0,row1,x*width/result.columns,std::max(x*width/result.columns+1,(x+1)*width/result.columns));
    }
    return result;
}
}
Runtime::BoardCell MiniBoardFrame::at(int row,int column) const {
    return row>=0 && row<rows && column>=0 && column<columns?cells[row*columns+column]:Runtime::BoardCell{};
}
std::string encode_board_views(const BoardViews& views) {
    std::string wire=std::string(views.highRate?"!view-v2;":"!view-v1;")+std::to_string(views.remaining);
    for(const auto& view:views.players)wire+=';'+std::to_string(view.slot)+','+std::to_string(view.full?1:0)+','+
        std::to_string(view.columns)+','+std::to_string(view.rows)+','+std::to_string(view.fps);
    return wire;
}
std::optional<BoardViews> decode_board_views(std::string_view wire) {
    if(wire.size()>2048 || (wire.substr(0,9)!="!view-v1;" && wire.substr(0,9)!="!view-v2;"))return {};
    const auto fields=Util::Text::split(wire,';');
    if(fields.size()<2 || fields.size()>34 || (fields[0]!="!view-v1" && fields[0]!="!view-v2"))return {};
    const auto remaining=Util::Text::parse_integer<int>(fields[1]);if(!remaining || *remaining<0 || *remaining>32)return {};
    BoardViews views;views.remaining=*remaining;views.highRate=fields[0]=="!view-v2";std::set<int> slots;
    for(std::size_t i=2;i<fields.size();++i) {
        const auto f=Util::Text::split_exact<5>(fields[i],',');if(!f)return {};
        int values[5];for(int j=0;j<5;++j){const auto n=Util::Text::parse_integer<int>((*f)[j]);if(!n)return {};values[j]=*n;}
        if(values[0]<0 || values[0]>=32 || !slots.insert(values[0]).second || values[1]<0 || values[1]>1 ||
            values[2]<0 || values[2]>200 || values[3]<0 || values[3]>160 || values[4]<1 || values[4]>(views.highRate?120:60) ||
            (!values[1] && (!values[2] || !values[3] || values[4]>(views.highRate?60:12))))return {};
        views.players.push_back({values[0],values[1]!=0,values[2],values[3],values[4]});
    }
    return views;
}
std::string encode_board_budget(const BoardBudget& b) {
    return std::string(b.highRate?"!budget-v2;":"!budget-v1;")+std::to_string(b.full?1:0)+';'+std::to_string(b.columns)+';'+std::to_string(b.rows)+';'+std::to_string(b.fps);
}
std::optional<BoardBudget> decode_board_budget(std::string_view wire) {
    const auto f=Util::Text::split_exact<5>(wire,';');if(!f || ((*f)[0]!="!budget-v1" && (*f)[0]!="!budget-v2"))return {};
    const bool highRate=(*f)[0]=="!budget-v2";
    int v[4];for(int i=0;i<4;++i){const auto n=Util::Text::parse_integer<int>((*f)[i+1]);if(!n)return {};v[i]=*n;}
    if(v[0]<0 || v[0]>1 || v[1]<0 || v[1]>200 || v[2]<0 || v[2]>160 || v[3]<0 || v[3]>(highRate?60:12) ||
        ((v[1]>0 || v[2]>0 || v[3]>0) && (!v[1] || !v[2] || !v[3])))return {};
    return BoardBudget{v[0]!=0,v[1],v[2],v[3],highRate};
}
MiniBoardFrame capture_mini_board(const Runtime::PlayerSessionState& p,int columns,int rows,std::uint64_t sequence) {
    const auto& f=p.gameplay.playfield;
    std::vector<bool> garbageRows(f.visibleHeight+Runtime::kDisplayHiddenRows);
    for(int row=-Runtime::kDisplayHiddenRows;row<f.visibleHeight;++row)for(int col=0;col<f.width;++col)
        if(f.field.cell(row,col).is_garbage_like()){garbageRows[row+Runtime::kDisplayHiddenRows]=true;break;}
    return project(f.width,f.visibleHeight,columns,rows,sequence,[&](int r0,int r1,int c0,int c1) {
        return sample_region(std::max(-Runtime::kDisplayHiddenRows,r0),std::min(f.visibleHeight,std::max(r0+1,r1)),c0,c1,
            [&](int row,int col){return f.field.cell(row,col);},
            [&](int row){return garbageRows[row+Runtime::kDisplayHiddenRows];});
    });
}
MiniBoardFrame project_mini_board(const MiniBoardFrame& f,int columns,int rows) {
    const double scale=std::max(1.,double(f.logicalWidth*2)/f.columns);
    std::vector<bool> garbageRows(f.rows);
    for(int row=0;row<f.rows;++row)for(int col=0;col<f.columns;++col)
        if(f.at(row,col).is_garbage_like()){garbageRows[row]=true;break;}
    auto result=project(f.logicalWidth,f.logicalHeight,columns,rows,f.sequence,[&](int r0,int r1,int c0,int c1) {
        const int first=f.rows+int(std::floor((r0-f.logicalHeight)/scale));
        const int end=f.rows+int(std::ceil((r1-f.logicalHeight)/scale));
        return sample_region(std::max(0,first),std::min(f.rows,std::max(first+1,end)),
            c0*f.columns/f.logicalWidth,std::min(f.columns,std::max(c0*f.columns/f.logicalWidth+1,(c1*f.columns+f.logicalWidth-1)/f.logicalWidth)),
            [&](int row,int col){return f.at(row,col);},[&](int row){return garbageRows[row];});
    });
    result.clock=f.clock;return result;
}
std::string encode_mini_board(const MiniBoardFrame& f) {
    std::string raw,packed;raw.reserve(f.cells.size());
    for(auto cell:f.cells)raw+=static_cast<char>(color_code(cell));
    for(std::size_t i=0;i<raw.size();) {
        auto end=i+1;while(end<raw.size() && end-i<255 && raw[end]==raw[i])++end;
        packed+=static_cast<char>(end-i);packed+=raw[i];i=end;
    }
    const bool compressed=packed.size()<raw.size();
    std::string timing;
    if(f.clock)for(double value:{f.clock->elapsedSeconds,f.clock->rate}) {
        std::uint64_t bits=0;std::memcpy(&bits,&value,8);
        for(int byte=7;byte>=0;--byte)timing+=static_cast<char>((bits>>(byte*8))&255);
    }
    return "!mini-v1;"+std::to_string(f.sequence)+';'+std::to_string(f.logicalWidth)+';'+std::to_string(f.logicalHeight)+';'+
        std::to_string(f.columns)+';'+std::to_string(f.rows)+';'+std::to_string((compressed?1:0)+(f.clock?2:0))+';'+timing+(compressed?packed:raw);
}
namespace {
std::optional<std::pair<MiniBoardFrame,bool>> read_mini_header(std::string_view& wire) {
    if(wire.size()>33000 || wire.substr(0,9)!="!mini-v1;")return {};
    wire.remove_prefix(9);std::string_view fields[6];
    for(auto& field:fields){const auto end=wire.find(';');if(end==std::string_view::npos)return {};field=wire.substr(0,end);wire.remove_prefix(end+1);}
    const auto seq=Util::Text::parse_integer<std::uint64_t>(fields[0]);if(!seq || !*seq)return {};
    int v[5];for(int i=0;i<5;++i){const auto n=Util::Text::parse_integer<int>(fields[i+1]);if(!n)return {};v[i]=*n;}
    if(v[0]<1 || v[0]>100 || v[1]<0 || v[1]>100 || v[2]<1 || v[2]>v[0]*2 || v[3]<1 || v[3]>160 || v[4]<0 || v[4]>3)return {};
    MiniBoardFrame frame{*seq,v[0],v[1],v[2],v[3],{}};
    if(v[4]&2) {
        if(wire.size()<16)return {};double values[2];
        for(auto& value:values) {
            std::uint64_t bits=0;for(int byte=0;byte<8;++byte)bits=(bits<<8)|static_cast<unsigned char>(wire[byte]);
            wire.remove_prefix(8);std::memcpy(&value,&bits,8);
            if(!std::isfinite(value) || value<0 || value>1e18)return {};
        }
        frame.clock=RemoteClockSample{values[0],values[1]};
    }
    return std::pair<MiniBoardFrame,bool>{std::move(frame),(v[4]&1)!=0};
}
}
std::optional<std::uint64_t> validated_mini_board_sequence(std::string_view wire) {
    const auto header=read_mini_header(wire);if(!header)return {};
    const auto& frame=header->first;const std::size_t size=frame.columns*frame.rows;
    if(!header->second) {
        if(wire.size()!=size || std::any_of(wire.begin(),wire.end(),[](unsigned char code){return code>11;}))return {};
    } else {
        if(wire.size()%2)return {};
        std::size_t decoded=0;
        for(std::size_t i=0;i<wire.size();i+=2) {
            const auto count=static_cast<unsigned char>(wire[i]),code=static_cast<unsigned char>(wire[i+1]);
            if(!count || code>11 || count>size-decoded)return {};
            decoded+=count;
        }
        if(decoded!=size)return {};
    }
    return frame.sequence;
}
std::optional<MiniBoardFrame> decode_mini_board(std::string_view wire) {
    auto header=read_mini_header(wire);if(!header)return {};
    auto frame=std::move(header->first);const std::size_t size=frame.columns*frame.rows;
    if(!header->second) {
        if(wire.size()!=size)return {};frame.cells.reserve(size);
        for(unsigned char code:wire){if(code>11)return {};frame.cells.push_back(from_code(code));}
    } else {
        if(wire.size()%2)return {};frame.cells.reserve(size);
        for(std::size_t i=0;i<wire.size();i+=2){const auto count=static_cast<unsigned char>(wire[i]),code=static_cast<unsigned char>(wire[i+1]);
            if(!count || code>11 || count>size-frame.cells.size())return {};frame.cells.insert(frame.cells.end(),count,from_code(code));}
        if(frame.cells.size()!=size)return {};
    }
    return frame;
}
void publish_board_views(Runtime::GameContext& c,const BoardViews& views) {
    auto& n=network_services(c);if(!n.boardViewsEnabled || n.connection!=ConnectionState::Connected || !n.multiplayerRoom || n.matchPhase!=RoomMatchPhase::Playing)return;
    const auto wire=encode_board_views(views);const auto now=c.real_now();
    if(wire!=n.lastBoardViews || now-n.lastBoardViewSend>=Runtime::Duration::from_seconds(1)) {
        const auto mode=[](const BoardViews& v,int slot) {
            for(const auto& view:v.players)if(view.slot==slot)return view.full?2:1;
            return 0;
        };
        const auto discard=[&](int slot) {
            const int next=mode(views,slot);
            return !next || (!n.lastBoardViews.empty() && mode(n.boardViews,slot)!=next) || (next==1 && n.peerMiniBoards.count(slot));
        };
        for(auto& [slot,player]:c.session.roomScene.players)
            if(mode(n.boardViews,slot)!=mode(views,slot)) {
                player.presentation.remoteAttackBaselineKnown=false;
                player.presentation.spike={};
            }
        // A re-entered full scope needs a fresh anchor, rather than queued
        // motions from before it was hidden or reduced to a thumbnail.
        for(auto it=n.peerTimelines.begin();it!=n.peerTimelines.end();) {
            if(discard(it->first))it=n.peerTimelines.erase(it);
            else ++it;
        }
        n.playerInbox.erase(std::remove_if(n.playerInbox.begin(),n.playerInbox.end(),[&](const PlayerWirePacket& packet) {
            if(!discard(packet.slot))return false;
            const auto& data=packet.data;
            const bool named=data.rfind("!motion-v1;",0)==0 || data.rfind("!snapshot-v1;",0)==0 || data.rfind("!board-v1;",0)==0;
            const bool legacy=data.size()>1 && data[0]=='!' && (data[1]=='-' || (data[1]>='0' && data[1]<='9') || (data[1]>='A' && data[1]<='Z'));
            return named || legacy;
        }),n.playerInbox.end());
        n.lastBoardViews=wire;n.boardViews=views;n.lastBoardViewSend=now;
        if(views.highRate && !n.boardViewsV2Confirmed) {
            auto legacy=views;legacy.highRate=false;
            for(auto& view:legacy.players)view.fps=std::min(view.fps,view.full?60:12);
            send(n,encode_board_views(legacy)); // Old relays ignore the v2 advertisement.
        }
        send(n,wire);
    }
}
void send_mini_board_frame(Runtime::GameContext& c,const Runtime::PlayerSessionState& player) {
    auto& n=network_services(c);if(!n.multiplayerRoom || !n.boardBudget || n.boardBudget->fps<=0)return;
    const auto now=c.real_now();const auto& budget=*n.boardBudget;
    const auto interval=Runtime::Duration::from_seconds(1./budget.fps);
    if(n.miniSequence && now-n.lastMiniSample<interval)return;
    if(n.lastMiniSample==Runtime::TimePoint{} || now-n.lastMiniSample>=interval*2.)n.lastMiniSample=now;
    else n.lastMiniSample+=interval;
    auto frame=capture_mini_board(player,budget.columns,budget.rows,n.miniSequence);
    frame.clock=capture_player_clock(c,player);
    const bool changed=!n.lastMiniFrame || n.lastMiniFrame->columns!=frame.columns || n.lastMiniFrame->rows!=frame.rows ||
        n.lastMiniFrame->logicalWidth!=frame.logicalWidth || n.lastMiniFrame->logicalHeight!=frame.logicalHeight || n.lastMiniFrame->cells!=frame.cells;
    if(changed || now-n.lastMiniSend>=Runtime::Duration::from_seconds(1)) {
        if(changed)frame.sequence=++n.miniSequence;
        const auto wire=encode_mini_board(frame);send(n,wire);n.lastMiniFrame=std::move(frame);
        n.lastMiniSend=now;++n.miniFramesSent;n.miniBytesSent+=wire.size();
    }
}
void send_board_visual_tick(Runtime::GameContext& c) {
    flush_piece_motion_frame(c);
    auto& n=network_services(c);if(!n.boardBudget || !n.boardSync.enabled || !c.session.roomScene.selfAlive){send_field_visibility(c);return;}
    const auto now=c.real_now();const auto& p=c.session.player;
    const bool miniDue=n.boardBudget->fps>0 && now-n.lastMiniSample>=Runtime::Duration::from_seconds(1./n.boardBudget->fps);
    if(n.boardBudget->full && (n.forceFullCheckpoint || now-n.boardSync.lastSnapshot>=Runtime::Duration::from_seconds(.5))) {
        const bool active=p.presentation.activePose.has_value();
        send_board_state_frame(c,p,p.presentation.activePose?ActivePieceState::active(*p.presentation.activePose):
            n.boardSync.lastPlacement?ActivePieceState::locked():ActivePieceState::spawn(),active?std::nullopt:n.boardSync.lastPlacement,true);
        n.forceFullCheckpoint=false;
    } else if(miniDue)send_mini_board_frame(c,p);
    send_field_visibility(c);
}
}
