#include "tetr/config/presets.h"
#include "tetr/config/normalization.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/app/place.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/input/services.h"
#include "tetr/app/settings_model.h"
#include "../src/app/settings_targets.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using UserConfig::SettingId;
using UserConfig::PresetKind;
namespace Presets=UserConfig::Presets;
namespace Model=App::Settings::Model;
UserConfig::PresetFields numeric(const UserConfig::Settings& user) {
    UserConfig::PresetFields result;
    for(const auto& descriptor:UserConfig::setting_schema()) std::visit([&](auto member) {
        using Member=decltype(member);
        if constexpr (std::is_same_v<Member,int UserConfig::Settings::*> || std::is_same_v<Member,double UserConfig::Settings::*>)
            result.emplace(descriptor.id,user.*member);
    },descriptor.member);
    return result;
}
UserConfig::Settings conflicting() {
    UserConfig::Settings user;
    user.gameMode=3;user.garbageMode=5;
    user.qpExpert=2;user.qpNoHold=1;user.qpGravity=2;user.qpSnowball=2;user.qpFool=4;user.qpInvisible=1;
    user.fieldWidth=17;user.fieldHeight=28;user.nextCount=9;user.fieldInvisible=1;user.antiGravity=1;
    user.holdEnabled=0;user.infiniteHold=1;user.holdCount=0;
    user.gravityInterval=-.25;user.instantGravity=1;user.gravityProgression=2;user.gravityLevelLines=2;user.gravityAcceleration=4;
    user.softDropFactor=.02;user.softDropFixed=1;user.das=.073;user.arr=.004;user.dcd=.3;
    user.infiniteMovement=1;user.lockResetLimit=0;user.lockDelay=0;
    user.spinBonuses=8;user.b2bReward=1;user.b2bCharging=1;user.halfClearEnabled=1;
    user.autoplayMode=4;user.simulationPps=9;user.turnPieces=3;
    user.disableLeft=user.disableRight=user.disableClockwise=user.disableCounterclockwise=user.disableSoftDrop=user.disableHardDrop=1;
    user.disablePause=1;user.keyCodes=UserConfig::default_key_codes();
    user.uiLanguage=1;user.squareStyle=1;user.obscuredColors=1;
    return user;
}
Model::SettingItem field(Model::SettingNode& tree,SettingId id,Model::ValuePart part=Model::ValuePart::Raw) {
    if(tree.item && tree.item->id==id && tree.item->part==part) return *tree.item;
    for(auto& child:tree.children) {
        try{return field(child,id,part);}catch(const std::out_of_range&){}
    }
    throw std::out_of_range("missing setting");
}
void layering() {
    auto user=conflicting();
    const auto baseline=numeric(user);
    const auto keys=user.keyCodes;
    assert(Presets::apply(user,PresetKind::Classic));
    assert(user.rotationSystem==9 && user.randomPieces==6 && user.nextCount==1);
    assert(!user.holdEnabled && user.disableHardDrop && !user.ghost && user.lockDelay==0);
    assert(user.qpFool==4 && user.qpSnowball==2 && user.autoplayMode==4 && !user.instantGravity);
    assert(!user.disableLeft && !user.disableRight && !user.disableClockwise && !user.disableCounterclockwise && !user.disableSoftDrop);
    assert(user.simulationPps==9 && user.turnPieces==3 && !user.infiniteHold && !user.infiniteMovement);
    assert(user.spinBonuses==9 && user.b2bReward==2 && !user.b2bCharging && !user.perfectClearEnabled && !user.halfClearEnabled);
    assert(user.keyCodes==keys && user.uiLanguage==1 && user.squareStyle==1 && user.obscuredColors==1);
    auto tree=Model::make_tree(user,false,0);
    auto das=field(tree,SettingId::ForcedDas);
    assert(Model::assign_scalar(das,"0.31"));
    assert(Presets::modified(user.presets.layers.front()));
    const auto classicSerial=user.presets.layers.front().serial;
    user.autoplayWidth=451; // Never covered by the presets because bots are off.
    assert(Presets::apply(user,PresetKind::Marathon));
    const auto marathonSerial=user.presets.layers.back().serial;
    assert(user.gameMode==0 && user.goal==2 && user.racingDistance==150 && user.gravityProgression==2);
    assert(user.rotationSystem==9 && user.randomPieces==6 && !user.holdEnabled && user.holdCount==1);
    assert(user.disableHardDrop && user.disable180 && !user.infiniteMovement && user.lockResetLimit==0);
    assert(user.gravityLevelLines==10 && user.gravityAcceleration==1 && user.softDropFixed);
    assert(user.forcedDas==.31 && user.forcedArr==6/60.0988 && user.das==.073 && user.arr==.004);
    user.forcedDas=.22;Presets::record_edits(user);
    assert(Presets::source(user,SettingId::ForcedDas)->serial==classicSerial);
    assert(Presets::apply(user,PresetKind::Delayed));
    assert(user.gameMode==0 && user.goal==2 && user.gravityProgression==2 && user.rotationSystem==9 && user.randomPieces==6);
    assert(std::abs(user.forcedDas-10.0/60)<1e-12 && std::abs(user.forcedArr-2.0/60)<1e-12);
    assert(user.forcedSdf==20 && !user.forcedSdfFixed && user.forcedDcd==0 && user.softDropFactor==.02 && user.softDropFixed);
    assert(std::abs(user.entryDelay-6.0/60)<1e-12 && std::abs(user.lineClearDelay-30.0/60)<1e-12);
    assert(Presets::remove(user,marathonSerial));
    assert(user.gameMode==0 && user.gravityProgression==1 && user.gravityInterval==48/60.0988);
    assert(user.forcedDas==10.0/60 && user.racingDistance==std::get<int>(baseline.at(SettingId::RacingDistance)));
    user.racingDistance=200; // Now uncovered, so this becomes an ordinary personal edit.
    assert(Presets::undo(user));
    assert(user.forcedDas==.22 && user.presets.layers.size()==1 && user.presets.layers.front().serial==classicSerial);
    assert(Presets::undo(user));
    auto expected=baseline;expected[SettingId::AutoplayWidth]=451;expected[SettingId::RacingDistance]=200;
    assert(numeric(user)==expected && user.presets.base.empty() && user.presets.layers.empty());
    assert(!Presets::undo(user));

    assert(Presets::apply(user,PresetKind::Classic));user.forcedDas=.4;Presets::record_edits(user);
    assert(Presets::apply(user,PresetKind::Classic));
    assert(user.presets.layers.size()==2);
    assert(Presets::undo(user) && user.forcedDas==.4);
    UserConfig::reset_section(user,UserConfig::Section::Game);
    assert(user.presets.layers.size()==1 && Presets::modified(user.presets.layers.front()));
    assert(Presets::clear(user) && user.das==.073 && user.arr==.004);
    assert(Presets::apply(user,PresetKind::Delayed));
    UserConfig::reset_settings(user);
    assert(user.presets.layers.empty() && user.presets.base.empty() && user.presets.nextSerial==1);
    assert(!Presets::undo(user) && !Presets::clear(user));
    assert(Presets::apply(user,PresetKind::Marathon));
    const auto kept=numeric(user);Presets::commit(user);
    assert(user.presets.layers.empty() && !Presets::clear(user) && numeric(user)==kept);
}
void independent_concerns() {
    auto classicFirst=conflicting(),marathonFirst=conflicting();
    assert(Presets::apply(classicFirst,PresetKind::Classic));
    assert(Presets::apply(classicFirst,PresetKind::Marathon));
    assert(Presets::apply(marathonFirst,PresetKind::Marathon));
    assert(Presets::apply(marathonFirst,PresetKind::Classic));
    assert(numeric(classicFirst)==numeric(marathonFirst));
    assert(marathonFirst.gameMode==0 && marathonFirst.goal==2 && marathonFirst.racingDistance==150 && marathonFirst.gravityProgression==2);
    assert(marathonFirst.rotationSystem==9 && marathonFirst.randomPieces==6 && marathonFirst.nextCount==1);
    assert(!marathonFirst.holdEnabled && marathonFirst.disableHardDrop && marathonFirst.forcedDas==16/60.0988 && marathonFirst.das==.073);
    marathonFirst.gravityInterval=.9;Presets::record_edits(marathonFirst);
    assert(Presets::source(marathonFirst,SettingId::GravityInterval)->kind==PresetKind::Marathon);
    assert(Presets::modified(marathonFirst.presets.layers.front()));
    assert(Presets::remove(marathonFirst,marathonFirst.presets.layers.front().serial));
    assert(marathonFirst.gravityProgression==1 && marathonFirst.gravityInterval==48/60.0988);
    // Delayed affects handling only; Marathon does not reset rules or handling.
    for(const auto& definition:Presets::definitions()) {
        auto user=conflicting();const auto baseline=numeric(user);
        user.disableUndo=0;assert(Presets::apply(user,definition.kind));assert(!user.disableUndo);
        auto disabled=conflicting();disabled.disableUndo=1;
        assert(Presets::apply(disabled,definition.kind));assert(disabled.disableUndo==1);
        assert(!definition.fields.count(SettingId::DisableUndo));
        if(definition.kind==PresetKind::Delayed) {
            assert(user.gameMode==3 && user.rotationSystem==0 && user.randomPieces==0 && user.nextCount==9);
            assert(user.gravityProgression==2 && user.gravityInterval==-.25 && user.instantGravity);
        }
        if(definition.kind==PresetKind::Marathon) {
            for(auto id:{SettingId::Das,SettingId::Arr,SettingId::SoftDropFactor,SettingId::HoldEnabled,
                SettingId::RotationSystem,SettingId::RandomPieces,SettingId::NextCount,SettingId::AutoplayMode})
                assert(numeric(user).at(id)==baseline.at(id));
        }
    }
    UserConfig::Settings race;race.gameMode=1;race.racingDistance=75;
    assert(Presets::apply(race,PresetKind::Classic));assert(race.gameMode==0 && race.goal==2 && race.racingDistance==75);
}
void persistence() {
    auto user=conflicting();
    const auto baseline=numeric(user);
    user.persistencePath=(std::filesystem::temp_directory_path()/"termino_presets_test.ini").string();
    assert(Presets::apply(user,PresetKind::Classic));user.forcedDas=.31;
    assert(Presets::apply(user,PresetKind::Marathon));user.gravityInterval=1.4;
    assert(UserConfig::save(user));
    UserConfig::Settings loaded;UserConfig::load(loaded,user.persistencePath);
    assert(loaded.presets.layers.size()==2 && loaded.forcedDas==.31 && std::abs(loaded.gravityInterval-1.4)<1e-12);
    assert(Presets::modified(loaded.presets.layers[0]) && Presets::modified(loaded.presets.layers[1]));
    assert(Presets::undo(loaded) && loaded.forcedDas==.31);
    const auto owned=loaded.presets.base;
    assert(Presets::clear(loaded));
    const auto restored=numeric(loaded);
    for(const auto& [id,value]:baseline) {
        if(owned.count(id) || value.index()==0) assert(restored.at(id)==value);
        else assert(std::abs(std::get<double>(restored.at(id))-std::get<double>(value))<1e-12);
    }
    UserConfig::PresetState state;
    assert(Presets::decode(Presets::encode(user.presets),state));
    const auto valid=Presets::encode(state);
    for(const auto& malformed:{std::string("3 1 0 0"),valid+" trailing",std::string("1 1 1 99999 0 1 0"),std::string("1 1 0 999999999")}) {
        assert(!Presets::decode(malformed,state));assert(Presets::encode(state)==valid);
    }
    std::ofstream file(std::filesystem::u8path(user.persistencePath),std::ios::app);
    file<<"\n[PRESETS]\nState = invalid\n";file.close();
    UserConfig::load(loaded,user.persistencePath);
    assert(loaded.presets.layers.empty() && loaded.forcedDas==.31); // Bad recovery data never changes effective settings.
    std::filesystem::remove(std::filesystem::u8path(user.persistencePath));
}
void two_players_and_hidden_fields() {
    auto one=conflicting(),two=conflicting();one.gameMode=two.gameMode=6;
    one.das=.07;two.das=.12;one.holdEnabled=1;two.holdEnabled=0;
    assert(Presets::apply(one,PresetKind::Classic,true));assert(Presets::apply(two,PresetKind::Marathon,true));
    assert(one.gameMode==6 && two.gameMode==6 && one.presets.layers.front().kind!=two.presets.layers.front().kind);
    auto both=Model::make_both_tree(one,two);
    auto das=field(both,SettingId::Das);
    assert(Model::assign_scalar(das,"0.2"));
    auto hold=field(both,SettingId::HoldEnabled);Model::apply_choice(hold,1);
    assert(one.holdEnabled && two.holdEnabled && one.das==.2 && two.das==.2);
    assert(Presets::clear(one,true) && Presets::clear(two,true));
    assert(one.das==.2 && two.das==.2 && one.holdEnabled==1 && two.holdEnabled==1 && one.gameMode==6 && two.gameMode==6);
    // Hidden gravity constants locate the mode that hides them after Tab.
    one={};two={};one.gameMode=two.gameMode=6;two.gravityProgression=2;
    both=Model::make_both_tree(one,two);
    auto constants=field(both,SettingId::GravityAcceleration);
    const auto visible=Model::make_tree(one,true,0),full=Model::make_tree(one,true,0,true);
    const auto location=App::Settings::locate_target_selection(visible,full,one,{"GAME","gravity"},
        "setting-"+std::to_string(static_cast<int>(constants.id)));
    const auto game=std::find_if(visible.children.begin(),visible.children.end(),[](const auto& node){return node.key=="GAME";});
    assert(game!=visible.children.end());
    const auto gravity=std::find_if(game->children.begin(),game->children.end(),[](const auto& node){return node.key=="gravity";});
    assert(gravity!=game->children.end());
    assert(location.path==std::vector<std::string>({"GAME","gravity"}));
    assert(gravity->children[location.selected].item->part==Model::ValuePart::GravityMode);
}
void progressive_gravity_and_replay() {
    using Runtime::GravityProgression;
    const auto base=Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(1));
    assert(Runtime::progressed_gravity(GravityProgression::Marathon,0,base)==base);
    const double second=1092.0/1377;
    assert(std::abs(Runtime::progressed_gravity(GravityProgression::Marathon,10,base).interval().seconds()-second)<1e-8);
    assert(std::abs(Runtime::progressed_gravity(GravityProgression::Marathon,5,base,5,2).interval().seconds()-second*second)<1e-8);
    assert(std::abs(Runtime::progressed_gravity(GravityProgression::Nes,10,base).interval().seconds()-43.0/48)<1e-8);
    assert(Runtime::progressed_gravity(GravityProgression::Fixed,1000,base)==base);
    assert(Runtime::progressed_gravity(GravityProgression::Marathon,1000,Runtime::GravityInterval::disabled()).is_disabled());
    UserConfig::Settings user;
    auto tree=Model::make_tree(user,false,0);
    auto mode=field(tree,SettingId::GravityInterval,Model::ValuePart::GravityMode);
    Model::apply_choice(mode,4);
    assert(user.gravityProgression==2 && user.presets.layers.empty());
    tree=Model::make_tree(user,false,0);
    auto lines=field(tree,SettingId::GravityLevelLines),factor=field(tree,SettingId::GravityAcceleration);
    assert(!Model::assign_scalar(lines,"0") && !Model::assign_scalar(lines,"1000001"));
    assert(!Model::assign_scalar(factor,"nan") && !Model::assign_scalar(factor,"0") && !Model::assign_scalar(factor,"101"));
    assert(Model::assign_scalar(lines,"7") && Model::assign_scalar(factor,"1.5"));
    const auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    Runtime::RuntimeConfig decoded;Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,prefix));
    assert(decoded.handling.gravityProgression==GravityProgression::Marathon);
    assert(decoded.handling.gravityLevelLines==7 && decoded.handling.gravityAcceleration==1.5);
    Runtime::RuntimeConfig old=config;
    assert(Replay::decode_config_prefix("{{Model,0}{Speed,0.6}}",old,prefix));
    assert(old.handling.gravityProgression==GravityProgression::Fixed);
    assert(old.handling.gravityLevelLines==10 && old.handling.gravityAcceleration==1);
    // Exercise the live piece entry point with no preset stack. Interrupt the
    // movement poll after it has selected this level's actual runtime gravity.
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};Input::Services input;Replay::ReplaySessionState replay;
    context.attach_input(input);context.attach_replay(replay);context.attach_user_settings(user);context.config=config;
    auto& player=session->player;auto& board=player.gameplay.playfield;
    board.width=10;board.visibleHeight=board.activeHeight=20;board.field.initialize(10,20);
    player.gameplay.scoring.totalLines=14;
    struct Stop{};input.keyStateProvider=[&](int)->bool {
        if(context.session.dynamicRules.gravityInterval) throw Stop{};
        return false;
    };
    Input::refresh_bindings(input,UserConfig::default_key_codes());
    App::PieceLoopState loop;
    bool stopped=false;std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    try{App::run_piece(context,board.field,Piece::get_definition(Piece::Type::from_index(0)),player,loop);}catch(const Stop&){stopped=true;}
    std::cout.rdbuf(previous);
    assert(stopped);
    const auto expected=Runtime::progressed_gravity(config.handling.gravityProgression,14,config.handling.gravityInterval,7,1.5);
    assert(Runtime::current_gravity_interval(context)==expected);
}
void sparse_authoring_and_storage() {
    using Id=SettingId;
    const auto path=std::filesystem::temp_directory_path()/"termino_sparse_presets_test.txt";
    std::filesystem::remove(path);
    Presets::Collection collection;assert(Presets::load(collection,path));
    assert(collection.presets.size()==3 && collection.nextId==4);
    for(std::size_t i=0;i<3;++i) {
        assert(collection.presets[i].kind==Presets::definitions()[i].kind);
        assert(collection.presets[i].fields==Presets::definitions()[i].fields);
    }
    UserConfig::Settings draft;draft.keyCodes=UserConfig::default_key_codes();draft.presetDefined.emplace();
    auto tree=Model::make_tree(draft,false,0);
    auto das=field(tree,Id::Das);assert(Model::assign_scalar(das,"4f"));
    auto sdf=field(tree,Id::SoftDropFactor);assert(Model::assign_scalar(sdf,"2f"));
    assert(Presets::defined(draft,Id::SoftDropFixed) && !Presets::defined(draft,Id::Arr));
    assert(!Model::assign_scalar(das,"nan"));
    Presets::write_value(draft,Id::ServerIp,std::string("192.168.1.2"));Presets::define(draft,Id::ServerIp);
    Presets::write_value(draft,Id::ControlLeft,std::vector<int>{37,38});Presets::define(draft,Id::ControlLeft);
    Presets::define(draft,Id::UiLanguage);draft.uiLanguage=1;
    Presets::SavedPreset saved{0,"中文预设 \"测试\"",Presets::draft_fields(draft)};
    assert(Presets::save(saved,path) && saved.id==4);
    assert(Presets::load(collection,path) && collection.presets.size()==4);
    assert(collection.presets.back().fields==saved.fields);
    assert(Presets::save(saved,path) && Presets::load(collection,path) && collection.presets.size()==4);
    UserConfig::Settings player;player.das=.27;player.arr=.031;player.serverIp="old";
    player.keyCodes=UserConfig::default_key_codes();const auto originalKeys=UserConfig::control_keys(player,0);
    assert(Presets::apply(player,saved));assert(player.arr==.031 && player.serverIp=="192.168.1.2");
    assert(UserConfig::control_keys(player,0)==std::vector<int>({37,38}));
    UserConfig::PresetState state;assert(Presets::decode(Presets::encode(player.presets),state));
    assert(state.layers.front().name==saved.name && state.layers.front().kind==PresetKind::Custom);
    assert(Presets::undo(player) && player.das==.27 && player.arr==.031 && player.serverIp=="old");
    assert(UserConfig::control_keys(player,0)==originalKeys);
    assert(Presets::decode("1 2 1 "+std::to_string(static_cast<int>(Id::Das))+" 1 0.1 1 1 1 1 "+std::to_string(static_cast<int>(Id::Das))+" 1 0.2 1 "+std::to_string(static_cast<int>(Id::Das))+" 1 0.2",state)); // Legacy numeric layers.
    assert(Presets::apply(draft,collection.presets[1]));
    assert(Presets::defined(draft,Id::ForcedArr) && !Presets::defined(draft,Id::Arr));assert(Presets::undo(draft));
    assert(!Presets::defined(draft,Id::ForcedArr) && Presets::defined(draft,Id::Das));
    assert(Model::assign_scalar(das,"未定义") && !Presets::defined(draft,Id::Das));
    assert(Presets::apply(draft,collection.presets[1]));Presets::undefine(draft,Id::Das);
    assert(Presets::clear(draft) && !Presets::defined(draft,Id::Das));
    Presets::define(draft,Id::FieldWidth);draft.fieldWidth=13;Presets::define(draft,Id::Das);draft.das=.23;
    tree=Model::make_tree(draft,false,0);
    const auto find=[&](const auto& self,const Model::SettingNode& node,const std::string& key)->const Model::SettingNode* {
        if(node.key==key) return &node;
        for(const auto& child:node.children) if(const auto* found=self(self,child,key)) return found;
        return nullptr;
    };
    const auto* playfield=find(find,tree,"playfield");assert(playfield);
    assert(!find(find,tree,"preset-new"));
    const auto& reset=playfield->children.back();
    UserConfig::Settings original;original.fieldWidth=17;
    Model::reset_draft(draft,reset,&original);assert(draft.fieldWidth==17 && draft.das==.23);
    Model::reset_draft(draft,reset,nullptr);assert(!Presets::defined(draft,Id::FieldWidth) && Presets::defined(draft,Id::Das));
    UserConfig::Settings defaults;Model::reset_draft(draft,reset,&defaults);
    assert(draft.fieldWidth==10 && Presets::defined(draft,Id::FieldWidth) && draft.das==.23);
    Presets::define(draft,Id::GravityInterval);draft.gravityInterval=2.3;
    const auto* handling=find(find,tree,"HANDLING");assert(handling);
    Model::reset_draft(draft,handling->children.back(),nullptr);
    assert(draft.gravityInterval==2.3 && Presets::defined(draft,Id::GravityInterval));
    assert(!Presets::defined(draft,Id::Das));
    const auto* shape=find(find,tree,"cheese");assert(shape);
    draft.cheesePattern=-2;Presets::define(draft,Id::CheesePattern);
    Model::reset_draft(draft,shape->children.back(),&defaults);
    assert(draft.cheesePattern==-1); // Shape reset keeps the separate refill switch.
    saved.fields[Id::MapId]=3;saved.fields[Id::MapGoal]=2;saved.fields[Id::MapRotationSystem]=7;
    assert(Presets::save(saved,path));
    assert(Presets::remap_maps({{1,0},{2,1},{3,2}},path));assert(Presets::load(collection,path));
    assert(std::get<int>(collection.presets.back().fields.at(Id::MapId))==2);
    assert(Presets::remap_maps({{2,0}},path));assert(Presets::load(collection,path));
    assert(std::get<int>(collection.presets.back().fields.at(Id::MapId))==0);
    assert(std::get<int>(collection.presets.back().fields.at(Id::MapGoal))==0);
    saved.fields[Id::Das]=std::string("invalid");assert(!Presets::save(saved,path));
    assert(Presets::load(collection,path) && collection.presets.back().fields.at(Id::Das).index()==1);
    std::filesystem::remove(path);
}
void two_player_storage_and_conditional_branches() {
    using Id=SettingId;using Part=Model::ValuePart;
    const auto path=std::filesystem::temp_directory_path()/"termino_two_player_presets_test.txt";
    std::filesystem::remove(path);
    Presets::SavedPreset saved{0,"双人预设",{{Id::GameMode,6},{Id::PreMove,0}}};
    assert(Presets::is_local_versus(saved));
    saved.playerTwoFields=UserConfig::PresetFields{{Id::GameMode,6},{Id::PreMove,1},{Id::MapId,3},{Id::MapGoal,2}};
    assert(Presets::save(saved,path));Presets::Collection library;assert(Presets::load(library,path));
    assert(library.presets.back().playerTwoFields==saved.playerTwoFields);
    UserConfig::Settings one,two;one.gameMode=two.gameMode=6;one.preMove=1;two.preMove=0;
    assert(Presets::apply(one,saved,true,false) && Presets::apply(two,saved,true,true));
    assert(one.preMove==0 && two.preMove==1 && one.gameMode==6 && two.gameMode==6);
    assert(Presets::clear(one,true) && Presets::clear(two,true));assert(one.preMove==1 && two.preMove==0);
    assert(Presets::remap_maps({{1,0},{2,1},{3,2}},path));assert(Presets::load(library,path));
    assert(std::get<int>(library.presets.back().playerTwoFields->at(Id::MapId))==2);
    saved.playerTwoFields=UserConfig::PresetFields{};assert(Presets::save(saved,path));
    assert(Presets::load(library,path) && library.presets.back().playerTwoFields && library.presets.back().playerTwoFields->empty());
    assert(Presets::apply(two,library.presets.back(),true,true) && two.preMove==0 && two.presets.layers.empty());
    {std::ofstream legacy(path);legacy<<"TETR_PRESETS 1 2 1\n1 3 \"Legacy\" 1 "<<static_cast<int>(Id::PreMove)<<" 0 1\n";}
    assert(Presets::load(library,path) && !library.presets.back().playerTwoFields);
    assert(Presets::apply(two,library.presets.back(),true,true) && two.preMove==1);
    saved.fields[Id::GameMode]=0;assert(!Presets::is_local_versus(saved));
    saved.fields.erase(Id::GameMode);assert(!Presets::is_local_versus(saved));
    std::filesystem::remove(path);
    {
        UserConfig::Settings source,target;
        source.keyCodes=UserConfig::default_key_codes();target.keyCodes=UserConfig::default_player_two_key_codes();
        source.presetDefined.emplace();target.presetDefined.emplace();
        const auto targetKeys=UserConfig::control_keys(target,0);
        Presets::SavedPreset bindings{0,"Bindings and field",{{Id::ControlLeft,std::vector<int>{source.keyCodes[3]}},{Id::FieldWidth,13}}};
        assert(Presets::apply(source,bindings));
        UserConfig::copy_settings_except_bindings(target,source);
        assert(!Presets::defined(target,Id::ControlLeft) && Presets::defined(target,Id::FieldWidth));
        assert(UserConfig::control_keys(target,0)==targetKeys && target.fieldWidth==13);
        assert(Presets::undo(target) && UserConfig::control_keys(target,0)==targetKeys);
    }

    // With every controller defined, the draft exposes exactly the normal
    // settings' parameter branches. Action-only authoring entries may differ.
    const auto collect=[](const auto& self,const Model::SettingNode& node,const std::string& path,std::set<std::string>& items)->void {
        if(node.item) items.insert(path+":"+std::to_string(static_cast<int>(node.item->id))+":"+std::to_string(static_cast<int>(node.item->part)));
        for(const auto& child:node.children) self(self,child,path+"/"+child.key,items);
    };
    for(int mode:{0,1,2,-2,3,4,5,6}) for(int garbage:{0,1,3,4,5}) for(int bot:{0,1}) {
        UserConfig::Settings normal;normal.gameMode=mode;normal.garbageMode=garbage;normal.autoplayMode=bot;
        normal.holdEnabled=bot;normal.infiniteHold=garbage%2;normal.b2bCharging=bot;
        normal.perfectClearEnabled=bot;normal.halfClearEnabled=1-bot;normal.cheesePattern=garbage%3+1;
        normal.lockDelay=bot ? .5 : 0;normal.gravityProgression=bot ? 2 : 0;
        auto draft=normal;draft.presetDefined.emplace();
        for(const auto& descriptor:UserConfig::setting_schema()) Presets::define(draft,descriptor.id);
        const auto ordinary=Model::make_tree(normal,mode==6,0),authoring=Model::make_tree(draft,mode==6,0);
        std::set<std::string> expected,actual;collect(collect,ordinary,"root",expected);collect(collect,authoring,"root",actual);
        assert(expected==actual);
    }
    UserConfig::Settings draft;draft.presetDefined.emplace();
    auto tree=Model::make_tree(draft,false,0);
    const auto has=[&](const auto& self,const Model::SettingNode& node,Id id)->bool {
        if(node.item && node.item->id==id) return true;
        for(const auto& child:node.children) if(self(self,child,id)) return true;
        return false;
    };
    assert(has(has,tree,Id::BlitzTime) && has(has,tree,Id::AutoplayPps) && has(has,tree,Id::HoldCount));
    auto modeItem=field(tree,Id::GameMode,Part::GameMode),holdItem=field(tree,Id::HoldEnabled);
    auto botItem=field(tree,Id::AutoplayMode),gravityItem=field(tree,Id::GravityInterval,Part::GravityMode);
    Model::apply_choice(modeItem,6);Model::apply_choice(holdItem,0);
    Model::apply_choice(botItem,0);Model::apply_choice(gravityItem,2);
    tree=Model::make_tree(draft,true,0);
    assert(!has(has,tree,Id::BlitzTime) && !has(has,tree,Id::ReplayPath) && !has(has,tree,Id::RacingDistance));
    assert(!has(has,tree,Id::AutoplayPps) && !has(has,tree,Id::InfiniteHold) && !has(has,tree,Id::HoldCount));
    assert(!has(has,tree,Id::GravityLevelLines));
    Presets::undefine(draft,Id::AutoplayMode);tree=Model::make_tree(draft,true,0);
    assert(has(has,tree,Id::AutoplayPps));
}
void local_root_draft_reset() {
    using Id=SettingId;
    UserConfig::Settings original;original.fieldWidth=17;
    UserConfig::Settings defaults;
    for(const auto* source:std::vector<const UserConfig::Settings*>{nullptr,&original,&defaults}) {
        UserConfig::Settings draft;draft.presetDefined.emplace();
        draft.gameMode=6;draft.fieldWidth=13;
        Presets::define(draft,Id::GameMode);Presets::define(draft,Id::FieldWidth);
        const auto tree=Model::make_tree(draft,true,0);
        const auto& reset=tree.children.back();assert(reset.kind==Model::NodeKind::ResetGame);
        Model::reset_draft(draft,reset,source);
        assert(draft.gameMode==6 && Presets::defined(draft,Id::GameMode));
        assert(Presets::defined(draft,Id::FieldWidth)==(source!=nullptr));
        assert(draft.fieldWidth==(source ? source->fieldWidth : defaults.fieldWidth));
    }
}

