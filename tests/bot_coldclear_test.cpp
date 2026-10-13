#include "tetr/bot/bot_coldclear.h"
#include "tetr/legacy/bot_adapter.h"
#include "tetr/runtime/game_context.h"
#include "tetr/engine/locked_piece_resolution.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/rules/garbage_queue.h"
#include "../src/bot/coldclear/core.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
ColdClear::Input input_for(Runtime::GameContext& context, const Runtime::Playfield& field) {
    ColdClear::Input input;
    input.environment = Bot::make_environment(context.config);
    input.environment.secondsPerPiece = .4;
    input.root.hold = -1;
    for (int p = 0; p < 7; ++p) {
        input.pieces[p] = Piece::get_definition(input.environment.pieceTypes[p]);
        input.metadata[p] = Piece::metadata(input.environment.pieceTypes[p]);
    }
    for (int r = -Runtime::kHiddenRowCapacity; r < 20; ++r) for (int c = 0; c < 10; ++c)
        if (field.cell(r,c).occupied()) {
            input.root.board.rows[19-r] |= 1u << c;
            if (field.cell(r,c).is_piece()) input.root.board.pieceRows[19-r] |= 1u << c;
        }
    return input;
}
void terrain_goldens() {
    std::ifstream file(COLDCLEAR_FIXTURE);
    require(bool(file),"cannot read upstream terrain fixtures");
    std::string line;
    int count = 0;
    while (std::getline(file,line)) {
        if (line.empty() || line[0] == '#') continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::replace(line.begin(),line.end(),';',' ');
        std::istringstream stream(line);
        int b2b,t,expected; stream >> b2b >> t >> expected;
        ColdClear::Board board;
        int row = 0, value;
        while (stream >> value) board.rows[row++] = value;
        const int actual = ColdClear::terrain(board,b2b,t);
        if (actual != expected) {
            std::cerr << "Terrain fixture " << count << " t=" << t << " expected=" << expected << " actual=" << actual << '\n';
            throw std::runtime_error("Standard evaluation differs from original Rust");
        }
        ++count;
    }
    require(count == 696,"incomplete terrain parity fixtures");
    require((ColdClear::Value{10,4}.add({3,-1}) == ColdClear::Value{13,0}),"spike must reset on non-clear");
    require((ColdClear::Value{10,4}.add({3,2}) == ColdClear::Value{13,6}),"spike must accumulate clears");
    require(ColdClear::Value{100,0}.weight({0,0},0) == 12100,"wrong original MCTS weight");
}

