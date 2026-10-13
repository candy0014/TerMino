#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/player_input.h"
#include "tetr/input/services.h"
#include "tetr/replay/config_codec.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/settings_model.h"
#include "../src/app/ui_text.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <set>
#include <thread>

namespace {
using Id=UserConfig::SettingId;
using Clock=std::chrono::steady_clock;
Runtime::TimePoint tp(double t) { return Runtime::TimePoint::from_seconds(t); }
Runtime::Duration dur(double t) { return Runtime::Duration::from_seconds(t); }
bool near(double a,double b) { return std::fabs(a-b)<1e-12; }

struct Fixture {
    Input::KeyEventBuffer events;
    Input::Services input;
    Engine::MovementInputState keys;
    Engine::MovementState state;
    Engine::PlayerInputConfig config;
    Engine::MovementController movement{state,
        [](Piece::Pose p) { return p.position.row<=100 && p.position.col>=0 && p.position.col<100; },
        [](Piece::Pose& p,Runtime::Rotation r,Piece::KickIndex& k) { p.rotation=r; k=Piece::KickIndex{}; return true; }};
    Fixture() {
        std::array<int,Input::kBindableActionCount> bindings;
        bindings.fill(-1); bindings[0]=10; bindings[1]=11; bindings[2]=12; bindings[3]=13; bindings[4]=14; bindings[11]=15;
        input.bindings=Input::BindingSet::from_key_codes(bindings);
        input.keyEventSource=&events; input.begin_gameplay_input();
        config.das=dur(.1); config.arr=dur(.02); config.lockResetLimit=15;
        state.pose={{0,50},Runtime::Rotation{}};
    }
    void key(int k,bool down,double t) {
        events.update(k,down,Clock::time_point{}+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t)));
    }
    auto frame(double t) {
        return Engine::process_buffered_player_input_frame(keys,movement,input,{14},{10,11},config,tp(t),
            [](Clock::time_point time) { return tp(std::chrono::duration<double>(time.time_since_epoch()).count()); });
    }
};

void dcd_pauses_without_catchup() {
    Fixture f; f.config.dcd=dur(.2);
    f.key(10,true,.01); f.frame(.12); assert(f.state.pose.position.col==48);
    f.key(14,true,.13); f.frame(.13);
    const auto col=f.state.pose.position.col;
    f.frame(.329); assert(f.state.pose.position.col==col);
    f.frame(.33); assert(f.state.pose.position.col==col-1);
    f.frame(.34); assert(f.state.pose.position.col==col-1);
    f.frame(.35); assert(f.state.pose.position.col==col-2);

    Fixture uncharged; uncharged.config.dcd=dur(.2);
    uncharged.key(10,true,.01); uncharged.key(14,true,.03);
    uncharged.frame(.03); uncharged.frame(.11);
    assert(uncharged.state.pose.position.col==48); // An uncharged DAS is unaffected.

    Fixture drop; drop.config.dcd=dur(.2);
    drop.key(10,true,.01); drop.frame(.12);
    drop.key(13,true,.15); assert(drop.frame(.15).command==Engine::PlayerInputCommand::HardDrop);
    drop.state.pose={{0,50},Runtime::Rotation{}};
    drop.keys.preparePiece(drop.input.bindings,[&](int key) { return drop.input.gameplay_key_down(key); },tp(.16));
    drop.key(13,false,.16); drop.frame(.349); assert(drop.state.pose.position.col==50);
    drop.frame(.35); assert(drop.state.pose.position.col==49);

    Fixture opposite; opposite.config.dcd=dur(.2);
    opposite.key(10,true,.01); opposite.frame(.12);
    opposite.key(14,true,.13); opposite.frame(.13);
    opposite.key(11,true,.14); opposite.frame(.14);
    assert(opposite.keys.mostRecentActive({10,11})==11); // A future cut timer cannot override press order.

    Fixture instantArr; instantArr.config.arr=dur(0); instantArr.config.dcd=dur(.025);
    instantArr.key(10,true,.01); instantArr.frame(.12);
    instantArr.key(14,true,.13); instantArr.frame(.13);
    const auto due=instantArr.keys.nextHorizontalRepeat(10,dur(.1),dur(0),tp(.14));
    assert(due && near(due->seconds(),.155));

    Fixture longArr; longArr.config.arr=dur(1); longArr.config.dcd=dur(.025);
    longArr.key(10,true,.01); longArr.frame(.11);
    longArr.key(14,true,.13); longArr.frame(.2);
    assert(longArr.state.pose.position.col==48);
    longArr.frame(1.11); assert(longArr.state.pose.position.col==47);
}

