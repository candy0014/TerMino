#include "tetr/autoplay/availability.h"
#include "tetr/app/init.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/player_input.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/input/services.h"
#include "tetr/legacy/bot_adapter.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <set>
#include <tuple>
#include <vector>

namespace {
using Restrictions = Runtime::MovementRestrictions;
using Mode = Runtime::AutoplayMode;
using PoseKey = std::tuple<int,int,int>;
PoseKey pose_key(Piece::Pose pose) {
    return {pose.position.row,pose.position.col,pose.rotation.index()};
}

// Independent action graph: collision and rotation use the piece definition,
// but no production movement-script filter or transition implementation.
std::set<PoseKey> reference_landings(const Runtime::Playfield& field,const Piece::Definition& piece,
    Piece::Pose start,const Runtime::HandlingConfig& handling,Restrictions restrictions) {
    std::vector<Piece::Pose> queue;
    std::set<PoseKey> seen,landings;
    const auto floor=[&](Piece::Pose pose) {
        while(piece.can_place(pose.translated(1,0),field)) ++pose.position.row;
        return pose;
    };
    const auto enqueue=[&](Piece::Pose pose) {
        if(!piece.can_place(pose,field)) return;
        if(handling.gravityInterval.is_instant()) pose=floor(pose);
        if(seen.insert(pose_key(pose)).second) queue.push_back(pose);
    };
    const auto horizontal=[&](Piece::Pose pose,int direction) {
        if((direction<0 && restrictions.disableLeft) || (direction>0 && restrictions.disableRight)) return;
        if(handling.das>Runtime::Duration{}) {enqueue(pose.translated(0,direction));return;}
        bool changed=false;
        while(piece.can_place(pose.translated(0,direction),field)) {
            pose=pose.translated(0,direction);changed=true;
            if(handling.gravityInterval.is_instant()) pose=floor(pose);
        }
        if(changed) enqueue(pose);
    };
    enqueue(start);
    for(std::size_t i=0;i<queue.size();++i) {
        const auto current=queue[i];
        if(!restrictions.disableHardDrop) landings.insert(pose_key(floor(current)));
        else if(handling.gravityInterval.enabled() && current==floor(current)) landings.insert(pose_key(current));
        horizontal(current,-1);horizontal(current,1);
        for(int delta:{1,-1,2}) {
            if((delta==1 && restrictions.disableClockwise) || (delta==-1 && restrictions.disableCounterclockwise) ||
                (delta==2 && restrictions.disable180)) continue;
            auto rotated=current;Piece::KickIndex kick;
            if(!piece.try_rotate(rotated,current.rotation.rotated(delta),field,kick)) continue;
            enqueue(rotated);
            if(handling.gravityInterval.is_instant()) {horizontal(rotated,-1);horizontal(rotated,1);}
        }
        if(!restrictions.disableSoftDrop) {
            const bool instant=handling.gravityInterval.is_instant() || handling.softDropFactor<=0;
            enqueue(instant ? floor(current) : current.translated(1,0));
        }
        if(handling.gravityInterval.enabled() && !handling.gravityInterval.is_instant()) enqueue(current.translated(1,0));
    }
    return landings;
}
unsigned command_inputs(char code) {
    switch(code) {
        case 'L':case 'l':return 1;
        case 'R':case 'r':return 2;
        case 'X':return 4;
        case 'Z':return 8;
        case 'A':return 16;
        case 'S':case 's':return 32;
        case 'H':return 64;
        case 'B':case 'b':return 1|4;
        case 'C':case 'c':return 2|4;
        case 'D':case 'd':return 1|8;
        case 'E':case 'e':return 2|8;
        case 'F':case 'f':return 1|16;
        case 'G':case 'g':return 2|16;
        case 'V':case 'W':return 0; // Gravity/waiting are not key inputs.
        default:assert(false);return 127;
    }
}
void unified_path_restriction_matrix() {
    using D=Runtime::Duration;
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    Engine::AutoplaySpinRules spin;spin.bonuses=Runtime::SpinBonuses::None;
    int checks=0,routes=0;
    for(unsigned mask=0;mask<128;++mask) for(int gravityMode=0;gravityMode<3;++gravityMode) for(std::size_t family=0;family<types.size();++family) {
        const auto restrictions=Restrictions::from_mask(mask);
        Runtime::HandlingConfig handling;
        handling.gravityInterval=gravityMode==0 ? Runtime::GravityInterval::disabled() : gravityMode==1 ?
            Runtime::GravityInterval::periodic(D::from_seconds(.01)) : Runtime::GravityInterval::instant();
        handling.das=(mask+family)%2 ? D{} : D::from_seconds(.1);
        handling.arr=(mask+family)%3 ? D::from_seconds(.02) : D{};
        handling.softDropFactor=(mask+family)%2 ? 0 : 20;
        handling.epld=D::from_seconds(.05);
        Runtime::Playfield field;field.initialize(10,8);
        if(mask%2) for(int col=0;col<10;++col) for(int row=8-col%3;row<8;++row)
            field.set_cell(row,col,Runtime::BoardCell::garbage());
        const auto piece=Piece::get_definition(types[family]);
        Engine::MovementState origin;origin.pose=Piece::spawn_pose(10,piece.type());
        if(handling.gravityInterval.is_instant()) while(piece.can_place(origin.pose.translated(1,0),field)) ++origin.pose.position.row;
        auto target=Piece::spawn_pose(10,piece.type());target.position.col=2+2*((mask/4+family)%3);
        target.rotation=target.rotation.rotated(mask%4);
        assert(piece.can_place(target,field));
        while(piece.can_place(target.translated(1,0),field)) ++target.position.row;
        const auto expected=reference_landings(field,piece,origin.pose,handling,restrictions);
        for(bool timed:{false,true}) {
            ++checks;
            auto path=Engine::search_autoplay_path(field,piece,origin,target,{},handling,restrictions,
                handling.gravityInterval,timed,{},spin);
            if(bool(path)!=(expected.count(pose_key(target))!=0)) {
                std::cerr<<"restriction reachability mismatch: mask="<<mask<<" gravity="<<gravityMode<<" family="<<family<<" timed="<<timed<<'\n';
                std::abort();
            }
            if(!path) continue;
            ++routes;assert(path->script.back()==(restrictions.disableHardDrop ? 'W' : 'H'));
            auto actual=origin;
            const auto canPlace=[&](Piece::Pose pose){return piece.can_place(pose,field);};
            const auto rotate=[&](Piece::Pose& pose,Runtime::Rotation rotation,Piece::KickIndex& kick){return piece.try_rotate(pose,rotation,field,kick);};
            for(char code:path->script) {
                assert(!(command_inputs(code)&mask));
                // Replay unrestricted: a banned input cannot be hidden by the
                // execution layer ignoring it. It must not appear in the route.
                Engine::apply_movement_script_command(actual,Engine::decode_movement_script_command(code),canPlace,rotate,
                    handling.das==D{},handling.arr==D{},{},handling.gravityInterval.is_instant(),
                    handling.softDropFactor<=0 || handling.gravityInterval.is_instant());
            }
            assert(actual.pose==target && Engine::autoplay_landing_matches(field,piece,actual,target,{},spin));
            for(const auto& step:path->steps) {
                assert(!(command_inputs(Engine::movement_script_command_char(step.command))&mask));
                if(restrictions.disableHardDrop) assert(step.command!=Engine::MovementScriptCommand::HardDrop);
            }
            if(restrictions.disableHardDrop) {
                assert(path->steps.back().command==Engine::MovementScriptCommand::WaitForLock && path->steps.back().scoreDelta==0);
                assert(path->steps.back().delay==(timed ? handling.epld : D{}));
            }
        }
    }
    assert(checks==5376 && routes>0);
    std::cerr<<"unified restrictions: checks="<<checks<<" routes="<<routes<<'\n';
}
void reconstructed_scripts_cannot_bypass_restrictions() {
    using D=Runtime::Duration;
    Runtime::Playfield field;field.initialize(10,8);
    const auto piece=Piece::get_definition(Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)[5]);
    Engine::MovementState origin;origin.pose=Piece::spawn_pose(10,piece.type());
    Runtime::HandlingConfig handling;handling.gravityInterval=Runtime::GravityInterval::instant();
    for(unsigned mask=0;mask<128;++mask) for(char code:std::string("LlRrXZA SsBCDEFGbcdefgH")) {
        if(code==' ' || !(command_inputs(code)&mask)) continue;
        const auto restrictions=Restrictions::from_mask(mask);
        const std::string script=std::string(1,code)+(restrictions.disableHardDrop ? "W" : "H");
        assert(!Engine::make_autoplay_path(field,piece,origin,handling,restrictions,handling.gravityInterval,script));
    }
    handling.gravityInterval=Runtime::GravityInterval::disabled();
    assert(!Engine::make_autoplay_path(field,piece,origin,handling,{},handling.gravityInterval,"VH"));
    assert(!Engine::make_autoplay_path(field,piece,origin,handling,{},handling.gravityInterval,"CH"));
    while(piece.can_place(origin.pose.translated(1,0),field)) ++origin.pose.position.row;
    auto restrictions=Restrictions::from_mask(64);
    assert(!Engine::search_autoplay_path(field,piece,origin,origin.pose,{},handling,restrictions,handling.gravityInterval));
    assert(!Engine::make_autoplay_path(field,piece,origin,handling,restrictions,handling.gravityInterval,"W"));
    handling.gravityInterval=Runtime::GravityInterval::periodic(D::from_seconds(.01));
    auto path=Engine::make_autoplay_path(field,piece,origin,handling,restrictions,handling.gravityInterval,"W");
    assert(path && path->script=="W" && path->duration==handling.epld && path->steps.front().scoreDelta==0);
    assert(!Engine::make_autoplay_path(field,piece,origin,handling,restrictions,handling.gravityInterval,"H"));
}

