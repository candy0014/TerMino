// Offline conditional-placement export. This file is not linked into a BOT.
// Input: RootSnapshot-compatible diagnostic lines, with one selected family,
// HOLD disabled and no pending garbage, plus these label-only fields:
// sample_id=N teacher_cells=x:y,x:y,x:y,x:y teacher_after=40,row,masks,...
// teacher_lines=N teacher_spin=0|1 teacher_mini=0|1.
// Coordinates/masks are native: x=0..9, y=0..39 increasing from the bottom.
// Labels are used ONLY to identify the demonstrated outcome, never to compute
// an alternative's features/reward. No queue beyond the selected family exists.

#include "spin_paths.h"
#include "../terrain_bridge_model.h"
#include "rule_tetr_srs_plus.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace B = Bot_chatgpt;
namespace A = Bot_chatgpt::Analysis;

namespace {
using Cells = std::array<int, 4>; // Sorted native y*10+x.
using Rows = std::array<std::uint32_t, 40>;
constexpr B::TerrainWeights selectorWeights{16, 72, 48, 16, 140, 620, 80, 80, 45, 70};

struct Label {
    int sampleId = 0, lines = 0;
    bool spin = false, mini = false;
    Cells cells{};
    Rows after{};
};

Label label_from(const std::string& line) {
    std::istringstream stream(line);
    std::string token;
    stream >> token;
    std::map<std::string, std::string> fields;
    while (stream >> token) {
        const auto equal = token.find('=');
        if (equal == std::string::npos ||
            !fields.emplace(token.substr(0, equal), token.substr(equal + 1)).second)
            throw std::runtime_error("Malformed/duplicate label field");
    }
    Label label;
    label.sampleId = B::root_integer(fields.at("sample_id"));
    label.lines = B::root_integer(fields.at("teacher_lines"));
    const int spin = B::root_integer(fields.at("teacher_spin"));
    const int mini = B::root_integer(fields.at("teacher_mini"));
    if (label.sampleId < 0 || label.lines < 0 || label.lines > 4 ||
        spin < 0 || spin > 1 || mini < 0 || mini > spin)
        throw std::runtime_error("Invalid teacher identity/clear/spin label");
    label.spin = spin;
    label.mini = mini;
    const auto cells = B::split_root(fields.at("teacher_cells"), ',');
    if (cells.size() != 4) throw std::runtime_error("Teacher must occupy exactly four cells");
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto xy = B::split_root(cells[i], ':');
        if (xy.size() != 2) throw std::runtime_error("Invalid teacher cell");
        const int x = B::root_integer(xy[0]), y = B::root_integer(xy[1]);
        if (x < 0 || x >= 10 || y < 0 || y >= 40)
            throw std::runtime_error("Teacher cell outside native board");
        label.cells[i] = y * 10 + x;
    }
    std::sort(label.cells.begin(), label.cells.end());
    if (std::adjacent_find(label.cells.begin(), label.cells.end()) != label.cells.end())
        throw std::runtime_error("Duplicate teacher cell");
    const auto after = B::split_root(fields.at("teacher_after"), ',');
    if (after.size() != 40) throw std::runtime_error("Teacher after-board requires 40 rows");
    for (std::size_t y = 0; y < after.size(); ++y) {
        const int mask = B::root_integer(after[y]);
        if (mask < 0 || mask > 1023) throw std::runtime_error("Invalid teacher after-board mask");
        label.after[y] = static_cast<std::uint32_t>(mask);
    }
    return label;
}

Cells cells_of(const B::Landing& landing) {
    Cells cells{};
    std::size_t count = 0;
    for (int y = 0; y < landing->height; ++y)
        for (int x = 0; x < 10; ++x)
            if (landing->data[y] & (1u << x)) {
                if (count == cells.size()) throw std::runtime_error("Native piece has extra cells");
                cells[count++] = (landing->row + y) * 10 + x;
            }
    if (count != cells.size()) throw std::runtime_error("Native piece does not have four cells");
    std::sort(cells.begin(), cells.end());
    return cells;
}

void write_json_string(const std::string& text) {
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') std::cout << '\\' << char(ch);
        else if (ch < 0x20) std::cout << "\\u00" << hex[ch >> 4] << hex[ch & 15];
        else std::cout << char(ch);
    }
    std::cout << '"';
}

