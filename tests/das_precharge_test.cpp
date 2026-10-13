#include "tetr/app/game.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/engine/entry_delay.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/pause.h"
#include "tetr/engine/player_input.h"
#include "tetr/engine/undo.h"
#include "tetr/input/services.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/render/board_renderer.h"
#include "support/terminal_screen.h"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
using Clock=std::chrono::steady_clock;
Runtime::Duration dur(double t) { return Runtime::Duration::from_seconds(t); }
constexpr int left=20, right=10, hardDrop=30, clockwise=31;
struct Capture {
    std::ostringstream stream;
    std::streambuf* original=std::cout.rdbuf(stream.rdbuf());
    ~Capture() { std::cout.rdbuf(original); }
};
struct Fixture {
    Runtime::SessionState session;
    Runtime::RuntimeServices runtime;
    Runtime::GameContext context{session,runtime};
    UserConfig::Settings user;
    Replay::ReplaySessionState replay;
    Input::KeyEventBuffer events;
    Input::Services input;
    Engine::PieceLoopState loop;
    Fixture() {
        std::array<int,Input::kBindableActionCount> codes;
        codes.fill(-1); codes[0]=left; codes[1]=right; codes[3]=hardDrop;
        codes[4]=clockwise; codes[8]=40; codes[9]=41; codes[11]=42;
        input.bindings=Input::BindingSet::from_key_codes(codes);
        input.keyEventSource=&events;
        input.keyStateProvider=[this](int key) {
            std::uint64_t cursor=0;
            const auto down=events.snapshot(cursor);
            const auto found=down.find(key);
            return found!=down.end() && found->second;
        };
        context.attach_input(input); context.attach_replay(replay); context.attach_user_settings(user);
        context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        context.config.field.nextCount=0;
        context.config.handling.gravityInterval=Runtime::GravityInterval::disabled();
        context.config.handling.das=dur(.2); context.config.handling.arr=dur(.1);
        auto& player=session.player;
        player.presentation.margin=30;
        player.gameplay.playfield.width=10;
        player.gameplay.playfield.visibleHeight=20;
        player.gameplay.playfield.nextCount=0;
        player.pieces.allowedPieces={Piece::Type::from_index(3)};
        Engine::initialize_board(context,player);
    }
    void key(int code,bool down,Clock::time_point time=Clock::now()) { events.update(code,down,time); }
    auto spawn() { return Engine::make_spawn_movement_state(10,Piece::Type::from_index(3)); }
};

