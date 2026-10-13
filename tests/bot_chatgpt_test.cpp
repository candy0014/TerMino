#include "../src/bot/chatgpt/evaluation.h"
#include "../src/bot/chatgpt/root_snapshot.h"
#include "../src/bot/chatgpt/fixed_root_search.h"
#include "../src/bot/native_host.h"
#include "rule_tetr_srs_plus.h"
#include "tetr/bot/bot_chatgpt.h"
#include "tetr/legacy/bot_adapter.h"
#include "tetr/runtime/game_context.h"
#include "tetr/rules/garbage_queue.h"

#include <array>
#include <cassert>
#include <random>
#include <limits>
#include <set>
#include <map>
#include <tuple>

int main() {
    assert(Bot_chatgpt::search_budget_ms(-1) == 1);
    assert(Bot_chatgpt::search_budget_ms(0) == 1);
    assert(Bot_chatgpt::search_budget_ms(60) == 20);
    assert(Bot_chatgpt::search_budget_ms(150) == 50);
    assert(Bot_chatgpt::search_budget_ms(300) == 100);
    assert(Bot_chatgpt::search_budget_ms(1000000000) == 100);
    std::ostringstream rootText;
    rootText << "TETR_BOT_DIAGNOSTIC trace_version=1 root_hold=0 root_row=-2 root_col=4 root_rotation=0"
             << " root_b2b=6 root_combo=2 root_garbage_height=0 pieces=17 incoming=7 ready=3"
             << " root_cleared=1 opener_double=1 forecast_step=0.4 root_next=5,1,2,3,4,6"
             << " root_packets=3:-0.25,4:1.125 selected=5 row=18 col=4 rotation=0 board=";
    for (int y = 0; y < 40; ++y) rootText << (y ? "," : "") << (y == 0 ? 3 : 0);
    const auto parsedRoot = Bot_chatgpt::RootSnapshot::parse(rootText.str());
    assert(parsedRoot.next == std::vector<int>({5,1,2,3,4,6}));
    assert(parsedRoot.status.b2b == 6 && parsedRoot.status.combo == 2 && parsedRoot.status.previousCleared);
    assert(parsedRoot.status.incoming == 7 && parsedRoot.status.readyIncoming == 3 && parsedRoot.hold == 0);
    assert(parsedRoot.packets.size() == 2 && parsedRoot.packets[1].secondsUntilReady == 1.125);
    assert(BotNativeHost::make_map(parsedRoot.field).row[0] == 3);
    for (const auto& bad : {std::string("TETR_BOT_DIAGNOSTIC trace_version=0"),
                           rootText.str() + " root_b2b=6", rootText.str().substr(0, rootText.str().size() - 2)}) {
        bool rejected = false;
        try { (void)Bot_chatgpt::RootSnapshot::parse(bad); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }
    const auto fire = Bot_chatgpt::experimental_policy_for({});
    const auto urgent = Bot_chatgpt::experimental_policy_for({18, 10, 8, 8});
    const auto mixed = Bot_chatgpt::experimental_policy_for({9, 2, 0, 0});
    assert(fire.digPressure == 0 && fire.attack == 560 && fire.b2b == 480);
    assert(urgent.digPressure == 1 && urgent.attack == 420 && urgent.b2b == 360);
    assert(fire.garbageClear == 240 && urgent.garbageClear == 480);
    assert(mixed.digPressure > 0 && mixed.digPressure < 1);
    assert(mixed.garbageClear > fire.garbageClear && mixed.garbageClear < urgent.garbageClear);
    assert(mixed.attack > urgent.attack && mixed.attack < fire.attack);
    assert(Bot_chatgpt::experimental_policy_for({10, 2, 0, 0}).digPressure >= mixed.digPressure);
    assert(Bot_chatgpt::experimental_policy_for({9, 2, 4, 0}).digPressure >= mixed.digPressure);
    assert(Bot_chatgpt::experimental_policy_for({9, 2, 4, 4}).digPressure >=
           Bot_chatgpt::experimental_policy_for({9, 2, 4, 0}).digPressure);
    assert(Bot_chatgpt::experimental_policy_for({9, 10, 0, 0}).digPressure >= mixed.digPressure);
    assert(Bot_chatgpt::experimental_policy_for({10, 10, 0, 0}).digPressure == 1);
    auto comboPolicy = fire;
    comboPolicy.comboContinuation = 240;
    assert(Bot_chatgpt::combo_reserve(comboPolicy, 0, 10) == 0);
    assert(Bot_chatgpt::combo_reserve(comboPolicy, 4, 0) == 0);
    assert(Bot_chatgpt::combo_reserve(comboPolicy, 4, 10) == 960);
    assert(Bot_chatgpt::combo_reserve(comboPolicy, 100, 10) == 1920);
    Bot_chatgpt::GarbageForecast forecast;
    forecast.reset({{3,-1}, {4,1.5}, {5,-3}}, 1.0);
    assert(forecast.ready(0,12,0) == 3);
    assert(forecast.ready(1,12,0) == 3); // FIFO: later old packet stays blocked.
    assert(forecast.ready(2,12,0) == 12);
    assert(forecast.ready(0,10,0) == 1); // Two rows cancelled from ready front.
    assert(forecast.ready(1,7,0) == 0); // Five consumed; no ready prefix left.
    assert(forecast.ready(0,5,0) == 5); // Cancelling the entire blocker exposes old packet.
    assert(forecast.ready(2,7,0) == 7);
    forecast.reset({{3,.25}}, .1);
    assert(forecast.ready(2,3,0) == 0 && forecast.ready(3,3,0) == 3);
    forecast.reset({}, 1);
    assert(forecast.ready(10,0,0) == 0); // No state carried to a new root/game.
    const int maximum=std::numeric_limits<int>::max();
    forecast.reset({{1,-1},{maximum,-1},{maximum,1}},1);
    assert(forecast.ready(0,maximum,0)==maximum);
    assert(forecast.ready(0,maximum-8,0)==maximum-8);
    forecast.reset({{1,1},{maximum,-1},{maximum,-1}},1);
    assert(forecast.ready(0,maximum,0)==0);
    assert(forecast.ready(1,maximum,0)==maximum);
    // Cross-check against the actual queue implementation, including shuffled
    // timestamps, partial packets, opener cancellation and repeated rises.
    std::mt19937 forecastRandom(20260925);
    for (int sample = 0; sample < 100; ++sample) {
        std::deque<Rules::GarbagePacket> queue;
        std::vector<Bot_chatgpt::VisiblePacket> visible;
        const auto delay = Runtime::Duration::from_seconds(.5);
        const double step = .25 * (1 + forecastRandom() % 4);
        for (int i = 0; i < 8; ++i) {
            const int lines = 1 + forecastRandom() % 6;
            const double readyAt = .25 * (int(forecastRandom() % 20) - 4);
            queue.emplace_back(lines, Runtime::TimePoint::from_seconds(readyAt - .5));
            visible.push_back({lines, readyAt});
        }
        forecast.reset(visible, step);
        for (int depth = 0; depth < 8; ++depth) {
            const auto now = Runtime::TimePoint::from_seconds(depth * step);
            const auto check = [&] {
                auto copy = queue;
                const auto ready = Rules::consume_ready_garbage(copy, now, delay, 1000);
                int lines = 0;
                for (const auto& chunk : ready.chunks) lines += chunk.lines;
                assert(forecast.ready(depth, Rules::pending_garbage(queue), 0) == lines);
            };
            check();
            if (forecastRandom() % 2) {
                Rules::cancel_incoming_garbage(queue, {
                    int(forecastRandom() % 9), 10 + depth, bool(sample % 2)});
            } else {
                Rules::consume_ready_garbage(queue, now, delay, 8);
            }
            check();
        }
    }
    using NativeEngine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule,
        Bot_chatgpt::Evaluation, Bot_chatgpt::Search>;
    NativeEngine engine;
    assert(engine.prepare(10, 40));
    {
        NativeEngine cold(engine.context());
        *cold.status() = parsedRoot.status;
        cold.ai()->set_garbage_forecast(parsedRoot.packets, parsedRoot.step);
        cold.search_config()->rootPreviousCleared = parsedRoot.status.previousCleared;
        const auto rootMap = BotNativeHost::make_map(parsedRoot.field);
        const auto* start = Bot_chatgpt::native_pose(cold.context().get(), 'T', parsedRoot.pose);
        int calls = 0;
        const auto pass = [&] {
            ++calls;
            return cold.run_hold(rootMap, start, 'I', true, "JLOSZ", 5, 0);
        };
        assert(Bot_chatgpt::fixed_root_passes(cold, pass, 1).target.node);
        const auto initialBytes = cold.memory_usage();
        assert(Bot_chatgpt::fixed_root_passes(cold, pass, 10).target.node);
        assert(calls == 11 && cold.memory_usage() > initialBytes);
    }
    Runtime::Playfield cavity;
    cavity.initialize(10, 20); cavity.clear();
    cavity.set_cell(16, 0, Runtime::BoardCell::garbage());
    cavity.set_cell(17, 1, Runtime::BoardCell::garbage());
    const auto cavityMap = BotNativeHost::make_map(cavity);
    assert(Bot_chatgpt::covered_empty_cells(cavityMap) == 5);
    Bot_chatgpt::Evaluation::Result cavityEval;
    cavityEval.shape = {0, 0, cavityMap.count, &cavityMap};
    Bot_chatgpt::Landing cavityPiece(engine.context()->generate('J'));
    Bot_chatgpt::Evaluation::Status cavityState;
    engine.ai()->set_covered_empty_weight(0);
    const auto unpenalized = engine.ai()->get(cavityPiece, cavityEval, 0, cavityState, {});
    engine.ai()->set_covered_empty_weight(80);
    const auto penalized = engine.ai()->get(cavityPiece, cavityEval, 0, cavityState, {});
    assert(unpenalized.value - penalized.value == 400);
    assert(unpenalized.reward == penalized.reward && unpenalized.attack == penalized.attack);
    engine.ai()->set_covered_empty_weight(0);
    Bot_chatgpt::Search search;
    Bot_chatgpt::Search::Config searchConfig;
    search.init(engine.context().get(), &searchConfig);
    for (char type : std::string("IJLOSTZ")) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            for (int row : {-12, -4, -2, 0, 12}) {
                for (int col : {2, 4, 7}) {
                    const Piece::Pose pose{{row, col}, Runtime::Rotation::from_index(rotation)};
                    const auto* node = Bot_chatgpt::native_pose(engine.context().get(), type, pose);
                    assert(node);
                    const auto converted = Bot_chatgpt::host_pose(*node);
                    assert(converted.position.row == row && converted.position.col == col);
                    assert(converted.rotation.index() == rotation);
                }
            }
        }
    }
    std::mt19937 random(20260924);
    int spins = 0, landings = 0;
    for (int sample = 0; sample < 12; ++sample) {
        Runtime::Playfield field;
        field.initialize(10, 20);
        field.clear();
        // Deterministic surfaces plus buried garbage exercise overhangs/kicks.
        for (int col = 0; col < 10; ++col) {
            const int height = sample == 0 ? 0 : static_cast<int>(random() % 9);
            for (int row = 20 - height; row < 20; ++row) {
                if (row == 19 && col == sample % 10) continue;
                field.set_cell(row, col, Runtime::BoardCell::garbage());
            }
        }
        const auto map = BotNativeHost::make_map(field);
        int expectedHoles = 0;
        for (int x = 0; x < 10; ++x) {
            bool covered = false;
            for (int y = 39; y >= 0; --y) {
                if (map.row[y] & (1u << x)) covered = true;
                else expectedHoles += covered;
            }
        }
        assert(Bot_chatgpt::covered_empty_cells(map) == expectedHoles);
        for (char type : std::string("IJLOSTZ")) {
            const auto piece = Piece::get_definition(Piece::Type::from_index(Bot_chatgpt::piece_index(type)));
            const auto* candidates = search.search(map, engine.context()->generate(type), 0);
            assert(!candidates->empty());
            for (const auto& landing : *candidates) {
                const auto pose = Bot_chatgpt::host_pose(*landing.node);
                assert(piece.can_place(pose, field));
                assert(!piece.can_place(pose.translated(1, 0), field));
                std::set<std::pair<int,int>> nativeCells, hostCells;
                for (int y = 0; y < landing->height; ++y)
                    for (int x = 0; x < 10; ++x)
                        if ((landing->data[y] >> x) & 1) nativeCells.insert({19 - landing->row - y, x});
                for (const auto& cell : piece.cells(pose.rotation))
                    hostCells.insert({pose.position.row + cell[0], pose.position.col + cell[1]});
                assert(nativeCells == hostCells);
                Piece::Pose reachable;
                int kick = 0;
                assert(BotNativeHost::reachable(landing.node, piece, field,
                    {{-2,4},Runtime::Rotation{}}, landing.spin, landing.mini, reachable, kick));
                spins += landing.spin;
                ++landings;
            }
        }
    }
    assert(spins > 0 && landings > 1000);

    // Explicit Season 2 examples: preserve zero-line B2B, charge release,
    // multiplicative combo, and non-T mini (not a full-spin attack).
    m_tetris::TetrisMap map(10,40);
    map.row[0] = 1; map.top[0] = 1; map.roof = 1; map.count = 1;
    Bot_chatgpt::Evaluation::Result eval;
    eval.shape = {0, 0, 1, &map};
    m_tetris::TetrisContext::Env env{};
    Bot_chatgpt::Landing landing(engine.context()->generate('T'));
    Bot_chatgpt::Evaluation::Status state;
    state.b2b = 7; state.combo = 3;
    auto result = engine.ai()->get(landing, eval, 0, state, env);
    assert(result.b2b == 7 && result.combo == 0 && result.attack == 0);
    state.combo = 0; eval.shape.clear = 1;
    result = engine.ai()->get(landing, eval, 0, state, env);
    assert(result.b2b == 0 && result.attack == 6);
    state.b2b = 1; state.combo = 1; eval.shape.clear = 2; landing.spin = true;
    result = engine.ai()->get(landing, eval, 0, state, env);
    assert(result.b2b == 2 && result.combo == 2 && result.attack == 6);
    landing.node = engine.context()->generate('J'); landing.mini = true;
    result = engine.ai()->get(landing, eval, 0, state, env);
    assert(result.attack == 2 && result.b2b == 2);

    // A four-line attack cancels incoming lines using the configured rate;
    // fractional unused attack survives in the branch state.
    landing.node=engine.context()->generate('I');landing.spin=false;landing.mini=false;
    eval.shape.clear=4;
    state={};state.incoming=state.readyIncoming=3;state.cancellationMultiplier=2;
    auto scaled=engine.ai()->get(landing,eval,0,state,env);
    assert(scaled.attack==4 && scaled.incoming==0 && scaled.sentAttack==2);
    assert(scaled.cancellation.outgoingFraction==.5);
    state.cancellationMultiplier=0;
    scaled=engine.ai()->get(landing,eval,0,state,env);
    assert(scaled.incoming==3 && scaled.sentAttack==4);
    state.cancellationMultiplier=.5;
    scaled=engine.ai()->get(landing,eval,0,state,env);
    assert(scaled.incoming==1 && scaled.sentAttack==0);

    // Incoming rows rise only on a non-clear and cannot be cancelled after
    // rising. A zero-attack single stalls them too; at most eight rise at once.
    landing.node = engine.context()->generate('J'); landing.spin = false; landing.mini = false;
    state = {}; state.incoming = 12; state.readyIncoming = 10;
    eval.shape.clear = 0;
    result = engine.ai()->get(landing, eval, 0, state, env);
    assert(result.risen == 8 && result.incoming == 4 && result.readyIncoming == 2);
    eval.shape.clear = 1;
    result = engine.ai()->get(landing, eval, 0, state, env);
    assert(result.attack == 0 && result.risen == 0 && result.incoming == 12);
    state.risen = 4; state.incoming = state.readyIncoming = 2;
    eval.shape.clear = 4;
    result = engine.ai()->get(landing, eval, 0, state, env);
    assert(result.risen == 4 && result.incoming == 0 && result.readyIncoming == 0);
    state.dead = true; state.value = -1e9;
    result = engine.ai()->get(landing, eval, 1, state, env);
    assert(result.dead && result.value < -1e8);

    // Virtual garbage can block the next spawn even when the unshifted map
    // is safe; a subsequent clear must not erase that earlier death.
    landing.node = engine.context()->generate('J');
    while (landing->move_down && landing->move_down->check(map)) landing.node = landing->move_down;
    m_tetris::TetrisMap high(10,40);
    const auto* spawn = engine.context()->generate('T');
    high.row[spawn->row - 4] = spawn->data[0];
    high.roof = spawn->row - 3; high.count = 1;
    eval.shape = {0, 0, 1, &high};
    env.next = "T"; env.length = 1;
    state = {};
    assert(!engine.ai()->get(landing, eval, 0, state, env).dead);
    state.incoming = state.readyIncoming = 4;
    assert(engine.ai()->get(landing, eval, 0, state, env).dead);
    m_tetris::TetrisMap cleared(10,40);
    eval.shape = {0, 1, 0, &cleared};
    state = {}; state.risen = 4;
    assert(engine.ai()->get(landing, eval, 0, state, env).attack == 0);
    state.risen = 0;
    assert(engine.ai()->get(landing, eval, 0, state, env).attack == 5);

    // Intermediate pressure survives later downstacking in the score. Calm
    // paths have no extra cost; neither a HOLD nor a later clear resets it.
    eval.shape = {0, 0, 1, &map};
    state = {};
    const auto calm = engine.ai()->get(landing, eval, 0, state, env);
    assert(calm.pathRisk == 0);
    // Animation cost does not influence placement evaluation or garbage age.
    landing.movementSeconds=.5;
    const auto ignoredMotion=engine.ai()->get(landing,eval,0,state,env);
    assert(ignoredMotion.reward==calm.reward && ignoredMotion.elapsedSeconds==0);
    landing.movementSeconds=0;
    state.entryDelaySeconds=.2;state.lineClearDelaySeconds=.3;eval.shape.clear=1;
    const auto delayedClear=engine.ai()->get(landing,eval,0,state,env);
    state.entryDelaySeconds=state.lineClearDelaySeconds=0;
    const auto instantClear=engine.ai()->get(landing,eval,0,state,env);
    assert(std::abs(delayedClear.reward-instantClear.reward+60)<1e-9);
    state={};eval.shape.clear=0;
    state.pathRisk = 123;
    const auto exposed = engine.ai()->get(landing, eval, 0, state, env);
    assert(exposed.pathRisk == 123 && exposed.value == calm.value - 123);
    state = {}; state.incoming = 20; state.risen = 6;
    const auto pressure = engine.ai()->get(landing, eval, 0, state, env);
    assert(pressure.pathRisk == 35 * 3 * 3); // roof 1 + rise 6 + buffer 8 - 12
    state = {}; state.pathRisk = pressure.pathRisk;
    eval.shape.clear = 1;
    assert(engine.ai()->get(landing, eval, 1, state, env).pathRisk == pressure.pathRisk);

    // Both styles use exactly the same rules/geometry. Only the rewards
    // change, and a root-selected style stays fixed across hypothetical moves.
    state = {}; state.garbageHeight = 1;
    eval.clearedRows = 1;
    engine.ai()->set_policy(fire);
    const auto fireResult = engine.ai()->get(landing, eval, 0, state, env);
    engine.ai()->set_policy(urgent);
    const auto digResult = engine.ai()->get(landing, eval, 0, state, env);
    assert(fireResult.attack == digResult.attack && fireResult.b2b == digResult.b2b);
    assert(fireResult.garbageHeight == 0 && digResult.garbageHeight == 0);
    assert(std::abs(digResult.value - fireResult.value -
        ((urgent.attack - fire.attack) * fireResult.attack +
         (urgent.b2b - fire.b2b) * fireResult.b2b +
         urgent.garbageClear - fire.garbageClear)) < 1e-6);
    engine.ai()->set_policy(fire);

    // The continuation term changes only terminal value. It cannot become
    // raw attack, survive a broken combo, or accumulate as path reward.
    auto noComboPolicy = fire;
    noComboPolicy.comboContinuation = 0;
    state = {}; state.combo = 2;
    env = {};
    eval.shape = {0, 1, 1, &map};
    eval.clearedRows = 0;
    landing.spin = landing.mini = false;
    engine.ai()->set_policy(comboPolicy);
    const auto withCombo = engine.ai()->get(landing, eval, 0, state, env);
    engine.ai()->set_policy(noComboPolicy);
    const auto withoutCombo = engine.ai()->get(landing, eval, 0, state, env);
    assert(withCombo.attack == withoutCombo.attack && withCombo.reward == withoutCombo.reward);
    assert(withCombo.value - withoutCombo.value == 720);
    engine.ai()->set_policy(fire);
    eval.shape.clear = 0;
    assert(engine.ai()->get(landing, eval, 1, withCombo, env).combo == 0);
    assert(engine.ai()->get(landing, eval, 1, withCombo, env).value ==
           engine.ai()->get(landing, eval, 1, withoutCombo, env).value);

    // Known packets mature with hypothetical placements, in FIFO order; no
    // new opponent attack is manufactured. All lines consumed stay consumed.
    Bot_chatgpt::Evaluation timedEvaluation;
    timedEvaluation.init(engine.context().get());
    timedEvaluation.set_garbage_forecast({{3,-1}, {4,1.5}, {5,-3}}, 1.0);
    state = {}; state.incoming = 12; state.readyIncoming = 3;
    eval.shape = {0, 1, 1, &map};
    auto stalled = timedEvaluation.get(landing, eval, 0, state, env);
    assert(stalled.incoming == 12 && stalled.risen == 0);
    eval.shape.clear = 0;
    auto rose = timedEvaluation.get(landing, eval, 1, stalled, env);
    assert(rose.risen == 3 && rose.incoming == 9);
    rose = timedEvaluation.get(landing, eval, 2, rose, env);
    assert(rose.risen == 11 && rose.incoming == 1 && rose.readyIncoming == 1);
    state={};state.incoming=12;state.entryDelaySeconds=2;
    eval.shape.clear=1;
    stalled=timedEvaluation.get(landing,eval,0,state,env);
    eval.shape.clear=0;
    rose=timedEvaluation.get(landing,eval,1,stalled,env);
    assert(rose.elapsedSeconds==2 && rose.risen==8 && rose.incoming==4);
    state.pieceTime=true;
    eval.shape.clear=1;
    stalled=timedEvaluation.get(landing,eval,0,state,env);
    eval.shape.clear=0;
    rose=timedEvaluation.get(landing,eval,1,stalled,env);
    assert(rose.elapsedSeconds==1 && rose.risen==3);

    // Match the host's post-lock count and opener cancellation boundary. The
    // doubled cancellation is never counted as extra raw attack.
    state = {}; state.incoming = state.readyIncoming = 7;
    state.placedPieces = 13; state.openerDoubleCancelEnabled = true;
    landing.node = engine.context()->generate('T'); landing.spin = true; landing.mini = false;
    eval.shape.clear = 1;
    auto opener = engine.ai()->get(landing, eval, 0, state, env);
    assert(opener.placedPieces == 14 && opener.attack == 2 && opener.incoming == 3);
    state.placedPieces = 14;
    assert(engine.ai()->get(landing, eval, 0, state, env).incoming == 5);
    state.placedPieces = 13; state.openerDoubleCancelEnabled = false;
    assert(engine.ai()->get(landing, eval, 0, state, env).incoming == 5);
    landing.spin = false;

    Bot::Environment environment;
    environment.gravityIntervalSeconds = 0.6;
    environment.autoplaySearchWidth = 6;
    assert(Bot_chatgpt::init(environment));
    Runtime::PlayerSessionState player;
    Runtime::Playfield empty;
    empty.initialize(10,20); empty.clear();
    for (int hold : {-2,-1,0}) {
        int row = 0, col = 0, rotation = 0, kick = 0;
        const int selected = Bot_chatgpt::getpos(environment, player, empty,
            {5,1,2,3,4,6}, {}, -2,4,0,hold,row,col,rotation,kick);
        assert(selected == 5 || (hold == -1 && selected == 1) || (hold == 0 && selected == 0));
        const auto piece = Piece::get_definition(Piece::Type::from_index(selected));
        const Piece::Pose pose{{row,col},Runtime::Rotation::from_index(rotation)};
        assert(piece.can_place(pose,empty) && !piece.can_place(pose.translated(1,0),empty));
    }
    {
        // The option changes the route presenter, not the placement candidates
        // or their scores, including the cavity from replay/bug.txt.
        auto field=empty;const int rows[]{999,621,513,769,512};
        for(int y=0;y<5;++y) for(int x=0;x<10;++x) if(rows[y]&(1<<x))
            field.set_cell(19-y,x,Runtime::BoardCell::garbage());
        using Key=std::tuple<int,int,int,int,int>;
        for(double sdf:{0.,20.}) {
          std::map<Key,double> baseline;
          for(bool presentation:{false,true}) {
            auto settings=environment;settings.handlingSimulation=presentation;
            settings.softDropFactor=sdf;settings.naturalGravityEnabled=true;
            settings.rootPaceRemainingSeconds=.25;
            int row=0,col=0,rotation=0,kick=0;
            Bot_chatgpt::getpos(settings,player,field,{1,2,0,1,3,4},{},-2,4,0,6,row,col,rotation,kick);
            std::map<Key,double> scores;
            for(const auto& candidate:Bot_chatgpt::last_candidates()) {
                const Key key{candidate.type.index(),candidate.pose.position.row,candidate.pose.position.col,
                    candidate.pose.rotation.index(),candidate.rotation.rotated()?candidate.rotation.kick().index()+1:0};
                scores[key]=candidate.value;assert(candidate.movementSeconds==0);
            }
            if(!presentation) baseline=scores;else assert(scores==baseline);
          }
        }
    }
    {
        // Four O orientations and their input histories must not consume four
        // beam branches for every identical post-lock board.
        auto physical=environment;physical.handlingSimulation=true;
        physical.dasSeconds=1./6;physical.arrSeconds=1./30;physical.softDropFactor=20;
        int row=0,col=0,rotation=0,kick=0;
        assert(Bot_chatgpt::getpos(physical,player,empty,{3,5,1,2,0,4},{},
            -2,4,0,-2,row,col,rotation,kick)==3);
        const auto candidates=Bot_chatgpt::last_candidates();assert(candidates.size()==9);
        std::set<std::vector<std::pair<int,int>>> boards;
        for(const auto& candidate:candidates) {
            const auto piece=Piece::get_definition(candidate.type);
            std::vector<std::pair<int,int>> cells;
            for(const auto& offset:piece.cells(candidate.pose.rotation))
                cells.emplace_back(candidate.pose.position.row+offset[0],candidate.pose.position.col+offset[1]);
            std::sort(cells.begin(),cells.end());assert(boards.insert(cells).second);
        }
    }
    // Readiness follows the real FIFO queue, not a sum of every old packet:
    // an unready front packet blocks a ready packet behind it.
    environment.nowSeconds = 10;
    environment.incomingGarbageDelaySeconds = 2;
    player.garbage.buffer.emplace_back(3, Runtime::TimePoint::from_seconds(7));
    player.garbage.buffer.emplace_back(4, Runtime::TimePoint::from_seconds(9));
    player.garbage.buffer.emplace_back(5, Runtime::TimePoint::from_seconds(3));
    Runtime::Playfield pressured = empty;
    for (int y = 10; y < 20; ++y)
        for (int x = 0; x < 9; ++x) pressured.set_cell(y, x, Runtime::BoardCell::garbage());
    player.gameplay.playfield.garbageHeight = 10;
    int row = 0, col = 0, rotation = 0, kick = 0;
    Bot_chatgpt::getpos(environment, player, pressured, {5,1,2,3,4,6}, {},
                        -2,4,0,-2,row,col,rotation,kick);
    assert(Bot_chatgpt::last_diagnostics().incoming == 12);
    assert(Bot_chatgpt::last_diagnostics().readyIncoming == 3);
    const auto rootTrace = Bot_chatgpt::last_diagnostics();
    assert(rootTrace.rootNext == std::vector<int>({5,1,2,3,4,6}));
    assert(rootTrace.rootHold == -2 && rootTrace.rootRow == -2 && rootTrace.rootCol == 4);
    assert(rootTrace.rootGarbageHeight == 10 && rootTrace.rootB2b == player.gameplay.scoring.b2b);
    assert(rootTrace.rootPackets == (std::vector<std::pair<int,double>>{{3,-1},{4,1},{5,-5}}));
    const double originalPressure = Bot_chatgpt::last_diagnostics().digPressure;
    // Rejected automatic blends stay disabled in the production default.
    assert(originalPressure == 0);
    // Mode identifiers are not inputs to the posture selection.
    for (int model : {0, 3, 5}) {
        environment.garbageModel = model;
        Bot_chatgpt::getpos(environment, player, pressured, {5,1,2,3,4,6}, {},
                           -2,4,0,-2,row,col,rotation,kick);
        assert(Bot_chatgpt::last_diagnostics().digPressure == originalPressure);
    }
    // Root policy is refreshed, not carried over from a prior pressured game.
    player.garbage.buffer.clear();
    player.gameplay.playfield.garbageHeight = 0;
    Bot_chatgpt::getpos(environment, player, empty, {5,1,2,3,4,6}, {},
                       -2,4,0,-2,row,col,rotation,kick);
    assert(Bot_chatgpt::last_diagnostics().digPressure == 0);

    // A normal spawn can be blocked while the host-provided Clutch start is
    // legal. Search the actual root and verify its answer through host BFS.
    Runtime::Playfield clutchField = empty;
    for (int y = -3; y < 20; ++y)
        for (int x = 0; x < 9; ++x)
            clutchField.set_cell(y, x, Runtime::BoardCell::garbage());
    const auto clutchMap = BotNativeHost::make_map(clutchField);
    assert(!engine.context()->generate('I')->check(clutchMap));
    assert(!Bot_chatgpt::spawn_node(engine.context().get(), clutchMap, 'I', false));
    const auto* lifted = Bot_chatgpt::spawn_node(engine.context().get(), clutchMap, 'I', true);
    assert(lifted && lifted->check(clutchMap));
    const auto liftedPose = Bot_chatgpt::host_pose(*lifted);
    player.clutch.previousPieceCleared = true;
    environment.autoplaySearchWidth = 30;
    const int selected = Bot_chatgpt::getpos(environment, player, clutchField,
        {0,5,1,2,3,4}, {}, liftedPose.position.row, liftedPose.position.col,
        liftedPose.rotation.index(), -2, row, col, rotation, kick);
    assert(selected == 0 && Bot_chatgpt::last_diagnostics().fallbackReason == 0);
    const auto piece = Piece::get_definition(Piece::Type::from_index(selected));
    const Piece::Pose targetPose{{row,col}, Runtime::Rotation::from_index(rotation)};
    assert(piece.can_place(targetPose, clutchField));
    assert(!piece.can_place(targetPose.translated(1,0), clutchField));

    // In the host protocol, returning the current family does NOT perform a
    // HOLD reset, even when that family also occupies HOLD. A low actual start
    // on one side of a divider cannot be replaced by an above-board start.
    Runtime::Playfield divider = empty;
    for (int y = 8; y < 20; ++y) divider.set_cell(y, 5, Runtime::BoardCell::garbage());
    const Piece::Pose lowStart{{16,1},Runtime::Rotation{}};
    player.clutch.previousPieceCleared = false;
    for (int held : {-2, 5}) {
        const int chosen = Bot_chatgpt::getpos(environment, player, divider, {5,5}, {},
            lowStart.position.row, lowStart.position.col, 0, held, row, col, rotation, kick);
        assert(chosen == 5);
        const auto t = Piece::get_definition(Piece::Type::from_index(chosen));
        const auto* target = Bot_chatgpt::native_pose(engine.context().get(), 'T',
            {{row,col},Runtime::Rotation::from_index(rotation)});
        Piece::Pose reachable;
        int checkedKick = 0;
        assert(target && (BotNativeHost::reachable(target, t, divider, lowStart, false, false, reachable, checkedKick) ||
                          BotNativeHost::reachable(target, t, divider, lowStart, true, false, reachable, checkedKick) ||
                          BotNativeHost::reachable(target, t, divider, lowStart, true, true, reachable, checkedKick)));
    }

    // Spawn collision kills a non-clear branch, but a clear permits the same
    // host Clutch lift. An illegal HOLD cannot borrow its own future clear.
    eval.shape = {0, 0, clutchMap.count, &clutchMap};
    eval.before = &clutchMap;
    state = {};
    env = {}; env.next = "I"; env.length = 1;
    landing.node = lifted;
    assert(engine.ai()->get(landing, eval, 0, state, env).dead);
    eval.shape.clear = 1;
    assert(!engine.ai()->get(landing, eval, 0, state, env).dead);
    eval.shape.clear = 0;
    const auto death = engine.ai()->get(landing, eval, 0, state, env);
    eval.shape = {0, 4, 0, &cleared};
    const auto ghost = engine.ai()->get(landing, eval, 3, death, env);
    assert(ghost.dead && ghost.value == death.value && ghost.attack == death.attack);
    assert(ghost.reward == death.reward && ghost.placedPieces == death.placedPieces);
    eval.shape = {0, 1, clutchMap.count, &clutchMap};
    env.is_hold = true;
    assert(engine.ai()->get(landing, eval, 0, state, env).dead);
    state.previousCleared = true;
    assert(!engine.ai()->get(landing, eval, 0, state, env).dead);

    // The real host adapter passes numeric timing and rule capability, not a
    // test label. Simulation and paced live play both use the same forecast.
    Runtime::SessionState hostSession;
    Runtime::RuntimeServices hostServices;
    Runtime::GameContext hostContext(hostSession, hostServices);
    hostContext.config.autoplay.mode = Runtime::AutoplayMode::ChatGPT;
    hostContext.config.autoplay.searchWidth = 3;
    hostContext.config.handling.gravityInterval = Runtime::GravityInterval::periodic(
        Runtime::Duration::from_seconds(.6));
    hostContext.config.simulationPps = 2.5;
    hostContext.config.garbage.mode = Runtime::GarbageMode::Multiplayer;
    assert(Bot::init(hostContext));
    const std::vector<Piece::Type> hostNext{Piece::Type::from_index(5), Piece::Type::from_index(0)};
    Piece::Pose hostAnswer;
    Runtime::RotationRecord hostRotation;
    const auto queryHost = [&] {
        Bot::getpos(hostContext, hostSession.player, empty, hostNext, {},
            {{-2,4},Runtime::Rotation{}}, Autoplay::HoldInput{false, Piece::Type::none()},
            hostAnswer, hostRotation);
    };
    queryHost();
    assert(Bot_chatgpt::last_diagnostics().forecastStepSeconds == .4);
    assert(Bot_chatgpt::last_diagnostics().openerDoubleCancelEnabled);
    hostContext.config.simulationPps = 0;
    hostContext.config.autoplay.pps = 4;
    hostContext.config.garbage.mode = Runtime::GarbageMode::Survival;
    queryHost();
    assert(Bot_chatgpt::last_diagnostics().forecastStepSeconds == .25);
    assert(!Bot_chatgpt::last_diagnostics().openerDoubleCancelEnabled);
    hostSession.player.garbage.buffer={{maximum,Runtime::TimePoint{}},
        {maximum,Runtime::TimePoint{}}};
    queryHost();
    assert(Bot_chatgpt::last_diagnostics().incoming==maximum);
    assert(Bot_chatgpt::last_diagnostics().readyIncoming>=0);
}
