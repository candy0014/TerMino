#include "../src/bot/chatgpt/spin_quota_engine.h"
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
        A::PlainEngine plain(geometry.context());
        Bot_chatgpt::SpinQuotaEngine adapter(geometry.context(), 0);
        A::configure_native(plain, root); A::configure_native(adapter, root);
        for (int revision = 0; revision < 2; ++revision) {
            if (revision) {
                root.status.b2b = 4; root.status.combo = 2;
                plain.update(); adapter.update();
                A::configure_native(plain, root); A::configure_native(adapter, root);
            }
            for (int i = 0; i < 80; ++i) {
                const auto a = A::native_pass(plain, root);
                const auto b = A::native_pass(adapter, root);
                assert(a.target == b.target && a.change_hold == b.change_hold);
                assert(std::abs(a.status.value - b.status.value) < 1e-9);
                assert(a.status.attack == b.status.attack && a.status.b2b == b.status.b2b);
                assert(a.status.placedPieces == b.status.placedPieces);
                assert(adapter.statistics().probes == 0);
                if (i == 79) assert(A::first_legal(geometry.context().get(), root, b));
            }
        }
    }
    const auto setup = Bot_chatgpt::RootSnapshot::parse(
        "TETR_BOT_DIAGNOSTIC trace_version=1 root_hold=4 root_row=-2 root_col=4 root_rotation=0 "
        "root_b2b=1 root_combo=2 root_garbage_height=13 pieces=131 incoming=0 ready=0 root_cleared=1 "
        "opener_double=1 forecast_step=1 root_next=2,5,4,6,2,3 root_packets=- selected=4 row=6 col=6 rotation=0 "
        "board=1021,895,767,1007,959,895,895,895,895,895,895,895,767,911,256,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
    Bot_chatgpt::SpinQuotaEngine quota(geometry.context());
    A::configure_native(quota, setup);
    const auto result = A::native_pass(quota, setup, 100);
    assert(A::first_legal(geometry.context().get(), setup, result));
    assert(quota.statistics().probes > 0 && quota.statistics().verifiedSpinExits > 0);
    assert(quota.statistics().continuationExpansions > 0);
    assert(result.status.placedPieces - setup.status.placedPieces == 7);
    // End-of-slice work remains on the original frontier; a zero-ms native pass
    // can safely resume the same tree with unchanged evaluation and horizon.
    const auto resumed = A::native_pass(quota, setup, 0);
    assert(A::first_legal(geometry.context().get(), setup, resumed));
    assert(resumed.status.value >= result.status.value - 1e-9);
    auto pressure = setup;
    pressure.field.clear(); pressure.status = {};
    pressure.status.incoming = pressure.status.readyIncoming = 20;
    pressure.packets = {{20, -1}};
    Bot_chatgpt::SpinQuotaEngine pressured(geometry.context());
    A::configure_native(pressured, pressure);
    const auto safe = A::native_pass(pressured, pressure, 20);
    assert(A::first_legal(geometry.context().get(), pressure, safe));
    assert(pressured.statistics().probes == 0); // No fake Spin exits after unknown garbage rise.
    bool rejected = false;
    try { Bot_chatgpt::SpinQuotaEngine invalid(geometry.context(), 21); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
}
