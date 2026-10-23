#include "tetr/compat/legacy_encoding.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/runtime_services.h"
#include "tetr/runtime/session_state.h"
#include "tetr/util/deterministic_random.h"

#include <algorithm>
#include <cassert>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace TetrTests::piece_queue_equivalence_test {
namespace {
struct LegacyPieceState {
    std::deque<std::pair<int,bool>> bag;
    std::vector<int> allowed;
    int bagCount=0;
    std::vector<int> foolLast;
    int bagBreather=1;
    std::mt19937 random;
};

void legacy_add_bag(LegacyPieceState& s,bool randomPieces,int fool,int floor){
    s.bagCount++;
    if(randomPieces){
        if(fool==3||fool==4){
            int tmp=s.random()%17;
            if(tmp<14) s.bag.push_back({s.allowed[s.random()%s.allowed.size()],1});
            else if(tmp<16) s.bag.push_back({103+s.random()%7+(floor-1)*7,1});
            else s.bag.push_back({103+(s.random()%(std::min(3,std::max(1,floor-1))*7)),1});
            return;
        }
        s.bag.push_back({s.allowed[s.random()%s.allowed.size()],1});
        return;
    }
    if(fool==3||fool==4){
        std::vector<int> res=s.allowed;
        if(s.bagBreather) s.bagBreather=0;
        else{
            if(s.bagCount%2==0) res.emplace_back(103+(s.random()%(std::min(3,std::max(1,floor-1))*7)));
            if(s.foolLast.empty()) s.foolLast={0,1,2,3,4,5,6};
            Util::deterministic_shuffle(s.foolLast,s.random);
            res.emplace_back(103+s.foolLast.back()+(floor-1)*7),s.foolLast.pop_back();
        }
        Util::deterministic_shuffle(res,s.random);
        for(int i=0;i<(int)res.size();i++) s.bag.push_back({res[i],i==0});
        return;
    }
    std::vector<int> res=s.allowed;
    Util::deterministic_shuffle(res,s.random);
    for(int i=0;i<(int)res.size();i++) s.bag.push_back({res[i],i==0});
}
}

int run(int argc,char** argv){
    {
        Runtime::PlayerSessionState player;
        bool threw=false;
        try { (void)Engine::current_piece(player); } catch(const std::logic_error&) { threw=true; }
        assert(threw);
        threw=false;
        try { Engine::consume_current_piece(player); } catch(const std::logic_error&) { threw=true; }
        assert(threw);
    }
    std::mt19937 gen(0x51A6E16u);
    for(int tc=0;tc<30000;tc++){
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        context.config.legacyRandomizerRules=true;
        context.config.legacyEventGeometry=tc%2!=0;
        auto& player=session.player;
        auto& modern=player.pieces;
        LegacyPieceState legacy;

        const int count=1+gen()%12;
        for(int i=0;i<count;i++){
            const int v=gen()%173;
            const auto type=Piece::Type::from_index(v);
            modern.allowedPieces.push_back(context.config.legacyEventGeometry?type:Piece::canonical_event_piece(type));
            legacy.allowed.push_back(v);
        }
        const unsigned seed=gen();
        modern.random.seed(seed);
        legacy.random.seed(seed);
        modern.bagCount=legacy.bagCount=gen()%20;
        modern.qpBagBreather=(legacy.bagBreather=gen()%2)!=0;
        if(gen()%2){
            modern.foolLastTypes=legacy.foolLast={0,2,4,6};
        }
        context.config.randomPieces=gen()%2;
        const int foolMode=(gen()%3==0)?0:(3+gen()%2);
        context.config.quickPlay.fool=Compat::fool_mode_from_legacy(foolMode);
        context.session.quickPlay.floor=1+gen()%10;

        const int rounds=1+gen()%8;
        for(int i=0;i<rounds;i++){
            Engine::add_bag(context,player);
            legacy_add_bag(legacy,context.config.randomPieces,foolMode,context.session.quickPlay.floor);
        }
        assert(modern.bag.size()==legacy.bag.size());
        for(std::size_t i=0;i<modern.bag.size();++i){
            const auto original=Piece::Type::from_index(legacy.bag[i].first);
            assert(modern.bag[i].type==(context.config.legacyEventGeometry?original:Piece::canonical_event_piece(original)));
            assert(modern.bag[i].startsNewBag==legacy.bag[i].second);
        }
        assert(modern.bagCount==legacy.bagCount);
        assert(modern.foolLastTypes==legacy.foolLast);
        assert(modern.qpBagBreather==legacy.bagBreather);
        // Compare the next RNG output to prove identical consumption order.
        assert(modern.random()==legacy.random());
    }
    return 0;
}
} // namespace TetrTests::piece_queue_equivalence_test