void sdf_rates_and_infinity() {
    const auto periodic=Runtime::GravityInterval::periodic(dur(.6));
    const auto disabled=Runtime::GravityInterval::disabled();
    assert(near(Engine::soft_drop_interval(periodic,20).seconds(),.03));
    assert(near(Engine::soft_drop_interval(disabled,20).seconds(),.05));
    assert(near(Engine::soft_drop_interval(periodic,.05,true).seconds(),.05));
    assert(near(Engine::soft_drop_interval(Runtime::GravityInterval::periodic(dur(.01)),.05,true).seconds(),.01));
    assert(near(Engine::soft_drop_interval(disabled,.2,true).seconds(),.2));
    assert(Engine::soft_drop_interval(disabled,0)==dur(0));

    Fixture fixed; fixed.config.softDropFactor=.05; fixed.config.softDropFixed=true; fixed.config.gravityInterval=periodic;
    fixed.key(12,true,.01); assert(fixed.frame(.161).scoreDelta==4);
    assert(fixed.state.pose.position.row==4);
    Fixture noGravity; noGravity.config.softDropFactor=4; noGravity.config.gravityInterval=disabled;
    noGravity.key(12,true,.01); noGravity.frame(.51); assert(noGravity.state.pose.position.row==3);
    Fixture instant; instant.config.softDropFactor=0; instant.config.gravityInterval=disabled;
    instant.key(12,true,.01); assert(instant.frame(.01).scoreDelta==100);
    assert(instant.state.pose.position.row==100);
}