void write_feature_definition() {
    // Explicit selector constants above freeze the primary-well choice. A
    // fitted feature coefficient must never change which well is extracted.
    std::cout << R"("feature_definition":{"name":"moch-terrain-fixed-selector","version":1,"selector_weights":{"aggregate_height":16,"maximum_height":72,"roughness":48,"steepness":16,"shallow_cavity":140,"buried_cavity":620,"extra_cover":80,"secondary_well":80,"excessive_well":45,"ready_well_row":70},"feature_names":["aggregate_height","maximum_height","shallow_cavities","buried_cavities","extra_cover","roughness","steepness","secondary_wells","excessive_well","ready_well_rows","primary_well_depth","occupied_cells","removable_bridge_cavities"],"v1_reference_intercept":-16000,"v1_reference_coefficients":[-16,-72,-140,-620,-80,-48,-16,-80,-45,70,0,0,0],"old_terrain_is_separate_fixed_base":true})";
}

struct Candidate {
    Cells cells{};
    Rows after{};
    Piece::Pose pose;
    int kick = 0, lines = 0, garbageLines = 0, occupiedCells = 0, bridgeCavities = 0;
    bool spin = false, mini = false, positive = false, production = false, perfectClear = false;
    double oldTerrain = 0, v1Terrain = 0, fixedReward = 0;
    B::TerrainFeatures features;
    B::Evaluation::Status status;
};