void config_round_trip() {
    UserConfig::Settings user;
    user.keyCodes=UserConfig::default_key_codes();
    const UserConfig::SettingId ids[] = {UserConfig::SettingId::DisableLeft, UserConfig::SettingId::DisableRight,
        UserConfig::SettingId::DisableClockwise, UserConfig::SettingId::DisableCounterclockwise,
        UserConfig::SettingId::Disable180, UserConfig::SettingId::DisableSoftDrop, UserConfig::SettingId::DisableHardDrop};
    const auto path=std::filesystem::temp_directory_path()/"tetr_disabled_movement.ini";
    user.persistencePath=path.string();
    for (unsigned mask=0;mask<128;++mask) {
        user.disableUndo=mask%2;
        for (unsigned bit=0;bit<7;++bit) assert(UserConfig::assign_setting_text(user,ids[bit], mask & (1u<<bit) ? "1" : "0"));
        assert(UserConfig::save(user));
        UserConfig::Settings loaded;
        UserConfig::load(loaded,path.string());
        for (auto mode:{Runtime::GameMode::Zen,Runtime::GameMode::QuickPlay}) {
            const auto config=Runtime::normalize_user_config(loaded,mode);
            assert(config.movement.mask()==mask); // QuickPlay's field defaults preserve action restrictions.
            assert(config.disableUndo==(user.disableUndo!=0));
            const auto prefix=Replay::encode_config_prefix(config)+"}";
            Runtime::RuntimeConfig decoded;
            decoded.movement=Restrictions::from_mask(127);
            decoded.disableUndo=!config.disableUndo;
            Replay::ConfigPrefixResult result;
            assert(Replay::decode_config_prefix(prefix,decoded,result));
            assert(decoded.movement.mask()==mask);
            assert(decoded.disableUndo==config.disableUndo);
            assert((prefix.find("DisabledMoves")!=std::string::npos)==(mask!=0));
        }
    }
    UserConfig::reset_section(user,UserConfig::Section::Game);
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Zen).movement.mask()==0);
    assert(user.disableUndo==0);
    std::filesystem::remove(path);
}

