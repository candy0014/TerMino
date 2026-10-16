#include "tetr/replay/challenge_prefix.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/util/text_codec.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace Replay {
namespace {
bool valid(const Challenges::PrefixBoundary& b) {
    return !b.challengeId.empty() && b.ruleVersion>0 &&
        std::all_of(b.challengeId.begin(),b.challengeId.end(),[](unsigned char c){
            return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_';}) &&
        std::isfinite(b.gameSeconds) && b.gameSeconds>=0 &&
        std::isfinite(b.realSeconds) && b.realSeconds>=0;
}
PrefixPreparation clip(const std::string& recording,const Challenges::PrefixBoundary& boundary) {
    if(!valid(boundary))return {};
    Runtime::RuntimeConfig config;ConfigPrefixResult prefix;
    if(!decode_config_prefix(recording,config,prefix))return {};
    // Walk raw complete operation records so no part of the violating lock is
    // retained, even when several locks have identical timestamps.
    std::size_t end=prefix.operationOffset;
    std::uint64_t count=0;
    for(std::size_t offset=prefix.operationOffset;count<boundary.placements;) {
        const auto open=recording.find('{',offset);
        if(open==std::string::npos)return {};
        const auto close=Util::Text::matching_brace(recording,open);
        if(close==std::string::npos)return {};
        const auto comma=recording.find(',',open);
        if(comma!=std::string::npos && comma<close && recording.compare(open+1,comma-open-1,"end_all")==0)return {};
        if(comma!=std::string::npos && comma<close && recording.compare(open+1,comma-open-1,"end_place")==0) {
            ++count;end=close+1;
        }
        offset=close+1;
    }
    // Keep config/seeds and unknown future fields, replacing only result times.
    std::string output="{";
    for(std::size_t offset=1;offset+1<prefix.operationOffset;) {
        const auto open=recording.find('{',offset);
        if(open==std::string::npos || open>=prefix.operationOffset-1)break;
        const auto close=Util::Text::matching_brace(recording,open);
        if(close==std::string::npos || close>=prefix.operationOffset)return {};
        const auto token=recording.substr(open,close-open+1);
        if(token.rfind("{result_game_time,",0)!=0 && token.rfind("{result_real_time,",0)!=0)output+=token;
        offset=close+1;
    }
    std::ostringstream times;times.imbue(std::locale::classic());times<<std::setprecision(17);
    times<<"{result_game_time,"<<boundary.gameSeconds<<"}{result_real_time,"<<boundary.realSeconds<<"}}";
    output+=times.str();
    output+=recording.substr(prefix.operationOffset,end-prefix.operationOffset);
    auto operations=decode_operation_stream(recording.substr(0,end),prefix.operationOffset);
    Runtime::Duration elapsed{};
    while(!operations.empty()){elapsed=operations.front().timestamp;operations.pop();}
    output+="{wall_time,"+std::to_string(elapsed.seconds())+"}{end_all,"+std::to_string(boundary.gameSeconds)+"}";
    return {PrefixPreparationStatus::Ready,std::move(output),boundary};
}
}
std::string encode_challenge_prefixes(const std::vector<Challenges::PrefixBoundary>& boundaries) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    for(const auto& b:boundaries)if(valid(b))out<<"{challenge_prefix,"<<b.challengeId<<','<<b.ruleVersion<<','<<b.placements<<','<<b.gameSeconds<<','<<b.realSeconds<<'}';
    return out.str();
}
PrefixPreparation prepare_challenge_replay(const std::string& recording,const std::string& id,
    std::uint32_t version,bool restricted,const PrefixAnalyzer& analyze) {
    if(!restricted)return {PrefixPreparationStatus::Ready,recording,{}};
    Runtime::RuntimeConfig config;ConfigPrefixResult prefix;
    if(!decode_config_prefix(recording,config,prefix))return {};
    std::optional<Challenges::PrefixBoundary> cached;bool bad=false;
    for(std::size_t offset=1;offset+1<prefix.operationOffset;) {
        const auto open=recording.find('{',offset);
        if(open==std::string::npos || open>=prefix.operationOffset-1)break;
        const auto close=Util::Text::matching_brace(recording,open);
        if(close==std::string::npos || close>=prefix.operationOffset)return {};
        const auto fields=Util::Text::split(std::string_view(recording).substr(open+1,close-open-1),',');
        if(fields.size()>1 && fields[0]=="challenge_prefix" && fields[1]==id) {
            if(fields.size()!=6){bad=true;break;}
            const auto v=Util::Text::parse_integer<std::uint32_t>(fields[2]);
            const auto count=Util::Text::parse_integer<std::uint64_t>(fields[3]);
            const auto game=Util::Text::parse_finite_real(fields[4]),real=Util::Text::parse_finite_real(fields[5]);
            if(!v || !count || !game || !real || cached){bad=true;break;}
            cached=Challenges::PrefixBoundary{id,*v,*count,*game,*real};
        }
        offset=close+1;
    }
    if(!bad && cached && cached->ruleVersion==version) {
        auto prepared=clip(recording,*cached);
        if(prepared.status==PrefixPreparationStatus::Ready)return prepared;
    }
    if(!analyze)return {PrefixPreparationStatus::NeedsAnalysis,{},{}};
    const auto computed=analyze(recording);
    if(!computed || computed->challengeId!=id || computed->ruleVersion!=version)return {};
    return clip(recording,*computed);
}
}
