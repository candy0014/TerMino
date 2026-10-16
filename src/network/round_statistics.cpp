#include "tetr/network/round_statistics.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include "tetr/util/text_codec.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace Network {
namespace {
constexpr std::size_t maxResultBytes=262144,partBytes=3000;
constexpr std::string_view prefix="!result-v1;";
constexpr std::array<int Runtime::ScoringState::*,9> counters{{
    &Runtime::ScoringState::placedBlocks,&Runtime::ScoringState::totalAttack,&Runtime::ScoringState::totalLines,
    &Runtime::ScoringState::totalGarbageLines,&Runtime::ScoringState::wins,&Runtime::ScoringState::maxSpike,
    &Runtime::ScoringState::totalOutgoingAttack,&Runtime::ScoringState::maxCombo,&Runtime::ScoringState::maxB2B}};
void put(std::string& s,std::uint64_t n) {do {const auto b=n&127;n>>=7;s+=char(b|(n?128:0));} while(n);}
std::optional<std::uint64_t> take(std::string_view& s) {
    std::uint64_t n=0;
    for(int i=0;i<10;++i) {
        if(s.empty()) return {};const auto b=static_cast<unsigned char>(s.front());s.remove_prefix(1);
        if(i==9 && (b&254)) return {};n|=std::uint64_t(b&127)<<(i*7);
        if(!(b&128)) {if(i && !b) return {};return n;}
    }return {};
}
void put_real(std::string& s,double d) {std::uint64_t bits=0;std::memcpy(&bits,&d,8);for(int i=7;i>=0;--i)s+=char(bits>>(8*i));}
std::optional<double> take_real(std::string_view& s) {
    if(s.size()<8)return {};std::uint64_t bits=0;for(int i=0;i<8;++i)bits=(bits<<8)|static_cast<unsigned char>(s[i]);s.remove_prefix(8);
    double d=0;std::memcpy(&d,&bits,8);if(!std::isfinite(d)||d<0||d>1e18)return {};return d;
}
}
RoundStatistics capture_round_statistics(const Runtime::GameContext& c,const Runtime::PlayerSessionState& p) {
    RoundStatistics r;r.score=p.gameplay.scoring;
    for(const auto piece:p.pieces.allowedPieces) if(piece.valid())r.score.roundPieceTypes.insert(piece.index());
    r.gameSeconds=p.timing.completedGameTime.value_or(Runtime::elapsed_game_time(c.session,c.now())*c.config.timeMultiplier).seconds();
    r.realSeconds=p.timing.completedRealTime.value_or(p.timing.realGameStart?c.real_now()-*p.timing.realGameStart:Runtime::Duration::from_seconds(r.gameSeconds)).seconds();
    r.pieceTime=c.uses_piece_time();return r;
}
std::string encode_round_statistics(const RoundStatistics& r) {
    std::string s(prefix);put_real(s,r.gameSeconds);put_real(s,r.realSeconds);put(s,r.pieceTime);
    put(s,(static_cast<std::uint64_t>(r.score.score)<<1)^static_cast<std::uint64_t>(-(r.score.score<0)));
    for(const auto m:counters)put(s,std::max(0,r.score.*m));
    put(s,r.score.totalHolds);put(s,r.score.allClearCount);put(s,r.score.totalReceivedAttack);
    put(s,r.score.roundPieceTypes.size());for(int type:r.score.roundPieceTypes)put(s,type);
    put(s,r.score.clearStatistics.size());for(const auto& [key,counts]:r.score.clearStatistics) {
        put(s,key.first);put(s,key.second);for(auto count:counts)put(s,count);
    }
    if(s.size()>maxResultBytes)throw std::length_error("Round statistics exceed result capacity");return s;
}
std::optional<RoundStatistics> decode_round_statistics(std::string_view s) {
    if(s.size()>maxResultBytes || s.substr(0,prefix.size())!=prefix)return {};s.remove_prefix(prefix.size());
    RoundStatistics r;const auto game=take_real(s),real=take_real(s);const auto piece=take(s),score=take(s);
    if(!game||!real||!piece||*piece>1||!score)return {};r.gameSeconds=*game;r.realSeconds=*real;r.pieceTime=*piece;
    r.score.score=(*score&1)?-static_cast<long long>(*score>>1)-1:static_cast<long long>(*score>>1);
    for(const auto m:counters) {const auto n=take(s);if(!n||*n>std::numeric_limits<int>::max())return {};r.score.*m=static_cast<int>(*n);}
    const auto holds=take(s),all=take(s),received=take(s);if(!holds||!all||!received)return {};
    r.score.totalHolds=*holds;r.score.allClearCount=*all;r.score.totalReceivedAttack=*received;
    const auto types=take(s);if(!types||*types>Piece::catalog().size())return {};
    for(std::uint64_t i=0;i<*types;++i) {const auto type=take(s);if(!type||*type>=Piece::catalog().size()||!r.score.roundPieceTypes.insert(static_cast<int>(*type)).second)return {};}
    const auto cells=take(s);if(!cells||*cells>32768)return {};
    for(std::uint64_t i=0;i<*cells;++i) {
        const auto type=take(s),lines=take(s);if(!type||*type>=Piece::catalog().size()||!lines||*lines>std::numeric_limits<int>::max())return {};
        Runtime::ClearCounts counts;for(auto& count:counts) {const auto n=take(s);if(!n)return {};count=*n;}
        if(!r.score.clearStatistics.emplace(std::make_pair(static_cast<int>(*type),static_cast<int>(*lines)),counts).second)return {};
    }
    if(!s.empty())return {};return r;
}
std::uint64_t round_statistics_digest(std::string_view s) {std::uint64_t h=14695981039346656037ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}return h;}
std::vector<std::string> round_statistics_parts(std::string_view s) {
    std::vector<std::string> parts;const auto count=(s.size()+partBytes-1)/partBytes;
    const auto digest=round_statistics_digest(s);
    for(std::size_t i=0;i<count;++i)parts.push_back("!result-part-v1;"+std::to_string(i)+";"+std::to_string(count)+";"+std::to_string(s.size())+";"+
        std::to_string(digest)+";"+std::string(s.substr(i*partBytes,partBytes)));
    return parts;
}
std::size_t ResultAssembler::missing_part() const {for(std::size_t i=0;i<parts_.size();++i)if(!parts_[i])return i;return 0;}
std::optional<std::string> ResultAssembler::insert(std::string_view wire) {
    std::array<std::string_view,5> f;for(auto& v:f) {const auto end=wire.find(';');if(end==std::string_view::npos)return {};v=wire.substr(0,end);wire.remove_prefix(end+1);}
    const auto index=Util::Text::parse_integer<std::size_t>(f[1]),count=Util::Text::parse_integer<std::size_t>(f[2]),bytes=Util::Text::parse_integer<std::size_t>(f[3]);
    const auto digest=Util::Text::parse_integer<std::uint64_t>(f[4]);
    if(f[0]!="!result-part-v1"||!index||!count||!bytes||!*bytes||*bytes>maxResultBytes||!digest||
        *count!=(*bytes+partBytes-1)/partBytes||*index>=*count||wire.size()!=std::min(partBytes,*bytes-*index*partBytes))return {};
    if(parts_.empty()) {parts_.resize(*count);bytes_=*bytes;digest_=*digest;}
    if(bytes_!=*bytes||digest_!=*digest||parts_.size()!=*count)return {};
    if(parts_[*index] && *parts_[*index]!=wire)return {};parts_[*index]=std::string(wire);
    if(std::any_of(parts_.begin(),parts_.end(),[](const auto& p){return !p;}))return {};
    std::string result;result.reserve(bytes_);for(const auto& p:parts_)result+=*p;
    if(round_statistics_digest(result)!=digest_||!decode_round_statistics(result))return {};return result;
}
void send_round_statistics(Runtime::GameContext& c,const Runtime::PlayerSessionState& p) {
    auto& n=network_services(c);if(!n.multiplayerRoom)return;
    const auto epoch=c.session.roomScene.enabled?c.session.roomScene.epoch:n.matchEpoch;
    const auto wire=encode_round_statistics(capture_round_statistics(c,p));
    n.roundResultWires[epoch][n.roomPlayerIndex]=wire;n.pendingResultEpoch=epoch;
    n.pendingRoundResult=wire;n.pendingResultCursor=0;n.lastResultSend={};poll_multiplayer_transfer(n);
}
void request_round_statistics(NetworkServices& n,std::uint64_t epoch,int slot) {
    std::size_t start=0;auto& incoming=n.resultIncoming[epoch][slot];if(incoming)start=incoming->missing_part();
    send_online(n,encode_match_packet(epoch,"!results-request;"+std::to_string(slot)+";"+std::to_string(start)));
}
}
