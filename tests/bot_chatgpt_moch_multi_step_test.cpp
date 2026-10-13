#include "../src/bot/chatgpt/moch_multi_step_evaluation.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <tuple>

namespace B = Bot_chatgpt;
static void set_row(m_tetris::TetrisMap& map,int y,unsigned row) {
    map.row[y]=row;
    for(int x=0;x<10;++x)if(row&(1u<<x)){map.top[x]=y+1;map.roof=y+1;++map.count;}
}
static m_tetris::TetrisMap rows(std::initializer_list<unsigned> values) {
    m_tetris::TetrisMap map(10,40);int y=0;for(unsigned row:values)set_row(map,y++,row);return map;
}
static auto fixed_state(const B::Evaluation::Status&s) {
    return std::make_tuple(s.combo,s.b2b,s.garbageHeight,s.incoming,s.readyIncoming,s.risen,s.dead,
        s.previousCleared,s.openerDoubleCancelEnabled,s.placedPieces,s.attack,s.reward,s.pathRisk);
}
static double max_formula_error=0;
static void near(double actual,double expected) {
    const double scale=std::max({1.0,std::abs(actual),std::abs(expected)});
    max_formula_error=std::max(max_formula_error,std::abs(actual-expected));
    // Reassociating original_value + terrain_delta can cancel large terms to
    // a small final score. Use the frozen model's absolute tolerance as well
    // as the floating-point tolerance for the -1e9 death penalty.
    assert(std::abs(actual-expected)<=std::max(1e-7,16*std::numeric_limits<double>::epsilon()*scale));
}

int main(int argc,char**argv) {
    using Engine=m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,B::Evaluation,B::Search>;
    Engine geometry;assert(geometry.prepare(10,40));const auto*context=geometry.context().get();
    B::Evaluation baseline;B::MochMultiStepEvaluation candidate;baseline.init(context);candidate.init(context);
    if(argc==2&&std::string(argv[1])=="--metadata") {
        std::cout<<B::moch_multi_step_model_sha256<<' '<<B::moch_multi_step_registration_sha256<<' '<<B::moch_multi_step_terrain_scale<<'\n';return 0;
    }
    // Runtime side of the independent Python/frozen-model board comparison.
    // Input contains occupancy only; no expected features or model scores.
    if(argc==2&&std::string(argv[1])=="--boards") {
        std::cout<<std::setprecision(17);const B::Landing dummy(context->generate('I'));
        const m_tetris::TetrisMap before(10,40);
        for(std::string line;std::getline(std::cin,line);)if(!line.empty()) {
            std::istringstream in(line);int id;assert(in>>id);m_tetris::TetrisMap map(10,40);
            for(int y=0;y<40;++y){unsigned row;assert(in>>row);assert(row<=1023);set_row(map,y,row);}
            std::string extra;assert(!(in>>extra));
            const auto a=baseline.eval(dummy,map,before,0),b=candidate.eval(dummy,map,before,0);
            std::cout<<id<<' '<<a.shape.value<<' '<<b.shape.value<<'\n';
        }
        return 0;
    }
    assert(argc==1);assert(B::moch_multi_step_terrain_scale==0.5);
    B::Search search;B::Search::Config config;search.init(context,&config);
    const auto empty=rows({});const auto pc=rows({1023u^(15u<<3)});
    const auto spin=rows({1023u^(1u<<4),1023u^(7u<<3),1u<<3});
    const auto garbage=rows({1022,1022,1021,1021,1019,7});
    const auto bridge=rows({1022,1022,1023u^(1u<<8)});
    auto high=rows({});for(int y=0;y<18;++y)set_row(high,y,63);
    int checks=0,pcs=0,spins=0,rises=0,deaths=0,absorbed=0,releases=0,holds=0;
    for(const auto&before:{empty,pc,spin,garbage,bridge,high})for(char type:std::string("IJLOSTZ")) {
        const auto moves=*search.search(before,context->generate(type),0);
        for(auto move:moves) {
            auto after=before;const int clear=move->attach(context,after);
            const auto a=baseline.eval(move,after,before,clear),b=candidate.eval(move,after,before,clear);
            assert(b.shape.value==a.shape.value*0.5);
            assert(a.shape.clear==b.shape.clear&&a.shape.count==b.shape.count&&a.shape.map==b.shape.map);
            assert(a.before==b.before&&a.clearedRows==b.clearedRows);
            if(!after.count){assert(a.shape.value==-16000);assert(b.shape.value==-8000);}
            for(int chain:{0,4,11})for(std::size_t depth:{0u,2u,6u})for(int scenario=0;scenario<5;++scenario) {
                B::Evaluation::Status previous;previous.b2b=chain;previous.combo=3;previous.garbageHeight=std::min(4,before.roof);
                previous.incoming=12;previous.readyIncoming=8;previous.placedPieces=scenario==0?0:30;previous.openerDoubleCancelEnabled=true;
                previous.previousCleared=scenario==2;previous.risen=scenario==1?8:0;previous.dead=scenario==3;
                previous.attack=19;previous.reward=1234.125;previous.pathRisk=315.25;previous.value=previous.dead?-1000000345.125:999.5;
                const std::vector<B::VisiblePacket> packets{{8,-1},{4,1.5}};
                baseline.set_garbage_forecast(packets,1);candidate.set_garbage_forecast(packets,1);
                const m_tetris::TetrisContext::Env env{scenario==4?nullptr:"IOT",scenario==4?0u:3u,type,scenario==4?'O':'T',scenario==2};
                auto oldMove=move,newMove=move;const auto old=baseline.get(oldMove,a,depth,previous,env),changed=candidate.get(newMove,b,depth,previous,env);
                assert(fixed_state(old)==fixed_state(changed)&&oldMove==newMove);
                if(previous.dead){assert(changed.value==previous.value&&fixed_state(changed)==fixed_state(previous));++absorbed;}
                else {
                    near(changed.value,old.value+(b.shape.value-a.shape.value));
                    const double danger=std::max(0,after.roof+changed.risen+std::min(8,changed.incoming)-12);
                    double expected=b.shape.value+changed.reward+480.0*changed.b2b+(changed.b2b?300.0:0.0)-95.0*danger*danger-changed.pathRisk;
                    if(env.hold=='T'||env.hold=='I')expected+=80;if(changed.dead)expected-=1e9;near(changed.value,expected);
                    pcs+=clear&&after.count==0&&!previous.risen;spins+=move.spin&&clear;rises+=changed.risen>previous.risen;
                    releases+=chain>=5&&changed.b2b==0&&clear;deaths+=changed.dead;holds+=env.is_hold;
                    if(changed.dead){auto ignored=move;const auto again=candidate.get(ignored,b,depth+3,changed,env);assert(again.value==changed.value&&fixed_state(again)==fixed_state(changed));}
                }
                ++checks;
            }
        }
    }
    assert(checks>1000&&pcs>0&&spins>0&&rises>0&&deaths>0&&absorbed>0&&releases>0&&holds>0);
    std::cout<<"moch multi-step transitions="<<checks<<" pc="<<pcs<<" spin="<<spins<<" rise="<<rises<<" death="<<deaths
        <<" absorbed="<<absorbed<<" b2b_release="<<releases<<" hold="<<holds<<" max_formula_rounding="<<max_formula_error<<'\n';
}
