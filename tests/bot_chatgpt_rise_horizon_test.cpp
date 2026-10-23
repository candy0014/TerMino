#include "../src/bot/chatgpt/rise_horizon_evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <iostream>
#include <tuple>

namespace TetrTests::bot_chatgpt_rise_horizon_test {
namespace B = Bot_chatgpt;
static auto fields(const B::Evaluation::Status& s) {
    return std::make_tuple(s.combo,s.b2b,s.garbageHeight,s.incoming,s.readyIncoming,
        s.risen,s.dead,s.previousCleared,s.openerDoubleCancelEnabled,s.placedPieces,
        s.attack,s.reward,s.pathRisk,s.value);
}
static void row(m_tetris::TetrisMap& m, int y, unsigned bits) {
    m.row[y]=bits;
    for(int x=0;x<10;++x) if(bits&(1u<<x)) {m.top[x]=y+1; m.roof=y+1; ++m.count;}
}
int run(int argc,char** argv) {
    using Engine=m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,B::Evaluation,B::Search>;
    Engine engine; assert(engine.prepare(10,40));
    const auto* c=engine.context().get();
    B::Evaluation original; B::RiseHorizonEvaluation candidate;
    original.init(c); candidate.init(c);
    B::Search moves; B::Search::Config config; moves.init(c,&config);
    int matched=0, stopped=0, rising=0, clearing=0, dying=0;
    for(int board=0;board<4;++board) {
        m_tetris::TetrisMap before(10,40);
        if(board==1) row(before,0,1023u^(15u<<3));
        if(board==2) {row(before,0,1022); row(before,1,1021); row(before,2,7);}
        if(board==3) for(int y=0;y<19;++y) row(before,y,63);
        for(char type:std::string("IJLOSTZ")) for(auto move:*moves.search(before,c->generate(type),0)) {
            auto after=before; const int clear=move->attach(c,after);
            const auto eval=original.eval(move,after,before,clear);
            const auto actual=candidate.eval(move,after,before,clear);
            assert(eval.shape.value==actual.shape.value && eval.clearedRows==actual.clearedRows);
            for(int scenario=0;scenario<5;++scenario) {
                B::Evaluation::Status previous;
                previous.b2b=7; previous.combo=2; previous.placedPieces=30;
                previous.incoming=12; previous.readyIncoming=8;
                previous.garbageHeight=board==2?2:0;
                previous.previousCleared=scenario==1; previous.dead=scenario==2;
                previous.risen=scenario>=3?8:0;
                previous.value=-4321.25; previous.reward=321; previous.pathRisk=177;
                original.set_garbage_forecast({{8,-1},{4,1.5}},1);
                candidate.set_garbage_forecast({{8,-1},{4,1.5}},1);
                const m_tetris::TetrisContext::Env env{"T",1,type,'I',scenario==1};
                auto a=move,b=move;
                const auto old=original.get(a,eval,0,previous,env);
                const auto changed=candidate.get(b,actual,0,previous,env);
                if(previous.risen) {assert(fields(changed)==fields(previous)); ++stopped;}
                else {
                    assert(fields(changed)==fields(old)); ++matched;
                    rising+=changed.risen>0; clearing+=clear>0; dying+=changed.dead;
                    if(changed.risen) {
                        // Even a later clear/attack cannot alter the boundary
                        // state; the next real decision resets risen to zero.
                        const auto next=candidate.get(b,actual,6,changed,env);
                        assert(fields(next)==fields(changed));
                    }
                }
            }
        }
    }
    assert(matched>100 && stopped>100 && rising && clearing && dying);
    std::cout<<"rise horizon matched="<<matched<<" stopped="<<stopped
        <<" rises="<<rising<<" clears="<<clearing<<" dead="<<dying<<'\n';
    return 0;
}
} // namespace TetrTests::bot_chatgpt_rise_horizon_test
