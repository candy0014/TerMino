#include "tetr/config/normalization.h"
#include "tetr/config/map_store.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/input/services.h"
#include "tetr/replay/config_codec.h"
#include "tetr/render/board_renderer.h"
#include "tetr/rules/game_completion.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/settings_model.h"
#include "support/terminal_screen.h"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using namespace Runtime;
using Id=UserConfig::SettingId;
namespace Model=App::Settings::Model;
const Model::SettingItem* find(const Model::SettingNode& tree,Id id) {
    if(tree.item && tree.item->id==id) return &*tree.item;
    for(const auto& child:tree.children) if(const auto* item=find(child,id)) return item;
    return nullptr;
}
void settings_and_maps() {
    UserConfig::Settings user;
    const std::array<Id,6> limits{{Id::BlitzTime,Id::RacingDistance,Id::GarbageTarget,Id::PieceTarget,Id::AttackTarget,Id::AllClearTarget}};
    for(int goal=0;goal<=6;++goal) {
        user.goal=goal;
        const auto tree=Model::make_tree(user,false,0);
        const auto* choice=find(tree,Id::Goal);assert(choice && choice->choice.values.size()==7);
        const auto* mode=find(tree,Id::GameMode);assert(mode && mode->choice.values==std::vector<int>({0,3,4,6,5}));
        for(std::size_t i=0;i<limits.size();++i) assert((find(tree,limits[i])!=nullptr)==(goal==int(i)+1));
        assert(find(tree,Id::GarbageMode));
        assert(!find(tree,Id::MapGoal));
    }
    auto map=std::make_shared<MapDefinition>();map->id=8;
    user.goal=5;map->goal=MapGoal::Disabled;
    UserConfig::Maps::select(user,map.get());assert(user.goal==5 && user.mapId==8);
    map->goal=MapGoal::ClearInitial;UserConfig::Maps::select(user,map.get());assert(user.goal==7);
    auto tree=Model::make_tree(user,false,0,false,map);
    assert(find(tree,Id::Goal)->choice.labels.back()=="Clear initial cells");
    assert(Model::choice_value(*find(tree,Id::Goal))==7);
    user.goal=4; // A manual override survives rebuilding the menu and starting a game.
    tree=Model::make_tree(user,false,0,false,map);assert(user.goal==4);
    map->goal=MapGoal::AllClear;user.allClearTarget=9;
    UserConfig::Maps::select(user,map.get());assert(user.goal==6 && user.allClearTarget==1);
    UserConfig::Maps::select(user,nullptr);assert(user.goal==6);
    UserConfig::Maps::select(user,map.get());user.goal=7;
    UserConfig::Maps::select(user,nullptr);assert(user.goal==0 && user.mapId==0);
    user.goal=7;tree=Model::make_tree(user,false,0);
    assert(find(tree,Id::Goal)->choice.values.size()==7 && Model::choice_value(*find(tree,Id::Goal))==0);
    assert(active_goal(normalize_user_config(user,GameMode::Zen))==GameGoal::None);
    user.goal=3;user.garbageTarget=17;user.pieceTarget=29;user.attackTarget=31;user.allClearTarget=4;
    const auto path=std::filesystem::temp_directory_path()/"termino_game_goals_test.ini";
    user.persistencePath=path.string();assert(UserConfig::save(user));
    UserConfig::Settings restored;UserConfig::load(restored,path.string());
    assert(restored.goal==3 && restored.garbageTarget==17 && restored.pieceTarget==29 && restored.attackTarget==31 && restored.allClearTarget==4);
    std::filesystem::remove(path);
}
void completion_and_replay() {
    RuntimeConfig config;ScoringState scoring;
    config.mode.blitzTime=Duration::from_seconds(10);
    config.mode.racingDistance=config.mode.garbageTarget=config.mode.pieceTarget=config.mode.attackTarget=config.mode.allClearTarget=2;
    for(int value=0;value<=7;++value) {
        config.mode.goal=static_cast<GameGoal>(value);
        config.initialMap=value==7 ? std::make_shared<MapDefinition>() : nullptr;
        scoring={};
        assert(!Rules::goal_reached(config,scoring,1,Duration::from_seconds(9.999)));
        scoring.totalLines=2;
        assert(Rules::goal_reached(config,scoring,1,{})==(value==2));
        scoring.totalGarbageLines=scoring.placedBlocks=scoring.totalAttack=2;scoring.allClearCount=2;
        assert(Rules::goal_reached(config,scoring,0,Duration::from_seconds(10))==(value!=0));
        config.mode.mode=GameMode::Multiplayer;
        assert(!Rules::goal_reached(config,scoring,0,Duration::from_seconds(10)));
        config.mode.mode=GameMode::Zen;
        RuntimeConfig decoded;Replay::ConfigPrefixResult prefix;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,prefix));
        assert(decoded.mode.goal==config.mode.goal && decoded.mode.garbageTarget==2 && decoded.mode.allClearTarget==2);
    }
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix("{{Model,1}{RacingDistance,75}{GarbageModel,3}}",config,prefix));
    assert(config.mode.mode==GameMode::Zen && config.mode.goal==GameGoal::Lines && config.mode.racingDistance==75);
    assert(Replay::decode_config_prefix("{{Model,2}{flagBlitz,0}{BlitzTime,33}{Speed,0.6}}",config,prefix));
    assert(config.mode.mode==GameMode::Zen && config.mode.goal==GameGoal::Time && config.mode.blitzTime.seconds()==33);
    assert(config.handling.gravityProgression==GravityProgression::Blitz && config.mode.legacyBlitzScoring);
    const auto canonical=Replay::encode_config_prefix(config)+"}";
    assert(canonical.find("{Model,0}")!=std::string::npos);
    assert(Replay::decode_config_prefix(canonical,config,prefix) && config.mode.legacyBlitzScoring);
    assert(Replay::decode_config_prefix("{{Model,2}{flagBlitz,1}{BlitzTime,15}{Speed,0.3}}",config,prefix));
    assert(config.mode.goal==GameGoal::Time && config.handling.gravityProgression==GravityProgression::Fixed && !config.mode.legacyBlitzScoring);
    assert(std::abs(config.handling.gravityInterval.interval().seconds()-.3)<1e-12);
    UserConfig::Settings user;user.gameMode=2;
    config=normalize_user_config(user,selected_game_mode(user));
    assert(config.mode.mode==GameMode::Zen && config.mode.goal==GameGoal::Time && config.handling.gravityProgression==GravityProgression::Blitz);
    const auto base=GravityInterval::disabled();
    assert(progressed_gravity(GravityProgression::Blitz,0,base).interval().seconds()==1);
    assert(progressed_gravity(GravityProgression::Blitz,4,base).interval().seconds()==.643);
    assert(progressed_gravity(GravityProgression::Blitz,1000,base).is_instant());
}
void loop_and_progress() {
    auto session=std::make_unique<SessionState>();RuntimeServices services;GameContext context{*session,services};
    Input::Services input;context.attach_input(input);input.keyStateProvider=[](int){return false;};
    auto& player=session->player;
    player.presentation.margin=50;player.gameplay.playfield.width=10;player.gameplay.playfield.visibleHeight=20;
    player.gameplay.playfield.nextCount=0;player.pieces.allowedPieces=Piece::types_for_rotation_system(RotationSystem::SrsPlus);
    Engine::initialize_board(context,player);
    context.config.mode.goal=GameGoal::Pieces;context.config.mode.pieceTarget=2;
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    int played=0;
    const auto result=Engine::run_piece_loop(context,player,[&](Playfield&,Piece::Definition,PlayerSessionState& p,bool) {
        ++played;++p.gameplay.scoring.placedBlocks;return Rules::PlayResult::Locked;
    });
    assert(result==Engine::GameLoopExit::GoalReached && played==2);
    player.gameplay.scoring.placedBlocks=0;
    assert(Engine::run_piece_loop(context,player,[](Playfield&,Piece::Definition,PlayerSessionState&,bool) {
        return Rules::PlayResult::Dead;
    })==Engine::GameLoopExit::Dead);
    context.config.mode.goal=GameGoal::Time;context.config.mode.blitzTime={};
    assert(Engine::run_piece_loop(context,player,[](Playfield&,Piece::Definition,PlayerSessionState&,bool) {
        assert(false);return Rules::PlayResult::Locked;
    })==Engine::GameLoopExit::GoalReached);
    const std::array<std::string,6> expected{{"1/2 LINES","1/2 GARBAGE","1/2 P","1/2 A","1/2 ALLCLEAR","1/2 MAP CELLS"}};
    auto map=std::make_shared<MapDefinition>();map->rows.back()="GG        ";context.config.initialMap=map;
    player.gameplay.playfield.field.set_cell(19,0,BoardCell::garbage());player.gameplay.playfield.field.mark_initial_cell(19,0);
    auto& scoring=player.gameplay.scoring;
    scoring.totalLines=scoring.totalGarbageLines=scoring.placedBlocks=scoring.totalAttack=1;scoring.allClearCount=1;
    context.config.mode.racingDistance=context.config.mode.garbageTarget=context.config.mode.pieceTarget=context.config.mode.attackTarget=context.config.mode.allClearTarget=2;
    player.timing.completedGameTime=Duration::from_seconds(4);
    Test::TerminalScreen screen;
    context.config.mode.goal=GameGoal::None;output.str("");Render::print_message(context,player);screen.feed(output.str());
    const auto pieceAt=screen.position_of("1 P"),attackAt=screen.position_of("1 A");
    const int floor=player.presentation.topMargin+player.gameplay.playfield.visibleHeight+1;
    for(int value=2;value<=7;++value) {
        context.config.mode.goal=static_cast<GameGoal>(value);
        output.str("");Render::print_message(context,player);screen.feed(output.str());
        assert(screen.position_of(expected[value-2]).first>0);
        if(value>2) {
            const auto& previousText=expected[value-3];const auto at=screen.position_of(previousText);
            assert(!at.first || screen.at(at.first,at.second+int(previousText.size())).glyph!=" ");
        }
        assert(screen.position_of("0.25 PPS").first>0 && screen.position_of("15.00 APM").first>0);
        assert(screen.position_of("00:04.000").first>0);
        if(value==4) {
            const auto at=screen.position_of("1/2 P");assert(at.first==pieceAt.first && at.second+5==pieceAt.second+3);
        }
        if(value==5) {
            const auto at=screen.position_of("1/2 A");assert(at.first==attackAt.first && at.second+5==attackAt.second+3);
        }
        if(value!=4 && value!=5) assert(screen.position_of(expected[value-2]).first==floor);
        for(int y=floor+1;y<=screen.rows();++y) for(int x=1;x<=screen.columns();++x)
            assert(screen.at(y,x).glyph.empty() || screen.at(y,x).glyph==" ");
    }
    context.config.mode.goal=GameGoal::Time;context.config.mode.blitzTime=Duration::from_seconds(64);
    output.str("");Render::print_message(context,player);screen.feed(output.str());
    assert(screen.position_of("01:00.000").first==floor && !screen.position_of("00:04.000").first);
    assert(screen.position_of("1 P")==pieceAt && screen.position_of("1 A")==attackAt);
    assert(screen.position_of("0.25 PPS").first>0 && screen.position_of("15.00 APM").first>0);
    assert(!screen.position_of("MAP CELLS").first && !screen.position_of("LEFT").first);
    std::cout.rdbuf(previous);
}
}
int main() {settings_and_maps();completion_and_replay();loop_and_progress();}