void input_and_script_guards() {
    constexpr int controls[]={0,1,4,5,6,2,3};
    constexpr const char* commands[]={"Ll","Rr","X","Z","A","Ss","H"};
    for (unsigned bit=0;bit<7;++bit) {
        const auto restricted=Restrictions::from_mask(1u<<bit);
        Engine::MovementState state;
        state.pose={{0,4},Runtime::Rotation{}};
        const auto start=state.pose;
        const auto canPlace=[](Piece::Pose pose) {return pose.position.row<19 && pose.position.col>=0 && pose.position.col<10;};
        const auto rotate=[](Piece::Pose& pose,Runtime::Rotation rotation,Piece::KickIndex& kick) {
            pose.rotation=rotation; kick=Piece::KickIndex{}; return true;
        };
        Engine::MovementController controller(state,canPlace,rotate);
        Engine::MovementInputState input;
        std::array<int,Input::kBindableActionCount> keys;
        keys.fill(-1); keys[controls[bit]]=40+int(bit);
        const auto bindings=Input::BindingSet::from_key_codes(keys);
        const auto rotations=Engine::collect_action_keys(bindings,{Input::Action::RotateClockwise,Input::Action::RotateCounterclockwise,Input::Action::Rotate180});
        const auto horizontal=Engine::collect_action_keys(bindings,{Input::Action::MoveLeft,Input::Action::MoveRight});
        Engine::PlayerInputConfig config;
        config.movement=restricted;
        for (int frame=0;frame<3;++frame) {
            const auto result=Engine::process_player_input_frame(input,controller,bindings,rotations,horizontal,config,
                [](int) {return true;},[frame] {return Runtime::TimePoint::from_seconds(frame+1);});
            assert(result.command==Engine::PlayerInputCommand::None && result.scoreDelta==0);
            assert(state.pose==start);
        }
        for (const char* c=commands[bit];*c;++c) {
            const auto effect=Engine::apply_movement_script_command(state,Engine::decode_movement_script_command(*c),canPlace,rotate,false,false,restricted);
            assert(!effect.changed && !effect.terminate && state.pose==start);
        }
        assert(controller.applyGravity(Runtime::TimePoint::from_seconds(2),Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.6))).changed);
        assert(state.pose.position.row==1); // Restrictions apply to input; natural gravity still works.
    }
}