void switches_control_spawn_and_charge_independently() {
    for(bool charge:{false,true}) for(bool preMove:{false,true}) {
        Fixture f; Capture output;
        f.context.config.handling.dasPrecharge=charge;
        f.context.config.handling.preMove=preMove;
        f.context.config.field.entryDelay=dur(.05);
        f.key(left,true,Clock::now()-std::chrono::seconds(30));
        Input::GameplayInputScope capture(&f.input);
        Engine::schedule_entry_delay(f.context,f.session.player);
        assert(Engine::wait_for_entry_delay(f.context,f.session.player,[] {
            return Rules::PlayResult::ContinueFrame;
        },&f.loop.input)==Rules::PlayResult::ContinueFrame);
        assert((f.loop.input.phase(left)==Engine::KeyPhase::Repeating)==charge);
        auto state=f.spawn(); const auto before=state.pose;
        assert(Engine::apply_initial_movement(f.context,f.loop.input,state,
            [](Piece::Pose) { return true; })==(charge && preMove));
        assert(state.pose.position.col==before.position.col-(charge && preMove ? 1 : 0));
        Engine::MovementController movement(state,[](Piece::Pose p) { return p.position.col>=0 && p.position.col<10; },
            [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
        Engine::PlayerInputConfig config; config.das=dur(.2); config.arr=dur(.1);
        const auto start=f.loop.input.timestamp(left);
        const auto frame=[&](Runtime::TimePoint now) {
            Engine::process_player_input_frame(f.loop.input,movement,f.input.bindings,{}, {left,right},config,
                [&](int key) { return f.input.gameplay_key_down(key); },[&] { return now; });
        };
        if (!charge) {
            frame(start+dur(.1));
            assert(state.pose==before);
            frame(start+dur(.2));
            assert(state.pose.position.col==before.position.col-1);
        } else if (!preMove) {
            frame(start+dur(.100001));
            assert(state.pose.position.col==before.position.col-1); // Charged DAS still works after spawning.
        }
    }
    // With DAS=0 there is no waiting time to charge; Pre-move still controls
    // the spawn shift when the separate precharge switch is disabled.
    for(bool preMove:{false,true}) {
        Fixture f;
        f.context.config.handling.dasPrecharge=false;
        f.context.config.handling.preMove=preMove;
        f.context.config.handling.das={};
        f.key(left,true);
        Input::GameplayInputScope capture(&f.input);
        auto state=f.spawn(); const auto before=state.pose;
        assert(Engine::apply_initial_movement(f.context,f.loop.input,state,
            [](Piece::Pose) { return true; })==preMove);
        assert(state.pose.position.col==before.position.col-(preMove ? 1 : 0));
    }
}

void disabled_pause_precharge_freezes_existing_charge() {
    Fixture f;
    f.context.config.handling.dasPrecharge=false;
    f.loop.input.beginPress(left,true,f.context.real_now()-dur(.05));
    const auto timestamp=f.loop.input.timestamp(left);
    Engine::GamePauseScope scope(f.context,&f.loop.input);
    f.context.pause.started=f.runtime.clock.now();
    f.context.pause.paused=true;
    f.key(left,true);
    f.context.pause.prechargeMovement();
    f.runtime.clock.sleep_for(dur(.3));
    f.context.pause.prechargeMovement();
    assert(f.loop.input.timestamp(left)==timestamp);
    assert(!f.loop.input.horizontalCharged(left,f.context.real_now(),dur(.2)));
}

void switches_persist_reset_and_roundtrip_replays() {
    const auto file=std::filesystem::temp_directory_path()/"tetr_precharge_switches.ini";
    for(bool charge:{false,true}) for(bool preMove:{false,true}) {
        UserConfig::Settings user;
        assert(user.preMove==1 && user.dasPrecharge==1);
        user.preMove=preMove; user.dasPrecharge=charge; user.persistencePath=file.string();
        assert(UserConfig::save(user));
        UserConfig::Settings loaded;
        UserConfig::load(loaded,file.string());
        assert(loaded.preMove==preMove && loaded.dasPrecharge==charge);
        const auto config=Runtime::normalize_user_config(loaded,Runtime::GameMode::Zen);
        assert(config.handling.preMove==preMove && config.handling.dasPrecharge==charge);
        Runtime::RuntimeConfig decoded;
        decoded.handling.preMove=!preMove; decoded.handling.dasPrecharge=!charge;
        Replay::ConfigPrefixResult prefix;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,prefix));
        assert(decoded.handling.preMove==preMove && decoded.handling.dasPrecharge==charge);
        UserConfig::reset_settings(loaded,false);
        assert(loaded.preMove==preMove && loaded.dasPrecharge==charge);
        UserConfig::reset_section(loaded,UserConfig::Section::Handling,false);
        assert(loaded.preMove==1 && loaded.dasPrecharge==1);
    }
    std::filesystem::remove(file);
    Runtime::RuntimeConfig decoded;
    decoded.handling.preMove=false; decoded.handling.dasPrecharge=false;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix("{}",decoded,prefix));
    assert(decoded.handling.preMove && decoded.handling.dasPrecharge);
}

