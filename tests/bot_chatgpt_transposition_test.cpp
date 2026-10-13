#include "../src/bot/chatgpt/transposition_engine.h"
#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <iostream>
namespace B=Bot_chatgpt;
namespace A=B::Analysis;

namespace Bot_chatgpt {
struct TranspositionTestAccess {
    static void key_guards(TranspositionEngine&e) {
        auto*node=e.root_->children;while(node&&node->is_hold)node=node->children_next;assert(node);
        auto state=node->status.get_raw();
        const auto original=state;
        auto dominated=[&]{node->status.set(state);return e.dominated(node);};
        e.reset_table();assert(!e.dominated(node));assert(e.dominated(node));
        // These are different futures despite equal occupancy. Each must
        // survive lookup; a high score cannot mask queue/pressure differences.
        auto check=[&](auto change){state=original;change();assert(!dominated());e.reset_table();state=original;assert(!dominated());};
        check([&]{++state.combo;});check([&]{++state.b2b;});
        check([&]{++state.incoming;});check([&]{++state.readyIncoming;});
        check([&]{++state.risen;});check([&]{++state.garbageHeight;});
        check([&]{state.previousCleared=!state.previousCleared;});
        state=original; state.attackRules.halfClearEnabled=true;
        e.reset_table(); assert(!dominated());
        state.pieceRows[0]=1; assert(!dominated());
        e.reset_table(); state=original; assert(!dominated());
        const auto oldHold=node->parent->hold;node->parent->hold=oldHold=='I'?'T':'I';
        assert(!e.dominated(node));node->parent->hold=oldHold;
        e.reset_table();state=original;assert(!dominated());
        state.reward+=10;state.pathRisk+=20;assert(!dominated()); // no componentwise dominance
        state=original;state.reward-=10;state.pathRisk+=10;assert(dominated());
        state=original;e.update();assert(!dominated()); // new real root cannot inherit entries
    }
};
}

int main() {
    A::PlainEngine owner;assert(owner.prepare(10,40));
    for(int held:{-2,-1,4}) {
        B::RootSnapshot root;root.field.initialize(10,20);root.field.clear();
        root.next={2,4,0,3,5,1};root.hold=held;root.step=1;
        root.pose=B::host_pose(*owner.context()->generate('L'));
        A::PlainEngine plain(owner.context());B::TranspositionEngine disabled(owner.context(),false);
        for(int revision=0;revision<2;++revision) {
            if(revision){plain.update();disabled.update();root.status.b2b=4;root.status.combo=2;}
            A::configure_native(plain,root);A::configure_native(disabled,root);
            for(int i=0;i<100;++i) {
                const auto a=A::native_pass(plain,root),b=A::native_pass(disabled,root);
                assert(a.target==b.target&&a.change_hold==b.change_hold&&a.status.value==b.status.value);
                assert(a.status.attack==b.status.attack&&a.status.placedPieces==b.status.placedPieces);
            }
            assert(disabled.statistics().pruned==0);
        }
        B::TranspositionEngine candidate(owner.context());A::configure_native(candidate,root);
        for(int i=0;i<200;++i){const auto b=A::native_pass(candidate,root);assert(A::first_legal(owner.context().get(),root,b));}
        assert(candidate.statistics().pruned>0);
        if(held==4)B::TranspositionTestAccess::key_guards(candidate);
    }
    // On a fully explored two-piece game tree, pruning equivalent prefixes
    // must preserve the optimum. This checks outcomes, not the table layout.
    for(int family=0;family<7;++family) {
        B::RootSnapshot root;root.field.initialize(10,20);root.field.clear();
        root.next={family,(family+3)%7};root.hold=-2;root.step=1;
        root.pose=B::host_pose(*owner.context()->generate("IJLOSTZ"[family]));
        A::PlainEngine plain(owner.context());B::TranspositionEngine candidate(owner.context());
        A::configure_native(plain,root);A::configure_native(candidate,root);
        A::PlainEngine::RunResult a,b;
        for(int i=0;i<800;++i){a=A::native_pass(plain,root);b=A::native_pass(candidate,root);}
        assert(a.status.value==b.status.value&&A::first_legal(owner.context().get(),root,b));
    }
    std::cout<<"transposition scheduler controls and exhaustive short trees passed; table_bytes="<<B::TranspositionEngine::table_bytes()<<'\n';
}