void compare_lock(Runtime::GameContext& context, Runtime::Playfield field,
                  ColdClear::Input input, ColdClear::Move move) {
    Runtime::PlayerSessionState player;
    player.gameplay.playfield.activeHeight = 20;
    player.gameplay.playfield.garbageHeight = input.root.garbageHeight;
    player.gameplay.scoring.combo = input.root.combo;
    player.gameplay.scoring.b2b = input.root.b2b;
    auto piece = input.pieces[move.piece];
    require(piece.can_place(move.pose,field),"invalid comparison fixture pose");
    const auto actual = ColdClear::lock_move(input,input.root,move);
    const auto expected = Engine::resolve_locked_piece(context,field,piece,player,move.pose,move.rotation);
    require(actual.lock.clear.clearedLines == expected.clear.clearedLines,"clear mismatch");
    require(actual.lock.clear.clearedGarbageLines == expected.clear.clearedGarbageLines,"garbage clear mismatch");
    require(actual.lock.clear.perfectClear == expected.clear.perfectClear,"PC mismatch");
    require(actual.lock.clear.halfClear == expected.clear.halfClear,"half clear mismatch");
    require(actual.lock.spin.spin == expected.spin.spin && actual.lock.spin.mini == expected.spin.mini,"spin mismatch");
    require(actual.lock.chain.b2b == expected.chain.b2b && actual.lock.chain.combo == expected.chain.combo,"chain mismatch");
    require(actual.lock.attack.attack == expected.attack.attack,"host damage mismatch");
    require(actual.lock.attack.perfectClearAttack == expected.attack.perfectClearAttack,"PC damage mismatch");
    require(actual.lock.attack.halfClearAttack == expected.attack.halfClearAttack,"half damage mismatch");
}
void rule_parity(Runtime::GameContext& context) {
    Runtime::Playfield field; field.initialize(10,20);
    context.config.garbage.mode = Runtime::GarbageMode::Multiplayer;
    for (auto mode : {Runtime::SpinBonuses::AllMiniPlus,Runtime::SpinBonuses::AllSpinPlus,
                      Runtime::SpinBonuses::TSpins,Runtime::SpinBonuses::None,Runtime::SpinBonuses::Handheld}) {
        context.config.spinBonuses = mode;
        for (int b2b : {0,1,4,10}) for (int combo : {0,1,5}) {
            // Reachable TSD geometry with three occupied corners.
            field.clear();
            for (int c = 0; c < 10; ++c) {
                if (c != 4) field.set_cell(19,c,Runtime::BoardCell::garbage());
                if (c < 3 || c > 5) field.set_cell(18,c,Runtime::BoardCell::garbage());
            }
            field.set_cell(17,3,Runtime::BoardCell::piece(1));
            auto input = input_for(context,field);
            input.root.b2b = b2b; input.root.combo = combo; input.root.garbageHeight = 2;
            compare_lock(context,field,input,{5,{{18,4},Runtime::Rotation{2}},Runtime::RotationRecord::from_kick(Piece::KickIndex{0}),3,false});
            // Quad with a garbage-special bonus; also exercises PC and B2B charge.
            field.clear();
            for (int r = 16; r < 20; ++r) for (int c = 0; c < 10; ++c)
                if (c != 4) field.set_cell(r,c,Runtime::BoardCell::garbage());
            input = input_for(context,field);
            input.root.b2b = b2b; input.root.combo = combo; input.root.garbageHeight = 4;
            auto moves = ColdClear::find_moves(input,input.root,0,{{-2,4},{}},false);
            bool found = false;
            for (auto move : moves) {
                const auto child = ColdClear::lock_move(input,input.root,move);
                if (child.lock.clear.clearedLines != 4) continue;
                compare_lock(context,field,input,move); found = true; break;
            }
            require(found,"quad fixture has no legal I placement");
        }
    }
    context.config.spinBonuses = Runtime::SpinBonuses::AllMiniPlus;
    context.config.garbage.mode = Runtime::GarbageMode::None;
}

void custom_reward_parity(Runtime::GameContext& context) {
    Runtime::Playfield field; field.initialize(10,20);
    for (int row=18;row<20;++row) for (int col=0;col<10;++col)
        if (col!=3 && col!=4) field.set_cell(row,col,Runtime::BoardCell::garbage());
    field.set_cell(16,0,Runtime::BoardCell::garbage());
    context.config.attackRules.halfClearEnabled=true;
    context.config.attackRules.halfClearAttack=7;
    context.config.attackRules.halfClearCharge=3;
    context.config.attackRules.b2bReward=Runtime::B2BReward::Chaining;
    context.config.attackRules.b2bChargeReduction=-2;
    auto input=input_for(context,field);
    input.root.b2b=10;
    bool found=false;
    for(const auto& move:ColdClear::find_moves(input,input.root,3,{{-2,4},{}},false)) {
        const auto result=ColdClear::lock_move(input,input.root,move);
        if(result.lock.clear.clearedLines!=2) continue;
        require(result.lock.clear.halfClear && result.lock.chain.b2b==13,"custom half-clear fixture");
        compare_lock(context,field,input,move);
        found=true; break;
    }
    require(found,"half-clear fixture has no legal O placement");
    context.config.attackRules={};
}