void quick_play_mod_recovery() {
    UserConfig::Settings user;user.qpGravity=1;user.qpNoHold=2;user.qpDefaultFloor=4;
    const auto original=UserConfig::snapshot_quick_play_mods(user).values;
    const auto changeMode=[&](int mode) {
        auto tree=Model::make_tree(user,false,0);
        const auto find=[&](auto&& self,Model::SettingNode& node)->Model::SettingItem* {
            if (node.item && node.item->id==SettingId::GameMode && node.item->part==Model::ValuePart::GameMode) return &*node.item;
            for (auto& child:node.children) if (auto* item=self(self,child)) return item;
            return nullptr;
        };
        auto* item=find(find,tree);assert(item);Model::apply_choice(*item,mode);
    };
    changeMode(3);assert(user.quickPlayModBackup==original);
    user.qpGravity=2;user.qpNoHold=0;user.qpFool=4;user.qpDefaultFloor=9;
    const auto path=std::filesystem::temp_directory_path()/"termino_qp_mod_recovery.ini";
    user.persistencePath=path.string();assert(UserConfig::save(user));
    UserConfig::Settings loaded;UserConfig::load(loaded,path.string());
    assert(loaded.quickPlayModBackup==original && loaded.qpGravity==2 && loaded.qpFool==4);
    const auto before=UserConfig::snapshot_quick_play_mods(loaded);loaded.gameMode=0;
    UserConfig::reconcile_quick_play_mods(loaded,before);
    assert(UserConfig::snapshot_quick_play_mods(loaded).values==original && !loaded.quickPlayModBackup);
    changeMode(1);assert(UserConfig::snapshot_quick_play_mods(user).values==original && !user.quickPlayModBackup);
    user.qpGravity=0;const auto next=UserConfig::snapshot_quick_play_mods(user).values;
    changeMode(3);user.qpSnowball=2;changeMode(2);
    assert(UserConfig::snapshot_quick_play_mods(user).values==next);
    // A mode supplied by a preset takes the snapshot before its own Mod values.
    Presets::SavedPreset qp;qp.kind=PresetKind::Custom;qp.name="QP recovery";
    qp.fields={{SettingId::GameMode,3},{SettingId::QpGravity,2}};
    assert(Presets::apply(user,qp));assert(user.quickPlayModBackup==next && user.qpGravity==2);
    assert(Presets::undo(user));assert(UserConfig::snapshot_quick_play_mods(user).values==next);
    std::filesystem::remove(path);
}

}
int main() {
    layering();independent_concerns();persistence();two_players_and_hidden_fields();progressive_gravity_and_replay();sparse_authoring_and_storage();two_player_storage_and_conditional_branches();
    local_root_draft_reset();quick_play_mod_recovery();
    std::cerr<<"presets_test: OK\n";
}