void availability_and_initialization(Runtime::GameContext& context) {
    for (const auto& bot:Runtime::kAutoplayModes) {
        context.config.autoplay.mode=bot.mode;
        for (unsigned mask=0;mask<128;++mask) {
            context.config.movement=Restrictions::from_mask(mask);
            const auto checked=Autoplay::availability(context.config,bot.mode);
            const bool expected=bot.mode==Mode::Disabled ||
                ((bot.mode==Mode::Candy0014 || bot.mode==Mode::While1 || bot.mode==Mode::ColdClear) ? !(mask & 64) : mask==0);
            assert(checked.usable==expected && checked.reason.empty()==expected);
            assert(Bot::init(context)==expected); // UI checks and actual initialization agree for every combination.
        }
    }
    context.config.movement={};
    context.config.field.width=11; context.config.field.height=25;
    const auto firstReason=Autoplay::availability(context.config,Mode::Candy0014);
    assert(!firstReason && firstReason.reason=="Requires a field width of 10.");
    context.config.field.width=10; context.config.field.height=20;
    context.config.handling.das={};
    assert(Autoplay::availability(context.config,Mode::Candy0014));
    assert(Autoplay::availability(context.config,Mode::While1));
    assert(!Autoplay::availability(context.config,Mode::Mech));
    context.config.handling.das=Runtime::Duration::from_seconds(.1);
}

void legal_bot_paths(Runtime::GameContext& context) {
    auto& player=context.session.player;
    player.gameplay.playfield.field.initialize(10,20);
    auto& field=player.gameplay.playfield.field;
    context.config.autoplay.searchWidth=2;
    for (auto mode:{Mode::Candy0014,Mode::While1}) for (bool instant:{false,true}) for (unsigned mask=0;mask<64;++mask)
        for (bool zeroDas:{false,true}) for (int layout=0;layout<2;++layout) for (int type=0;type<7;++type) {
            if (instant && mode==Mode::Candy0014) continue;
            context.config.autoplay.mode=mode;
            context.config.handling.gravityInterval=instant ? Runtime::GravityInterval::instant()
                : Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.6));
            context.config.movement=Restrictions::from_mask(mask);
            context.config.handling.das=Runtime::Duration::from_seconds(zeroDas ? 0 : .1);
            assert(Bot::init(context));
            field.clear();
            if (layout) for (int col=0;col<10;++col) for (int row=19-col%4;row<20;++row)
                field.set_cell(row,col,Runtime::BoardCell::piece(1));
            const auto current=Piece::Type::from_index(type);
            const std::vector<Piece::Type> sequence{current,Piece::Type::from_index((type+1)%7),Piece::Type::from_index((type+2)%7)};
            const Piece::Pose start{{-2,4},Runtime::Rotation{}};
            auto answer=start;
            auto rotation=Runtime::RotationRecord::none();
            const auto selected=Bot::getpos(context,player,field,sequence,{},start,{layout!=0,Piece::Type::none()},answer,rotation);
            assert(selected==current || (layout && selected==sequence[1]));
            const auto piece=Piece::get_definition(selected);
            assert(piece.can_place(answer,field) && !piece.can_place(answer.translated(1,0),field));
            const auto script=Bot::getop(context,field,piece,start,answer,rotation);
            if (script.empty()) std::cerr<<"missing path mode="<<int(mode)<<" mask="<<mask<<" instant="<<instant<<" zeroDas="<<zeroDas<<" layout="<<layout<<" piece="<<type<<'\n';
            assert(!script.empty() && script.back()=='H');
            const auto& r=context.config.movement;
            if (r.disableLeft) assert(script.find_first_of("Ll")==std::string::npos);
            if (r.disableRight) assert(script.find_first_of("Rr")==std::string::npos);
            if (r.disableClockwise) assert(script.find('X')==std::string::npos);
            if (r.disableCounterclockwise) assert(script.find('Z')==std::string::npos);
            if (r.disable180) assert(script.find('A')==std::string::npos);
            if (r.disableSoftDrop) assert(script.find_first_of("Ss")==std::string::npos);
            Engine::MovementState actual;
            actual.pose=start;
            const auto canPlace=[&](Piece::Pose pose) {return piece.can_place(pose,field);};
            for (char command:script) {
                Engine::apply_movement_script_command(actual,Engine::decode_movement_script_command(command),canPlace,
                    [&](Piece::Pose& pose,Runtime::Rotation target,Piece::KickIndex& kick) {return piece.try_rotate(pose,target,field,kick);},
                    zeroDas,false,r,instant,true);
            }
            assert(Engine::autoplay_landing_matches(field,piece,actual,answer,rotation,
                Engine::autoplay_spin_rules(context.config)));
        }
    field.clear();
    context.config.movement=Restrictions::from_mask(31);
    const auto piece=Piece::get_definition(Piece::Type::from_index(3));
    assert(Bot::getop(context,field,piece,{{-2,4},Runtime::Rotation{}},{{18,3},Runtime::Rotation{}},{}).empty());
    context.config.movement.disableHardDrop=true;
    assert(Bot::getop(context,field,piece,{{-2,4},Runtime::Rotation{}},{{18,4},Runtime::Rotation{}},{}).empty());
}