void rotation_parity(Runtime::GameContext& context) {
    Runtime::Playfield field; field.initialize(10,20);
    for (int c = 0; c < 10; ++c) for (int r = 20-c%5; r < 20; ++r)
        field.set_cell(r,c,Runtime::BoardCell::garbage());
    for (auto system : {Runtime::RotationSystem::SrsPlus,Runtime::RotationSystem::Srs,Runtime::RotationSystem::Ars,
                        Runtime::RotationSystem::SrsX,Runtime::RotationSystem::TetraX,Runtime::RotationSystem::Nrs,
                        Runtime::RotationSystem::Asc,Runtime::RotationSystem::None}) for (bool legacy : {false,true}) {
        context.config.rotationSystem = system; context.config.legacyRotationRules = legacy;
        const auto input = input_for(context,field);
        for (int p = 0; p < 7; ++p) for (int row = 12; row < 20; ++row) for (int col = 0; col < 10; ++col)
            for (int rotation = 0; rotation < 4; ++rotation) {
                const auto& piece = input.pieces[p];
                const Piece::Pose start{{row,col},Runtime::Rotation{rotation}};
                require(input.root.board.can_place(piece,start) == piece.can_place(start,field),"bitboard collision mismatch");
                if (!piece.can_place(start,field)) continue;
                for (int delta = 1; delta <= 3; ++delta) {
                    auto actual = start, expected = start;
                    Piece::KickIndex actualKick, expectedKick;
                    const auto target = start.rotation.rotated(delta);
                    const bool a = input.root.board.try_rotate(piece,actual,target,actualKick);
                    const bool b = piece.try_rotate(expected,target,field,expectedKick);
                    require(a == b && (!a || (actual == expected && actualKick == expectedKick)),"bitboard kick differs from host");
                }
            }
        auto& player = context.session.player;
        player.gameplay.playfield.field.initialize(10,20);
        context.config.autoplay.mode = Runtime::AutoplayMode::ColdClear;
        context.config.autoplay.searchWidth = 1;
        const auto type = input.environment.pieceTypes[5];
        const auto start = ColdClear::spawn(input,ColdClear::State{},5);
        auto answer = start; Runtime::RotationRecord rotation;
        require(Bot::init(context),"standard rotation unavailable through menu dispatch");
        const auto selected = Bot::getpos(context,player,player.gameplay.playfield.field,{type},{},start,{false,{}},answer,rotation);
        const auto diagnostic = Bot_coldclear::last_diagnostics();
        require(selected == type && diagnostic.selected == 5 && diagnostic.target == answer,"standard piece ID lost in dispatch");
    }
    context.config.rotationSystem = Runtime::RotationSystem::SrsPlus; context.config.legacyRotationRules = false;
}

void execute_script(Runtime::GameContext& context, const Runtime::Playfield& field,
                    Piece::Type selected, Piece::Pose start, Piece::Pose target, Runtime::RotationRecord rotation) {
    const auto piece = Piece::get_definition(selected);
    const auto script = Bot::getop(context,field,piece,start,target,rotation);
    require(!script.empty() && script.back() == 'H',"Cold Clear answer is unreachable");
    Engine::MovementState state; state.pose = start;
    for (char code : script) Engine::apply_movement_script_command(state,Engine::decode_movement_script_command(code),
        [&](Piece::Pose p) { return piece.can_place(p,field); },
        [&](Piece::Pose& p, Runtime::Rotation r, Piece::KickIndex& k) { return piece.try_rotate(p,r,field,k); },
        context.config.handling.das == Runtime::Duration{},context.config.handling.arr == Runtime::Duration{},
        context.config.movement,context.config.handling.gravityInterval.is_instant(),
        context.config.handling.gravityInterval.is_instant() || context.config.handling.softDropFactor <= 0);
    require(Engine::autoplay_landing_matches(field,piece,state,target,rotation,Engine::autoplay_spin_rules(context.config)),
        "script playback changed Cold Clear placement/spin");
}

