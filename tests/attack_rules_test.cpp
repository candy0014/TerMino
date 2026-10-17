#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/locked_piece_resolution.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/render/board_renderer.h"
#include "tetr/replay/config_codec.h"
#include "tetr/rules/placement_resolution.h"
#include "tetr/runtime/game_context.h"
#include "tetr/app/settings_model.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <limits>
#include <memory>
#include <iostream>
#include <sstream>

namespace {
using Id=UserConfig::SettingId;
using Node=App::Settings::Model::SettingNode;
const Node* find(const Node& node,Id id) {
    if(node.item && node.item->id==id) return &node;
    for(const auto& child:node.children) if(auto result=find(child,id)) return result;
    return nullptr;
}
const Node& group(const Node& node,const std::string& key) {
    for(const auto& child:node.children) if(child.key==key) return child;
    assert(false); return node;
}
void settings_and_replays() {
    UserConfig::Settings user;
    auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    auto& r=config.attackRules;
    assert(r.b2bReward==Runtime::B2BReward::FixedAttack && r.b2bRewardAmount==1 && r.b2bCharging && r.b2bChargeThreshold==4);
    assert(r.b2bChargeReduction==0 && r.perfectClearEnabled && r.perfectClearAttack==5 && r.perfectClearCharge==1);
    assert(!r.halfClearEnabled && r.halfClearAttack==0 && r.halfClearCharge==0 && r.garbageSpecialAttack==1);
    auto tree=App::Settings::Model::make_tree(user,false,0);
    const auto& attack=group(group(tree,"GAME"),"attack-rules");
    assert(find(attack,Id::AttackMultiplier));
    auto multiplierItem=*find(attack,Id::AttackMultiplier)->item;
    assert(App::Settings::Model::assign_scalar(multiplierItem,"1.123456789"));
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Multiplayer).garbage.sendMultiplier==1.123456789);
    user.attackMultiplier=1;
    const auto& reward=group(attack,"b2b-reward");
    assert(reward.title=="B2B reward");
    assert(std::none_of(attack.children.begin(),attack.children.end(),[](const Node& node) {
        return node.key=="b2b-reward-mode" || node.key=="b2b-charging";
    }));
    assert(find(reward,Id::B2BReward));
    assert(find(reward,Id::B2BRewardAmount));
    assert(find(reward,Id::B2BCharging));
    assert(find(attack,Id::SpinBonuses));
    const auto& game=group(tree,"GAME");
    assert(std::none_of(game.children.begin(),game.children.end(),[](const Node& node) {
        return node.item && node.item->id==Id::SpinBonuses;
    }));
    auto editedReward=reward;
    assert(App::Settings::Model::assign_scalar(*editedReward.children[1].item,"3"));
    assert(user.b2bRewardAmount==3);
    for(int mode:{1,2}) {
        user.b2bReward=mode;
        auto hiddenTree=App::Settings::Model::make_tree(user,false,0);
        assert(!find(hiddenTree,Id::B2BRewardAmount));
        assert(user.b2bRewardAmount==3);
    }
    user.b2bReward=0;
    auto restoredTree=App::Settings::Model::make_tree(user,false,0);
    assert(find(restoredTree,Id::B2BRewardAmount));
    assert(find(reward,Id::B2BChargeThreshold) && find(reward,Id::B2BChargeReduction));
    assert(find(group(attack,"perfect-clear"),Id::PerfectClearCharge));
    assert(!find(group(attack,"half-clear"),Id::HalfClearAttack));
    user.b2bCharging=0; user.perfectClearEnabled=0; user.halfClearEnabled=1;
    tree=App::Settings::Model::make_tree(user,false,0);
    assert(!find(tree,Id::B2BChargeThreshold) && !find(tree,Id::B2BChargeReduction));
    assert(!find(tree,Id::PerfectClearAttack) && !find(tree,Id::PerfectClearCharge));
    assert(find(tree,Id::HalfClearAttack) && find(tree,Id::HalfClearCharge));
    user.b2bReward=1; user.b2bRewardAmount=3; user.b2bChargeThreshold=9; user.b2bChargeReduction=-2;
    user.perfectClearAttack=17; user.perfectClearCharge=3;
    user.halfClearAttack=7; user.halfClearCharge=2; user.garbageSpecialAttack=4;
    user.attackMultiplier=1.123456789;
    assert(UserConfig::assign_setting_text(user,Id::B2BChargeReduction,"-3"));
    assert(user.b2bChargeReduction==-3);
    user.persistencePath=(std::filesystem::temp_directory_path()/"termino_attack_rules.ini").string();
    assert(UserConfig::save(user));
    UserConfig::Settings loaded;
    UserConfig::load(loaded,user.persistencePath);
    std::filesystem::remove(user.persistencePath);
    assert(loaded.b2bReward==1 && loaded.b2bRewardAmount==3 && loaded.b2bCharging==0 && loaded.b2bChargeThreshold==9 && loaded.b2bChargeReduction==-3);
    assert(loaded.perfectClearEnabled==0 && loaded.perfectClearAttack==17 && loaded.perfectClearCharge==3);
    assert(loaded.halfClearEnabled==1 && loaded.halfClearAttack==7 && loaded.halfClearCharge==2 && loaded.garbageSpecialAttack==4);
    assert(loaded.attackMultiplier==user.attackMultiplier);
    config=Runtime::normalize_user_config(loaded,Runtime::GameMode::Zen);
    Runtime::RuntimeConfig decoded;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,prefix));
    assert(decoded.garbage.sendMultiplier==user.attackMultiplier);
    const auto& d=decoded.attackRules;
    assert(!d.legacy && d.b2bReward==Runtime::B2BReward::Chaining && d.b2bRewardAmount==3 && !d.b2bCharging && d.b2bChargeReduction==-3);
    assert(d.b2bChargeThreshold==9 && !d.perfectClearEnabled && d.perfectClearAttack==17 && d.perfectClearCharge==3);
    assert(d.halfClearEnabled && d.halfClearAttack==7 && d.halfClearCharge==2 && d.garbageSpecialAttack==4);
    assert(Replay::decode_config_prefix("{{AttackRulesVersion,1}{B2BReward,0}}",decoded,prefix));
    assert(!decoded.attackRules.legacy && decoded.attackRules.b2bRewardAmount==1);
    assert(Replay::decode_config_prefix("{}",decoded,prefix));
    assert(decoded.attackRules.legacy && !decoded.attackRules.halfClearEnabled);
    UserConfig::reset_section(loaded,UserConfig::Section::Game);
    assert(loaded.b2bReward==0 && loaded.b2bRewardAmount==1 && loaded.b2bCharging==1 && loaded.b2bChargeReduction==0 && loaded.b2bChargeThreshold==4);
    assert(loaded.perfectClearEnabled==1 && loaded.perfectClearAttack==5 && loaded.perfectClearCharge==1);
    assert(loaded.halfClearEnabled==0 && loaded.halfClearAttack==0 && loaded.halfClearCharge==0 && loaded.garbageSpecialAttack==1);
    assert(loaded.attackMultiplier==1);
    user.b2bReward=1; user.b2bRewardAmount=8; user.spinBonuses=2;
    tree=App::Settings::Model::make_tree(user,false,0);
    const auto& reset=group(group(group(tree,"GAME"),"attack-rules"),"b2b-reward").children.back();
    App::Settings::Model::reset_subtree(user,reset,false);
    assert(user.b2bReward==0 && user.b2bRewardAmount==1 && user.spinBonuses==2);
    assert(user.b2bCharging==1 && user.b2bChargeThreshold==4 && user.b2bChargeReduction==0);
    App::UiText::set_language(App::UiText::Language::Chinese);
    for(auto id:{Id::B2BReward,Id::B2BRewardAmount,Id::B2BCharging,Id::B2BChargeThreshold,Id::B2BChargeReduction,
        Id::PerfectClearEnabled,Id::PerfectClearAttack,Id::PerfectClearCharge,
        Id::HalfClearEnabled,Id::HalfClearAttack,Id::HalfClearCharge,Id::GarbageSpecialAttack}) {
        const auto label=UserConfig::setting_descriptor(id).label;
        assert(App::UiText::tr(label)!=label);
    }
}
void chains_and_damage() {
    Rules::AttackInput attack;
    attack.linesCleared=4; attack.combo=1; attack.b2bTransition=Rules::B2BTransition::Increment;
    attack.b2b=1001;
    assert(Rules::calculate_attack(attack).attack==5);
    attack.attackRules.b2bRewardAmount=3;
    for(int displayed:{1,2,1000}) {
        attack.b2b=displayed+1;
        assert(Rules::calculate_attack(attack).attack==7);
    }
    attack.b2b=1;
    assert(Rules::calculate_attack(attack).attack==4);
    attack.b2b=2; attack.combo=2;
    assert(Rules::calculate_attack(attack).attack==8); // (Quad 4 + fixed 3) * 1.25.
    attack.garbageMultiplier=.5;
    assert(Rules::calculate_attack(attack).attack==4);
    attack.garbageMultiplier=1; attack.combo=1;
    attack.attackRules.b2bRewardAmount=0;
    assert(Rules::calculate_attack(attack).attack==4);
    attack.attackRules.b2bRewardAmount=3;
    attack.garbageMode=Runtime::GarbageMode::QuickPlay;
    assert(Rules::calculate_attack(attack).attack==7);
    attack.garbageMode=Runtime::GarbageMode::None;
    attack.attackRules.b2bReward=Runtime::B2BReward::None;
    assert(Rules::calculate_attack(attack).attack==4);
    attack.attackRules.b2bReward=Runtime::B2BReward::Chaining;
    for(auto [displayed,bonus]:{std::pair{1,1}, {2,1}, {3,2}, {7,2}, {8,3}, {23,3}, {24,4}, {67,5}, {185,6}, {504,7}, {1370,8}}) {
        attack.b2b=displayed+1;
        assert(Rules::calculate_attack(attack).attack==4+bonus);
    }
    attack.b2b=3; attack.combo=2;
    assert(Rules::calculate_attack(attack).attack==7); // Fractional chaining bonus survives combo scaling.
    attack.clearedGarbageLines=4; attack.attackRules.garbageSpecialAttack=3;
    assert(Rules::calculate_attack(attack).attack==10);
    attack.clearedGarbageLines=1;
    assert(Rules::calculate_attack(attack).attack==10);
    attack.attackRules.garbageSpecialAttack=0;
    assert(Rules::calculate_attack(attack).attack==7);
    Rules::ChainInput chain;
    chain.linesCleared=1; chain.currentB2B=4;
    assert(Rules::update_chain(chain).b2bCharging==0);
    chain.currentB2B=5;
    assert(Rules::update_chain(chain).b2bCharging==4);
    chain.attackRules.b2bChargeReduction=-2;
    assert(Rules::update_chain(chain).b2bCharging==6);
    chain.attackRules.b2bChargeReduction=7;
    assert(Rules::update_chain(chain).b2bCharging==0);
    chain.attackRules.b2bChargeReduction=0; chain.attackRules.b2bCharging=false;
    auto result=Rules::update_chain(chain);
    assert(result.b2bCharging==0 && result.b2b==0);
    chain.attackRules.b2bCharging=true; chain.attackRules.b2bChargeThreshold=10;
    assert(Rules::update_chain(chain).b2bCharging==0);
    chain.currentB2B=11;
    assert(Rules::update_chain(chain).b2bCharging==10);
    chain.linesCleared=4; chain.perfectClear=true; chain.attackRules.perfectClearCharge=3;
    assert(Rules::update_chain(chain).b2b==14); // Total increment, including on Quads/Spins.
    chain.attackRules.perfectClearEnabled=false;
    assert(Rules::update_chain(chain).b2b==12);
    chain.perfectClear=false; chain.halfClear=true; chain.attackRules.halfClearEnabled=true;
    chain.attackRules.halfClearCharge=2;
    assert(Rules::update_chain(chain).b2b==13);
    chain.attackRules.halfClearCharge=0;
    assert(Rules::update_chain(chain).b2b==11);
    chain.currentB2B=std::numeric_limits<int>::max(); chain.attackRules.halfClearCharge=1000000;
    assert(Rules::update_chain(chain).b2b==std::numeric_limits<int>::max());
    chain.linesCleared=1; chain.halfClear=false; chain.attackRules.b2bChargeReduction=-1000000;
    assert(Rules::update_chain(chain).b2bCharging==std::numeric_limits<int>::max());
    attack={}; attack.linesCleared=2; attack.perfectClear=true;
    assert(Rules::calculate_attack(attack).attack==6);
    attack.attackRules.perfectClearAttack=9;
    assert(Rules::calculate_attack(attack).attack==10);
    attack.attackRules.perfectClearEnabled=false;
    assert(Rules::calculate_attack(attack).attack==1);
    attack={}; attack.linesCleared=4; attack.combo=2; attack.garbageMultiplier=.5;
    attack.perfectClear=true;
    const auto scaled=Rules::calculate_attack(attack);
    assert(scaled.attack==4 && scaled.perfectClearAttack==2);
    attack.clearedGarbageLines=1;attack.garbageMultiplier=0;
    assert(Rules::calculate_attack(attack).attack==0);
    attack={}; attack.linesCleared=2;
    attack.perfectClear=false; attack.halfClear=true; attack.attackRules.halfClearEnabled=true;
    attack.attackRules.halfClearAttack=7;
    auto half=Rules::calculate_attack(attack);
    assert(half.attack==8 && half.perfectClearAttack==0 && half.halfClearAttack==7);
    attack.attackRules.halfClearEnabled=false;
    assert(Rules::calculate_attack(attack).attack==1);
    attack={}; attack.garbageMode=Runtime::GarbageMode::QuickPlay;
    attack.linesCleared=1; attack.combo=1; attack.b2bCharging=6;
    assert(Rules::calculate_attack(attack).attack==7);
    attack.attackRules.b2bCharging=false;
    assert(Rules::calculate_attack(attack).attack==1);
    attack={}; attack.garbageMode=Runtime::GarbageMode::QuickPlay;
    attack.linesCleared=4; attack.spin=true; attack.perfectClear=true;
    assert(Rules::calculate_attack(attack).attack==13);
    attack.attackRules.legacy=true;
    assert(Rules::calculate_attack(attack).attack==11);
}
void clears_and_display() {
    auto session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context(*session,services);
    auto& player=session->player;
    auto& field=player.gameplay.playfield.field;
    field.initialize(10,20); player.gameplay.playfield.activeHeight=20;
    player.gameplay.playfield.width=10;
    player.gameplay.playfield.visibleHeight=20;
    player.presentation.margin=20;
    auto piece=Piece::get_definition(Piece::Type::from_index(3));
    const Piece::Pose pose{{18,4},Runtime::Rotation{}};
    const auto prepare=[&](bool remnant,bool pieceRemnant) {
        field.clear();
        for(const auto& cell:piece.cells(pose.rotation))
            for(int col=0;col<10;++col) field.set_cell(pose.position.row+cell[0],col,Runtime::BoardCell::garbage());
        for(const auto& cell:piece.cells(pose.rotation))
            field.set_cell(pose.position.row+cell[0],pose.position.col+cell[1],Runtime::BoardCell::empty());
        if(remnant) field.set_cell(14,0,pieceRemnant ? Runtime::BoardCell::piece(0) : Runtime::BoardCell::timed_solid());
    };
    context.config.attackRules.halfClearEnabled=true;
    context.config.attackRules.halfClearAttack=7;
    context.config.attackRules.halfClearCharge=3;
    prepare(true,false);
    auto result=Engine::resolve_locked_piece(context,field,piece,player,pose,{});
    assert(result.clear.clearedLines==2 && result.clear.halfClear && !result.clear.perfectClear);
    assert(result.attack.attack==9 && result.chain.b2b==3);
    prepare(true,true);
    result=Engine::resolve_locked_piece(context,field,piece,player,pose,{});
    assert(!result.clear.halfClear && !result.clear.perfectClear);
    prepare(false,false);
    result=Engine::resolve_locked_piece(context,field,piece,player,pose,{});
    assert(result.clear.perfectClear && !result.clear.halfClear && result.attack.attack==6 && result.chain.b2b==1);
    context.config.attackRules.perfectClearEnabled=false;
    prepare(false,false);
    result=Engine::resolve_locked_piece(context,field,piece,player,pose,{});
    assert(!result.clear.perfectClear && !result.clear.halfClear && result.attack.attack==1);
    Network::RemotePlacementSnapshot placement;
    placement.piece=piece.type(); placement.halfClear=true; placement.clearedLines=2;
    player.pieces.bag.push_back({piece.type(),false});
    auto wire=Network::encode_board_state(context,player,Network::ActivePieceState::locked(),placement);
    auto decoded=Network::decode_board_state(wire,10,20);
    assert(decoded && decoded->placement && decoded->placement->halfClear && !decoded->placement->perfectClear);
    std::ostringstream output;
    auto original=std::cout.rdbuf(output.rdbuf());
    Render::print_all_clear(context,player,false,true);
    Test::TerminalScreen screen; screen.feed(output.str());
    assert(screen.text().find("ＨＡＬＦ")!=std::string::npos);
    output.str(""); Render::print_all_clear(context,player,true); screen.feed(output.str());
    assert(screen.text().find("ＡＬＬ")!=std::string::npos && screen.text().find("ＨＡＬＦ")==std::string::npos);
    output.str(""); Render::print_all_clear(context,player,false); screen.feed(output.str());
    assert(screen.text().find("ＡＬＬ")==std::string::npos && screen.text().find("ＨＡＬＦ")==std::string::npos);
    std::cout.rdbuf(original);
}
}
int main() { settings_and_replays(); chains_and_damage(); clears_and_display(); }
