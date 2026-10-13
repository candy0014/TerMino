#include "../src/bot/chatgpt/garbage_rollout_engine.h"
#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <iostream>

namespace B=Bot_chatgpt;
namespace A=B::Analysis;
namespace Bot_chatgpt {
struct GarbageRolloutTestAccess {
    static void transitions(GarbageRolloutEngine&e) {
        e.packetEnds_={2,8};e.totalIncoming_=8;
        for(int scenario=0;scenario<10;++scenario) {
            GarbageRolloutEngine::State s;
            e.write_row(s.map,0,3);e.write_row(s.map,1,1);
            s.status.garbageHeight=2;s.status.risen=8;
            e.insert_garbage(s,8,0,scenario);
            for(int y=0;y<8;++y)assert(s.map.row[y]==(1023u^(1u<<((scenario+(y<6?3:0))%10))));
            assert(s.map.row[8]==3&&s.map.row[9]==1&&s.map.count==75&&s.map.roof==10);
            assert(s.status.garbageHeight==10&&s.status.risen==0);
        }
        // A physical I placement can now dig a newly received straight packet.
        // The original relative-board model cannot represent these four rows.
        e.packetEnds_={8};e.totalIncoming_=8;e.visible_="OITLSZ";
        e.evaluator_.set_garbage_forecast({{8,-1}},1);
        GarbageRolloutEngine::State root;root.status.incoming=root.status.readyIncoming=8;
        root.status.placedPieces=30;
        std::optional<Landing> o;
        for(auto m:*e.search_.search(root.map,e.context()->generate('O'),0))if(m->col==0)o=m;
        assert(o);
        const GarbageRolloutEngine::Choice first{*o,1,' ',false};
        const auto risen=e.advance(root,first,0,5);
        assert(risen.status.garbageHeight==8&&risen.status.incoming==0&&risen.status.risen==0&&!risen.status.dead);
        std::optional<Landing> i;
        for(auto m:*e.search_.search(risen.map,e.context()->generate('I'),1))if(m->col==5&&m->height==4)i=m;
        assert(i);
        const auto dug=e.advance(risen,{*i,2,' ',false},1,5);
        assert(dug.status.garbageHeight==4&&dug.status.attack==5&&dug.status.b2b==1&&dug.map.roof==6);
        assert(dug.status.placedPieces==32&&!dug.status.dead);
        const auto saved=e.visible_;e.visible_="OI";
        const auto shortPath=e.rollout(root,first,5,GarbageRolloutEngine::Clock::time_point::max());
        assert(shortPath&&e.visible_=="OI"); // no hidden continuation appended
        assert(!e.rollout(root,first,5,GarbageRolloutEngine::Clock::time_point::min()));
        e.visible_=saved;
    }
};
}
int main() {
    A::PlainEngine owner;assert(owner.prepare(10,40));
    B::GarbageRolloutEngine candidate(owner.context());B::GarbageRolloutTestAccess::transitions(candidate);
    B::GarbageBeamEngine beam(owner.context());B::GarbageRolloutTestAccess::transitions(beam);
    for(int held:{-2,-1,4}) {
        B::RootSnapshot root;root.field.initialize(10,20);root.field.clear();
        root.next={2,4,0,3,5,1};root.hold=held;root.step=1;root.pose=B::host_pose(*owner.context()->generate('L'));
        A::PlainEngine plain(owner.context());B::GarbageRolloutEngine control(owner.context());
        for(int revision=0;revision<2;++revision) {
            if(revision){plain.update();control.update();root.status.b2b=4;root.status.combo=2;}
            A::configure_native(plain,root);A::configure_native(control,root);
            for(int pass=0;pass<100;++pass) {
                const auto a=A::native_pass(plain,root),b=A::native_pass(control,root);
                assert(a.target==b.target&&a.change_hold==b.change_hold&&a.status.value==b.status.value);
            }
        }
    }
    std::cout<<"garbage rollout FIFO/physical dig/visible horizon and no-pressure native controls passed\n";
}
