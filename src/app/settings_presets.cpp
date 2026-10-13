#include "settings_presets.h"
#include "ui_text.h"
#include "tetr/config/presets.h"

#include <algorithm>

namespace App::Settings {
namespace {
using Node=Model::SettingNode;
using Kind=Model::NodeKind;
using UiText::tr;
std::string layer_label(const UserConfig::PresetLayer* layer) {
    if (!layer) return tr("None");
    return tr(UserConfig::Presets::name(*layer))+
        (UserConfig::Presets::modified(*layer) ? tr(" (adjusted)") : "");
}
std::string paired(const std::string& first,const std::string& second) {
    return first==second ? first : first+" | "+second;
}
const UserConfig::PresetLayer* top(const UserConfig::Settings& user) {
    return user.presets.layers.empty() ? nullptr : &user.presets.layers.back();
}
std::string summary(const UserConfig::Settings& user) {
    return user.presets.layers.empty() ? tr("None") :
        std::to_string(user.presets.layers.size())+" "+tr("layers")+" / "+layer_label(top(user));
}
}
Node make_preset_menu(UserConfig::Settings& first,bool localVersus,UserConfig::Settings* second) {
    UserConfig::Presets::record_edits(first);
    if (second) UserConfig::Presets::record_edits(*second);
    Node menu{"presets","Presets","Apply adds one layer. Newer layers override shared parameters; ordinary edits belong to their latest source layer."};
    menu.descriptionSuffix="Using, editing, deleting presets and changing layers require confirmation.";
    menu.summary=second ? paired(summary(first),summary(*second)) : summary(first);
    const bool active=!first.presets.layers.empty() || (second && !second->presets.layers.empty());
    Node undo{"preset-undo","Undo latest layer","Restore the previous layer, including its earlier adjustments. Uncovered settings stay personal.",Kind::PresetUndo};
    undo.summary=second ? paired(layer_label(top(first)),layer_label(top(*second))) : layer_label(top(first));
    undo.enabled=active;
    Node clear{"preset-clear","Undo all presets","Restore every preset-owned parameter to its original value; uncovered edits remain.",Kind::PresetClear};
    clear.enabled=active;
    Node commit{"preset-commit","Keep current values and end presets","Keep the current configuration and discard preset recovery. Confirm twice.",Kind::PresetCommit};
    commit.enabled=active;
    Node layers{"preset-layers","Applied presets","Layers are listed oldest to newest. Open one to remove that layer while keeping the others."};
    const auto count=std::max(first.presets.layers.size(),second ? second->presets.layers.size() : std::size_t{});
    layers.enabled=count>0;
    for (std::size_t i=0;i<count;++i) {
        const auto* one=i<first.presets.layers.size() ? &first.presets.layers[i] : nullptr;
        const auto* two=second && i<second->presets.layers.size() ? &second->presets.layers[i] : nullptr;
        Node layer{"preset-layer-"+std::to_string(i),"", "Remove only this layer; remaining layers are recomputed in their original order."};
        layer.title=std::to_string(i+1)+". "+(second ? paired(layer_label(one),layer_label(two)) : layer_label(one));
        Node remove{"preset-remove","Remove this layer",layer.description,Kind::PresetRemove};
        remove.layerIndex=i;
        layer.children.push_back(std::move(remove));layers.children.push_back(std::move(layer));
    }
    menu.children={std::move(undo),std::move(clear),std::move(commit),std::move(layers)};
    UserConfig::Presets::Collection collection;
    const bool readable=UserConfig::Presets::load(collection);
    if(!first.presetDefined) {
        Node create{"preset-new","New preset","Create a preset with every parameter undefined.",Kind::PresetNew};
        create.enabled=readable;menu.children.insert(menu.children.begin(),std::move(create));
    }
    for (const auto& saved:collection.presets) {
        const bool builtin=saved.kind!=UserConfig::PresetKind::Custom;
        Node record{"preset-"+std::to_string(saved.id),"#"+std::to_string(saved.id)+": "+(builtin ? tr(saved.name) : saved.name),
            "Select, edit or delete this preset."};
        record.presetId=saved.id;record.literalTitle=true;
        Node use{"preset-use","Use preset","Apply the defined parameters; undefined parameters keep their current values.",Kind::PresetApply};
        use.presetId=saved.id;use.presetKind=saved.kind;
        use.descriptionSuffix=tr(UserConfig::Presets::is_local_versus(saved) ?
            "This preset uses Local Versus: apply its Player 1 and Player 2 parameters to both players." :
            "This preset applies to the current edit target.");
        if(localVersus) use.descriptionSuffix+=' '+tr("Local Versus keeps the match mode and applies only rules and handling; the 150-line goal is single-player only.");
        Node edit{"preset-edit","Edit preset","Edit the saved parameters; S saves changes to this preset.",Kind::PresetEdit};
        edit.presetId=saved.id;edit.confirmTwice=true;
        Node remove{"preset-delete","Delete preset","Delete only this preset from the collection; already applied layers remain available to undo.",Kind::PresetDelete};
        remove.presetId=saved.id;remove.confirmTwice=true;
        record.children={std::move(use),std::move(edit),std::move(remove)};
        menu.children.push_back(std::move(record));
    }
    if(!readable) menu.description="Could not read the preset collection. Existing presets will not be overwritten.";
    return menu;
}
bool preset_action(Kind kind) {
    return kind==Kind::PresetApply || kind==Kind::PresetUndo || kind==Kind::PresetClear ||
        kind==Kind::PresetCommit || kind==Kind::PresetRemove;
}
bool apply_preset_action(UserConfig::Settings& user,const Node& node,bool keepMatchMode,bool playerTwo) {
    using namespace UserConfig::Presets;
    switch (node.kind) {
        case Kind::PresetApply: {
            Collection collection;if(!load(collection)) return false;
            const auto found=std::find_if(collection.presets.begin(),collection.presets.end(),[&](const auto& preset){return preset.id==node.presetId;});
            return found!=collection.presets.end() && apply(user,*found,keepMatchMode,playerTwo);
        }
        case Kind::PresetUndo:if(!user.presets.layers.empty()) undo(user,keepMatchMode);return true;
        case Kind::PresetClear:if(!user.presets.layers.empty()) clear(user,keepMatchMode);return true;
        case Kind::PresetCommit:commit(user);return true;
        case Kind::PresetRemove:
            if(node.layerIndex<user.presets.layers.size()) remove(user,user.presets.layers[node.layerIndex].serial,keepMatchMode);
            return true;
        default:return false;
    }
}
}
