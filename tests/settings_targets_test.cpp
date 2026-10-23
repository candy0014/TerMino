#include "../src/app/settings_targets.h"
#include "tetr/config/presets.h"

#include <cassert>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace TetrTests::settings_targets_test {
namespace {
namespace Model=App::Settings::Model;
using Node=Model::SettingNode;
using Id=Model::SettingId;
using Part=Model::ValuePart;
using Path=std::vector<std::string>;

const Node& child(const Node& node,const std::string& key) {
    for (const auto& entry:node.children) if (entry.key==key) return entry;
    throw std::runtime_error("missing child: "+key);
}
const Node& menu(const Node& root,const Path& path) {
    const auto* node=&root;
    for (const auto& key:path) node=&child(*node,key);
    return *node;
}
const Node& field(const Node& node,Id id,Part part=Part::Raw) {
    for (const auto& entry:node.children)
        if (entry.item && entry.item->id==id && entry.item->part==part) return entry;
    throw std::runtime_error("missing field: "+std::to_string(static_cast<int>(id)));
}
void hidden_row(const Path& path,Id selected,Part selectedPart,Id control,Part controlPart,
    const std::function<void(UserConfig::Settings&,UserConfig::Settings&)>& setup,
    Path expectedPath={}) {
    UserConfig::Settings one,two;
    one.gameMode=two.gameMode=6;
    setup(one,two);
    const auto both=Model::make_both_tree(one,two);
    const auto oldKey=field(menu(both,path),selected,selectedPart).key;
    const auto visible=Model::make_tree(one,true,0);
    const auto full=Model::make_tree(one,true,0,true);
    const auto location=App::Settings::locate_target_selection(visible,full,one,path,oldKey);
    if (expectedPath.empty()) expectedPath=path;
    assert(location.path==expectedPath);
    const auto& row=menu(visible,location.path).children.at(location.selected);
    assert(row.item && row.item->id==control && row.item->part==controlPart);
}
}

