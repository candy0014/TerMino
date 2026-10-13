// Offline cold-root comparison. Not linked into the playing BOT.
#include "root_snapshot.h"
#include "fixed_root_search.h"
#include "rule_tetr_srs_plus.h"
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) try {
    if (argc != 5) throw std::runtime_error("usage: root_probe LOG ITERATIONS STRIDE MAX_ROOTS");
    const int iterations = Bot_chatgpt::root_integer(argv[2]);
    const int stride = Bot_chatgpt::root_integer(argv[3]);
    const int limit = Bot_chatgpt::root_integer(argv[4]);
    if (iterations < 1 || stride < 1 || limit < 1) throw std::runtime_error("Positive limits required");
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,
        Bot_chatgpt::Evaluation, Bot_chatgpt::Search>;
    Engine geometry;
    if (!geometry.prepare(10, 40)) return 2;
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("Cannot read root log");
    int index = 0, sampled = 0, lineNumber = 0;
    constexpr char types[] = "IJLOSTZ";
    for (std::string line; std::getline(input, line);) {
        ++lineNumber;
        if (line.find("TETR_BOT_DIAGNOSTIC side=first ") != 0) continue;
        if (index++ % stride) continue;
        const auto root = Bot_chatgpt::RootSnapshot::parse(line);
        const auto map = BotNativeHost::make_map(root.field);
        std::string preview;
        for (std::size_t i = 1; i < root.next.size(); ++i) preview += types[root.next[i]];
        for (double weight : {0., 80.}) {
            Engine engine(geometry.context()); // Fresh tree for each policy/root.
            engine.memory_limit(256ull << 20);
            *engine.status() = root.status;
            engine.ai()->set_covered_empty_weight(weight);
            engine.ai()->set_garbage_forecast(root.packets, root.step);
            engine.search_config()->rootPreviousCleared = root.status.previousCleared;
            const auto* start = Bot_chatgpt::native_pose(engine.context().get(), types[root.next[0]], root.pose);
            if (!start || !start->check(map)) throw std::runtime_error("Invalid actual start");
            const char held = root.hold >= 0 ? types[root.hold] : ' ';
            const auto run = [&] {
                return root.hold != -2 ? engine.run_hold(map, start, held, true, preview.data(), preview.size(), 0)
                    : engine.run(map, start, preview.data(), preview.size(), 0);
            };
            const auto best = Bot_chatgpt::fixed_root_passes(engine, run, iterations);
            if (!best.target.node) throw std::runtime_error("No replay target");
            const int selected = Bot_chatgpt::piece_index(best.target->status.t);
            const int expected = best.change_hold ? (root.hold >= 0 ? root.hold :
                (root.next.size() > 1 ? root.next[1] : -1)) : root.next[0];
            if (selected != expected) throw std::runtime_error("Unexpected HOLD selection");
            const bool swaps = best.change_hold && selected != root.next[0];
            const auto* spawn = swaps ? Bot_chatgpt::spawn_node(engine.context().get(), map,
                types[selected], root.status.previousCleared) : start;
            Piece::Pose answer; int kick = 0;
            const bool legal = spawn && BotNativeHost::reachable(best.target.node,
                Piece::get_definition(Piece::Type::from_index(selected)), root.field,
                Bot_chatgpt::host_pose(*spawn), best.target.spin, best.target.mini, answer, kick);
            auto after = map;
            const auto clears = best.target->attach(engine.context().get(), after);
            const auto pose = Bot_chatgpt::host_pose(*best.target.node);
            std::cout << std::setprecision(17) << "{\"line\":" << lineNumber
                << ",\"pieces\":" << root.status.placedPieces << ",\"weight\":" << weight
                << ",\"input_holes\":" << Bot_chatgpt::covered_empty_cells(map)
                << ",\"selected\":" << selected << ",\"row\":" << pose.position.row
                << ",\"col\":" << pose.position.col << ",\"rotation\":" << pose.rotation.index()
                << ",\"legal\":" << (legal ? "true" : "false")
                << ",\"spin\":" << best.target.spin << ",\"mini\":" << best.target.mini
                << ",\"lines\":" << clears << ",\"holes_after\":" << Bot_chatgpt::covered_empty_cells(after)
                << ",\"roof_after\":" << after.roof << ",\"projected_attack\":" << best.status.attack
                << ",\"projected_b2b\":" << best.status.b2b << ",\"projected_risen\":" << best.status.risen
                << ",\"projected_dead\":" << best.status.dead
                << ",\"projected_depth\":" << best.status.placedPieces - root.status.placedPieces
                << ",\"passes\":" << iterations << ",\"tree_bytes\":" << engine.memory_usage()
                << ",\"value\":" << best.status.value << "}\n";
        }
        if (++sampled == limit) break;
    }
    if (!sampled) throw std::runtime_error("No traced roots sampled");
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
