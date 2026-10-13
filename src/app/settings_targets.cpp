#include "settings_targets.h"
#include "settings_presets.h"
#include "settings_maps.h"
#include "tetr/config/normalization.h"
#include "tetr/config/presets.h"

#include <algorithm>

namespace App::Settings::Model {
SettingItem rebind_item(const SettingItem& item,UserConfig::Settings& owner) {
    auto copy=item;
    copy.owner=&owner;
    copy.value=UserConfig::bind_setting(owner,UserConfig::setting_descriptor(item.id));
    copy.secondaryOwner=nullptr;
    return copy;
}
bool mixed_value(const SettingItem& item) {
    if (!item.secondaryOwner) return false;
    const auto other=rebind_item(item,*item.secondaryOwner);
    const bool firstDefined=UserConfig::Presets::defined(*item.owner,item_fields(item).front());
    const bool secondDefined=UserConfig::Presets::defined(*item.secondaryOwner,item_fields(other).front());
    if(firstDefined!=secondDefined) return true;
    if(!firstDefined) return false;
    return item.choice.labels.empty() ? scalar_value(item)!=scalar_value(other) : choice_value(item)!=choice_value(other);
}
namespace {
bool joint_setting(const SettingItem& item) {
    using Id=SettingId;
    const auto section=UserConfig::setting_descriptor(item.id).section;
    return !item.keyBinding && section!=UserConfig::Section::Network && section!=UserConfig::Section::Others &&
        item.id!=Id::GameMode && item.id!=Id::ReplayPath && item.id!=Id::ReplayTimeMultiplier;
}
void merge(SettingNode& first,const SettingNode& second,UserConfig::Settings& other,bool firstVisible,bool secondVisible) {
    firstVisible=firstVisible && first.visible;
    secondVisible=secondVisible && second.visible;
    first.visible=firstVisible || secondVisible;
    if (first.item && second.item) {
        auto& item=*first.item;
        if (joint_setting(item)) {
            if(item.id==SettingId::RotationSystem && std::find(second.item->choice.values.begin(),second.item->choice.values.end(),-1)!=second.item->choice.values.end() &&
                std::find(item.choice.values.begin(),item.choice.values.end(),-1)==item.choice.values.end()) {
                item.choice.labels.push_back("Custom");item.choice.values.push_back(-1);
            }
            item.secondaryOwner=&other;
            item.secondaryEffective=second.item->effective && secondVisible;
            item.effective=item.effective && firstVisible;
        } else if (UserConfig::setting_descriptor(item.id).section!=UserConfig::Section::Others) {
            item.bothReadOnly=true;
        }
    }
    for (auto& child:first.children) {
        const auto it=std::find_if(second.children.begin(),second.children.end(),[&](const auto& node){return node.key==child.key;});
        if (it!=second.children.end()) merge(child,*it,other,firstVisible,secondVisible);
    }
    first.children.erase(std::remove_if(first.children.begin(),first.children.end(),[](const auto& node){return !node.visible;}),first.children.end());
}
}
SettingNode make_both_tree(UserConfig::Settings& first,UserConfig::Settings& second) {
    auto tree=make_tree(first,true,2,true);
    const auto other=make_tree(second,true,2,true);
    merge(tree,other,second,true,true);
    for (auto& node:tree.children) if(node.key=="presets") node=make_preset_menu(first,true,&second);
    for(auto& node:tree.children) if(node.key=="maps") {
        const auto secondTree=make_tree(second,true,2);
        const auto secondMenu=std::find_if(secondTree.children.begin(),secondTree.children.end(),[](const auto& child){return child.key=="maps";});
        if(secondMenu!=secondTree.children.end() && node.summary!=secondMenu->summary) node.summary+=" | "+secondMenu->summary;
    }
    return tree;
}
}

