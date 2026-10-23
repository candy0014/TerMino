#include "../src/bot/chatgpt/key_piece_evaluation.h"
#include "../src/bot/chatgpt/analysis/spin_paths.h"
#include "rule_tetr_srs_plus.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <tuple>

namespace TetrTests::bot_chatgpt_key_piece_test {
namespace B = Bot_chatgpt;
class InspectKeyQueue : public B::KeyPieceEvaluation {
public:
    struct Status : B::KeyPieceEvaluation::Status { std::size_t cursor = 0; };
    std::string visible;
    mutable int checked = 0, corrected = 0;
    Status get(B::Landing& move, const Result& value, std::size_t depth, const Status& previous,
               const m_tetris::TetrisContext::Env& env) const {
        Status result;
        static_cast<B::KeyPieceEvaluation::Status&>(result) = B::KeyPieceEvaluation::get(move,value,depth,previous,env);
        result.cursor = previous.cursor + 1 + int(previous.holdEmpty && env.is_hold);
        if (previous.cursor < visible.size() && !previous.dead) {
            const auto supply=projection_environment(env,previous.holdEmpty);
            assert(supply.length==visible.size()-result.cursor);
            if(supply.length)assert(std::string_view(supply.next,supply.length)==std::string_view(visible).substr(result.cursor));
            corrected+=supply.length!=env.length; ++checked;
        }
        return result;
    }
};
using Map = m_tetris::TetrisMap;
static Map rows(std::initializer_list<unsigned> values) {
    Map map(10, 40); int y = 0;
    for (unsigned row : values) {
        map.row[y] = row;
        for (int x = 0; x < 10; ++x) if (row & (1u << x)) { map.top[x] = y + 1; map.roof = y + 1; ++map.count; }
        ++y;
    }
    return map;
}
static auto physical(const B::Evaluation::Status& s) {
    return std::make_tuple(s.combo, s.b2b, s.garbageHeight, s.incoming, s.readyIncoming, s.risen,
        s.dead, s.previousCleared, s.openerDoubleCancelEnabled, s.placedPieces, s.attack, s.reward, s.pathRisk);
}
static void host_legal(const m_tetris::TetrisContext* context, const Map& map, B::Landing move, bool previousClear) {
    Runtime::Playfield field; field.initialize(10, 20); field.clear();
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 10; ++x)
        if (map.row[y] & (1u << x)) field.set_cell(19 - y, x, Runtime::BoardCell::garbage());
    const auto* spawn = B::spawn_node(context, map, move->status.t, previousClear); assert(spawn);
    Piece::Pose answer; int kick;
    assert(BotNativeHost::reachable(move.node, Piece::get_definition(Piece::Type::from_index(B::piece_index(move->status.t))),
        field, B::host_pose(*spawn), move.spin, move.mini, answer, kick));
}
int run(int argc,char** argv) {
    using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, B::Evaluation, B::Search>;
    Engine owner; assert(owner.prepare(10, 40)); const auto* context = owner.context().get();
    B::KeyPieceGeometry geometry; geometry.init(context);
    const auto quad = rows({1022, 1022, 1022, 1022, 48});
    const auto tsd = rows({1007, 967, 8});
    const auto weakQuad = rows({1022, 1022, 1022, 1010});
    const auto miniSlot = rows({1007,1007,1007,1007,1007,1007,895,991,799,831,543,903});
    int checked = 0, spins = 0, minis = 0, setups = 0;
    for (const auto& map : {quad, tsd, weakQuad}) for (char family : std::string("IJLOSTZ")) {
        const auto keys = geometry.ready(map, family, false);
        for (auto key : keys) {
            host_legal(context, map, key, false); ++checked; spins += key.spin; minis += key.mini;
        }
        for (char prep : std::string("IJLOSTZ")) for (const auto& plan : geometry.weak(map, prep, family, false)) {
            host_legal(context, map, plan.move, false);
            Map after = map; const int clear = plan.move->attach(context, after);
            assert(clear == plan.lines); host_legal(context, after, plan.key, clear > 0);
            assert(B::KeyPieceGeometry::lines(after, plan.key.node) > 0); ++setups;
        }
    }
    assert(checked > 0 && spins > 0 && minis > 0 && setups > 0);
    // Full independent forward enumeration audits the reverse Spin search,
    // including first-successful kicks and Mini/full category preference.
    B::Analysis::Moves reference(context);
    std::mt19937 random(20260927);
    int exact = 0, normalChecks = 0;
    for (int sample = 0; sample < 45; ++sample) {
        auto map = sample % 2 ? tsd : weakQuad;
        for (int i = 0; i < 3; ++i) {
            const int y = random() % 8, x = random() % 10;
            if (!(map.row[y] & (1u << x)) && (map.row[y] | (1u << x)) != 1023) {
                map.row[y] |= 1u << x; ++map.count; map.roof = std::max(map.roof,y+1); map.top[x] = std::max(map.top[x],y+1);
            }
        }
        const bool previousClear = sample % 2;
        if (sample >= 35) {
            Map high(10, 40);
            for (int y = 0; y < 40; ++y) {
                high.row[y] = y < 16 ? 1022u : map.row[y - 16];
                for (int x = 0; x < 10; ++x) if (high.row[y] & (1u << x)) {
                    high.top[x] = y + 1; high.roof = y + 1; ++high.count;
                }
            }
            map = high;
        }
        for (char family : std::string("IJLOSTZ")) {
            const auto all = reference.enumerate(map,B::spawn_node(context,map,family,previousClear));
            const auto keys = geometry.ready(map,family,previousClear);
            std::vector<bool> normal(context->node_max(), false), tested(context->node_max(), false);
            for (auto move : all) if (!move.spin) normal[move->index] = true;
            // Include fitting but unreachable endpoints, not just successful
            // paths. Normal placements may end in a translation OR rotation.
            for (int r = 0; r < 4; ++r) for (int y = 0; y < std::min(44, map.roof + 5); ++y)
                for (int x = -4; x <= 10; ++x) {
                    auto* target = context->get(family,x,y,r);
                    if (!target || tested[target->index] || target->row < 0 || target->row + target->height > 40 ||
                        !target->check(map) || (target->move_down && target->move_down->check(map))) continue;
                    tested[target->index] = true;
                    const auto found = geometry.landing(map,target,previousClear,0);
                    assert(bool(found) == normal[target->index]);
                    if (found) {
                        assert(!found->spin);
                        if (sample < 3) host_legal(context,map,*found,previousClear);
                    }
                    assert(bool(geometry.landing(map,target,previousClear,0)) == normal[target->index]);
                    ++normalChecks;
                }
            for (auto expected : all) if (B::KeyPieceGeometry::lines(map,expected.node) && expected.spin) {
                const auto found = geometry.landing(map,expected.node,previousClear,expected.mini?1:2);
                assert(found && found->spin == expected.spin && found->mini == expected.mini);
                assert(std::any_of(keys.begin(),keys.end(),[&](B::Landing k) {
                    return k.node == expected.node && k.spin && (!k.mini || expected.mini);
                }));
                host_legal(context,map,*found,previousClear); ++exact;
            }
            for (auto key : keys) {
                assert(std::any_of(all.begin(),all.end(),[&](B::Landing a) {
                    return a.node == key.node && a.spin == key.spin && a.mini == key.mini;
                }));
            }
        }
    }
    assert(exact > 0 && normalChecks > 1000);
    // A changed, irrelevant column reuses the route without a new BFS. A
    // blocked entry is rechecked instead of reusing the old clear blindly.
    const auto iKey = geometry.ready(quad,'I',false).front();
    const auto unrelated = rows({1022,1022,1022,1022,48,512});
    // ready() itself may hit its board cache after the broad graph audit has
    // evicted the separate path entry. Warm the actual path under test.
    assert(geometry.landing(quad,iKey.node,false,0));
    const auto hits = geometry.statistics().pathHits;
    assert(geometry.landing(unrelated,iKey.node,false,0));
    assert(geometry.statistics().pathHits > hits);
    const auto blocked = rows({1022,1022,1022,1022,1021,1021,1021,1021});
    assert(!geometry.landing(blocked,iKey.node,false,0));
    B::KeyPieceEvaluation candidate; B::Evaluation baseline;
    candidate.init(context); baseline.init(context); candidate.set_key_state(true);
    candidate.set_feature_weights(1, 1); // Keep the exact geometry/legacy-value fixtures.
    candidate.set_garbage_forecast({}, 1); baseline.set_garbage_forecast({}, 1);
    candidate.begin_search(quad, "OIT", 'L', false);
    // Synthetic terminal result: isolate the future estimate without giving
    // the evaluator a fake actual clear or a completion-triggering before map.
    auto move = B::Landing(context->generate('O'));
    auto evaluated = baseline.eval(move, quad, quad, 0); evaluated.before = nullptr;
    B::Evaluation::Status previous; previous.b2b = 5; previous.placedPieces = 30;
    const m_tetris::TetrisContext::Env env{"IT", 2, 'O', 'L', false};
    const auto plain = baseline.get(move, evaluated, 3, previous, env);
    const auto changed = candidate.get(move, evaluated, 3, previous, env);
    assert(physical(plain) == physical(changed)); assert(changed.value > plain.value);
    assert(candidate.last_projection().key->status.t == 'I' && candidate.last_projection().lines == 4);
    const auto cold = changed.value;
    candidate.set_garbage_forecast({}, 1); // A NEW real decision keeps geometry.
    candidate.begin_search(quad, "OIT", 'L', false);
    assert(candidate.get(move, evaluated, 3, previous, env).value == cold);
    assert(candidate.statistics().planHits > 0 && candidate.geometry_statistics().boardHits > 0);
    candidate.set_cache_enabled(false);
    assert(candidate.get(move, evaluated, 3, previous, env).value == cold);
    candidate.set_cache_enabled(true);
    const m_tetris::TetrisContext::Env absent{"TZ", 2, 'O', 'L', false};
    assert(candidate.get(move, evaluated, 3, previous, absent).value == baseline.get(move, evaluated, 3, previous, absent).value);
    // A held key cannot be played without a known current piece.
    const m_tetris::TetrisContext::Env hidden{nullptr, 0, 'O', 'I', false};
    assert(candidate.get(move, evaluated, 3, previous, hidden).value == baseline.get(move, evaluated, 3, previous, hidden).value);
    const m_tetris::TetrisContext::Env held{"TZ",2,'O','I',false};
    candidate.set_key_state(false);
    assert(candidate.get(move,evaluated,3,previous,held).value == baseline.get(move,evaluated,3,previous,held).value);
    candidate.set_key_state(true);
    assert(candidate.get(move,evaluated,3,previous,held).value > baseline.get(move,evaluated,3,previous,held).value);
    // One O makes the incomplete well into a real quad. The weak estimate has
    // half credit; the ACTUAL completing O receives an extra terminal bonus.
    candidate.begin_search(weakQuad,"JOIT",'L',false);
    auto weakEval = baseline.eval(move,weakQuad,weakQuad,0); weakEval.before=nullptr;
    const m_tetris::TetrisContext::Env weakEnv{"OIT",3,'J','L',false};
    const auto weakPlain=baseline.get(move,weakEval,0,previous,weakEnv);
    const auto weakScore=candidate.get(move,weakEval,0,previous,weakEnv);
    assert(physical(weakPlain)==physical(weakScore) && weakScore.value>weakPlain.value);
    assert(candidate.last_projection().weak && candidate.last_projection().lines==4);
    bool completed=false;
    for(auto setup:geometry.weak(weakQuad,'O','I',false)) {
        auto after=weakQuad; const int clear=setup.move->attach(context,after);
        if(B::KeyPieceGeometry::lines(after,setup.key.node)!=4)continue;
        const auto ev=baseline.eval(setup.move,after,weakQuad,clear);
        const m_tetris::TetrisContext::Env next{"IT",2,'O','L',false};
        const auto a=baseline.get(setup.move,ev,1,weakPlain,next);
        const auto b=candidate.get(setup.move,ev,1,weakScore,next);
        assert(physical(a)==physical(b));
        assert(!candidate.last_projection().weak && candidate.last_projection().completionBonus>0);
        assert(b.value>a.value);
        bool noRepeat=false;
        for(auto park:geometry.parking(after,'L',false)) {
            auto later=after;park->attach(context,later);
            if(!geometry.preserves(later,setup.key,false))continue;
            const auto parked=baseline.eval(park,later,after,0);
            const m_tetris::TetrisContext::Env laterEnv{"IT",2,'L','O',false};
            const auto follow=candidate.get(park,parked,2,b,laterEnv);
            assert(physical(follow)==physical(baseline.get(park,parked,2,b,laterEnv)));
            assert(candidate.last_projection().completionBonus==0);noRepeat=true;break;
        }
        assert(noRepeat);completed=true;break;
    }
    assert(completed);
    // This historical shape needs an L to clear a blocking row AND complete
    // the fourth well row above it. The reserved I has no contiguous footprint
    // before that prep, so it cannot be handled by simple old-coordinate reuse.
    const auto obstructed=rows({1007,1007,511,511,511,1007,399,3,7});
    bool remapped=false;
    for(const auto& setup:geometry.weak(obstructed,'L','I',true)) if(!setup.originalKey) {
        auto after=obstructed; setup.move->attach(context,after);
        assert(B::KeyPieceGeometry::lines(after,setup.key.node)==4);
        host_legal(context,obstructed,setup.move,true); host_legal(context,after,setup.key,setup.lines>0); remapped=true;
    }
    assert(remapped);
    // Current queue and garbage deadlines are always replayed, including
    // empty HOLD's two-preview consumption. Reusing a proof does not reuse
    // yesterday's attack, safety, policy or combo estimate.
    candidate.begin_search(quad,"JOIT",' ',false);
    auto pressure=previous; pressure.incoming=4;
    const std::vector<B::VisiblePacket> packets{{4,0.5}};
    baseline.set_garbage_forecast(packets,1); candidate.set_garbage_forecast(packets,1);
    const m_tetris::TetrisContext::Env waiting{"OIT",3,'J',' ',false};
    candidate.set_key_state(false);
    assert(candidate.get(move,evaluated,0,pressure,waiting).value==baseline.get(move,evaluated,0,pressure,waiting).value);
    candidate.set_key_state(true);
    assert(candidate.get(move,evaluated,0,pressure,waiting).value>baseline.get(move,evaluated,0,pressure,waiting).value);
    assert(candidate.last_projection().steps==1); // empty HOLD skips O, uses I.
    assert(physical(candidate.get(move,evaluated,0,pressure,waiting))==physical(baseline.get(move,evaluated,0,pressure,waiting)));
    auto dead=previous; dead.dead=true; dead.value=-1e9;
    assert(candidate.get(move,evaluated,0,dead,env).value==dead.value);
    // Non-T mini and T full Spin both receive terminal credit without being
    // relabelled as ordinary clears or requiring a positive attack amount.
    int valuedMinis=0,valuedFull=0;
    baseline.set_garbage_forecast({},1); candidate.set_garbage_forecast({},1);
    for(const auto& map:{quad,tsd,weakQuad,miniSlot})for(char family:std::string("IJLOSTZ")) {
        if(geometry.ready(map,family,false).empty())continue;
        std::string visible="O";visible+=family;
        candidate.begin_search(map,visible,' ',false); candidate.set_key_state(false);
        auto ev=baseline.eval(move,map,map,0);ev.before=nullptr;
        const std::string tail(1,family);
        const m_tetris::TetrisContext::Env next{tail.data(),tail.size(),'O',' ',false};
        const auto a=baseline.get(move,ev,3,previous,next);
        const auto b=candidate.get(move,ev,3,previous,next);
        assert(physical(a)==physical(b));
        if(b.value>a.value) { valuedMinis+=candidate.last_projection().key.mini; valuedFull+=candidate.last_projection().key.spin&&!candidate.last_projection().key.mini; }
        candidate.set_cache_enabled(false);
        const auto uncached=candidate.get(move,ev,3,previous,next);
        assert(uncached.value==b.value); candidate.set_cache_enabled(true);
    }
    assert(valuedMinis>0&&valuedFull>0);
    // Validate actual native callbacks, not just synthetic future choices.
    // Empty HOLD's extra consumption can occur several beam layers down.
    using Inspected = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,InspectKeyQueue,B::Search>;
    Inspected inspect(owner.context()); inspect.ai()->visible="LSIOTJ"; inspect.status()->holdEmpty=true;
    inspect.ai()->set_garbage_forecast({},1); inspect.ai()->set_key_state(true);
    const auto empty=rows({}); inspect.ai()->begin_search(empty,"LSIOTJ",' ',false);
    for(int i=0;i<20;++i)inspect.run_hold(empty,context->generate('L'),' ',true,"SIOTJ",5,0);
    assert(inspect.ai()->checked>100 && inspect.ai()->corrected>0);
    std::cout << "normal endpoints=" << normalChecks << " key geometry=" << checked << " spins=" << spins << " minis=" << minis
              << " setups=" << setups << " independent spins="<<exact<<" valued minis="<<valuedMinis
              << " persistent cache, weak completion, HOLD, garbage verified\n";
    return 0;
}
} // namespace TetrTests::bot_chatgpt_key_piece_test
