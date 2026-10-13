#include "spin_paths.h"
#include "rule_tetr_srs_plus.h"
#include <fstream>
#include <iomanip>
#include <iostream>

namespace A = Bot_chatgpt::Analysis;
void write_state(std::optional<A::State>& state, A::SpinPaths& probe, const Bot_chatgpt::RootSnapshot& root) {
    if (!state) { std::cout << "null"; return; }
    probe.verify(root, *state);
    const auto& s = *state;
    std::cout << "{\"verified\":true,\"depth\":" << s.path.size()
        << ",\"spins\":" << s.spins << ",\"broken\":" << s.broken
        << ",\"attack\":" << s.status.attack << ",\"b2b\":" << s.status.b2b
        << ",\"roof\":" << s.map.roof << ",\"peak_roof\":" << s.peakRoof
        << ",\"holes\":" << Bot_chatgpt::covered_empty_cells(s.map)
        << ",\"incoming\":" << s.status.incoming << ",\"value\":" << s.status.value
        << ",\"risk\":" << s.status.pathRisk << ",\"reward\":" << s.status.reward
        << ",\"board\":[";
    for (int y = 0; y < 40; ++y) std::cout << (y ? "," : "") << s.map.row[y];
    std::cout << "],\"path\":[";
    for (std::size_t i = 0; i < s.path.size(); ++i) {
        const auto& step = s.path[i];
        const auto pose = Bot_chatgpt::host_pose(*step.landing.node);
        std::cout << (i ? ",{" : "{") << "\"piece\":\"" << A::types[step.choice.piece]
            << "\",\"hold\":" << step.choice.swap << ",\"cursor\":" << step.choice.cursor
            << ",\"held_after\":" << step.choice.hold
            << ",\"row\":" << pose.position.row << ",\"col\":" << pose.position.col
            << ",\"rotation\":" << pose.rotation.index() << ",\"spin\":" << step.landing.spin
            << ",\"mini\":" << step.landing.mini << ",\"lines\":" << step.lines
            << ",\"attack\":" << step.attack << ",\"b2b\":" << step.b2b << ",\"combo\":" << step.combo
            << ",\"incoming\":" << step.incoming << ",\"roof\":" << step.roof << ",\"holes\":" << step.holes
            << ",\"value\":" << step.value << ",\"reward\":" << step.reward << ",\"risk\":" << step.risk
            << ",\"production_geometry\":" << step.productionGeometry
            << ",\"production_spin_variant\":" << step.productionSpin << '}';
    }
    std::cout << "]}";
}
void write_result(A::Result& r, A::SpinPaths& probe, const Bot_chatgpt::RootSnapshot& root) {
    std::cout << "{\"reached_depth\":" << r.reachedDepth << ",\"live_matched\":" << r.liveMatched
        << ",\"work\":{\"expanded\":" << r.work.expanded << ",\"evaluated\":" << r.work.evaluated
        << ",\"break_cuts\":" << r.work.brokenCuts << ",\"unknown_rise_cuts\":" << r.work.unknownRiseCuts
        << ",\"dead_cuts\":" << r.work.deadCuts << ",\"preview_cuts\":" << r.work.previewCuts
        << ",\"duplicates\":" << r.work.duplicates << ",\"beam_cuts\":" << r.work.beamCuts
        << "},\"best\":";
    write_state(r.best, probe, root);
    std::cout << ",\"two_spins\":"; write_state(r.twoSpins, probe, root);
    std::cout << ",\"prefix\":"; write_state(r.prefix, probe, root);
    std::cout << '}';
}

int main(int argc, char** argv) try {
    if (argc != 4) throw std::runtime_error("usage: spin_probe ROOTS_LOG DEPTH WIDTH_PER_SPIN_BUCKET");
    const int depth = Bot_chatgpt::root_integer(argv[2]), width = Bot_chatgpt::root_integer(argv[3]);
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Bot_chatgpt::Evaluation, Bot_chatgpt::Search>;
    Engine geometry;
    if (!geometry.prepare(10, 40)) return 2;
    A::SpinPaths probe(geometry.context().get());
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("Cannot read roots log");
    int count = 0;
    std::cout << std::setprecision(17);
    for (std::string line; std::getline(input, line);) {
        const auto root = Bot_chatgpt::RootSnapshot::parse(line);
        std::map<std::string, std::string> fields;
        std::istringstream words(line);
        for (std::string word; words >> word;) {
            const auto split = word.find('=');
            if (split != word.npos) fields.emplace(word.substr(0, split), word.substr(split + 1));
        }
        const auto number = [&](const char* key) { return Bot_chatgpt::root_integer(fields.at(key)); };
        const A::LiveMove live{root.liveSelected, root.liveRow, root.liveCol, root.liveRotation,
            number("audit_spin"), number("audit_mini"), number("audit_lines"), number("audit_attack"), number("audit_b2b")};
        if ((live.spin != 0 && live.spin != 1) || (live.mini != 0 && live.mini != 1) ||
            live.lines < 0 || live.lines > 4 || live.attack < 0 || live.b2b < 0)
            throw std::runtime_error("Invalid observed move");
        auto forced = probe.run(root, live, depth, width, A::Mode::LiveFirst);
        auto free = probe.run(root, live, depth, width, A::Mode::Free);
        auto keep = probe.run(root, live, depth, width, A::Mode::Preserve);
        std::cout << "{\"pieces\":" << root.status.placedPieces << ",\"root_b2b\":" << root.status.b2b
            << ",\"horizon\":" << depth << ",\"width_per_bucket\":" << width << ",\"live_first\":";
        write_result(forced, probe, root);
        std::cout << ",\"free\":"; write_result(free, probe, root);
        std::cout << ",\"preserve\":"; write_result(keep, probe, root);
        std::cout << "}\n" << std::flush;
        ++count;
    }
    if (!count) throw std::runtime_error("Empty roots log");
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
}