void twenty_g_paths_cannot_cross_a_pit(Runtime::GameContext& context) {
    context.config={};
    context.config.autoplay.mode=Mode::While1;
    context.config.autoplay.searchWidth=2;
    context.config.handling.gravityInterval=Runtime::GravityInterval::instant();
    context.config.field.holdEnabled=false;
    auto& rules=context.config.movement;
    rules.disableLeft=rules.disableClockwise=rules.disableCounterclockwise=rules.disable180=rules.disableSoftDrop=true;
    auto& field=context.session.player.gameplay.playfield.field;
    field.initialize(10,20);
    for(int row=10;row<20;++row) for(int col=0;col<10;++col)
        if(col!=3 && col!=4)field.set_cell(row,col,Runtime::BoardCell::garbage());
    const auto type=Piece::Type::from_index(3);
    const auto piece=Piece::get_definition(type);
    const Piece::Pose start{{0,1},Runtime::Rotation{}};
    auto across=start;across.position.col=6;
    while(piece.can_place(across.translated(1,0),field))++across.position.row;
    for(bool zeroDas:{false,true}) for(bool zeroArr:{false,true}) {
        context.config.handling.das=Runtime::Duration::from_seconds(zeroDas ? 0 : .1);
        context.config.handling.arr=Runtime::Duration::from_seconds(zeroArr ? 0 : .02);
        assert(Bot::getop(context,field,piece,start,across,Runtime::RotationRecord::none()).empty());
        assert(Bot::init(context));
        auto answer=start;auto rotation=Runtime::RotationRecord::none();
        const auto selected=Bot::getpos(context,context.session.player,field,{type,type,type},{},start,
            {false,Piece::Type::none()},answer,rotation);
        assert(selected==type && answer.position.col>=1 && answer.position.col<=3);
        assert(piece.can_place(answer,field) && !piece.can_place(answer.translated(1,0),field));
        assert(!Bot::getop(context,field,piece,start,answer,rotation).empty());
    }
}

void init_disables_unsupported_bot() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    UserConfig::Settings user;
    user.keyCodes=UserConfig::default_key_codes(); user.autoplayMode=3; user.disableLeft=1;
    Input::Services input;
    Legacy::BotStrategy strategy;
    Replay::ReplaySessionState replay;
    context.attach_user_settings(user); context.attach_input(input); context.attach_autoplay(strategy);
    context.attach_replay(replay);
    std::ostringstream output;
    auto* old=std::cout.rdbuf(output.rdbuf());
    App::initialize_game(context);
    std::cout.rdbuf(old);
    assert(context.config.autoplay.mode==Mode::Disabled);
    assert(context.config.movement.disableLeft && user.autoplayMode==3); // Selection persists for later rule changes.
}
}

int main() {
    config_round_trip();
    input_and_script_guards();
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    availability_and_initialization(context);
    legal_bot_paths(context);
    twenty_g_paths_cannot_cross_a_pit(context);
    unified_path_restriction_matrix();
    reconstructed_scripts_cannot_bypass_restrictions();
    init_disables_unsupported_bot();
    std::cout<<"disabled_movement_test: OK\n";
}
