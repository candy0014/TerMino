#include "../src/bot/chatgpt/coverage_search.h"
#include "../src/bot/chatgpt/analysis/spin_paths.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <iostream>
#include <map>
#include <random>

namespace TetrTests::bot_chatgpt_coverage_test {
namespace B = Bot_chatgpt;
namespace A = Bot_chatgpt::Analysis;
using NativeEngine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::CoverageSearch>;
int run(int argc,char** argv) {
    NativeEngine engine; assert(engine.prepare(10, 40));
    const auto* context = engine.context().get();
    B::CoverageSearch candidate; B::Search original;
    B::Search::Config config;
    candidate.init(context, &config); original.init(context, &config);
    A::Moves full(context);
    int certified = 0, added = 0;
    const auto compare = [&](const Runtime::Playfield& field, char type, const m_tetris::TetrisNode* start) {
        const auto board = BotNativeHost::make_map(field);
        std::map<std::size_t, int> expected, actual;
        for (const auto& l : full.enumerate(board, start)) {
            const int rank = int(l.spin) * 2 - int(l.mini);
            expected[l->index_filtered] = std::max(expected[l->index_filtered], rank);
        }
        std::set<std::size_t> old;
        for (const auto& l : *original.search(board, start, 0)) old.insert(l->index_filtered);
        const auto first = *candidate.search(board, start, 0);
        assert(first == *candidate.search(board, start, 0));
        for (const auto& l : first) {
            assert(actual.emplace(l->index_filtered, int(l.spin) * 2 - int(l.mini)).second);
            Piece::Pose answer; int kick = 0;
            assert(BotNativeHost::reachable(l.node,
                Piece::get_definition(Piece::Type::from_index(B::piece_index(type))),
                field, B::host_pose(*start), l.spin, l.mini, answer, kick));
            ++certified; added += !old.count(l->index_filtered);
        }
        assert(actual == expected);
        for (auto geometry : old) assert(actual.count(geometry));
    };
    Runtime::Playfield tower; tower.initialize(10, 20); tower.clear();
    for (int y = 15; y < 20; ++y) tower.set_cell(y, 0, Runtime::BoardCell::garbage());
    compare(tower, 'O', context->generate('O'));
    // The standard search now also retains the route above the side tower.
    const auto towerMap=BotNativeHost::make_map(tower);
    const auto towerLandings=*original.search(towerMap,context->generate('O'),0);
    const auto* onTower=context->generate('O');
    while(onTower->move_left)onTower=onTower->move_left;
    onTower=onTower->drop(towerMap);
    assert(std::any_of(towerLandings.begin(),towerLandings.end(),[&](const auto& landing) {
        return landing->index_filtered==onTower->index_filtered;
    }));
    std::mt19937 random(2026092505);
    for (int sample = 0; sample < 10; ++sample) {
        Runtime::Playfield field; field.initialize(10, 20); field.clear();
        for (int col = 0; col < 10; ++col) {
            const int height = random() % (sample < 5 ? 10 : 19);
            for (int row = 0; row < height; ++row)
                if (row == height - 1 || random() % 5)
                    field.set_cell(19 - row, col, Runtime::BoardCell::garbage());
        }
        for (char type : std::string("IJLOSTZ")) compare(field, type, context->generate(type));
    }
    // A moved/rotated current piece is not silently replaced by a normal spawn.
    Runtime::Playfield empty; empty.initialize(10, 20); empty.clear();
    for (char type : std::string("IJLOSTZ")) {
        compare(empty, type, context->generate(type));
        compare(empty, type, context->get(type, 3, 10, 1));
    }
    assert(candidate.search(BotNativeHost::make_map(empty), nullptr, 0)->empty());
    std::cout << "host_certified_landings=" << certified << " added_geometry=" << added
              << " full_graph_strongest_spin_equivalence=1\n";
    return 0;
}
} // namespace TetrTests::bot_chatgpt_coverage_test