void initial_movement_before_gravity_and_collision() {
    for (bool zeroArr:{false,true}) for (bool instantGravity:{false,true}) {
        Fixture f;
        Runtime::SessionState session; Runtime::RuntimeServices runtime;
        Runtime::GameContext context{session,runtime}; context.attach_input(f.input);
        context.config.handling.das=dur(.1); context.config.handling.arr=dur(zeroArr ? 0 : .02);
        context.config.handling.gravityInterval=instantGravity ? Runtime::GravityInterval::instant() : Runtime::GravityInterval::disabled();
        context.config.handling.softDropFactor=0;
        f.input.keyStateProvider=[=](int code) {return code==10 || (code==12 && !instantGravity);};
        f.state.pose={{-2,4},Runtime::Rotation{}};
        f.events.update(10,true,Clock::now()-std::chrono::milliseconds(200));
        // The center spawn collides; the pre-shifted column is a valid 20G path.
        const auto place=[](Piece::Pose p) { return p.position.col==3 && p.position.row<=18; };
        assert(!place(f.state.pose));
        assert(Engine::apply_initial_movement(context,f.keys,f.state,place));
        assert(f.state.pose.position.col==3); // Exactly one cell, even with ARR=0.
        assert(f.state.pose.position.row==18); // This first horizontal cell settles before any rotation/repeat.
        assert(session.player.gameplay.scoring.score==(instantGravity ? 0 : 20));
        Engine::MovementController move(f.state,place,
            [](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
        move.applyGravity(context.real_now(),Runtime::GravityInterval::instant());
        assert(f.state.pose.position.row==18 && f.state.pose.position.col==3);
    }
    Fixture f;
    Runtime::SessionState session; Runtime::RuntimeServices runtime;
    Runtime::GameContext context{session,runtime}; context.attach_input(f.input);
    f.events.update(10,true,Clock::now()-std::chrono::milliseconds(200));
    f.events.update(10,false); // A released pre-input must not move the next piece.
    const auto before=f.state.pose;
    assert(!Engine::apply_initial_movement(context,f.keys,f.state,[](Piece::Pose) { return true; }));
    assert(f.state.pose==before);
}

void every_horizontal_cell_falls_into_a_pit() {
    for (const int direction:{-1,1}) for (const bool zeroDas:{false,true}) for (bool zeroArr:{false,true})
        for (const bool softKeyFirst:{false,true}) for (int mode=0;mode<5;++mode) {
        std::array<int,Input::kBindableActionCount> codes; codes.fill(-1);
        const int softKey=softKeyFirst ? 8 : 12;
        codes[0]=10; codes[1]=11; codes[2]=softKey;
        const auto bindings=Input::BindingSet::from_key_codes(codes);
        const int moveKey=direction<0 ? 10 : 11;
        const auto column=[&](int logical) { return direction>0 ? logical : 8-logical; };
        Engine::MovementState state; state.pose={{5,column(2)},Runtime::Rotation{}};
        Engine::MovementInputState input;
        Engine::MovementController movement(state,[&](Piece::Pose p) {
            if (p.position.col<0 || p.position.col>8) return false;
            const int logical=column(p.position.col);
            return p.position.row<=(logical==3 || logical==4 ? 10 : 5);
        },[](Piece::Pose&,Runtime::Rotation,Piece::KickIndex&) { return false; });
        Engine::PlayerInputConfig config;
        config.arr=dur(zeroArr ? 0 : .02); config.das=dur(zeroDas ? 0 : .1); config.lockResetLimit=15;
        config.gravityInterval=mode>=2 ? Runtime::GravityInterval::instant()
            : (mode==1 ? Runtime::GravityInterval::disabled() : Runtime::GravityInterval::periodic(dur(.6)));
        config.softDropFactor=mode>=2 ? 20 : 0;
        config.softDropFixed=mode==1;
        config.movement.disableSoftDrop=mode==4;
        const bool softDown=mode!=3;
        const auto frame=[&](double time) {
            return Engine::process_player_input_frame(input,movement,bindings,{},{10,11},config,
                [&](int key) { return key==moveKey || (key==softKey && softDown); },[&] { return tp(time); });
        };
        assert(frame(1).scoreDelta==(mode>=3 ? 0 : 5));
        assert(state.pose.position.row==10 && state.pose.position.col==column(zeroDas ? 4 : 3));
        assert(state.lockResetCount==(zeroDas && !zeroArr ? 2 : 1));
        if (!zeroDas) {
            frame(1.09); assert(state.pose.position.col==column(3));
            assert(frame(1.1).scoreDelta==0 && state.pose.position.col==column(4));
            assert(state.lockResetCount==2);
        }
        const auto count=state.lockResetCount;
        assert(frame(1.2).scoreDelta==0 && state.lockResetCount==count);
    }
    // An infinite SDF setting alone does not settle a piece without its key.
    Fixture inactive; inactive.config.arr=dur(0); inactive.config.das=dur(0); inactive.config.softDropFactor=0;
    inactive.key(11,true,.01); inactive.frame(.01);
    assert(inactive.state.pose.position.row==0 && inactive.state.pose.position.col==99);
}

std::set<std::pair<int,int>> occupied_cells(const Piece::Definition& piece,Piece::Pose pose) {
    std::set<std::pair<int,int>> result;
    for (const auto& cell:piece.cells(pose.rotation)) result.emplace(pose.position.row+cell[0],pose.position.col+cell[1]);
    return result;
}

void instant_drop_allows_the_example_right_and_180() {
    for (bool buffered:{false,true}) for (bool zeroArr:{false,true})
        for (int keyOrder=0;keyOrder<3;++keyOrder) for (int mode=0;mode<4;++mode) {
        Runtime::Playfield field; field.initialize(4,3);
        field.set_cell(1,3,Runtime::BoardCell::garbage());
        field.set_cell(2,2,Runtime::BoardCell::garbage());
        field.set_cell(2,3,Runtime::BoardCell::garbage());
        const auto piece=Piece::get_definition(Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[4]);
        Engine::MovementState state; state.pose={{1,1},Runtime::Rotation{2}};
        const std::set<std::pair<int,int>> before{{1,1},{1,2},{2,0},{2,1}};
        const std::set<std::pair<int,int>> after{{0,2},{0,3},{1,1},{1,2}};
        assert(occupied_cells(piece,state.pose)==before && piece.can_place(state.pose,field));
        Engine::MovementController movement(state,[&](Piece::Pose p) { return piece.can_place(p,field); },
            [&](Piece::Pose& p,Runtime::Rotation r,Piece::KickIndex& k) { return piece.try_rotate(p,r,field,k); });
        Engine::MovementInputState input;
        std::array<int,Input::kBindableActionCount> codes; codes.fill(-1);
        const int softKey=keyOrder==1 ? 6 : 12, rotateKey=keyOrder==2 ? 8 : 14;
        codes[1]=11; codes[2]=softKey; codes[6]=rotateKey;
        const auto bindings=Input::BindingSet::from_key_codes(codes);
        Engine::PlayerInputConfig config;
        config.arr=dur(zeroArr ? 0 : .02); config.das=dur(.1); config.lockResetLimit=15;
        config.gravityInterval=mode>=2 ? Runtime::GravityInterval::instant()
            : (mode==1 ? Runtime::GravityInterval::disabled() : Runtime::GravityInterval::periodic(dur(.6)));
        config.softDropFactor=0;
        const bool softDown=mode!=2;
        input.beginPress(11,true,tp(0)); input.setPhase(11,Engine::KeyPhase::Repeating);
        Engine::PlayerInputResult result;
        if (buffered) {
            Input::KeyEventBuffer events;
            Input::Services services; services.bindings=bindings; services.keyEventSource=&events;
            events.update(11,true,Clock::time_point{});
            if (softDown) events.update(softKey,true,Clock::time_point{});
            services.begin_gameplay_input();
            events.update(rotateKey,true,Clock::time_point{}+std::chrono::seconds(1));
            result=Engine::process_buffered_player_input_frame(input,movement,services,{rotateKey},{11},config,tp(1),
                [](Clock::time_point time) { return tp(std::chrono::duration<double>(time.time_since_epoch()).count()); });
        } else {
            result=Engine::process_player_input_frame(input,movement,bindings,{rotateKey},{11},config,
                [&](int key) { return key==11 || key==rotateKey || (key==softKey && softDown); },[] { return tp(1); });
        }
        assert(result.command==Engine::PlayerInputCommand::None && result.scoreDelta==0);
        assert(occupied_cells(piece,state.pose)==after);
        assert(movement.grounded() && state.lockResetCount==2);
    }
}

void buffered_rotation_precedes_instant_fall() {
    for (bool gravity:{false,true}) {
        Fixture f;
        f.config.gravityInterval=gravity ? Runtime::GravityInterval::instant() : Runtime::GravityInterval::disabled();
        f.config.softDropFactor=0;
        if (!gravity) {
            f.events.update(12,true,Clock::time_point{});
            f.input.begin_gameplay_input();
        }
        Engine::MovementController movement(f.state,[](Piece::Pose p) { return p.position.row<=100; },
            [](Piece::Pose& p,Runtime::Rotation r,Piece::KickIndex& k) {
                if (p.position.row!=0) return false;
                p.rotation=r; k=Piece::KickIndex{}; return true;
            });
        f.key(14,true,1);
        const auto result=Engine::process_buffered_player_input_frame(f.keys,movement,f.input,{14},{10,11},f.config,tp(1),
            [](Clock::time_point time) { return tp(std::chrono::duration<double>(time.time_since_epoch()).count()); });
        assert(f.state.pose.rotation==Runtime::Rotation{1} && f.state.pose.position.row==100);
        assert(result.scoreDelta==(gravity ? 0 : 100));
    }
}

void hard_drop_finishes_before_instant_fall() {
    for (bool gravity:{false,true}) {
        Fixture f;
        f.config.gravityInterval=gravity ? Runtime::GravityInterval::instant() : Runtime::GravityInterval::disabled();
        f.config.softDropFactor=0;
        if (!gravity) {
            f.events.update(12,true,Clock::time_point{});
            f.input.begin_gameplay_input();
        }
        f.key(13,true,1);
        const auto result=f.frame(1);
        assert(result.command==Engine::PlayerInputCommand::HardDrop && result.scoreDelta==200);
        assert(f.state.pose.position.row==100);
    }
}

const App::Settings::Model::SettingNode* find(const App::Settings::Model::SettingNode& tree,Id id) {
    if (tree.item && tree.item->id==id) return &tree;
    for (const auto& node:tree.children) if (const auto* result=find(node,id)) return result;
    return nullptr;
}

void units_persistence_replay_and_resets() {
    UserConfig::Settings user;
    assert(user.dcd==0 && user.squareStyle==0);
    for (const auto id:{Id::Das,Id::Arr,Id::Dcd}) {
        assert(UserConfig::assign_setting_text(user,id,"  +1.5F  "));
        assert(near(UserConfig::clamped_real(user,id),.025));
        assert(UserConfig::assign_setting_text(user,id,"0.04S"));
        assert(near(UserConfig::clamped_real(user,id),.04));
        for (const auto text:{"-1f","nan","inf","abc","1ff"}) assert(!UserConfig::assign_setting_text(user,id,text));
    }
    assert(UserConfig::assign_setting_text(user,Id::SoftDropFactor,"20"));
    assert(user.softDropFixed==0 && user.softDropFactor==20);
    assert(UserConfig::assign_setting_text(user,Id::SoftDropFactor,"1.5f"));
    assert(user.softDropFixed==1 && near(user.softDropFactor,.025));
    auto tree=App::Settings::Model::make_tree(user,false,0);
    auto sdf=*find(tree,Id::SoftDropFactor)->item;
    assert(App::Settings::Model::assign_scalar(sdf,"0.05s"));
    assert(App::Settings::Model::scalar_value(sdf)=="0.05s");
    user.persistencePath=(std::filesystem::temp_directory_path()/"tetr_handling_controls_test.ini").string();
    assert(UserConfig::save(user));
    UserConfig::Settings restored; UserConfig::load(restored,user.persistencePath);
    assert(restored.softDropFixed==1 && near(restored.softDropFactor,.05));
    assert(near(restored.dcd,.04));
    std::filesystem::remove(user.persistencePath);
    const auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    assert(config.handling.softDropFixed && near(config.handling.dcd.seconds(),.04));
    Runtime::RuntimeConfig replay; Replay::ConfigPrefixResult decoded;
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",replay,decoded));
    assert(replay.handling.softDropFixed && near(replay.handling.softDropFactor,.05) && near(replay.handling.dcd.seconds(),.04));
    assert(Replay::decode_config_prefix("{{SDF,20}{DAS,0.1}}",replay,decoded));
    assert(!replay.handling.softDropFixed && replay.handling.dcd==dur(0));
    user.fieldWidth=17;
    UserConfig::reset_settings(user);
    assert(user.fieldWidth==10 && user.softDropFixed==1 && near(user.softDropFactor,.05));
    assert(near(user.das,.04) && near(user.arr,.04) && near(user.dcd,.04));
    UserConfig::reset_section(user,UserConfig::Section::Handling);
    assert(user.softDropFixed==0 && user.softDropFactor==0 && user.dcd==0 && user.das==.1 && user.arr==.01);
    App::UiText::set_language(App::UiText::Language::Chinese);
    assert(App::UiText::tr("Reverse")=="逆位");
}

void event_wait_wakes_both_readers() {
    Input::KeyEventBuffer events;
    std::uint64_t cursor=0; events.snapshot(cursor);
    auto reader=[&] { events.wait(cursor,std::chrono::seconds(2)); };
    auto first=std::async(std::launch::async,reader), second=std::async(std::launch::async,reader);
    events.update(10,true);
    assert(first.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    assert(second.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    first.get(); second.get();
    auto later=cursor;
    assert(events.read(cursor).size()==1 && events.read(later).size()==1);
}

void buffered_undo_has_release_debounce() {
    for (bool autoplay:{false,true}) {
        Fixture disabled; disabled.config.autoplay=autoplay; disabled.config.disableUndo=true;
        disabled.key(15,true,.01); disabled.key(15,false,.02);
        assert(disabled.frame(.1).command==Engine::PlayerInputCommand::None);
        Fixture f; f.config.autoplay=autoplay;
        f.key(15,true,.01); f.key(15,false,.02);
        assert(f.frame(.1).command==Engine::PlayerInputCommand::Undo);
        f.keys.preparePiece(f.input.bindings,[&](int key) { return f.input.gameplay_key_down(key); },tp(.1));
        assert(f.keys.phase(15)==Engine::KeyPhase::AwaitRelease);
        assert(f.frame(.2).command==Engine::PlayerInputCommand::None);
    }
}
}
int main() {
    dcd_pauses_without_catchup(); sdf_rates_and_infinity(); initial_movement_before_gravity_and_collision();
    every_horizontal_cell_falls_into_a_pit(); instant_drop_allows_the_example_right_and_180(); buffered_rotation_precedes_instant_fall();
    hard_drop_finishes_before_instant_fall();
    units_persistence_replay_and_resets(); event_wait_wakes_both_readers();
    buffered_undo_has_release_debounce();
}
