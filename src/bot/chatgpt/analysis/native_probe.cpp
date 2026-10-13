// P1: same production evaluator/enumerator, cold native tree, two checkpoints.
#include "native_trace.h"
#include "spin_paths.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

namespace A = Bot_chatgpt::Analysis;
using Clock = std::chrono::steady_clock;
using Bot_chatgpt::RootSnapshot;

template<class Result>
void answer(std::ostream& out, const Result& r, const RootSnapshot& root) {
    if (!r.target.node) throw std::runtime_error("Missing target");
    const auto p = Bot_chatgpt::host_pose(*r.target.node);
    out << "\"move\":[" << Bot_chatgpt::piece_index(r.target->status.t) << ',' << p.position.row
        << ',' << p.position.col << ',' << p.rotation.index() << ',' << r.target.spin << ',' << r.target.mini
        << "],\"hold\":" << r.change_hold << ",\"value\":" << r.status.value
        << ",\"depth\":" << r.status.placedPieces - root.status.placedPieces
        << ",\"attack\":" << r.status.attack << ",\"b2b\":" << r.status.b2b
        << ",\"risen\":" << r.status.risen << ",\"dead\":" << r.status.dead;
}

void coverage(std::ostream& out, const m_tetris::TetrisContext* context, const RootSnapshot& root) {
    A::Moves full(context); Bot_chatgpt::Search production;
    Bot_chatgpt::Search::Config config{root.status.previousCleared}; production.init(context, &config);
    const auto map = BotNativeHost::make_map(root.field);
    int count = 0, missed = 0;
    for (auto choice : A::choices(root, 0, root.hold)) {
        const auto* start = choice.swap ? Bot_chatgpt::spawn_node(context, map, A::types[choice.piece], root.status.previousCleared)
            : Bot_chatgpt::native_pose(context, A::types[choice.piece], root.pose);
        std::set<std::size_t> complete, partial;
        for (auto l : full.enumerate(map, start)) complete.insert(l->index_filtered);
        for (auto l : *production.search(map, start, 0)) partial.insert(l->index_filtered);
        for (auto geometry : complete) { ++count; missed += !partial.count(geometry); }
    }
    out << "\"root_geometry\":" << count << ",\"missing_root_geometry\":" << missed;
}

int main(int argc, char** argv) try {
    if (argc != 5) throw std::runtime_error("usage: native_probe ROOTS timed|passes LOW HIGH");
    const bool timed = std::string(argv[2]) == "timed";
    if (!timed && std::string(argv[2]) != "passes") throw std::runtime_error("Unknown budget kind");
    const int low = Bot_chatgpt::root_integer(argv[3]), high = Bot_chatgpt::root_integer(argv[4]);
    if (low < 1 || high <= low || high > 10000) throw std::runtime_error("Invalid budget");
    A::PlainEngine geometry; if (!geometry.prepare(10, 40)) return 2;
    std::ifstream input(argv[1]); if (!input) throw std::runtime_error("Cannot read root file");
    std::cout << std::setprecision(17);
    int sampled = 0;
    for (std::string line; std::getline(input, line);) {
        if (line.find("TETR_BOT_DIAGNOSTIC side=first ") != 0) continue;
        const auto root = RootSnapshot::parse(line);
        A::TracedEngine engine(geometry.context()); A::configure_native(engine, root);
        auto* trace = engine.ai(); trace->root = &root;
        trace->records.reserve(200000);
        engine.status()->held = root.hold;
        struct Snapshot { A::TracedEngine::RunResult best; std::size_t pass, evaluations; double ms; };
        std::vector<Snapshot> snapshots;
        const auto started = Clock::now();
        for (std::size_t pass = 1; ; ++pass) {
            if (engine.memory_usage() + trace->records.capacity() * sizeof(A::NativeTrace::Record) > (512ull << 20))
                throw std::runtime_error("Offline diagnostic memory cap exceeded");
            trace->pass = pass;
            const auto best = A::native_pass(engine, root);
            const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            const double work = timed ? ms : double(pass);
            if (work >= (snapshots.empty() ? low : high)) {
                snapshots.push_back({best, pass, trace->records.size() - 1, ms});
                if (snapshots.size() == 2) break;
            }
        }
        std::cout << "{\"pieces\":" << root.status.placedPieces << ',';
        coverage(std::cout, geometry.context().get(), root);
        std::cout << ",\"traced\":[";
        for (std::size_t s = 0; s < snapshots.size(); ++s) {
            const auto& shot = snapshots[s];
            if (!A::first_legal(geometry.context().get(), root, shot.best)) throw std::runtime_error("Unreachable first move");
            if (s) std::cout << ',';
            std::cout << '{'; answer(std::cout, shot.best, root);
            std::cout << ",\"legal\":true,\"passes\":" << shot.pass << ",\"evaluations\":" << shot.evaluations
                << ",\"ms\":" << shot.ms << ",\"path\":[";
            const auto path = trace->path(shot.best.status.trace);
            bool comma = false;
            for (auto id : path) {
                if (comma) std::cout << ','; comma = true;
                const auto& n = trace->records[id]; const auto pose = Bot_chatgpt::host_pose(*n.landing.node);
                std::cout << "{\"id\":" << id << ",\"parent\":" << n.parent << ",\"depth\":" << n.depth
                    << ",\"created_pass\":" << n.createdPass << ",\"expanded_pass\":" << n.expandedPass
                    << ",\"sibling_rank\":" << trace->sibling_rank(id)
                    << ",\"move\":[" << Bot_chatgpt::piece_index(n.landing->status.t) << ',' << pose.position.row
                    << ',' << pose.position.col << ',' << pose.rotation.index() << ',' << n.landing.spin << ',' << n.landing.mini
                    << "],\"hold\":" << n.hold << ",\"lines\":" << n.lines << ",\"value\":" << n.status.value
                    << ",\"attack\":" << n.status.attack << ",\"b2b\":" << n.status.b2b << ",\"roof\":" << n.roof
                    << ",\"holes\":" << n.holes << ",\"risen\":" << n.status.risen << ",\"dead\":" << n.status.dead
                    << ",\"risk\":" << n.status.pathRisk << ",\"next_mismatch\":" << n.nextMismatch
                    << ",\"hidden_active\":" << n.hiddenActive << '}';
            }
            std::cout << "]}";
        }
        // Uninstrumented cold-root decisions measure any trace-overhead effect.
        std::cout << "],\"plain\":[";
        for (int budget : {low, high}) {
            A::PlainEngine plain(geometry.context()); A::configure_native(plain, root);
            const auto start = Clock::now();
            A::PlainEngine::RunResult result;
            if (timed) result = A::native_pass(plain, root, budget);
            else for (int i = 0; i < budget; ++i) result = A::native_pass(plain, root);
            const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (!A::first_legal(geometry.context().get(), root, result)) throw std::runtime_error("Unreachable plain first move");
            if (budget != low) std::cout << ',';
            std::cout << '{'; answer(std::cout, result, root);
            std::cout << ",\"legal\":true,\"ms\":" << ms << '}';
        }
        std::cout << "]}\n" << std::flush;
        ++sampled;
    }
    if (!sampled) throw std::runtime_error("No roots");
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
