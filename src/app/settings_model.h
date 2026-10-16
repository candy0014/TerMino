#ifndef TETR_APP_SETTINGS_MODEL_H
#define TETR_APP_SETTINGS_MODEL_H

#include "tetr/config/settings_schema.h"

#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace Runtime { struct MapDefinition; }
namespace App::Settings::Model {
using SettingId = UserConfig::SettingId;
using SettingValue = UserConfig::SettingReference;

struct Choice {
    std::vector<std::string> labels;
    std::vector<int> values;
    bool cycleImmediately = false;
};

// These projections expose old signed/sentinel values as independent controls.
// They write the original field, so config files and replay formats stay stable.
enum class ValuePart {
    Raw, GameMode, CheeseStyle, CheeseRefill, HoleLayout,
    HoleMagnitude, GravityMode, GravitySeconds, SimulationMode, TurnMode, HoldDisabled
};
struct SettingItem {
    SettingId id;
    std::string label;
    SettingValue value;
    Choice choice;
    bool keyBinding = false;
    bool squarePreview = false;
    UserConfig::Settings* owner = nullptr;
    ValuePart part = ValuePart::Raw;
    std::string hint;
    bool effective = true;
    UserConfig::Settings* secondaryOwner = nullptr;
    bool secondaryEffective = true;
    bool bothReadOnly = false;
    bool allowEmptyText = false;
    bool readOnly = false;
};

enum class NodeKind { Group, Setting, Player, ResetSection, ResetSubtree, ResetGame, Connect, Disconnect, RoomAction, Separator,
    PresetApply, PresetUndo, PresetClear, PresetCommit, PresetRemove, CopyOtherPlayer,
    MapNew, MapUse, MapEdit, MapDelete, MapDisable, PresetNew, PresetRename, PresetSave, PresetEdit, PresetDelete,
    ChallengeChange, ChallengeReplay, ChallengeRestore, RecentReplay, ChallengeStart };
struct SettingNode {
    std::string key;
    std::string title;
    std::string description;
    NodeKind kind = NodeKind::Group;
    std::optional<SettingItem> item;
    std::vector<SettingNode> children;
    UserConfig::Section section = UserConfig::Section::Global;
    int player = 0;
    bool visible = true;
    std::vector<SettingId> resetFields;
    bool preserveCheeseRefill = false;
    std::string summary;
    bool enabled = true;
    UserConfig::PresetKind presetKind = UserConfig::PresetKind::Classic;
    std::size_t layerIndex = 0;
    std::string descriptionSuffix;
    bool resetPresets = false;
    bool confirmTwice = false;
    int mapId = 0;
    bool literalTitle = false;
    std::shared_ptr<const Runtime::MapDefinition> mapPreview;
    int presetId = 0;
    std::string challengeId,replayPath;
    std::optional<bool> challengeQualified;
    bool literalDescription=false;
};

SettingNode make_tree(UserConfig::Settings& user, bool localVersus, int player, bool includeHidden = false,
                     std::shared_ptr<const Runtime::MapDefinition> mapOverride = {});
SettingNode make_both_tree(UserConfig::Settings& first, UserConfig::Settings& second);
SettingNode make_host_tree(UserConfig::Settings& host,std::shared_ptr<const Runtime::MapDefinition> map);
SettingItem rebind_item(const SettingItem& item, UserConfig::Settings& owner);
bool mixed_value(const SettingItem& item);
bool uses_inline_input(const SettingItem& item);
int choice_value(const SettingItem& item);
void apply_choice(SettingItem& item, int value);
std::size_t selected_choice(const SettingItem& item);
std::string scalar_value(const SettingItem& item);
bool assign_scalar(SettingItem& item, const std::string& text);
void reset_subtree(UserConfig::Settings& user, const SettingNode& reset, bool playerTwo);
std::vector<SettingId> item_fields(const SettingItem& item);
std::vector<SettingId> reset_fields(const SettingNode& node);
void reset_draft(UserConfig::Settings& user,const SettingNode& node,const UserConfig::Settings* source);
} // namespace App::Settings::Model
#endif
