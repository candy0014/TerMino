#include "../src/bot/chatgpt/o_roof_search.h"
#include "../src/bot/chatgpt/release_choice_search.h"
#include "../src/bot/chatgpt/evaluation.h"
#include "../src/bot/native_host.h"
#include "rule_tetr_srs_plus.h"

#include <array>
#include <cassert>
#include <iostream>
#include <random>
#include <set>
#include <type_traits>

namespace TetrTests::bot_chatgpt_o_roof_test {
namespace B = Bot_chatgpt;
using NativeEngine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::ORoofSearch>;
static_assert(std::is_same_v<B::Search::Config,B::ORoofSearch::Config>);
static_assert(std::is_same_v<B::Search::Config,B::ReleaseChoiceSearch::Config>);

struct TeacherFixture {
    int sample;
    bool previousCleared;
    std::vector<unsigned> rows;
    std::array<int, 4> cells;
};

// Frozen geometry fixtures from tactical_cases_v2/pressure_inputs/roots.log:
// the 14 O teacher placements missing from baseline enumeration. Rows retain
// every occupied cell, including garbage, bottom first; omitted rows are empty.
// These are enumeration regressions, not evaluation or match-rate samples.
const TeacherFixture teachers[] = {
    {6, true, {1007,591,3}, {30,31,40,41}},
    {37, true, {1007,1007,1007,1007,1015,1015,1015,1015,799,768}, {108,109,118,119}},
    {88, false, {767,767,767,767,767,895,895,895,895,1007,1007,991,964,12,8}, {120,121,130,131}},
    {137, true, {1019,1019,1019,1019,1019,1022,1022,1022,1022,1022,1022,1022,920,8,8}, {121,122,131,132}},
    {154, false, {767,1022,1022,1022,1015,1015,1015,999,967,992,896}, {118,119,128,129}},
    {162, false, {767,1022,1022,1022,1022,972,924,8}, {50,51,60,61}},
    {194, false, {1019,1022,959,959,959,959,959,15,12,28,8}, {80,81,90,91}},
    {215, true, {1007,895,1007,1007,1007,1007,1007,1007,959,776}, {107,108,117,118}},
    {224, true, {1015,1019,1019,1019,1007,895,1017,1008,1016,8}, {70,71,80,81}},
    {252, false, {1019,993,625,3}, {40,41,50,51}},
    {266, false, {959,959,959,959,959,959,903,519,780}, {80,81,90,91}},
    {275, false, {1021,1021,1021,1021,231,199}, {48,49,58,59}},
    {329, false, {1019,1019,1019,1007,1007,1007,1007,967,963,871,513}, {118,119,128,129}},
    {357, true, {1007,1007,1007,1007,454}, {55,56,65,66}},
};

Runtime::Playfield field_from_rows(const std::vector<unsigned>& rows) {
    Runtime::Playfield field;
    field.initialize(10, 20);
    field.clear();
    for (std::size_t y = 0; y < rows.size(); ++y)
        for (int x = 0; x < 10; ++x)
            if (rows[y] & (1u << x))
                field.set_cell(19 - int(y), x, Runtime::BoardCell::garbage());
    return field;
}

std::array<int, 4> occupied(const m_tetris::TetrisNode* node) {
    std::array<int, 4> result{};
    int count = 0;
    for (int y = 0; y < node->height; ++y)
        for (int x = 0; x < 10; ++x)
            if (node->data[y] & (1u << x)) result.at(count++) = (node->row + y) * 10 + x;
    assert(count == 4);
    std::sort(result.begin(), result.end());
    return result;
}

int run(int argc,char** argv) {
    NativeEngine engine;
    assert(engine.prepare(10, 40));
    const auto* context = engine.context().get();
    B::Search original;
    B::Search::Config originalConfig;
    originalConfig.legacySourceDescent=true; // Keep this frozen experiment's historical control.
    B::ORoofSearch candidate;
    B::ORoofSearch::Config candidateConfig;
    original.init(context, &originalConfig);
    candidate.init(context, &candidateConfig);
    int certified = 0, added = 0, nonOComparisons = 0, recovered = 0;

    auto compare_non_o = [&](const Runtime::Playfield& field,
                             const m_tetris::TetrisNode* start, std::size_t depth) {
        assert(start && start->status.t != 'O');
        const auto map = BotNativeHost::make_map(field);
        const auto before = *original.search(map, start, depth);
        const auto after = *candidate.search(map, start, depth);
        // Equality includes order, geometry, spin, mini and exact kick label.
        assert(before == after);
        ++nonOComparisons;
    };
    auto certify_o = [&](const Runtime::Playfield& field,
                         const m_tetris::TetrisNode* start, std::size_t depth) {
        assert(start && start->status.t == 'O');
        const auto map = BotNativeHost::make_map(field);
        const auto before = *original.search(map, start, depth);
        const auto after = *candidate.search(map, start, depth);
        assert(after == *candidate.search(map, start, depth));
        std::set<std::size_t> oldGeometry, newGeometry;
        for (const auto& landing : before) oldGeometry.insert(landing->index_filtered);
        const auto* effectiveStart = start->check(map) ? start :
            B::spawn_node(context, map, 'O', depth > 0 || candidateConfig.rootPreviousCleared);
        for (const auto& landing : after) {
            assert(effectiveStart);
            assert(newGeometry.insert(landing->index_filtered).second);
            Piece::Pose answer;
            int kick = 0;
            assert(BotNativeHost::reachable(landing.node,
                Piece::get_definition(Piece::Type::from_index(3)), field,
                B::host_pose(*effectiveStart), landing.spin, landing.mini, answer, kick));
            ++certified;
            added += !oldGeometry.count(landing->index_filtered);
        }
        for (auto geometry : oldGeometry) assert(newGeometry.count(geometry));
        return after;
    };

    for (const auto& fixture : teachers) {
        const auto field = field_from_rows(fixture.rows);
        const auto map = BotNativeHost::make_map(field);
        originalConfig.rootPreviousCleared = candidateConfig.rootPreviousCleared = fixture.previousCleared;
        const auto* start = B::spawn_node(context, map, 'O', fixture.previousCleared);
        assert(start);
        const auto before = *original.search(map, start, 0);
        for (const auto& landing : before) assert(occupied(landing.node) != fixture.cells);
        const auto after = certify_o(field, start, 0);
        const auto teacher = std::find_if(after.begin(), after.end(), [&](const B::Landing& landing) {
            return occupied(landing.node) == fixture.cells && !landing.spin && !landing.mini;
        });
        assert(teacher != after.end());
        ++recovered;
        if (fixture.sample == 215) {
            // pressure_2 p105: preserve the route that translates at spawn
            // then drops onto the x7-8 platform above the old lowered start.
            const auto* shifted = start;
            while (shifted->col < teacher->node->col) {
                shifted = shifted->move_right;
                assert(shifted && shifted->check(map));
            }
            assert(occupied(shifted->drop(map)) == fixture.cells);
            const auto* lowered = start;
            while (lowered->low >= map.roof && lowered->move_down && lowered->move_down->check(map))
                lowered = lowered->move_down;
            assert(lowered->row < teacher->node->row);
        }
        for (char type : std::string("IJLSTZ")) {
            compare_non_o(field, context->generate(type), 0);
            compare_non_o(field, context->generate(type), 1);
        }
    }
    assert(recovered == 14);

    originalConfig.rootPreviousCleared = candidateConfig.rootPreviousCleared = false;
    const auto tower = field_from_rows({1,1,1,1,1});
    const int beforeTower = added;
    certify_o(tower, context->generate('O'), 0);
    assert(added > beforeTower); // Must translate above the tower before descent.

    std::mt19937 random(2026092604);
    for (int sample = 0; sample < 16; ++sample) {
        std::vector<unsigned> rows(19);
        for (int col = 0; col < 10; ++col) {
            const int height = random() % (sample < 8 ? 10 : 19);
            for (int y = 0; y < height; ++y)
                if (y == height - 1 || random() % 5) rows[y] |= 1u << col;
        }
        const auto field = field_from_rows(rows);
        certify_o(field, context->generate('O'), 0);
        for (char type : std::string("IJLSTZ")) compare_non_o(field, context->generate(type), 0);
    }
    // Actual moved/rotated roots must not be replaced with normal spawn poses.
    const auto empty = field_from_rows({});
    for (int rotation = 0; rotation < 4; ++rotation) {
        for (char type : std::string("IJLSTZ"))
            compare_non_o(empty, context->get(type, 3, 10, rotation), 0);
        certify_o(empty, context->get('O', 3, 10, rotation), 0);
    }
    assert(candidate.search(BotNativeHost::make_map(empty), nullptr, 0)->empty());

    // Both experimental searches must honor the host's movement and Spin
    // configuration. With Spins disabled the release search has no variants
    // to add, so its enumeration must exactly match the standard search.
    B::ReleaseChoiceSearch release;
    B::ReleaseChoiceSearch::Config releaseConfig;
    release.init(context,&releaseConfig);
    originalConfig.spinBonuses=candidateConfig.spinBonuses=releaseConfig.spinBonuses=
        Runtime::SpinBonuses::None;
    for(bool partialDescent:{false,true}) {
        originalConfig.allowPartialDescent=candidateConfig.allowPartialDescent=
            releaseConfig.allowPartialDescent=partialDescent;
        for(const auto& fixture:teachers) {
            const auto field=field_from_rows(fixture.rows);
            const auto map=BotNativeHost::make_map(field);
            originalConfig.rootPreviousCleared=candidateConfig.rootPreviousCleared=
                releaseConfig.rootPreviousCleared=fixture.previousCleared;
            for(char type:std::string("IJLSTZ")) {
                const auto* start=context->generate(type);
                const auto expected=*original.search(map,start,0);
                assert(expected==*candidate.search(map,start,0));
                assert(expected==*release.search(map,start,0));
                for(const auto& landing:expected) assert(!landing.spin && !landing.mini);
            }
        }
    }
    originalConfig={}; candidateConfig={};

    // Preserve root versus future Clutch eligibility on an obstructed spawn.
    const auto high = field_from_rows(std::vector<unsigned>(22, 511));
    const auto highMap = BotNativeHost::make_map(high);
    assert(!context->generate('O')->check(highMap));
    assert(certify_o(high, context->generate('O'), 0).empty());
    originalConfig.rootPreviousCleared = candidateConfig.rootPreviousCleared = true;
    assert(!certify_o(high, context->generate('O'), 0).empty());
    originalConfig.rootPreviousCleared = candidateConfig.rootPreviousCleared = false;
    assert(!certify_o(high, context->generate('O'), 1).empty());
    for (char type : std::string("IJLSTZ")) {
        compare_non_o(high, context->generate(type), 0);
        compare_non_o(high, context->generate(type), 1);
    }
    std::cout << "recovered_O_teacher=" << recovered << " non_O_exact_comparisons=" << nonOComparisons
              << " host_certified_O_landings=" << certified << " added_O_geometry=" << added << '\n';
    return 0;
}
} // namespace TetrTests::bot_chatgpt_o_roof_test