void controls_and_hold(Runtime::GameContext& context) {
    auto& player = context.session.player;
    auto& field = player.gameplay.playfield.field;
    field.initialize(10,20);
    player.gameplay.playfield.activeHeight = 20;
    context.config.autoplay.mode = Runtime::AutoplayMode::ColdClear;
    context.config.autoplay.searchWidth = 4;
    const auto types = Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    for (bool instant : {false,true}) for (bool zeroDas : {false,true}) for (unsigned mask : {0u,1u,2u,4u,8u,16u,32u,63u}) {
        context.config.handling.gravityInterval = instant ? Runtime::GravityInterval::instant() : Runtime::GravityInterval::disabled();
        context.config.handling.das = Runtime::Duration::from_seconds(zeroDas ? 0 : .1);
        context.config.movement = Runtime::MovementRestrictions::from_mask(mask);
        require(Bot::init(context),"supported movement settings rejected");
        for (int held : {-2,-1,0}) {
            field.clear();
            for (int c = 0; c < 10; ++c) for (int r = 20-c%3; r < 20; ++r)
                field.set_cell(r,c,Runtime::BoardCell::piece(1));
            const std::vector<Piece::Type> next{types[5],types[0],types[3]};
            const auto heldPiece = held >= 0 ? types[held] : Piece::Type::none();
            const Piece::Pose start{{-2,4},{}};
            auto answer = start; Runtime::RotationRecord rotation;
            const auto selected = Bot::getpos(context,player,field,next,{},start,{held != -2,heldPiece},answer,rotation);
            const auto diagnostic = Bot_coldclear::last_diagnostics();
            require(!diagnostic.fallback && diagnostic.target == answer && types[diagnostic.selected] == selected,"host rejected Cold Clear target");
            require(selected == next.front() || (held != -2 && selected == types[0]),"invalid hold choice");
            const auto definition = Piece::get_definition(selected);
            require(definition.can_place(answer,field) && !definition.can_place(answer.translated(1,0),field),"illegal answer");
            auto plannedStart = start;
            if (selected != next.front()) {
                const auto meta = Piece::metadata(selected);
                plannedStart = {{-2+meta.rotationOrigins[meta.spawnRotation][0],4+meta.rotationOrigins[meta.spawnRotation][1]},Runtime::Rotation{meta.spawnRotation}};
            }
            execute_script(context,field,selected,plannedStart,answer,rotation);
        }
    }
    context.config.handling.das = Runtime::Duration::from_seconds(.1);
    context.config.handling.gravityInterval = Runtime::GravityInterval::disabled();
    context.config.movement = {};
    // NEXT=0 cannot use an unseen piece for empty HOLD.
    auto answer = Piece::Pose{{-2,4},{}}; Runtime::RotationRecord rotation;
    field.clear();
    require(Bot::getpos(context,player,field,{types[3]}, {types[0],types[1],types[2],types[4],types[5],types[6]},answer,{true,Piece::Type::none()},answer,rotation) == types[3],"empty HOLD used an invisible NEXT");
}

void speculation_and_garbage(Runtime::GameContext& context) {
    Runtime::Playfield field; field.initialize(10,20);
    context.config.autoplay.searchWidth = 30;
    auto input = input_for(context,field);
    input.next = {3}; input.canHold = false;
    input.start = {{-2,4},{}}; input.root.bag = 1u<<0;
    Bot_coldclear::Diagnostics diagnostics;
    const auto result = ColdClear::search(input,diagnostics);
    require(result.move.piece == 3 && diagnostics.speculations > 0 && diagnostics.depth > 1,"missing bag-tail speculation");
    require(diagnostics.transpositions > 0,"DAG does not merge equivalent boards");
    auto env = input.environment;
    env.garbageBufferEnabled = true; env.garbageCap = 4; env.secondsPerPiece = .5;
    input.environment = env;
    input.root.incoming = {{8,Runtime::TimePoint::from_seconds(-10)}};
    const auto move = ColdClear::find_moves(input,input.root,3,input.start,false).front();
    const auto rise = ColdClear::lock_move(input,input.root,move);
    require(rise.state.rise == 4 && Rules::pending_garbage(rise.state.incoming) == 4,"garbage cap/rise mismatch");
    input.root.incoming = {{8,Runtime::TimePoint::from_seconds(10)}};
    const auto future = ColdClear::lock_move(input,input.root,move);
    require(future.state.rise == 0,"future garbage rose before its delay");
    // Host opener cancellation and fractional carry must agree for a quad.
    for (int r = 16; r < 20; ++r) for (int c = 0; c < 10; ++c) if (c != 4)
        field.set_cell(r,c,Runtime::BoardCell::garbage());
    input = input_for(context,field);
    input.environment.garbageBufferEnabled = true;
    input.environment.openerDoubleCancelEnabled = true;
    input.environment.cancellationMultiplier = .5;
    input.root.garbageHeight = 4;
    input.root.incoming = {{20,Runtime::TimePoint::from_seconds(-10)}};
    for (auto candidate : ColdClear::find_moves(input,input.root,0,{{-2,4},{}},false)) {
        const auto child = ColdClear::lock_move(input,input.root,candidate);
        if (child.lock.clear.clearedLines != 4) continue;
        auto queue = input.root.incoming;
        auto carry = input.root.cancellation;
        Rules::GarbageCancelInput cancel;
        cancel.attack = child.lock.attack.attack; cancel.perfectClearAttack = child.lock.attack.perfectClearAttack;
        cancel.placedPieces = 1; cancel.multiplayerEnabled = true; cancel.cancellationMultiplier = .5;
        Rules::cancel_incoming_garbage(queue,cancel,&carry);
        require(Rules::pending_garbage(queue) == Rules::pending_garbage(child.state.incoming),"opener cancellation mismatch");
        require(child.state.rise == 0,"clearing placement must stall garbage");
        return;
    }
    throw std::runtime_error("missing cancellation quad");
}