void countdown_charges_and_discards_other_prestart_actions() {
    Fixture f; Capture output;
    f.context.config.mode.mode=Runtime::GameMode::Racing;
    Input::GameplayInputScope capture(&f.input);
    f.key(left,true);
    f.key(hardDrop,true); f.key(hardDrop,false);
    f.key(clockwise,true); f.key(clockwise,false);
    assert(Engine::run_start_countdown(f.context,f.session.player,f.session.opponent,&f.loop.input)==Engine::CountdownResult::Completed);
    assert(f.loop.input.horizontalCharged(left,f.context.real_now(),dur(.2)));
    assert(f.session.player.gameplay.scoring.placedBlocks==0);
    Engine::finish_start_precharge(f.context,f.loop.input);
    assert(!f.input.next_gameplay_event());
    auto state=f.spawn(); const auto before=state.pose;
    assert(Engine::apply_initial_movement(f.context,f.loop.input,state,[](Piece::Pose) { return true; }));
    assert(state.pose.position.col==before.position.col-1);
    f.loop.input.preparePiece(f.input.bindings,[&](int key) { return f.input.gameplay_key_down(key); },f.context.real_now());
    Engine::MovementController movement(state,[](Piece::Pose p) { return p.position.col>=0 && p.position.col<10; },
        [](Piece::Pose& p,Runtime::Rotation r,Piece::KickIndex&) { p.rotation=r; return true; });
    Engine::PlayerInputConfig config; config.das=dur(.2); config.arr=dur(.1);
    const auto result=Engine::process_buffered_player_input_frame(f.loop.input,movement,f.input,{clockwise},{left,right},
        config,f.context.real_now(),[&](auto t) { return f.runtime.clock.at(t); });
    assert(result.command==Engine::PlayerInputCommand::None && state.pose.rotation==before.rotation);
}
void countdown_preserves_uncovered_cells() {
    const std::array<std::array<const char*,5>,3> masks={{{{"#####","....#","..###","....#","#####"}},
        {{"#####","....#","#####","#....","#####"}},{{"..#..","..#..","..#..","..#..","..#.."}}}};
    for(int style:{0,1}) {
        Fixture f;Capture output;auto& player=f.session.player;
        f.context.config.mode.mode=Runtime::GameMode::Racing;f.context.config.display.squareStyle=style;
        for(int row=1;row<=5;++row) for(int col=0;col<10;++col) player.gameplay.playfield.field.set_cell(row,col,Runtime::BoardCell::piece(0));
        Test::TerminalScreen screen;std::size_t processed=0;std::array<bool,3> seen{};
        const int top=player.presentation.topMargin+2,left=player.presentation.margin+8;
        const auto read=[&] {const auto value=output.stream.str();screen.feed(value.substr(processed));processed=value.size();};
        const auto verify=[&](int stage) {
            for(int row=0;row<5;++row) for(int col=0;col<20;++col) {
                const int digitColumn=col-7;
                if(stage>=0 && digitColumn>=0 && digitColumn<5 && masks[stage][row][digitColumn]=='#') continue;
                const auto value=screen.at(top+row,player.presentation.margin+1+col);
                assert(value.glyph==(style==0 || col%2==0 ? "█" : " "));
                if(value.glyph=="█") assert(value.foreground==Render::cell_colors(0,false).foreground);
            }
        };
        f.runtime.clock.waitInterrupt=[&] {
            read();int stage=-1;
            if(screen.at(top,left+2).glyph=="┓") stage=2;
            else if(screen.at(top,left).glyph=="┏") stage=screen.at(top+2,left).glyph=="┏" ? 1 : 0;
            if(stage>=0 && !seen[stage]) {verify(stage);seen[stage]=true;}
        };
        assert(Engine::run_start_countdown(f.context,player,f.session.opponent)==Engine::CountdownResult::Completed);
        f.runtime.clock.waitInterrupt={};read();verify(-1);assert(std::all_of(seen.begin(),seen.end(),[](bool value){return value;}));
        for(int row=1;row<=5;++row) for(int col=0;col<10;++col) assert(player.gameplay.playfield.field.cell(row,col)==Runtime::BoardCell::piece(0));
    }
}

void held_baseline_order_and_nested_piece_loop() {
    Fixture f; Capture output;
    f.key(left,true,Clock::now()-std::chrono::seconds(30));
    f.key(right,true,Clock::now()-std::chrono::seconds(20));
    Input::GameplayInputScope capture(&f.input);
    Engine::precharge_movement_input(f.context,f.loop.input);
    assert(f.loop.input.mostRecentActive({left,right})==right); // Newest physical press, despite a lower key code.
    f.key(right,false); Engine::precharge_movement_input(f.context,f.loop.input);
    f.key(clockwise,true); f.input.poll_gameplay_events();
    assert(f.input.next_gameplay_event());
    const auto result=Engine::run_piece_loop(f.context,f.session.player,
        [&](Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState&,bool) {
            assert(f.input.gameplay_input_active());
            assert(f.input.next_gameplay_event());
            assert(f.loop.input.horizontalCharged(left,f.context.real_now(),dur(.2)));
            return Rules::PlayResult::Retry;
        },&f.loop);
    assert(result==Engine::GameLoopExit::Retry && f.input.gameplay_input_active());
    capture.end(); assert(!f.input.gameplay_input_active());
}