int run(int argc,char** argv) {
    using namespace Model;
    for(const auto& entry:UserConfig::forced_handling_settings())
        hidden_row({"GAME","forced-handling"},entry.value,Part::Raw,entry.enabled,Part::Raw,
            [&](auto&,auto& two){UserConfig::assign_setting_text(two,entry.enabled,"1");});
    const Path pieces{"GAME","pieces"},timing{"GAME","timing"},gravity{"GAME","gravity"};
    const Path b2b{"GAME","attack-rules","b2b-reward"};
    const Path half{"GAME","attack-rules","half-clear"},perfect{"GAME","attack-rules","perfect-clear"};
    const Path shape{"GAME","garbage-rules","cheese"},holes{"GAME","garbage-rules","cheese","holes"};
    hidden_row(pieces,Id::HoldCount,Part::Raw,Id::HoldEnabled,Part::Raw,
        [](auto& one,auto& two){one.holdEnabled=0;two.holdEnabled=1;});
    hidden_row(pieces,Id::HoldCount,Part::Raw,Id::InfiniteHold,Part::Raw,
        [](auto& one,auto& two){one.infiniteHold=1;two.infiniteHold=0;});
    hidden_row(pieces,Id::InfiniteHold,Part::Raw,Id::HoldEnabled,Part::Raw,
        [](auto& one,auto&){one.holdEnabled=0;});
    hidden_row(timing,Id::LockResetLimit,Part::Raw,Id::LockDelay,Part::Raw,
        [](auto& one,auto&){one.lockDelay=0;});
    hidden_row(timing,Id::LockResetLimit,Part::Raw,Id::InfiniteMovement,Part::Raw,
        [](auto& one,auto&){one.infiniteMovement=1;});
    hidden_row(timing,Id::SimulationPps,Part::Raw,Id::SimulationPps,Part::SimulationMode,
        [](auto& one,auto& two){one.simulationPps=0;two.simulationPps=3;});
    hidden_row(timing,Id::TurnPieces,Part::Raw,Id::TurnPieces,Part::TurnMode,
        [](auto& one,auto& two){one.turnPieces=0;two.turnPieces=2;});
    hidden_row(gravity,Id::GravityInterval,Part::GravitySeconds,Id::GravityInterval,Part::GravityMode,
        [](auto& one,auto&){one.instantGravity=1;});
    hidden_row(b2b,Id::B2BRewardAmount,Part::Raw,Id::B2BReward,Part::Raw,
        [](auto& one,auto&){one.b2bReward=1;});
    for (auto id:{Id::B2BChargeThreshold,Id::B2BChargeReduction})
        hidden_row(b2b,id,Part::Raw,Id::B2BCharging,Part::Raw,[](auto& one,auto&){one.b2bCharging=0;});
    for (auto id:{Id::HalfClearAttack,Id::HalfClearCharge})
        hidden_row(half,id,Part::Raw,Id::HalfClearEnabled,Part::Raw,
            [](auto& one,auto& two){one.halfClearEnabled=0;two.halfClearEnabled=1;});
    for (auto id:{Id::PerfectClearAttack,Id::PerfectClearCharge})
        hidden_row(perfect,id,Part::Raw,Id::PerfectClearEnabled,Part::Raw,[](auto& one,auto&){one.perfectClearEnabled=0;});
    for (auto id:{Id::AutoplayPps,Id::AutoplayWidth})
        hidden_row({"AUTOPLAY"},id,Part::Raw,Id::AutoplayMode,Part::Raw,
            [](auto& one,auto& two){one.autoplayMode=0;two.autoplayMode=1;});
    {
        UserConfig::Settings target;target.gameMode=6;
        const auto visible=make_tree(target,true,1),full=make_tree(target,true,1,true);
        const auto location=App::Settings::locate_target_selection(visible,full,target,
            {"presets","preset-layers","preset-layer-0"},"preset-remove");
        assert((location.path==Path{"presets"}));
        const auto& row=menu(visible,location.path).children.at(location.selected);
        assert(row.key=="preset-layers" && !row.enabled && row.children.empty());
    }
    // A hidden ancestor must relocate to the cause in its parent menu.
    hidden_row(holes,Id::HoleCount,Part::HoleMagnitude,Id::CheesePattern,Part::CheeseStyle,
        [](auto& one,auto& two){one.cheesePattern=2;two.cheesePattern=1;},shape);
    hidden_row(shape,Id::CheeseDensity,Part::Raw,Id::CheesePattern,Part::CheeseStyle,
        [](auto& one,auto& two){one.cheesePattern=1;two.cheesePattern=2;});
    hidden_row(shape,Id::CheeseMessiness,Part::Raw,Id::CheesePattern,Part::CheeseStyle,
        [](auto& one,auto& two){one.cheesePattern=2;two.cheesePattern=1;});

    UserConfig::Settings one,two;
    one.gameMode=two.gameMode=6;
    one.fieldWidth=10;two.fieldWidth=5;
    one.holeCount=-3;two.holeCount=4;
    auto both=make_both_tree(one,two);
    auto magnitude=*field(menu(both,holes),Id::HoleCount,Part::HoleMagnitude).item;
    assert(mixed_value(magnitude));
    // One player's validation failure leaves both profiles unchanged.
    assert(!assign_scalar(magnitude,"6"));
    assert(one.holeCount==-3 && two.holeCount==4);
    assert(assign_scalar(magnitude,"4"));
    assert(one.holeCount==-4 && two.holeCount==4 && !mixed_value(magnitude));
    auto layout=*field(menu(both,holes),Id::HoleCount,Part::HoleLayout).item;
    apply_choice(layout,1);
    assert(one.holeCount==-4 && two.holeCount==-4);
    one.cheesePattern=-1;two.cheesePattern=1;
    auto style=*field(menu(both,shape),Id::CheesePattern,Part::CheeseStyle).item;
    assert(!mixed_value(style)); // Hidden storage signs do not imply different visible choices.
    apply_choice(style,2);
    assert(one.cheesePattern==-2 && two.cheesePattern==2);
    one.simulationPps=0;two.simulationPps=6;
    auto clock=*field(menu(both,timing),Id::SimulationPps,Part::SimulationMode).item;
    apply_choice(clock,1);
    assert(one.simulationPps==2.5 && two.simulationPps==6);
    assert(one.fieldWidth==10 && two.fieldWidth==5);

    both=make_both_tree(one,two);
    auto mode=*field(menu(both,{"MODEL"}),Id::GameMode,Part::GameMode).item;
    assert(mode.bothReadOnly && !mode.secondaryOwner);
    apply_choice(mode,0);
    assert(one.gameMode==6 && two.gameMode==6);
    const auto& controls=menu(both,{"CONTROLS"});
    for (const auto& node:controls.children) if (node.item)
        assert(node.item->bothReadOnly && !node.item->secondaryOwner);
    auto width=*field(menu(both,{"GAME","playfield"}),Id::FieldWidth).item;
    assert(mixed_value(width) && !width.bothReadOnly && width.secondaryOwner==&two);
    const auto visible=make_tree(one,true,0),full=make_tree(one,true,0,true);
    const auto key=field(menu(both,timing),Id::LineClearDelay).key;
    const auto location=App::Settings::locate_target_selection(visible,full,one,timing,key);
    assert(location.path==timing && menu(visible,location.path).children.at(location.selected).key==key);

    // Joint reset includes hidden descendants and preserves unrelated values.
    one.b2bCharging=two.b2bCharging=0;
    one.b2bChargeThreshold=9;two.b2bChargeThreshold=12;
    one.b2bChargeReduction=3;two.b2bChargeReduction=8;
    both=make_both_tree(one,two);
    const auto& reset=child(menu(both,b2b),"reset");
    reset_subtree(one,reset,false);reset_subtree(two,reset,true);
    assert(one.b2bCharging==1 && two.b2bCharging==1);
    assert(one.b2bChargeThreshold==4 && two.b2bChargeThreshold==4);
    assert(one.b2bChargeReduction==0 && two.b2bChargeReduction==0);
    assert(one.fieldWidth==10 && two.fieldWidth==5 && one.cheesePattern==-2 && two.cheesePattern==2);
    // Undefined is an actual value state, including when concrete backing values differ.
    one.presetDefined.emplace();two.presetDefined.emplace();
    both=make_both_tree(one,two);
    width=*field(menu(both,{"GAME","playfield"}),Id::FieldWidth).item;
    assert(!mixed_value(width));
    UserConfig::Presets::define(two,Id::FieldWidth);
    assert(mixed_value(width));
    assert(assign_scalar(width,"12"));
    assert(one.fieldWidth==12 && two.fieldWidth==12 && !mixed_value(width));
    assert(UserConfig::Presets::defined(one,Id::FieldWidth) && UserConfig::Presets::defined(two,Id::FieldWidth));
    assert(assign_scalar(width,"?"));
    assert(!UserConfig::Presets::defined(one,Id::FieldWidth) && !UserConfig::Presets::defined(two,Id::FieldWidth));
    std::cerr<<"settings_targets_test: OK\n";
    return 0;
}
} // namespace TetrTests::settings_targets_test
