#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <cmath>

namespace A = Bot_chatgpt::Analysis;
int main() {
    A::PlainEngine geometry; assert(geometry.prepare(10, 40));
    for (int held : {-2, -1, 4}) {
        Bot_chatgpt::RootSnapshot root;
        root.field.initialize(10, 20); root.field.clear();
        root.next = {2, 4, 0, 3, 5, 1}; root.hold = held; root.step = 1;
        root.pose = Bot_chatgpt::host_pose(*geometry.context()->generate('L'));
        root.status.openerDoubleCancelEnabled = true;
        A::PlainEngine plain(geometry.context()); A::TracedEngine traced(geometry.context());
        A::configure_native(plain, root); A::configure_native(traced, root);
        traced.ai()->root = &root; traced.status()->held = held;
        for (std::size_t pass = 1; pass <= 80; ++pass) {
            traced.ai()->pass = pass;
            const auto a = A::native_pass(plain, root);
            const auto b = A::native_pass(traced, root);
            assert(a.target == b.target && a.change_hold == b.change_hold);
            assert(std::abs(a.status.value - b.status.value) < 1e-9);
            assert(a.status.attack == b.status.attack && a.status.b2b == b.status.b2b);
            assert(a.status.risen == b.status.risen && a.status.dead == b.status.dead);
            assert(a.status.placedPieces == b.status.placedPieces);
            if (pass == 1 || pass == 80) {
                assert(A::first_legal(geometry.context().get(), root, b));
                const auto path = traced.ai()->path(b.status.trace);
                assert(!path.empty() && path.size() == std::size_t(b.status.placedPieces));
                for (auto id : path) {
                    const auto& record = traced.ai()->records[id];
                    assert(record.parent < id && record.createdPass <= pass);
                    assert(record.expandedPass == 0 || record.expandedPass >= record.createdPass);
                    assert(traced.ai()->sibling_rank(id) >= 1);
                }
            }
        }
        assert(traced.ai()->records.size() > 100);
    }
}
