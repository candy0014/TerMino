#include "../src/bot/chatgpt/fast_terrain_evaluation.h"
#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <random>
namespace TetrTests::bot_chatgpt_fast_terrain_test {
namespace B=Bot_chatgpt;namespace A=B::Analysis;
m_tetris::TetrisMap make_map(const std::vector<unsigned>&rows) {
    m_tetris::TetrisMap map(10,40);
    for(int y=0;y<int(rows.size());++y){map.row[y]=rows[y];for(int x=0;x<10;++x)if(rows[y]&(1u<<x)){map.top[x]=y+1;map.roof=y+1;++map.count;}}
    return map;
}
int run(int argc,char** argv) {
    A::PlainEngine owner;assert(owner.prepare(10,40));B::Evaluation base;B::FastTerrainEvaluation fast;
    base.init(owner.context().get());fast.init(owner.context().get());
    const m_tetris::TetrisMap empty(10,40);B::Landing landing(owner.context()->generate('O')->drop(empty));
    auto compare=[&](const m_tetris::TetrisMap&map){
        const auto a=base.eval(landing,map,map,0),b=fast.eval(landing,map,map,0);
        assert(a.shape.value==b.shape.value&&a.shape.clear==b.shape.clear&&a.shape.count==b.shape.count&&a.clearedRows==b.clearedRows);
        return a.shape.value;
    };
    std::cout<<std::setprecision(17);
    if(argc>1){int index;while(std::cin>>index){std::vector<unsigned>rows(40);for(auto&r:rows)std::cin>>r;std::cout<<index<<' '<<compare(make_map(rows))<<'\n';}return 0;}
    std::mt19937 random(2026092707);int checked=0;
    compare(empty);
    for(int sample=0;sample<10000;++sample) {
        std::vector<unsigned>rows(40);const int roof=1+random()%40;
        for(int y=0;y<roof;++y)rows[y]=random()%1024;
        compare(make_map(rows));++checked;
    }
    using Candidate=m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,B::FastTerrainEvaluation,B::Search>;
    for(int held:{-2,-1,4}) {
        B::RootSnapshot root;root.field.initialize(10,20);root.field.clear();root.next={2,4,0,3,5,1};root.hold=held;root.step=1;root.pose=B::host_pose(*owner.context()->generate('L'));
        A::PlainEngine plain(owner.context());Candidate candidate(owner.context());A::configure_native(plain,root);A::configure_native(candidate,root);
        for(int i=0;i<512;++i){const auto a=A::native_pass(plain,root);const auto b=A::native_pass(candidate,root);assert(a.target==b.target&&a.change_hold==b.change_hold&&a.status.value==b.status.value);}
    }
    std::cout<<"fast terrain exact random boards="<<checked<<";512-pass native controls passed\n";
    return 0;
}
} // namespace TetrTests::bot_chatgpt_fast_terrain_test
