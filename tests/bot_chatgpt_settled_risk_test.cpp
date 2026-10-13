#include "../src/bot/chatgpt/settled_risk_evaluation.h"
#include "../src/bot/chatgpt/clear_stall_risk_evaluation.h"
#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <iostream>
namespace B=Bot_chatgpt;
namespace A=B::Analysis;
int main() {
    A::PlainEngine engine;assert(engine.prepare(10,40));
    B::Evaluation base;B::SettledRiskEvaluation candidate;B::ClearStallRiskEvaluation clearOnly;
    base.init(engine.context().get());candidate.init(engine.context().get());clearOnly.init(engine.context().get());
    B::Search moves;B::Search::Config config;moves.init(engine.context().get(),&config);
    int checked=0,refunded=0,clearRefunded=0,nonClearGuarded=0;
    for(int height:{0,8,12,18})for(int incoming:{0,8,16,30})for(int ready:{0,1}) {
        Runtime::Playfield field;field.initialize(10,20);field.clear();
        for(int y=0;y<height;++y)for(int x=0;x<10;++x)if(x!=5)field.set_cell(19-y,x,Runtime::BoardCell::garbage());
        const auto map=BotNativeHost::make_map(field);
        const std::vector<B::VisiblePacket>packets=incoming?std::vector<B::VisiblePacket>{{incoming,ready?-1.0:3.0}}:std::vector<B::VisiblePacket>{};
        base.set_garbage_forecast(packets,1);candidate.set_garbage_forecast(packets,1);clearOnly.set_garbage_forecast(packets,1);
        B::Evaluation::Status previous;previous.incoming=incoming;previous.readyIncoming=ready?incoming:0;
        previous.garbageHeight=height;previous.b2b=6;previous.combo=2;previous.placedPieces=40;
        for(char family:std::string("IJLOSTZ"))for(auto landing:*moves.search(map,engine.context()->generate(family),1)) {
            auto after=map;const int lines=landing->attach(engine.context().get(),after);
            const auto eval=base.eval(landing,after,map,lines);
            m_tetris::TetrisContext::Env env{"TLS",3,family,'I',false};
            const auto a=base.get(landing,eval,2,previous,env),b=candidate.get(landing,eval,2,previous,env);
            const auto c=clearOnly.get(landing,eval,2,previous,env);
            assert(a.combo==b.combo&&a.b2b==b.b2b&&a.garbageHeight==b.garbageHeight&&a.incoming==b.incoming&&a.readyIncoming==b.readyIncoming&&a.risen==b.risen);
            assert(a.dead==b.dead&&a.previousCleared==b.previousCleared&&a.placedPieces==b.placedPieces&&a.attack==b.attack&&a.reward==b.reward);
            const double physical=std::max(0,after.roof+b.risen-12);
            assert(std::abs(b.pathRisk-std::pow(.96,2)*35*physical*physical)<1e-8);
            assert(std::abs((b.value-a.value)-(a.pathRisk-b.pathRisk))<1e-6);
            if(!incoming)assert(a.pathRisk==b.pathRisk&&a.value==b.value);
            const auto&expected=lines?b:a;
            assert(c.value==expected.value&&c.pathRisk==expected.pathRisk&&c.reward==expected.reward);
            assert(c.combo==a.combo&&c.b2b==a.b2b&&c.garbageHeight==a.garbageHeight&&c.incoming==a.incoming&&c.readyIncoming==a.readyIncoming&&c.risen==a.risen);
            assert(c.dead==a.dead&&c.previousCleared==a.previousCleared&&c.placedPieces==a.placedPieces&&c.attack==a.attack);
            clearRefunded+=c.pathRisk<a.pathRisk;nonClearGuarded+=!lines&&b.pathRisk<a.pathRisk&&c.pathRisk==a.pathRisk;
            ++checked;refunded+=b.pathRisk<a.pathRisk;
            auto dead=previous;dead.dead=true;dead.value=-1e9;dead.pathRisk=99;
            const auto terminal=candidate.get(landing,eval,2,dead,env);
            assert(terminal.dead&&terminal.value==dead.value&&terminal.pathRisk==99&&terminal.placedPieces==40);
            const auto clearTerminal=clearOnly.get(landing,eval,2,dead,env);
            assert(clearTerminal.dead&&clearTerminal.value==dead.value&&clearTerminal.pathRisk==99&&clearTerminal.placedPieces==40);
        }
    }
    assert(refunded>0);std::cout<<"settled risk transitions="<<checked<<" refunded="<<refunded<<"; exact rules/terminal death retained\n";
    assert(clearRefunded>0&&nonClearGuarded>0);std::cout<<"clear-only refunds="<<clearRefunded<<" guarded non-clears="<<nonClearGuarded<<'\n';
}