void clear_and_entry_delays_share_charge() {
    Fixture f; Capture output;
    Input::GameplayInputScope capture(&f.input);
    f.context.config.field.lineClearDelay=dur(.15);
    f.context.config.field.entryDelay=dur(.2);
    Rules::PlacementResult placement;
    placement.clear.clearedLines=1; placement.clear.clearedRows={19};
    auto piece=Piece::get_definition(Piece::Type::from_index(3));
    bool pressed=false, observedDuringClear=false, observedDuringEntry=false;
    const auto pump=[&] {
        if (!pressed) { f.key(left,true); pressed=true; }
        else if (f.loop.input.phase(left)!=Engine::KeyPhase::Released) observedDuringClear=true;
        f.session.player.timing.framePoll=f.context.real_now();
        return Rules::PlayResult::ContinueFrame;
    };
    assert(Engine::run_locked_line_clear_phase(f.context,f.session.player.gameplay.playfield.field,piece,
        f.session.player,placement,pump,&f.loop.input)==Rules::PlayResult::ContinueFrame);
    assert(pressed && observedDuringClear);
    const auto press=f.input.gameplay_press_time(left);
    assert(press && !f.loop.input.horizontalCharged(left,f.context.real_now(),dur(.2)));
    Engine::schedule_entry_delay(f.context,f.session.player);
    assert(Engine::wait_for_entry_delay(f.context,f.session.player,[&] {
        observedDuringEntry|=f.loop.input.horizontalCharged(left,f.context.real_now(),dur(.2));
        f.session.player.timing.framePoll=f.context.real_now();
        return Rules::PlayResult::ContinueFrame;
    },&f.loop.input)==Rules::PlayResult::ContinueFrame);
    assert(observedDuringEntry && f.loop.input.horizontalCharged(left,f.context.real_now(),dur(.2)));
    assert(f.input.gameplay_press_time(left)==press); // The clear-to-ARE boundary did not restart the hold.
    assert(f.session.player.gameplay.scoring.placedBlocks==0);
}

void release_and_switch_during_entry_reset_charge() {
    Fixture f; Capture output;
    Input::GameplayInputScope capture(&f.input);
    f.context.config.field.entryDelay=dur(.5);
    f.key(left,true);
    Engine::schedule_entry_delay(f.context,f.session.player);
    const auto end=*f.session.player.timing.entryDelayEnd;
    bool switched=false;
    assert(Engine::wait_for_entry_delay(f.context,f.session.player,[&] {
        if (!switched && f.context.real_now()>end-dur(.1)) {
            f.key(left,false); f.key(right,true); switched=true;
        }
        f.session.player.timing.framePoll=f.context.real_now();
        return Rules::PlayResult::ContinueFrame;
    },&f.loop.input)==Rules::PlayResult::ContinueFrame);
    assert(switched && f.loop.input.phase(left)==Engine::KeyPhase::Released);
    assert(f.loop.input.mostRecentActive({left,right})==right);
    assert(!f.loop.input.horizontalCharged(right,f.context.real_now(),dur(.2)));
    f.key(right,false); Engine::precharge_movement_input(f.context,f.loop.input);
    assert(f.loop.input.phase(right)==Engine::KeyPhase::Released);
}

