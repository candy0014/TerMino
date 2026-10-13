#include "../src/bot/chatgpt/spin_beam_quota_engine.h"
#include "../src/bot/chatgpt/analysis/native_trace.h"
#include <cassert>
#include <cmath>
#include <iostream>

namespace A = Bot_chatgpt::Analysis;

namespace Bot_chatgpt {
struct SpinBeamQuotaTestAccess {
    static SpinBeamQuotaEngine::RunResult fixed_work(SpinBeamQuotaEngine& engine,
        const RootSnapshot& root, std::size_t normalPasses, std::size_t probes) {
        assert(root.hold != -2 && normalPasses > 0);
        // The real zero-ms entry point initializes/reuses the root and performs
        // exactly one normal pass. Continue its same tree and quota routines.
        (void)A::native_pass(engine, root, 0);
        for (std::size_t i = 1; i < normalPasses; ++i) {
            ++engine.stats_.normalPasses;
            if (engine.root_->template run<true>(&engine.local_)) break;
        }
        for (std::size_t i = 0; i < probes; ++i)
            if (!engine.probe_opportunity<true>(SpinBeamQuotaEngine::Clock::time_point::max())) break;
        const auto best = engine.root_->get_best(&engine.local_);
        return best.first ? SpinBeamQuotaEngine::RunResult(best, best.first->is_hold)
                          : SpinBeamQuotaEngine::RunResult(false);
    }

    static void expired_probe_preserves_frontier(SpinBeamQuotaEngine& engine) {
        std::vector<std::size_t> waiting, expanded;
        for (const auto& queue : engine.local_.wait) waiting.push_back(queue.size());
        for (const auto& queue : engine.local_.sort) expanded.push_back(queue.size());
        const auto oldStats = engine.stats_;
        const auto oldBest = engine.root_->get_best(&engine.local_);
        // An already-expired slice must return the removed work to precisely
        // its original frontier, not discard it or mark it expanded.
        assert(engine.probe_opportunity<true>(SpinBeamQuotaEngine::Clock::time_point::min()));
        assert(engine.stats_.probes == oldStats.probes + 1);
        assert(engine.stats_.extraExpansions == oldStats.extraExpansions);
        assert(engine.stats_.verifiedSpinExits == oldStats.verifiedSpinExits);
        for (std::size_t i = 0; i < waiting.size(); ++i)
            assert(engine.local_.wait[i].size() == waiting[i]);
        for (std::size_t i = 0; i < expanded.size(); ++i)
            assert(engine.local_.sort[i].size() == expanded[i]);
        const auto best = engine.root_->get_best(&engine.local_);
        assert(best == oldBest);
    }
};
} // namespace Bot_chatgpt

int main() {
    A::PlainEngine geometry; assert(geometry.prepare(10, 40));
    for (int held : {-2, -1, 4}) {
        Bot_chatgpt::RootSnapshot root;
        root.field.initialize(10, 20); root.field.clear();
        root.next = {2, 4, 0, 3, 5, 1}; root.hold = held; root.step = 1;
        root.pose = Bot_chatgpt::host_pose(*geometry.context()->generate('L'));
        root.status.openerDoubleCancelEnabled = true;
        A::PlainEngine plain(geometry.context());
        Bot_chatgpt::SpinBeamQuotaEngine adapter(geometry.context(), 0);
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
    Bot_chatgpt::SpinBeamQuotaEngine quota(geometry.context());
    A::configure_native(quota, setup);
    // Mechanism assertions require a fixed amount of scheduler work. A 100-ms
    // call can lose its whole 20-ms quota slice to an OS scheduling pause.
    const auto result = Bot_chatgpt::SpinBeamQuotaTestAccess::fixed_work(quota, setup, 1000, 128);
    const auto mechanism = quota.statistics();
    std::cout << "fixed normal=" << mechanism.normalPasses << " probes=" << mechanism.probes
              << " spin_exits=" << mechanism.verifiedSpinExits
              << " continuation=" << mechanism.continuationExpansions
              << " preparatory=" << mechanism.preparatoryExpansions
              << " model_leaves=" << mechanism.modelLeaves
              << " peak_beam=" << mechanism.peakBeam << std::endl;
    assert(A::first_legal(geometry.context().get(), setup, result));
    assert(quota.statistics().probes > 0 && quota.statistics().verifiedSpinExits > 0);
    assert(quota.statistics().continuationExpansions > 0);
    assert(quota.statistics().preparatoryExpansions > 0);
    assert(quota.statistics().modelLeaves > 0);
    assert(quota.statistics().peakBeam > 1 && quota.statistics().peakBeam <= 4);
    assert(result.status.placedPieces - setup.status.placedPieces == 7);
    Bot_chatgpt::SpinBeamQuotaTestAccess::expired_probe_preserves_frontier(quota);
    // End-of-slice work remains on the original frontier; a zero-ms native pass
    // can safely resume the same tree with unchanged evaluation and horizon.
    const auto resumed = A::native_pass(quota, setup, 0);
    assert(A::first_legal(geometry.context().get(), setup, resumed));
    assert(resumed.status.value >= result.status.value - 1e-9);
    auto pressure = setup;
    pressure.field.clear(); pressure.status = {};
    pressure.status.incoming = pressure.status.readyIncoming = 20;
    pressure.packets = {{20, -1}};
    Bot_chatgpt::SpinBeamQuotaEngine pressured(geometry.context());
    A::configure_native(pressured, pressure);
    const auto safe = Bot_chatgpt::SpinBeamQuotaTestAccess::fixed_work(pressured, pressure, 80, 32);
    assert(A::first_legal(geometry.context().get(), pressure, safe));
    assert(pressured.statistics().probes == 0); // No fake Spin exits after unknown garbage rise.
    bool rejected = false;
    try { Bot_chatgpt::SpinBeamQuotaEngine invalid(geometry.context(), 21); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
}