void write_candidate(const Candidate& c) {
    const auto& f = c.features;
    std::cout << "{\"cells\":[";
    for (std::size_t i = 0; i < c.cells.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '[' << c.cells[i] % 10 << ',' << c.cells[i] / 10 << ']';
    }
    std::cout << "],\"post_rows\":[";
    for (std::size_t y = 0; y < c.after.size(); ++y) {
        if (y) std::cout << ',';
        std::cout << c.after[y];
    }
    std::cout << "],\"row\":" << c.pose.position.row << ",\"col\":" << c.pose.position.col
        << ",\"rotation\":" << c.pose.rotation.index() << ",\"kick\":" << c.kick
        << ",\"lines\":" << c.lines << ",\"spin\":" << c.spin << ",\"mini\":" << c.mini
        << ",\"positive\":" << c.positive << ",\"production_present\":" << c.production
        << ",\"perfect_clear\":" << c.perfectClear << ",\"attack\":" << c.status.attack
        << ",\"b2b\":" << c.status.b2b << ",\"combo\":" << c.status.combo
        << ",\"garbage_lines\":" << c.garbageLines << ",\"dead_without_next\":" << c.status.dead
        << ",\"reward\":" << c.status.reward << ",\"path_risk\":" << c.status.pathRisk
        << ",\"old_terrain\":" << c.oldTerrain << ",\"v1_terrain\":" << c.v1Terrain
        << ",\"fixed_reward\":" << c.fixedReward
        << ",\"features\":{\"aggregate_height\":" << f.aggregateHeight
        << ",\"maximum_height\":" << f.maximumHeight
        << ",\"shallow_cavities\":" << f.shallowCavities
        << ",\"buried_cavities\":" << f.buriedCavities
        << ",\"extra_cover\":" << f.extraCover << ",\"roughness\":" << f.roughness
        << ",\"steepness\":" << f.steepness << ",\"secondary_wells\":" << f.secondaryWells
        << ",\"excessive_well\":" << f.excessiveWell << ",\"ready_well_rows\":" << f.readyWellRows
        << ",\"primary_well_depth\":" << f.primaryWellDepth << ",\"occupied_cells\":" << c.occupiedCells
        << ",\"removable_bridge_cavities\":" << c.bridgeCavities << "}}";
}
} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("usage: moch_candidates ROOTS.log > candidates.jsonl");
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("Cannot open conditional roots");
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::Search>;
    Engine geometry;
    if (!geometry.prepare(10, 40)) throw std::runtime_error("Cannot prepare native geometry");
    const auto* context = geometry.context().get();
    A::Moves moves(context);
    B::Search production;
    B::Search::Config productionConfig;
    production.init(context, &productionConfig);
    std::set<int> sampleIds;
    int inputLine = 0;
    std::cout << std::boolalpha << std::setprecision(17);
    for (std::string line; std::getline(input, line);) {
        ++inputLine;
        if (line.empty()) continue;
        const auto root = B::RootSnapshot::parse(line);
        const auto teacher = label_from(line);
        if (!sampleIds.insert(teacher.sampleId).second) throw std::runtime_error("Duplicate sample_id");
        if (root.next.size() != 1 || root.hold != -2 || root.status.incoming ||
            root.status.readyIncoming || !root.packets.empty())
            throw std::runtime_error("Conditional root requires one family, no HOLD and no garbage packets");
        const int family = root.next.front();
        const char type = A::types[family];
        const auto before = BotNativeHost::make_map(root.field);
        const auto* start = B::spawn_node(context, before, type, root.status.previousCleared);
        const auto piece = Piece::get_definition(Piece::Type::from_index(family));
        productionConfig.rootPreviousCleared = root.status.previousCleared;
        const auto productionMoves = *production.search(before, start, 0);
        geometry.ai()->set_garbage_forecast({}, 0);
        std::vector<Candidate> candidates;
        std::set<std::array<int, 7>> seen;
        int enumerated = 0, hostRejected = 0, duplicate = 0;
        int teacherGeometry = 0, teacherReachable = 0, teacherSpin = 0, teacherLines = 0;
        int positives = 0, productionPositives = 0;
        for (auto landing : moves.enumerate(before, start)) {
            ++enumerated;
            const auto cells = cells_of(landing);
            const bool geometryMatch = cells == teacher.cells;
            teacherGeometry += geometryMatch;
            Piece::Pose answer;
            int hostKick = 0;
            if (!BotNativeHost::reachable(landing.node, piece, root.field, B::host_pose(*start),
                                         landing.spin, landing.mini, answer, hostKick)) {
                ++hostRejected;
                continue;
            }
            teacherReachable += geometryMatch;
            auto after = before;
            const int cleared = landing->attach(context, after);
            const std::array<int, 7> key{cells[0], cells[1], cells[2], cells[3],
                                        int(landing.spin), int(landing.mini), cleared};
            if (!seen.insert(key).second) { ++duplicate; continue; }
            const bool spinMatch = geometryMatch && landing.spin == teacher.spin && landing.mini == teacher.mini;
            const bool linesMatch = spinMatch && cleared == teacher.lines;
            teacherSpin += spinMatch;
            teacherLines += linesMatch;
            Candidate c;
            c.cells = cells;
            std::copy_n(after.row, 40, c.after.begin());
            c.pose = answer;
            c.kick = hostKick;
            c.lines = cleared;
            c.spin = landing.spin;
            c.mini = landing.mini;
            c.positive = linesMatch && c.after == teacher.after;
            for (const auto& possible : productionMoves)
                if (possible.spin == c.spin && possible.mini == c.mini && cells_of(possible) == cells) {
                    c.production = true;
                    break;
                }
            const auto evaluated = geometry.ai()->eval(landing, after, before, cleared);
            const m_tetris::TetrisContext::Env env{nullptr, 0, type, ' ', false};
            c.status = geometry.ai()->get(landing, evaluated, 0, root.status, env);
            c.garbageLines = root.status.garbageHeight - c.status.garbageHeight;
            c.perfectClear = after.count == 0;
            c.oldTerrain = evaluated.shape.value;
            c.fixedReward = c.status.value - c.oldTerrain;
            c.features = B::terrain_features(after, selectorWeights);
            c.bridgeCavities = B::bridge_terrain_features(after, selectorWeights).removableBridgeCavities;
            c.occupiedCells = after.count;
            c.v1Terrain = B::terrain_value(c.features, selectorWeights);
            positives += c.positive;
            productionPositives += c.positive && c.production;
            candidates.push_back(c);
        }
        const char* reason = !start ? "spawn_unavailable" : !teacherGeometry ? "geometry_missing"
            : !teacherReachable ? "native_unreachable" : !teacherSpin ? "spin_mismatch"
            : !teacherLines ? "lines_mismatch" : !positives ? "postboard_mismatch"
            : !productionPositives ? "production_missing" : "";
        std::cout << "{\"schema_version\":1,\"sample_id\":" << teacher.sampleId
            << ",\"input_line\":" << inputLine << ",\"family\":" << family
            << ",\"conditional_selected_family\":true,\"contains_future_queue\":false,\"input_root_line\":";
        write_json_string(line);
        std::cout << ',';
        write_feature_definition();
        std::cout << ",\"teacher_match_count\":" << positives
            << ",\"production_teacher_match_count\":" << productionPositives
            << ",\"enumerated\":" << enumerated << ",\"host_rejected\":" << hostRejected
            << ",\"duplicates\":" << duplicate << ",\"reject_reason\":\"" << reason
            << "\",\"candidates\":[";
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (i) std::cout << ',';
            write_candidate(candidates[i]);
        }
        std::cout << "]}\n";
    }
} catch (const std::exception& error) {
    std::cerr << "moch_candidates: " << error.what() << '\n';
    return 1;
}