void waiting_saturates_das_and_preserves_dcd() {
    Fixture f; Capture output;
    f.key(left,true,Clock::now()-std::chrono::seconds(30));
    Input::GameplayInputScope capture(&f.input);
    auto state=f.spawn(); const auto before=state.pose;
    // No room for the spawn pre-shift. Once gravity opens the side, there
    // must be one ARR shift rather than thirty seconds of queued repeats.
    assert(!Engine::apply_initial_movement(f.context,f.loop.input,state,[&](Piece::Pose p) { return p==before; }));
    state.pose.position.row=10;
    Engine::MovementController movement(state,[](Piece::Pose p) { return p.position.col>=0 && p.position.col<10; },
        [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
    Engine::PlayerInputConfig config; config.das=dur(.2); config.arr=dur(.1);
    Engine::process_player_input_frame(f.loop.input,movement,f.input.bindings,{}, {left,right},config,
        [&](int key) { return f.input.gameplay_key_down(key); },[&] { return f.context.real_now(); });
    assert(state.pose.position.col==before.position.col-1);
    const auto cut=f.context.real_now()+dur(1000000);
    f.loop.input.cancelHorizontalRepeats({left},f.context.real_now(),dur(.2),dur(.1),dur(1000000));
    f.context.config.field.entryDelay=dur(.1);
    Engine::schedule_entry_delay(f.context,f.session.player);
    Engine::wait_for_entry_delay(f.context,f.session.player,[] { return Rules::PlayResult::ContinueFrame; },&f.loop.input);
    assert(!f.loop.input.horizontalCharged(left,f.context.real_now(),dur(.2)));
    const auto due=f.loop.input.nextHorizontalRepeat(left,dur(.2),dur(.1),f.context.real_now());
    assert(due && std::fabs((*due-cut).seconds())<.01);
}

void both_local_players_precharge() {
    for(bool chargeFirst:{true,false}) {
        Fixture a,b; Capture output;
        a.context.config.handling.dasPrecharge=chargeFirst;
        Input::share_key_state(b.input,a.input);
        std::array<int,Input::kBindableActionCount> secondCodes;
        secondCodes.fill(-1); secondCodes[0]=60; secondCodes[1]=50;
        b.input.bindings=Input::BindingSet::from_key_codes(secondCodes);
        a.context.config.mode.mode=Runtime::GameMode::Racing;
        b.context.config.mode.mode=Runtime::GameMode::Racing;
        Input::GameplayInputScope firstCapture(&a.input),secondCapture(&b.input);
        a.key(left,true); a.key(50,true);
        assert(Engine::run_local_versus_countdown(a.context,a.session.player,b.context,b.session.player,
            &a.loop.input,&b.loop.input)==Engine::CountdownResult::Completed);
        Engine::finish_start_precharge(a.context,a.loop.input); Engine::finish_start_precharge(b.context,b.loop.input);
        assert(a.loop.input.horizontalCharged(left,a.context.real_now(),dur(.2))==chargeFirst);
        assert(b.loop.input.horizontalCharged(50,b.context.real_now(),dur(.2)));
        auto first=a.spawn(), second=b.spawn(); const auto column=first.pose.position.col;
        Engine::apply_initial_movement(a.context,a.loop.input,first,[](Piece::Pose) { return true; });
        Engine::apply_initial_movement(b.context,b.loop.input,second,[](Piece::Pose) { return true; });
        assert(first.pose.position.col==column-(chargeFirst ? 1 : 0) && second.pose.position.col==column+1);
    }
}

void app_first_piece_uses_prestart_charge() {
    Fixture f; Capture output;
    f.context.config.mode.mode=Runtime::GameMode::Racing;
    f.context.config.handling.das=dur(10); f.context.config.handling.arr=dur(0);
    f.replay.configPrefix=Replay::encode_config_prefix(f.context.config)+"}";
    f.key(left,true,Clock::now()-std::chrono::seconds(30));
    const auto dropAt=f.context.real_now()+dur(3.5);
    auto drop=std::async(std::launch::async,[&] {
        while (f.context.real_now()<dropAt) std::this_thread::sleep_for(std::chrono::microseconds(100));
        f.key(hardDrop,true); f.key(hardDrop,false);
    });
    App::run(f.context); drop.get();
    assert(f.session.player.gameplay.scoring.placedBlocks==1 && !f.input.gameplay_input_active());
    int minimumColumn=10;
    for (int row=0;row<20;++row) for (int column=0;column<10;++column)
        if (f.session.player.gameplay.playfield.field.cell(row,column).occupied()) minimumColumn=std::min(minimumColumn,column);
    assert(minimumColumn==0);
    Replay::ConfigPrefixResult header; Runtime::RuntimeConfig decoded;
    assert(Replay::decode_config_prefix(f.replay.data,decoded,header));
    auto operations=Replay::decode_operation_stream(f.replay.data,header.operationOffset);
    bool put=false;
    const auto spawnColumn=f.spawn().pose.position.col;
    while (!operations.empty()) {
        const auto& event=operations.front();
        if (!put && event.kind==Replay::OperationKind::PutDraw) {
            assert(event.payload_as<Replay::PosePayload>().pose.position.col==spawnColumn-1); put=true;
        }
        operations.pop();
    }
    assert(put);
}
}

int main() {
    switches_control_spawn_and_charge_independently(); disabled_pause_precharge_freezes_existing_charge();
    switches_persist_reset_and_roundtrip_replays();
    countdown_charges_and_discards_other_prestart_actions(); countdown_preserves_uncovered_cells(); held_baseline_order_and_nested_piece_loop();
    clear_and_entry_delays_share_charge(); release_and_switch_during_entry_reset_charge();
    waiting_saturates_das_and_preserves_dcd(); both_local_players_precharge(); app_first_piece_uses_prestart_charge();
}