namespace App::Settings {
namespace {
using Node=Model::SettingNode;
using Id=Model::SettingId;
using Part=Model::ValuePart;
std::string setting_key(Id id,Part part=Part::Raw) {
    auto key="setting-"+std::to_string(static_cast<int>(id));
    if (part!=Part::Raw) key+="-"+std::to_string(static_cast<int>(part));
    return key;
}
const Node* child(const Node& node,const std::string& key) {
    const auto it=std::find_if(node.children.begin(),node.children.end(),[&](const auto& entry){return entry.key==key;});
    return it==node.children.end() ? nullptr : &*it;
}
std::string hiding_control(const Node& node,const UserConfig::Settings& user) {
    if (!node.item) {
        if (node.key=="holes") return setting_key(Id::CheesePattern,Part::CheeseStyle);
        if (node.key=="QUICK PLAY" || node.key=="mode-garbage") return setting_key(Id::GameMode,Part::GameMode);
        return {};
    }
    const auto& item=*node.item;
    if (item.part==Part::GravitySeconds) return setting_key(Id::GravityInterval,Part::GravityMode);
    if (item.part==Part::CheeseRefill) return setting_key(Id::GarbageMode);
    if (item.id==Id::SimulationPps) return setting_key(Id::SimulationPps,Part::SimulationMode);
    if (item.id==Id::TurnPieces) return setting_key(Id::TurnPieces,Part::TurnMode);
    switch (item.id) {
        case Id::B2BRewardAmount: return setting_key(Id::B2BReward);
        case Id::B2BChargeThreshold:
        case Id::B2BChargeReduction: return setting_key(Id::B2BCharging);
        case Id::PerfectClearAttack:
        case Id::PerfectClearCharge: return setting_key(Id::PerfectClearEnabled);
        case Id::HalfClearAttack:
        case Id::HalfClearCharge: return setting_key(Id::HalfClearEnabled);
        case Id::InfiniteHold: return setting_key(Id::HoldEnabled);
        case Id::HoldCount: return setting_key(user.holdEnabled ? Id::InfiniteHold : Id::HoldEnabled);
        case Id::LockResetLimit: return setting_key(UserConfig::clamped_real(user,Id::LockDelay)>0 ? Id::InfiniteMovement : Id::LockDelay);
        case Id::GravityLevelLines: case Id::GravityAcceleration: return setting_key(Id::GravityInterval,Part::GravityMode);
        case Id::AutoplayHandlingSimulation:
        case Id::AutoplayPps:
        case Id::AutoplayWidth: return setting_key(Id::AutoplayMode);
        case Id::CheeseMessiness:
        case Id::CheeseDensity:
        case Id::HoleCount: return setting_key(Id::CheesePattern,Part::CheeseStyle);
        case Id::LayerHeight:
        case Id::GarbageInterval:
        case Id::GarbageInTime:
        case Id::GarbageEntryMode:
        case Id::SurvivalApm: return setting_key(Id::GarbageMode);
        case Id::BlitzTime:
        case Id::RacingDistance:
        case Id::MapGoal:
        case Id::GameMode: return setting_key(Id::GameMode,Part::GameMode);
        default: return {};
    }
}
bool find_row(const Node& menu,const std::string& key,MenuLocation& location) {
    for (std::size_t i=0;i<menu.children.size();++i)
        if (menu.children[i].key==key) { location.selected=i; return true; }
    for (const auto& node:menu.children) if (node.kind==Model::NodeKind::Group) {
        location.path.push_back(node.key);
        if (find_row(node,key,location)) return true;
        location.path.pop_back();
    }
    return false;
}
}
MenuLocation locate_target_selection(const Node& visible,const Node& unpruned,const UserConfig::Settings& user,
    const std::vector<std::string>& path,const std::string& selectedKey) {
    MenuLocation location;
    const Node* menu=&visible;
    for (const auto& key:path) {
        const auto* next=child(*menu,key);
        if (!next) break;
        if(next->kind!=Model::NodeKind::Group || !next->enabled || next->children.empty()) {
            for(std::size_t i=0;i<menu->children.size();++i)
                if(menu->children[i].key==key) {location.selected=i;break;}
            return location;
        }
        menu=next; location.path.push_back(key);
    }
    if (location.path==path) {
        for (std::size_t i=0;i<menu->children.size();++i)
            if (menu->children[i].key==selectedKey) { location.selected=i; return location; }
    }
    const Node* full=&unpruned;
    std::string controller;
    for (const auto& key:path) {
        full=child(*full,key);
        if (!full) break;
        if (!full->visible) { controller=hiding_control(*full,user); break; }
    }
    if (full && controller.empty()) if (const auto* row=child(*full,selectedKey)) controller=hiding_control(*row,user);
    if (!controller.empty()) {
        // Prefer a controller in the same submenu; duplicated parameter
        // presentations elsewhere remain a fallback only.
        for (std::size_t i=0;i<menu->children.size();++i)
            if (menu->children[i].key==controller) { location.selected=i; return location; }
        MenuLocation found;
        if (find_row(visible,controller,found)) return found;
    }
    return location;
}
}
