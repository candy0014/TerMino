#include "../src/bot/chatgpt/root_coverage_engine.h"
#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <iostream>
#include <map>

namespace B=Bot_chatgpt;
namespace A=B::Analysis;
namespace Bot_chatgpt {
struct RootCoverageTestAccess {
    static void promotion(RootCoverageEngine&e) {
        CoverageSearch full;Search::Config config;full.init(e.context().get(),&config);
        decltype(e.root_) selected=nullptr;
        std::size_t before=0;
        for(auto*child=e.root_->children;child;child=child->children_next) {
            if(!child->children)continue;
            const auto*start=spawn_node(e.context().get(),child->map,'O',false);
            if(!start)continue;
            std::size_t count=0;for(auto*n=child->children;n;n=n->children_next)++count;
            if(count<full.search(child->map,start,0)->size()){selected=child;before=count;break;}
        }
        assert(selected);const auto map=selected->map;*e.status()=selected->status.get_raw();
        const auto*start=spawn_node(e.context().get(),map,'O',false);
        const auto expected=*full.search(map,start,0);
        e.update();e.run(map,start,"TLSJZ",5,0);
        assert(e.root_==selected); // Exercise actual native subtree promotion.
        std::map<std::size_t,Landing>actual;
        for(auto*child=e.root_->children;child;child=child->children_next) {
            assert(actual.emplace(child->identity->index_filtered,child->identity).second);
        }
        assert(actual.size()==expected.size()&&actual.size()>before);
        for(const auto&landing:expected)assert(actual.at(landing->index_filtered)==landing);
        std::cout<<"promoted_root_landings="<<before<<"->"<<actual.size()<<'\n';
    }
};
}
int main() {
    A::PlainEngine owner;assert(owner.prepare(10,40));
    for(int held:{-2,-1,4}) {
        B::RootSnapshot root;root.field.initialize(10,20);root.field.clear();
        root.next={2,4,0,3,5,1};root.hold=held;root.step=1;
        root.pose=B::host_pose(*owner.context()->generate('L'));
        A::PlainEngine plain(owner.context());B::RootCoverageEngine disabled(owner.context(),false);
        for(int revision=0;revision<2;++revision) {
            if(revision){plain.update();disabled.update();root.status.b2b=4;root.status.combo=2;}
            A::configure_native(plain,root);A::configure_native(disabled,root);
            for(int pass=0;pass<100;++pass) {
                const auto a=A::native_pass(plain,root);
                const auto b=A::native_pass(disabled,root);
                assert(a.target==b.target&&a.change_hold==b.change_hold&&a.status.value==b.status.value);
            }
        }
    }
    B::RootSnapshot tower;tower.field.initialize(10,20);tower.field.clear();
    for(int y=15;y<20;++y)tower.field.set_cell(y,0,Runtime::BoardCell::garbage());
    tower.next={0,3};tower.hold=-2;tower.step=1;
    tower.pose=B::host_pose(*owner.context()->generate('I'));
    B::RootCoverageEngine candidate(owner.context());A::configure_native(candidate,tower);
    for(int pass=0;pass<100;++pass)A::native_pass(candidate,tower);
    B::RootCoverageTestAccess::promotion(candidate);
    B::RootCoverageSearch wrapper;B::Search original;B::CoverageSearch full;B::Search::Config config;
    wrapper.init(owner.context().get(),&config);original.init(owner.context().get(),&config);full.init(owner.context().get(),&config);
    const auto map=BotNativeHost::make_map(tower.field);
    for(char type:std::string("IJLOSTZ")) {
        const auto*start=owner.context()->generate(type);
        assert(*wrapper.search(map,start,0)==*full.search(map,start,0));
        for(int depth=1;depth<7;++depth)assert(*wrapper.search(map,start,depth)==*original.search(map,start,depth));
    }
    std::cout<<"root coverage native controls, root promotion, exact future-depth dispatch passed\n";
}