void clutch_hidden_rows(Runtime::GameContext& context) {
    Runtime::Playfield field; field.initialize(10,20);
    for (int r = -96; r < 20; ++r) for (int c = 0; c < 9; ++c) field.set_cell(r,c,Runtime::BoardCell::garbage());
    auto input = input_for(context,field); input.root.previousCleared = true;
    const auto lifted = ColdClear::spawn(input,input.root,5);
    require(lifted.position.row < -95 && input.pieces[5].can_place(lifted,field),"Clutch lift lost deep hidden rows");
    auto& player = context.session.player;
    player.clutch.previousPieceCleared = true;
    player.gameplay.playfield.garbageHeight = 116;
    context.config.autoplay.mode = Runtime::AutoplayMode::ColdClear;
    context.config.autoplay.searchWidth = 1;
    const auto types = input.environment.pieceTypes;
    auto answer = lifted; Runtime::RotationRecord rotation;
    const auto selected = Bot::getpos(context,player,field,{types[5],types[0],types[3]},{},lifted,{true,types[0]},answer,rotation);
    const auto diagnostic = Bot_coldclear::last_diagnostics();
    require(selected == types[0] && diagnostic.target == answer,"adapter lost a HOLD reached through Clutch lift");
    player.gameplay.playfield.garbageHeight = 0;
    player.clutch.previousPieceCleared = false;
    input.root.previousCleared = false;
    require(!input.pieces[5].can_place(ColdClear::spawn(input,input.root,5),field),"Clutch lift must require a preceding clear");
}

void twenty_g_moves_cannot_cross_a_pit(Runtime::GameContext& context) {
    const auto saved=context.config;
    context.config={};
    context.config.handling.gravityInterval=Runtime::GravityInterval::instant();
    auto& rules=context.config.movement;
    rules.disableLeft=rules.disableClockwise=rules.disableCounterclockwise=rules.disable180=rules.disableSoftDrop=true;
    Runtime::Playfield field;field.initialize(10,20);
    for(int row=10;row<20;++row)for(int col=0;col<10;++col)
        if(col!=3 && col!=4)field.set_cell(row,col,Runtime::BoardCell::garbage());
    for(bool zeroDas:{false,true})for(bool zeroArr:{false,true}) {
        context.config.handling.das=Runtime::Duration::from_seconds(zeroDas ? 0 : .1);
        context.config.handling.arr=Runtime::Duration::from_seconds(zeroArr ? 0 : .02);
        const auto input=input_for(context,field);
        const auto moves=ColdClear::find_moves(input,input.root,3,{{0,1},Runtime::Rotation{}},false);
        require(!moves.empty(),"20G pit must retain legal placements");
        for(const auto& move:moves) {
            require(move.pose.position.col>=1 && move.pose.position.col<=3,
                "20G search crossed a pit without settling each horizontal cell");
            require(input.pieces[3].can_place(move.pose,field) &&
                !input.pieces[3].can_place(move.pose.translated(1,0),field),"20G target is not grounded");
        }
    }
    context.config=saved;
}

void simulation(Runtime::GameContext& context, int placements, int width, unsigned seed) {
    auto& player = context.session.player;
    context.config = Runtime::RuntimeConfig{};
    context.config.autoplay.mode = Runtime::AutoplayMode::ColdClear;
    context.config.autoplay.searchWidth = width;
    context.config.handling.gravityInterval = Runtime::GravityInterval::disabled();
    context.config.garbage.mode = Runtime::GarbageMode::Multiplayer;
    auto& field = player.gameplay.playfield.field; field.initialize(10,20);
    player.gameplay.playfield.activeHeight = 20;
    player.gameplay.scoring = {};
    player.clutch = {};
    player.garbage.buffer.clear();
    const auto types = Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    std::mt19937 random(seed);
    std::vector<int> queue;
    for (int i = 0; i < placements*2+14; i += 7) {
        std::array<int,7> bag{0,1,2,3,4,5,6}; std::shuffle(bag.begin(),bag.end(),random);
        queue.insert(queue.end(),bag.begin(),bag.end());
    }
    int cursor = 0, held = -1, lines = 0, attack = 0;
    double elapsed = 0;
    for (int i = 0; i < placements; ++i) {
        std::vector<Piece::Type> next;
        for (int j = 0; j < 6; ++j) next.push_back(types[queue[cursor+j]]);
        std::vector<Piece::Type> unseen;
        const int end = cursor+5;
        for (int j = end+1; j < ((end/7)+1)*7; ++j) unseen.push_back(types[queue[j]]);
        auto meta = Piece::metadata(next.front());
        auto start = Piece::Pose{{-2+meta.rotationOrigins[meta.spawnRotation][0],4+meta.rotationOrigins[meta.spawnRotation][1]},Runtime::Rotation{meta.spawnRotation}};
        auto input = input_for(context,field); input.root.previousCleared = player.clutch.previousPieceCleared;
        start = ColdClear::spawn(input,input.root,queue[cursor]);
        require(Piece::get_definition(next.front()).can_place(start,field),"Cold Clear simulation topped out");
        auto answer = start; Runtime::RotationRecord rotation;
        const auto selected = Bot::getpos(context,player,field,next,unseen,start,{true,held < 0 ? Piece::Type::none() : types[held]},answer,rotation);
        const auto diagnostics = Bot_coldclear::last_diagnostics();
        require(!diagnostics.fallback,"unexpected search failure in seven-bag simulation");
        elapsed += diagnostics.milliseconds;
        if (selected != next.front()) {
            start = ColdClear::spawn(input,input.root,Piece::metadata(selected).familyIndex);
            const int oldCurrent = queue[cursor++];
            if (held < 0) ++cursor;
            held = oldCurrent;
        } else ++cursor;
        execute_script(context,field,selected,start,answer,rotation);
        auto piece = Piece::get_definition(selected);
        const auto lock = Engine::resolve_locked_piece(context,field,piece,player,answer,rotation);
        if (lock.attack.attack != diagnostics.attack || lock.spin.spin != diagnostics.spin || lock.clear.clearedLines != diagnostics.clearedLines) {
            std::cerr << "forecast placement=" << i << " selected=" << selected.index() << " pose=" << answer.position.row << ',' << answer.position.col << ',' << answer.rotation.index()
                      << " expected_attack=" << lock.attack.attack << " forecast_attack=" << diagnostics.attack
                      << " expected_spin=" << lock.spin.spin << " forecast_spin=" << diagnostics.spin
                      << " raw_pose=" << diagnostics.target.position.row << ',' << diagnostics.target.position.col << ',' << diagnostics.target.rotation.index()
                      << " raw_kick=" << diagnostics.rotation.kick().index() << " host_kick=" << rotation.kick().index()
                      << " expected_lines=" << lock.clear.clearedLines << " forecast_lines=" << diagnostics.clearedLines << '\n';
            throw std::runtime_error("search forecast differs from engine");
        }
        player.gameplay.scoring.b2b = lock.chain.b2b;
        player.gameplay.scoring.combo = lock.chain.combo;
        ++player.gameplay.scoring.placedBlocks;
        player.clutch.previousPieceCleared = lock.clear.clearedLines != 0;
        lines += lock.clear.clearedLines; attack += lock.attack.attack;
        int dest = 19;
        for (int r = 19; r >= -100; --r) {
            if (std::find(lock.clear.clearedRows.begin(),lock.clear.clearedRows.end(),r) != lock.clear.clearedRows.end()) continue;
            if (dest != r) field.copy_row(dest,r);
            --dest;
        }
        for (; dest >= -100; --dest) for (int c = 0; c < 10; ++c) field.set_cell(dest,c,Runtime::BoardCell::empty());
    }
    require(lines > placements/10,"Cold Clear is not clearing lines in simulation");
    std::cout << "placements=" << placements << " width=" << width << " seed=" << seed << " lines=" << lines
              << " attack=" << attack << " mean_search_ms=" << elapsed/placements << '\n';
}
}

int main(int argc, char** argv) {
    try {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        if (argc > 1) { simulation(context,std::stoi(argv[1]),argc > 2 ? std::stoi(argv[2]) : 300,argc > 3 ? std::stoul(argv[3]) : 279); return 0; }
        terrain_goldens();
        rotation_parity(context);
        rule_parity(context);
        custom_reward_parity(context);
        controls_and_hold(context);
        twenty_g_moves_cannot_cross_a_pit(context);
        speculation_and_garbage(context);
        clutch_hidden_rows(context);
        simulation(context,80,30,279);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
