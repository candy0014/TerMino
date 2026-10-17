#include "single_player_challenges.h"
#include "tetr/app/settings_model.h"
#include "settings_presets.h"
#include "settings_maps.h"
#include "tetr/config/map_store.h"
#include "tetr/config/presets.h"
#include "tetr/runtime/piece.h"
#include "tetr/replay/recent.h"

#include "tetr/config/normalization.h"
#include "tetr/util/text_codec.h"
#include "tetr/util/numeric.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>
#include <utility>

namespace App::Settings::Model {
namespace {
using Id = SettingId;
using Section = UserConfig::Section;

int magnitude(int value, int limit) {
    const auto wide = static_cast<std::int64_t>(value);
    return static_cast<int>(std::clamp(wide < 0 ? -wide : wide, std::int64_t{1}, static_cast<std::int64_t>(limit)));
}
int cheese_style(const UserConfig::Settings& user) { return magnitude(user.cheesePattern, 3); }
int holes(const UserConfig::Settings& user) { return magnitude(user.holeCount, std::clamp(user.fieldWidth, 1, 100)); }
int gravity_mode(const UserConfig::Settings& user) {
    if(user.gravityProgression==3) return 5;
    if (user.instantGravity != 0) return 2;
    if (user.gravityInterval < 0 || user.gravityInterval > 1e9) return 1;
    if (user.gravityInterval <= 1e-7) return 2;
    return user.gravityProgression==1 ? 3 : user.gravityProgression==2 ? 4 : user.gravityProgression==3 ? 5 : 0;
}
double gravity_seconds(const UserConfig::Settings& user) {
    const double value = std::abs(user.gravityInterval);
    return std::isfinite(value) && value > 1e-7 && value <= 1e9 ? value : 0.6;
}

bool setting_effective(Id id, const UserConfig::Settings& user, const Runtime::RuntimeConfig& config) {
    for(const auto& entry:UserConfig::forced_handling_settings())
        if(id==entry.personal && UserConfig::clamped_int(user,entry.enabled)) return false;
    if(config.initialMap && (id==Id::FieldWidth || id==Id::FieldHeight || id==Id::LayerHeight || id==Id::QpFool || id==Id::QpSnowball ||
        (id==Id::RotationSystem && config.initialMap->generator==Runtime::MapGenerator::RotationSystem))) return false;
    const auto garbage=config.garbage.mode;
    const bool quickplay=garbage==Runtime::GarbageMode::QuickPlay;
    const bool cheese=Runtime::is_cheese_mode(garbage);
    const bool snowball=Runtime::enabled(config.quickPlay.snowball);
    const bool reverseNoHold=Runtime::reverse(config.quickPlay.noHold);
    const bool specialShape=reverseNoHold || Runtime::enabled(config.quickPlay.doubleHole);
    const bool horizontal=!config.movement.disableLeft || !config.movement.disableRight;
    switch (id) {
        case Id::Goal: return config.mode.mode==Runtime::GameMode::Zen;
        case Id::GarbageMode:
            return !quickplay && config.mode.mode!=Runtime::GameMode::Multiplayer &&
                config.mode.mode!=Runtime::GameMode::LocalVersus;
        case Id::CheesePattern: return garbage!=Runtime::GarbageMode::None && !specialShape;
        case Id::CheeseMessiness: return cheese && std::abs(config.garbage.cheeseModel)==1;
        case Id::CheeseDensity: return garbage!=Runtime::GarbageMode::None && !specialShape && std::abs(config.garbage.cheeseModel)==2;
        case Id::HoleCount:
            return garbage!=Runtime::GarbageMode::None && !specialShape && std::abs(config.garbage.cheeseModel)==1;
        case Id::LayerHeight:
            return garbage==Runtime::GarbageMode::CheeseLayer && !Runtime::enabled(config.quickPlay.doubleHole);
        case Id::GarbageInterval: return garbage==Runtime::GarbageMode::CheeseTimer;
        case Id::HitMultiplier: return Runtime::uses_garbage_buffer(garbage) || garbage==Runtime::GarbageMode::Unclear;
        case Id::CancellationMultiplier: return Runtime::uses_garbage_buffer(garbage);
        case Id::GarbageInTime: return Runtime::uses_garbage_buffer(garbage) && !quickplay;
        case Id::GarbageEntryMode: return Runtime::uses_garbage_buffer(garbage);
        case Id::MessinessWithinAttack:
        case Id::AvoidSameColumn:
        case Id::GarbageCap: return Runtime::uses_garbage_buffer(garbage) && !quickplay;
        case Id::OpenerPhase: return garbage == Runtime::GarbageMode::Multiplayer;
        case Id::DisableUndo:
        case Id::DisablePause:
            return config.mode.mode!=Runtime::GameMode::Multiplayer &&
                config.mode.mode!=Runtime::GameMode::LocalVersus && garbage!=Runtime::GarbageMode::Multiplayer;
        case Id::SurvivalApm: return garbage==Runtime::GarbageMode::Survival;
        case Id::QpDefaultFloor: return quickplay || config.rotationSystem==Runtime::RotationSystem::AprilFools2026 ||
            config.quickPlay.fool==Runtime::FoolMode::AprilFools2026 || config.quickPlay.fool==Runtime::FoolMode::ReverseAprilFools2026;
        case Id::QpExpert:
        case Id::QpBackfire:
        case Id::QpSnakeEyes: return quickplay;
        case Id::GravityInterval:
        case Id::GravityLevelLines:
        case Id::GravityAcceleration:
            return !quickplay &&
                !Runtime::enabled(config.quickPlay.gravity);
        case Id::SoftDropFactor:
            return !config.movement.disableSoftDrop && !Runtime::autoplay_enabled(config.autoplay.mode);
        case Id::Das:
        case Id::Arr:
        case Id::Dcd: return horizontal && !Runtime::reverse(config.quickPlay.snowball);
        case Id::PreMove:
        case Id::DasPrecharge: return horizontal && !Runtime::autoplay_enabled(config.autoplay.mode);
        case Id::FieldWidth:
        case Id::FieldHeight: return !quickplay && !snowball;
        case Id::AntiGravity: return !quickplay;
        case Id::FieldInvisible:
        case Id::InvisibilityDelay:
        case Id::InvisibilityInterval:
        case Id::InvisibilityDuration: return !quickplay && !Runtime::enabled(config.quickPlay.invisible);
        case Id::NextCount: return !quickplay && !reverseNoHold;
        case Id::HoldEnabled: return !Runtime::enabled(config.quickPlay.noHold);
        case Id::InfiniteHold: return config.field.holdEnabled;
        case Id::HoldCount: return config.field.holdEnabled && !config.field.infiniteHold;
        case Id::Ghost: return !quickplay && !reverseNoHold;
        case Id::RotationSystem: return config.initialMap ? config.initialMap->generator==Runtime::MapGenerator::CustomSequence :
            !quickplay && config.quickPlay.fool==Runtime::FoolMode::Disabled;
        case Id::SpinBonuses: return !Runtime::enabled(config.quickPlay.allSpin);
        case Id::RandomPieces: return !reverseNoHold;
        case Id::LockDelay: return !quickplay && !Runtime::reverse(config.quickPlay.gravity);
        case Id::LockResetLimit: return !quickplay && !config.handling.infiniteMovement && config.handling.epld>Runtime::Duration{};
        case Id::InfiniteMovement: return config.handling.epld>Runtime::Duration{};
        case Id::LineClearDelay: return !quickplay && !Runtime::reverse(config.quickPlay.messy);
        case Id::AutoplayHandlingSimulation: return config.autoplay.mode==Runtime::AutoplayMode::ChatGPT;
        case Id::AutoplayPps: return Runtime::autoplay_enabled(config.autoplay.mode);
        case Id::AutoplayWidth:
            return config.autoplay.mode==Runtime::AutoplayMode::While1 ||
                static_cast<int>(config.autoplay.mode)>=static_cast<int>(Runtime::AutoplayMode::ZzztojBotris);
        default: return true;
    }
}

bool when(const UserConfig::Settings& user,Id controller,bool condition) {
    return !UserConfig::Presets::defined(user,controller) || condition;
}
// Hide only controls belonging to another setting's selected branch. Runtime
// overrides affect the hint, not whether the base setting can be configured.
bool setting_visible(Id id, const UserConfig::Settings& user, const Runtime::RuntimeConfig& config) {
    switch (id) {
        case Id::B2BRewardAmount: return when(user,Id::B2BReward,config.attackRules.b2bReward==Runtime::B2BReward::FixedAttack);
        case Id::B2BChargeThreshold:
        case Id::B2BChargeReduction: return when(user,Id::B2BCharging,user.b2bCharging!=0);
        case Id::PerfectClearAttack:
        case Id::PerfectClearCharge: return when(user,Id::PerfectClearEnabled,user.perfectClearEnabled!=0);
        case Id::HalfClearAttack:
        case Id::HalfClearCharge: return when(user,Id::HalfClearEnabled,user.halfClearEnabled!=0);
        case Id::CheeseMessiness: return when(user,Id::CheesePattern,cheese_style(user)==1);
        case Id::CheeseDensity: return when(user,Id::CheesePattern,cheese_style(user)==2);
        case Id::LayerHeight: return when(user,Id::GarbageMode,user.garbageMode==3);
        case Id::GarbageInterval: return when(user,Id::GarbageMode,user.garbageMode==4);
        case Id::GarbageInTime:
        case Id::GarbageEntryMode:
            return when(user,Id::GarbageMode,Runtime::uses_garbage_buffer(config.garbage.mode) || user.garbageMode==1 || user.garbageMode==5);
        case Id::SurvivalApm: return when(user,Id::GarbageMode,user.garbageMode==5);
        case Id::InfiniteHold: return when(user,Id::HoldEnabled,user.holdEnabled!=0);
        case Id::HoldCount: return when(user,Id::HoldEnabled,user.holdEnabled!=0) && when(user,Id::InfiniteHold,!user.infiniteHold);
        case Id::LockResetLimit: return when(user,Id::LockDelay,UserConfig::clamped_real(user,Id::LockDelay)>0) && when(user,Id::InfiniteMovement,!user.infiniteMovement);
        case Id::AutoplayPps:
        case Id::AutoplayHandlingSimulation:
        case Id::AutoplayWidth: return when(user,Id::AutoplayMode,user.autoplayMode!=0);
        default: return true;
    }
}

class TreeBuilder {
public:
    explicit TreeBuilder(UserConfig::Settings& settings,std::shared_ptr<const Runtime::MapDefinition> mapOverride={}) : user(settings),
        config(Runtime::normalize_user_config(settings, Runtime::selected_game_mode(settings))) {
        if(mapOverride) Runtime::apply_map_config(config,*mapOverride);
        else if(const auto map=UserConfig::Maps::find(settings.mapId)) Runtime::apply_map_config(config,*map);
    }
    bool when(Id id,bool condition) const {return Model::when(user,id,condition);}
    bool gravity_when(bool condition) const {
        const bool known=UserConfig::Presets::defined(user,Id::GravityInterval) ||
            UserConfig::Presets::defined(user,Id::InstantGravity) || UserConfig::Presets::defined(user,Id::GravityProgression);
        return !known || condition;
    }
    bool effective(Id id) const { return setting_effective(id,user,config); }
    SettingNode field(Id id, bool visible = true, std::string hint = {}) {
        covered.insert(id);
        const auto& descriptor = UserConfig::setting_descriptor(id);
        auto choices = UserConfig::choice_options(descriptor.choice);
        if(id==Id::Goal && !config.initialMap) {
            choices.labels.pop_back();choices.values.pop_back();
        }
        if(id==Id::RotationSystem && config.initialMap && config.initialMap->generator==Runtime::MapGenerator::CustomSequence) {
            choices.labels.push_back("Custom");choices.values.push_back(-1);
        }
        SettingItem item{id, std::string(descriptor.label), UserConfig::bind_setting(user, descriptor),
            {std::move(choices.labels), std::move(choices.values), descriptor.cycleImmediately},
            descriptor.keyBinding, descriptor.squarePreview, &user, ValuePart::Raw, std::move(hint)};
        item.effective = effective(id);
        SettingNode node{"setting-" + std::to_string(static_cast<int>(id)), item.label, item.hint,
            NodeKind::Setting, std::move(item), {}, descriptor.section};
        node.visible = visible && setting_visible(id,user,config);
        return node;
    }
    SettingNode projected(Id id, ValuePart part, std::string label, Choice choice,
                          std::string hint, bool visible = true) {
        auto node = field(id, visible, hint);
        node.key += "-" + std::to_string(static_cast<int>(part));
        node.title = label;
        node.item->label = std::move(label);
        node.item->choice = std::move(choice);
        node.item->part = part;
        if (part==ValuePart::CheeseRefill)
            node.item->effective=config.garbage.mode==Runtime::GarbageMode::CheeseLayer;
        return node;
    }
    SettingNode group(std::string key, std::string title, std::string hint,
                      std::vector<SettingNode> children, bool visible = true) {
        SettingNode node{std::move(key), std::move(title), std::move(hint), NodeKind::Group,
            {}, std::move(children)};
        node.visible = visible;
        return node;
    }
    SettingNode section(Section section, std::vector<SettingNode> children, std::string hint) {
        SettingNode node = group(std::string(UserConfig::section_name(section)),
            std::string(UserConfig::section_name(section)), std::move(hint), std::move(children));
        if (section == Section::Autoplay) node.title = "Bot";
        node.section = section;
        // Future schema fields appear automatically until given a more specific
        // home. Conditional fields are already marked covered even when hidden.
        for (const auto& descriptor : UserConfig::setting_schema()) {
            if (descriptor.section == section && descriptor.id != Id::ReplayTimeMultiplier &&
                !covered.count(descriptor.id)) node.children.push_back(field(descriptor.id));
        }
        node.children.push_back({"reset", "RESET", "Reset only this category; confirm twice.",
            NodeKind::ResetSection, {}, {}, section});
        return node;
    }
    UserConfig::Settings& user;
    Runtime::RuntimeConfig config;
    std::set<Id> covered;
};

Choice options(std::initializer_list<std::string> labels, std::initializer_list<int> values) {
    return {labels, values, false};
}
void prune(SettingNode& node, bool includeHidden = false) {
    if (node.item && !node.item->choice.values.empty() && node.item->choice.values.size() <= 4)
        node.item->choice.cycleImmediately = true;
    for (auto& child : node.children) prune(child,includeHidden);
    if (includeHidden) return;
    node.children.erase(std::remove_if(node.children.begin(), node.children.end(), [](const auto& child) {
        return !child.visible || (child.kind == NodeKind::Group && child.children.empty() && child.key!="preset-layers");
    }), node.children.end());
}
void collect_fields(const SettingNode& node, std::set<Id>& fields) {
    if (node.item) {
        fields.insert(node.item->id);
        if(node.item->id==Id::RotationSystem) fields.insert(Id::MapRotationSystem);
        if (node.item->id == Id::SoftDropFactor) fields.insert(Id::SoftDropFixed);
        if (node.item->id == Id::ForcedSdf) fields.insert(Id::ForcedSdfFixed);
        if (node.item->part == ValuePart::GravityMode) {
            fields.insert(Id::InstantGravity);fields.insert(Id::GravityProgression);
        }
    }
    for (const auto& child : node.children) collect_fields(child, fields);
}
bool contains_part(const SettingNode& node, ValuePart part) {
    if (node.item && node.item->part==part) return true;
    return std::any_of(node.children.begin(),node.children.end(),[&](const auto& child) {
        return contains_part(child,part);
    });
}
void add_subtree_resets(SettingNode& node) {
    if (node.key=="presets" || node.key=="maps" || node.key=="challenges" || node.key=="recent-replays") return;
    for (auto& child : node.children) add_subtree_resets(child);
    if (node.kind != NodeKind::Group || node.key == "root" || node.children.empty()) return;
    if (std::any_of(node.children.begin(), node.children.end(), [](const auto& child) {
        return child.kind == NodeKind::ResetSection;
    })) return;
    std::set<Id> fields;
    collect_fields(node, fields); // Include conditionally hidden fields in this branch.
    if (node.key == "player") {
        fields.clear();
        for (const auto& descriptor : UserConfig::setting_schema())
            if (descriptor.section != Section::Controls && descriptor.section != Section::Others &&
                (descriptor.section != Section::Handling || descriptor.id == Id::GravityInterval))
                fields.insert(descriptor.id);
    }
    SettingNode reset{"reset", "RESET", node.key == "player"
        ? "Reset the current player's game settings and presets; keep appearance, language and bindings. Confirm twice."
        : "Reset only this submenu; confirm twice.", NodeKind::ResetSubtree};
    reset.resetFields.assign(fields.begin(), fields.end());
    reset.resetPresets = node.key == "player";
    // Cheese shape and refill share signed storage but now belong to different
    // menus. Resetting the shape must preserve the refill switch outside it.
    reset.preserveCheeseRefill = contains_part(node,ValuePart::CheeseStyle) &&
        !contains_part(node,ValuePart::CheeseRefill);
    node.children.push_back(std::move(reset));
}
} // namespace

SettingNode make_tree(UserConfig::Settings& user, bool localVersus, int player, bool includeHidden,
                      std::shared_ptr<const Runtime::MapDefinition> mapOverride) {
    UserConfig::migrate_legacy_modes(user);
    TreeBuilder b(user,std::move(mapOverride));
    using Part = ValuePart;
    const auto mode = Runtime::selected_game_mode(user);
    b.covered.insert(Id::GravityProgression);
    b.covered.insert(Id::MapGoal); // Historical profile/replay field; Goal replaces this control.
    const bool standardMode = mode == Runtime::GameMode::Zen;
    SettingNode root{"root", "SETTINGS", "Choose a category; Right opens, Left goes back."};
    auto players = b.group("player", "Player", "Choose the player whose settings you are editing.", {
        {"player1", "Player 1", localVersus ? "Edit Player 1's independent settings." : "Only Player 1 is available outside Local Versus.", NodeKind::Player},
        {"player2", "Player 2", "Edit Player 2's independent settings.", NodeKind::Player},
        {"both", "Both players", "Edit the selected parameters for both players.", NodeKind::Player}
    });
    players.children[1].player = 1;
    players.children[1].visible = localVersus;
    players.children[2].player = 2;
    players.children[2].visible = localVersus;
    if (localVersus) players.title="Edit target";
    players.description = localVersus ? "Choose Player 1, Player 2 or Both players; Tab cycles targets without leaving the current item." : "Player 1 only; Local Versus enables Player 2.";
    root.children.push_back(std::move(players));
    // Both menus bind the same settings; only the mode-specific entry is hidden.
    auto quickplay = b.section(Section::QuickPlay, {
        b.field(Id::QpDefaultFloor, true, "1-10, default 1: Quick Play starts at this floor's minimum altitude; April Fools 2026 rotation uses special pieces up to this floor."),
        b.field(Id::QpExpert), b.field(Id::QpNoHold), b.field(Id::QpGravity),
        b.field(Id::QpMessy), b.field(Id::QpVolatile), b.field(Id::QpDoubleHole),
        b.field(Id::QpBackfire), b.field(Id::QpSnakeEyes), b.field(Id::QpInvisible),
        b.field(Id::QpAllSpin), b.field(Id::QpSnowball), b.field(Id::QpFool)
    }, "Quick Play modifiers; Right / Enter cycles each value.");
    auto modeQuickplay = quickplay;
    modeQuickplay.visible = b.when(Id::GameMode,mode == Runtime::GameMode::QuickPlay);
    auto garbageShape = b.group("cheese", "Garbage Shape", "Garbage shape and generation behavior.", {
            b.projected(Id::CheesePattern, Part::CheeseStyle, "Garbage shape",
                options({"Standard holes", "Random density", "Checkerboard"}, {1, 2, 3}), "Applies to ordinary garbage from every source; modifiers with their own shape take priority."),
            b.group("holes", "Hole pattern", "Set hole shape separately from its size.", {
                b.projected(Id::HoleCount, Part::HoleLayout, "Layout",
                    options({"Scattered", "Contiguous"}, {0, 1}), user.holeCount < 0
                        ? "Contiguous: one gap with a configurable run length."
                        : "Scattered: independently placed open columns."),
                b.projected(Id::HoleCount, Part::HoleMagnitude, user.holeCount < 0 ? "Run length" : "Hole count", {},
                    user.holeCount < 0 ? "Positive width of the contiguous gap." : "Number of open columns, limited by field width.")
            }, b.when(Id::CheesePattern,cheese_style(user)==1)),
            b.field(Id::CheeseMessiness, true, "Cheese modes: hole-change probability per row, from 0 to 1. Other modes use their attack messiness rules."),
            b.field(Id::CheeseDensity, true, "0-1: occupied-cell probability; 0 makes empty rows, 1 fills all but one hole.")
        });

    auto modeGarbage = b.group("mode-garbage", "GARBAGE", "Garbage generation, reception, sending and rise rules.", {
        b.field(Id::GarbageMode, true, "Choose garbage behavior; relevant controls appear below."),
        garbageShape,
        b.projected(Id::CheesePattern, Part::CheeseRefill, "Refill cleared lines",
            options({"OFF", "ON"}, {0, 1}), "ON replenishes cleared cheese lines; OFF uses a finite layer.", b.when(Id::GarbageMode,user.garbageMode == 3)),
        b.field(Id::LayerHeight, true, "Initial cheese height; 0 creates no initial garbage rows."),
        b.field(Id::GarbageInterval, b.when(Id::GarbageMode,user.garbageMode == 4), "Seconds between cheese rows; 0 adds a row each update, or each piece with the Per piece clock."),
        b.field(Id::HitMultiplier, true, "Multiply received attacks; fractional lines carry over to the next packet. 0 discards received attacks."),
        b.field(Id::GarbageInTime, true, "Time before buffered garbage becomes an attack; 0 attacks immediately. Up to 0.3s uses dark red then bright red; longer uses yellow, dark red, then bright red."),
        b.field(Id::GarbageEntryMode, true, "After non-clear waits for a non-clearing lock; After lock cancels first and raises the remaining ready garbage after any lock; Immediate raises ready garbage without waiting for a lock."),
        b.field(Id::GarbageRiseInterval, true,
            "Seconds between rising garbage rows. 0 raises the batch instantly. Only the first row waits for the entry rule; later rows continue during movement. Quick Play uses 5/60 seconds; EX/rEX rises instantly."),
        b.field(Id::SurvivalApm, b.when(Id::GarbageMode,user.garbageMode == 5), "Survival pressure in attacks per minute; 0 generates no survival garbage."),
        b.field(Id::CancellationMultiplier, true, "Incoming lines cancelled per attack point; unused attack keeps its original strength. Fractions carry over. 0 disables cancellation."),
        b.field(Id::MessinessWithinAttack, true, "0-1: probability of choosing a new hole column within one attack; 0 keeps it aligned, 1 rerolls every row."),
        b.field(Id::AvoidSameColumn, true, "When rerolling a garbage hole, exclude its previous column. Aligned rows can still share a hole."),
        b.field(Id::GarbageCap, true, "Maximum ready garbage rows per rise; 0 prevents rising, but keeps queued garbage.")
    }, b.when(Id::GameMode,standardMode));
    std::vector<SettingNode> sharedGarbageFields{std::move(garbageShape)};
    for (const auto id : {Id::HitMultiplier, Id::CancellationMultiplier, Id::GarbageInTime,
                         Id::GarbageEntryMode, Id::GarbageRiseInterval, Id::MessinessWithinAttack, Id::AvoidSameColumn, Id::GarbageCap}) {
        const auto found=std::find_if(modeGarbage.children.begin(),modeGarbage.children.end(),
            [&](const auto& node) {return node.item && node.item->id==id;});
        auto shared=*found;
        shared.visible=true; // Base rules remain configurable in every game mode.
        sharedGarbageFields.push_back(std::move(shared));
    }
    sharedGarbageFields.push_back(b.field(Id::OpenerPhase, true,
        "Opening piece count; 0 disables protection. Within this count, pending garbage at least equal to previously sent garbage grants extra cancellation, never extra attack."));
    auto garbage = b.group("garbage-rules", "GARBAGE", "Garbage buffering, shape and opening cancellation protection.",
        std::move(sharedGarbageFields));
    SettingNode recent{"recent-replays","Recent replays","The latest 30 games. Enter twice to watch without changing your mode or replay path."};
    recent.visible=mode==Runtime::GameMode::Replay && !user.presetDefined;
    if(recent.visible)for(const auto& entry:Replay::recent_replays()) {
        SettingNode row{entry.path.generic_u8string(),entry.label,"Enter twice to play this replay.",NodeKind::RecentReplay};
        row.literalTitle=true;row.replayPath=entry.path.generic_u8string();row.confirmTwice=true;recent.children.push_back(std::move(row));
    }
    if(recent.children.empty()) {SettingNode empty{"empty","No recent replays",{},NodeKind::Setting};empty.enabled=false;recent.children.push_back(std::move(empty));}
    auto model = b.section(Section::Model, {
        b.projected(Id::GameMode, Part::GameMode, "Model", options(
            {"Zen", "QuickPlay", "Multiplayer", "Local Versus", "Replay"},
            {0, 3, 4, 6, 5}), "Choose a mode; its configuration appears below."),
        b.field(Id::Goal,b.when(Id::GameMode,standardMode),"End the game when the selected goal is reached or you die. Clear initial cells requires a map."),
        b.field(Id::BlitzTime,b.when(Id::GameMode,standardMode) && b.when(Id::Goal,user.goal==1),"Challenge duration: minutes:seconds or seconds; 60 seconds carry into one minute. 0 ends the game immediately."),
        b.field(Id::RacingDistance,b.when(Id::GameMode,standardMode) && b.when(Id::Goal,user.goal==2)),
        b.field(Id::GarbageTarget,b.when(Id::GameMode,standardMode) && b.when(Id::Goal,user.goal==3)),
        b.field(Id::PieceTarget,b.when(Id::GameMode,standardMode) && b.when(Id::Goal,user.goal==4)),
        b.field(Id::AttackTarget,b.when(Id::GameMode,standardMode) && b.when(Id::Goal,user.goal==5)),
        b.field(Id::AllClearTarget,b.when(Id::GameMode,standardMode) && b.when(Id::Goal,user.goal==6)),
        b.field(Id::ReplayPath, b.when(Id::GameMode,mode == Runtime::GameMode::Replay)),
        std::move(recent),
        std::move(modeQuickplay),
        std::move(modeGarbage)
    }, "Game mode and the options used by that mode.");
    // Model now contains another storage section. Its reset covers the whole
    // branch, while the QuickPlay submenu still resets only its own section.
    model.children.pop_back();
    root.children.push_back(std::move(model));
    auto gravity = b.group("gravity", "Gravity", "Choose gravity behavior before setting its interval.", {
            b.projected(Id::GravityInterval, Part::GravityMode, "Gravity mode",
                options({"Periodic", "Disabled", "Instant (20G)","NES progression","Marathon progression","Increasing"}, {0, 1, 2,3,4,5}), "Gravity modes use words; no signed or sentinel numbers."),
            b.projected(Id::GravityInterval, Part::GravitySeconds, gravity_mode(user)>=3 ? "Starting interval (seconds)" : "Interval (seconds)", {}, "Positive seconds between gravity steps; progressive modes use this as their starting interval.", b.gravity_when(gravity_mode(user) == 0 || gravity_mode(user)==3 || gravity_mode(user)==4)),
            b.field(Id::GravityLevelLines,b.gravity_when(gravity_mode(user)==3 || gravity_mode(user)==4),"Cleared lines required for each speed level. Default 10; independent of presets."),
            b.field(Id::GravityAcceleration,b.gravity_when(gravity_mode(user)==3 || gravity_mode(user)==4),"1 follows the reference curve; above 1 accelerates more, below 1 accelerates less. Range 0.01-100.")
        });
    // Mark the projected fields before constructing either category so the
    // storage schema cannot add a second gravity control under HANDLING.
    b.covered.insert(Id::InstantGravity);
    b.covered.insert(Id::SoftDropFixed);
    b.covered.insert(Id::ForcedSdfFixed);
    std::vector<SettingNode> forcedHandling;
    for(const auto& entry:UserConfig::forced_handling_settings()) {
        forcedHandling.push_back(b.field(entry.enabled,true,"Override only this handling parameter; keep personal handling settings."));
        auto value=b.field(entry.value,b.when(entry.enabled,UserConfig::clamped_int(user,entry.enabled)!=0));
        if(entry.personal==Id::SoftDropFactor || entry.personal==Id::Das || entry.personal==Id::Arr || entry.personal==Id::Dcd)
            value.item->hint=value.description="Seconds: s/S; frames at 60 fps: f/F. SDF without a unit is a multiplier; 0 is instant.";
        forcedHandling.push_back(std::move(value));
    }
    root.children.push_back(b.section(Section::Handling, {
        b.field(Id::SoftDropFactor, true, "No unit: gravity multiplier (1 cell/s when gravity is disabled); s/S: fixed seconds per cell; f/F: frames at 60 fps. Uses the faster of soft drop and gravity. 0 means instant drop."),
        b.field(Id::Das, true, "Seconds (no unit or s/S), or decimal frames (f/F, 60 fps): 1.5f = 0.025s. 0 repeats immediately."),
        b.field(Id::Arr, true, "Seconds (no unit or s/S), or decimal frames (f/F, 60 fps): 1.5f = 0.025s. 0 moves to the wall after DAS."),
        b.field(Id::Dcd, true, "Pause charged DAS after rotation or hard drop. Seconds (no unit or s/S), or decimal frames (f/F, 60 fps). 0 disables DAS cut."),
        b.field(Id::PreMove, true, "Shift one cell before spawn collision and instant gravity when DAS is charged. Disabling this does not disable DAS precharge."),
        b.field(Id::DasPrecharge, true, "Charge DAS during countdown, line clear, entry delay and single-player pause. When OFF, DAS starts from spawn; pause freezes existing charge.")
    }, "Soft drop and movement repeat controls."));
    auto game = b.section(Section::Game, {
        b.group("playfield", "Playfield", "Field dimensions and visibility.", {
            b.field(Id::FieldWidth), b.field(Id::FieldHeight), b.field(Id::FieldInvisible),
            b.field(Id::InvisibilityDelay,b.when(Id::FieldInvisible,user.fieldInvisible==1),
                "Time after each piece locks before it becomes invisible; 0 hides it immediately."),
            b.field(Id::InvisibilityInterval,b.when(Id::FieldInvisible,user.fieldInvisible==2),
                "Time between flashes; the first flash begins after this interval. 0 keeps the field visible when flash duration is positive."),
            b.field(Id::InvisibilityDuration,b.when(Id::FieldInvisible,user.fieldInvisible==2),
                "How long each flash stays visible; 0 disables flashes."),
            b.field(Id::AntiGravity)
        }),
        b.group("pieces", "Pieces", "Next, hold, ghost and piece rules.", {
            b.field(Id::NextCount, true, "Number of preview pieces; 0 hides NEXT."), b.field(Id::HoldEnabled),
            b.field(Id::InfiniteHold, true, "Unlimited Hold uses before locking."),
            b.field(Id::HoldCount, true, "Hold uses per piece; 0 disables Hold unless Infinite Hold is ON."),
            b.field(Id::Ghost), b.field(Id::RotationSystem), b.field(Id::RandomPieces)
        }),
        b.group("timing", "Timing", "Locking, entry delays and game-clock behavior.", {
            b.field(Id::LockDelay, true, "Grounded lock delay (seconds); 0 locks immediately."),
            b.field(Id::LockResetLimit, true, "Maximum lock-delay resets per piece; 0 allows no resets."),
            b.field(Id::InfiniteMovement, true, "Allow unlimited grounded lock-delay resets; hard drop still locks immediately."),
            b.field(Id::LineClearDelay, true, "Line-clear delay (seconds); 0 clears immediately."),
            b.field(Id::EntryDelay, true, "ARE: seconds after locking and line-clear delay, before the next piece appears; 0 spawns immediately. 0.1 seconds = 6 frames at 60 fps."),
            b.projected(Id::SimulationPps, Part::SimulationMode, "Clock",
                options({"Real time", "Per piece"}, {0, 1}), "Per piece advances game time only after a placement."),
            b.field(Id::SimulationPps, b.when(Id::SimulationPps,user.simulationPps > 0), "Positive pieces per game second; movement uses real time."),
            b.projected(Id::TurnPieces, Part::TurnMode, "Turn taking",
                options({"Simultaneous", "Alternating"}, {0, 1}), "Alternating requires a positive quota for both players.", b.when(Id::GameMode,localVersus || mode == Runtime::GameMode::Multiplayer)),
            b.field(Id::TurnPieces, b.when(Id::TurnPieces,user.turnPieces > 0) && b.when(Id::GameMode,localVersus || mode == Runtime::GameMode::Multiplayer), "Positive pieces per turn; fractional quotas are allowed.")
        }),
        b.group("disabled-movement", "Disabled actions", "ON disables the action; gravity continues normally.", {
            b.field(Id::DisableLeft), b.field(Id::DisableRight), b.field(Id::DisableClockwise),
            b.field(Id::DisableCounterclockwise), b.field(Id::Disable180),
            b.field(Id::DisableSoftDrop), b.field(Id::DisableHardDrop),
            b.projected(Id::HoldEnabled, Part::HoldDisabled, "Disable Hold", options({"OFF", "ON"}, {0, 1}),
                "ON disables Hold; shares the Allow Hold setting in Pieces."),
            b.field(Id::DisableUndo), b.field(Id::DisablePause)
        }),
        std::move(gravity),
        std::move(garbage),
        b.group("attack-rules", "Attack rules", "B2B, clear rewards and garbage special attacks.", {
            b.field(Id::AttackMultiplier, true, "Attack multiplier; 0 produces no attack garbage."),
            b.field(Id::SpinBonuses, true, "Choose spin reward rules; preview each option before applying."),
            b.group("b2b-reward", "B2B reward", "Choose B2B attack rewards and configure charge release when the chain breaks.", {
                b.field(Id::B2BReward, true, "Fixed attack adds the configured bonus at displayed B2B >= 1; chaining follows TETR.IO's logarithmic bonus before combo scaling."),
                b.field(Id::B2BRewardAmount, true, "Attack bonus at displayed B2B >= 1, before combo scaling. Default 1; 0 adds no bonus."),
                b.field(Id::B2BCharging, true, "Release stored B2B when the chain breaks."),
                b.field(Id::B2BChargeThreshold, true, "Minimum displayed B2B for a surge; default 4."),
                b.field(Id::B2BChargeReduction, true, "Surge attack = max(0, displayed B2B - reduction).")
            }),
            b.group("perfect-clear", "Perfect clear", "Rewards for an empty field after clearing.", {
                b.field(Id::PerfectClearEnabled), b.field(Id::PerfectClearAttack),
                b.field(Id::PerfectClearCharge, true, "Total B2B increment for this clear; 0 adds no charge.")
            }),
            b.group("half-clear", "Half clear", "No placed-piece cells remain; garbage may remain. Excludes perfect clears.", {
                b.field(Id::HalfClearEnabled), b.field(Id::HalfClearAttack),
                b.field(Id::HalfClearCharge, true, "Total B2B increment for this clear; 0 adds no charge.")
            }),
            b.field(Id::GarbageSpecialAttack, true, "Flat bonus for a B2B-eligible clear removing garbage; not multiplied by the number of garbage rows.")
        }),
        std::move(quickplay),
        b.group("forced-handling","Forced handling","Game rules can override selected handling values without replacing personal handling.",std::move(forcedHandling))
    }, "Field, piece rules, gravity, and timing options.");
    game.children.pop_back(); // GAME's reset covers shared shape and QuickPlay settings too.
    root.children.push_back(std::move(game));
    root.children.push_back(b.section(Section::Autoplay, {
        b.field(Id::AutoplayMode),
        b.field(Id::AutoplayPps, b.when(Id::AutoplayMode,user.autoplayMode != 0), "Bot pieces per second; 0 runs without a speed limit."),
        b.field(Id::AutoplayHandlingSimulation, b.when(Id::AutoplayMode,user.autoplayMode != 0), "Freeze the piece while thinking, then animate the chosen landing using DAS / ARR / DCD and soft drop pacing. 20G settles immediately. ChatGPT only, OFF by default."),
        b.field(Id::AutoplayWidth, b.when(Id::AutoplayMode,user.autoplayMode != 0))
    }, "Bot selection; tuning appears when a bot is enabled."));
    root.children.push_back(b.section(Section::Controls, {
        b.field(Id::ControlLeft), b.field(Id::ControlRight), b.field(Id::ControlSoftDrop), b.field(Id::ControlHardDrop),
        b.field(Id::ControlRotateClockwise), b.field(Id::ControlRotateCounterclockwise),
        b.field(Id::ControlRotate180), b.field(Id::ControlHold),
        b.field(Id::ControlRetry), b.field(Id::ControlOpenSettings), b.field(Id::ControlReplay), b.field(Id::ControlUndo), b.field(Id::ControlPause)
    }, "Keyboard bindings; select an action to capture a key."));
    root.children.push_back(b.section(Section::Network, {
        b.field(Id::ServerIp),
        b.field(Id::Username,true,"Change your username before or after connecting; online changes sync with the server."),
        {"network-connect-gap","",{},NodeKind::Separator},
        {"connect", "CONNECT / RECONNECT", "Connection status is shown directly in Network.", NodeKind::Connect},
        {"disconnect", "DISCONNECT", "Disconnect from the server.", NodeKind::Disconnect},
        {"network-room-gap","",{},NodeKind::Separator},
        {"room-create", "Create room", "Create or join a room before starting a game.", NodeKind::RoomAction},
        {"room-join", "Join by name", "Enter the exact room name, including private rooms.", NodeKind::RoomAction},
        {"room-list", "Room lobby", "Public rooms: select a room to join.", NodeKind::RoomAction},
        {"network-reset-gap","",{},NodeKind::Separator}
    }, "Server address and connection actions."));
    root.children.push_back(b.section(Section::Others, {
        b.field(Id::SquareStyle), b.field(Id::ObscuredColors),
        b.field(Id::UiLanguage, true, "Choose Chinese or English; applies to both players and the start screen."),
        b.field(Id::ShowSpawnPosition, true,
            "Mark the next piece's normal spawn cells with light dots in empty cells and small holes in blocks; requires NEXT and ignores Clutch."),
        b.field(Id::ContinueInputUnfocused, true,
            "OFF ignores keyboard input while the game terminal is unfocused. ON continues accepting it. Shared by both players.")
    }, "Shared by both players: block appearance, color visibility, and interface language."));
    auto maps=make_map_menu(user);
    maps.children.push_back(b.field(Id::MapId,false));
    maps.children.push_back(b.field(Id::MapRotationSystem,false));
    root.children.push_back(std::move(maps));
    if(!user.presetDefined) root.children.push_back(make_challenge_menu(user));
    root.children.push_back(make_preset_menu(user,localVersus));
    SettingNode copyOther{"copy-other-player", "Copy other player's settings",
        "Copy the other player's full configuration into the selected player, keeping this player's keyboard bindings. Confirm twice.",
        NodeKind::CopyOtherPlayer};
    copyOther.visible=localVersus;
    root.children.push_back(std::move(copyOther));
    root.children.push_back({"reset", "RESET", "Clear presets; keep handling, appearance, network, language and keyboard bindings. Multiplayer mode is preserved.", NodeKind::ResetGame});
    if(localVersus) root.children.back().descriptionSuffix="Root reset keeps Local Versus mode.";
    if(user.presetDefined) {
        root.title="PRESET SETTINGS";
        root.description="Only defined parameters are saved. S saves; U makes the selected parameter undefined; Esc exits.";
        root.children.insert(root.children.begin(),{"preset-rename","Rename preset","Enter confirms the preset name. Esc cancels.",NodeKind::PresetRename});
        root.children.insert(root.children.begin()+1,{"preset-save","Save preset","Save the current defined parameters.",NodeKind::PresetSave});
        auto& mapMenu=*std::find_if(root.children.begin(),root.children.end(),[](const auto& node){return node.key=="maps";});
        SettingNode reset{"reset","RESET","Reset only this submenu; confirm twice.",NodeKind::ResetSubtree};
        reset.resetFields={Id::MapId,Id::MapRotationSystem,Id::MapGoal};mapMenu.children.push_back(std::move(reset));
        if(!UserConfig::Presets::defined(user,Id::MapId)) mapMenu.summary="Undefined";
    }
    add_subtree_resets(root);
    if(user.presetDefined) {
        const auto scope_resets=[&](auto&& self,SettingNode& menu)->void {
            std::set<Id> fields;
            for(auto& child:menu.children) {
                if(child.kind==NodeKind::ResetSection || child.kind==NodeKind::ResetSubtree || child.kind==NodeKind::ResetGame) continue;
                collect_fields(child,fields);self(self,child);
            }
            for(auto& child:menu.children) {
                if(child.kind==NodeKind::ResetSection) child.resetFields.assign(fields.begin(),fields.end());
                if(child.kind==NodeKind::ResetSection || child.kind==NodeKind::ResetSubtree || child.kind==NodeKind::ResetGame)
                    child.description="Choose undefined, original player settings, defaults, or cancel. Only this entry's parameters change.";
            }
        };
        scope_resets(scope_resets,root);
    }
    prune(root,includeHidden);
    // Player is a group with a live summary supplied by the view.
    root.player = player;
    return root;
}

SettingNode make_host_tree(UserConfig::Settings& host,std::shared_ptr<const Runtime::MapDefinition> map) {
    auto root=make_tree(host,false,0,false,map);
    root.children.erase(std::remove_if(root.children.begin(),root.children.end(),[](const auto& node){return node.key=="challenges";}),root.children.end());
    root.title="Host configuration (read-only)";
    root.description="Read-only host configuration. Personal handling, appearance and bindings are kept when copying.";
    for(auto& category:root.children) if(category.key=="presets") {
        category.summary.clear();category.description="Current preset values are shown in their setting categories.";
        category.children={{"values","Current preset values are shown in their setting categories."}};
    }
    for(auto& category:root.children) if(category.key=="maps") {
        category.children.clear();category.summary=map?map->name:"None";
        if(map) {
            SettingNode record{"host-map",map->name,"Read-only host configuration. Personal handling, appearance and bindings are kept when copying."};
            record.literalTitle=true;record.mapPreview=map;
            SettingNode size{"size","Playfield"};size.summary=std::to_string(map->width)+" x "+std::to_string(map->height);size.mapPreview=map;
            record.children.push_back(std::move(size));
            SettingNode generator{"generator",map->generator==Runtime::MapGenerator::CustomSequence?"Custom sequence":std::string(UserConfig::setting_descriptor(Id::RotationSystem).label)};
            const auto rotations=UserConfig::choice_options(UserConfig::ChoiceKind::RotationSystem);
            generator.summary=map->generator==Runtime::MapGenerator::CustomSequence?std::to_string(map->sequence.size()):rotations.labels.at(static_cast<std::size_t>(map->rotationSystem));
            for(std::size_t start=0;start<map->sequence.size();start+=16) {
                SettingNode row{"sequence-"+std::to_string(start),std::to_string(start+1)+"-"+std::to_string(std::min(start+16,map->sequence.size()))};
                row.literalTitle=true;
                for(std::size_t i=start;i<std::min(start+16,map->sequence.size());++i) row.summary+=std::string(Piece::metadata(map->sequence[i]).name)+" ";
                generator.children.push_back(std::move(row));
            }
            record.children.push_back(std::move(generator));category.children.push_back(std::move(record));
        } else category.children.push_back({"none","Do not use a map"});
    }
    const auto readonly=[&](auto&& self,SettingNode& node)->void {
        if(node.item) node.item->readOnly=true;
        if(node.kind!=NodeKind::Group && !node.item) node.enabled=false;
        for(auto& child:node.children) self(self,child);
    };
    readonly(readonly,root);
    root.children.push_back({"copy-host-config","Copy host configuration",root.description,NodeKind::CopyOtherPlayer});
    return root;
}
std::vector<SettingId> item_fields(const SettingItem& item) {
    if(item.id==Id::RotationSystem && item.owner) {
        const auto map=UserConfig::Maps::find(item.owner->mapId);
        if(map && map->generator==Runtime::MapGenerator::CustomSequence) return {Id::MapRotationSystem};
    }
    std::vector<Id> fields{item.id};
    if(item.id==Id::SoftDropFactor) fields.push_back(Id::SoftDropFixed);
    if(item.id==Id::ForcedSdf) fields.push_back(Id::ForcedSdfFixed);
    if(item.part==ValuePart::GravityMode) {fields.push_back(Id::InstantGravity);fields.push_back(Id::GravityProgression);}
    return fields;
}
std::vector<SettingId> reset_fields(const SettingNode& node) {
    if(node.kind==NodeKind::ResetSubtree || node.kind==NodeKind::ResetSection) return node.resetFields;
    std::vector<Id> fields;
    for(const auto& descriptor:UserConfig::setting_schema())
        if((node.kind==NodeKind::ResetGame && descriptor.section!=UserConfig::Section::Network) || descriptor.section==node.section) fields.push_back(descriptor.id);
    return fields;
}
void reset_draft(UserConfig::Settings& user,const SettingNode& node,const UserConfig::Settings* source) {
    const bool keepLocalMode=node.kind==NodeKind::ResetGame &&
        user.gameMode==static_cast<int>(Runtime::GameMode::LocalVersus);
    const bool keepOnlineMode=user.gameMode==static_cast<int>(Runtime::GameMode::Multiplayer) ||
        user.gameMode==-static_cast<int>(Runtime::GameMode::Multiplayer);
    for(const auto id:reset_fields(node)) {
        if((keepLocalMode || keepOnlineMode) && id==Id::GameMode) continue;
        if(source) {
            auto value=UserConfig::Presets::read_value(*source,id);
            if(id==Id::CheesePattern && node.preserveCheeseRefill)
                value=(user.cheesePattern<0 ? -1 : 1)*cheese_style(*source);
            UserConfig::Presets::write_value(user,id,value);UserConfig::Presets::define(user,id);
        }
        else UserConfig::Presets::undefine(user,id);
    }
    UserConfig::Presets::record_edits(user);
}

bool uses_inline_input(const SettingItem& item) {
    return !item.keyBinding && item.choice.labels.empty();
}

void reset_subtree(UserConfig::Settings& user, const SettingNode& reset, bool playerTwo) {
    const bool keepOnlineMode=user.gameMode==static_cast<int>(Runtime::GameMode::Multiplayer) ||
        user.gameMode==-static_cast<int>(Runtime::GameMode::Multiplayer);
    if (reset.resetPresets) UserConfig::Presets::clear(user);
    if (keepOnlineMode) user.gameMode=static_cast<int>(Runtime::GameMode::Multiplayer);
    UserConfig::Settings defaults;
    defaults.keyCodes = playerTwo ? UserConfig::default_player_two_key_codes() : UserConfig::default_key_codes();
    for (const auto id : reset.resetFields) {
        if (keepOnlineMode && id==Id::GameMode) continue;
        if (id==Id::CheesePattern && reset.preserveCheeseRefill) {
            user.cheesePattern=(user.cheesePattern<0 ? -1 : 1)*cheese_style(defaults);
            continue;
        }
        const auto& descriptor = UserConfig::setting_descriptor(id);
        if (descriptor.keyBinding) {
            UserConfig::set_control_keys(user, descriptor.controlIndex,
                UserConfig::control_keys(defaults, descriptor.controlIndex));
        } else {
            const auto source = UserConfig::bind_setting(defaults, descriptor);
            std::visit([&](auto target) {
                target.get() = std::get<decltype(target)>(source).get();
            }, UserConfig::bind_setting(user, descriptor));
        }
    }
    if (reset.resetPresets) user.presets = {};
    UserConfig::Presets::record_edits(user);
}

int choice_value(const SettingItem& item) {
    const auto& user = *item.owner;
    if(item.id==Id::Goal && user.goal==7 &&
        std::find(item.choice.values.begin(),item.choice.values.end(),7)==item.choice.values.end()) return 0;
    // Match gameplay normalization for historical/out-of-range style values.
    if (item.squarePreview) return UserConfig::clamped_int(user, Id::SquareStyle);
    if(item.id==Id::RotationSystem) {
        const auto map=UserConfig::Maps::find(user.mapId);
        if(map && map->generator==Runtime::MapGenerator::CustomSequence) return UserConfig::clamped_int(user,Id::MapRotationSystem);
    }
    if (item.id == Id::SpinBonuses || item.id == Id::RotationSystem || item.id == Id::RandomPieces)
        return UserConfig::clamped_int(user, item.id);
    switch (item.part) {
        case ValuePart::GameMode: return std::abs(UserConfig::clamped_int(user, Id::GameMode));
        case ValuePart::CheeseStyle: return cheese_style(user);
        case ValuePart::CheeseRefill: return user.cheesePattern >= 0 ? 1 : 0;
        case ValuePart::HoleLayout: return user.holeCount < 0 ? 1 : 0;
        case ValuePart::GravityMode: return gravity_mode(user);
        case ValuePart::SimulationMode: return user.simulationPps > 0 ? 1 : 0;
        case ValuePart::TurnMode: return user.turnPieces > 0 ? 1 : 0;
        case ValuePart::HoldDisabled: return UserConfig::clamped_int(user, Id::HoldEnabled) == 0 ? 1 : 0;
        default: return std::get<std::reference_wrapper<int>>(item.value).get();
    }
}
void apply_choice(SettingItem& item, int value) {
    if (item.bothReadOnly || item.readOnly) return;
    if (item.secondaryOwner) {
        auto first=*item.owner,second=*item.secondaryOwner;
        auto a=rebind_item(item,first),b=rebind_item(item,second);
        apply_choice(a,value); apply_choice(b,value);
        *item.owner=std::move(first); *item.secondaryOwner=std::move(second);
        return;
    }
    auto& user = *item.owner;
    const auto before=UserConfig::snapshot_quick_play_mods(user);
    if(item.id==Id::RotationSystem) {
        const auto map=UserConfig::Maps::find(user.mapId);
        if(map && map->generator==Runtime::MapGenerator::CustomSequence) {user.mapRotationSystem=std::clamp(value,-1,11);UserConfig::Presets::define(user,Id::MapRotationSystem);UserConfig::Presets::record_edits(user);return;}
        if(value<0) return;
    }
    switch (item.part) {
        case ValuePart::GameMode: user.gameMode = value; break;
        case ValuePart::CheeseStyle: user.cheesePattern = (user.cheesePattern < 0 ? -1 : 1) * value; break;
        case ValuePart::CheeseRefill: user.cheesePattern = (value ? 1 : -1) * cheese_style(user); break;
        case ValuePart::HoleLayout: user.holeCount = (value ? -1 : 1) * holes(user); break;
        case ValuePart::GravityMode:
            user.gravityProgression=value==3 ? 1 : value==4 ? 2 : value==5 ? 3 : 0;
            if (value == 2) user.instantGravity = 1;
            else { user.gravityInterval = gravity_seconds(user) * (value == 1 ? -1 : 1); user.instantGravity = 0; }
            break;
        case ValuePart::SimulationMode: user.simulationPps = value ? (user.simulationPps > 0 ? user.simulationPps : 2.5) : 0; break;
        case ValuePart::TurnMode: user.turnPieces = value ? (user.turnPieces > 0 ? user.turnPieces : 1) : 0; break;
        case ValuePart::HoldDisabled: user.holdEnabled = value ? 0 : 1; break;
        default: std::get<std::reference_wrapper<int>>(item.value).get() = value; break;
    }
    for(const auto id:item_fields(item)) UserConfig::Presets::define(user,id);
    UserConfig::reconcile_quick_play_mods(user,before);
    UserConfig::Presets::record_edits(user);
}
std::size_t selected_choice(const SettingItem& item) {
    const auto found = std::find(item.choice.values.begin(), item.choice.values.end(), choice_value(item));
    return static_cast<std::size_t>(found - item.choice.values.begin());
}
std::string scalar_value(const SettingItem& item) {
    const auto duration=[&] {
        switch(item.id) {
            case Id::BlitzTime: return true;
            default: return false;
        }
    }();
    if(duration) {
        const double seconds=item.part==ValuePart::GravitySeconds?gravity_seconds(*item.owner):
            std::get<std::reference_wrapper<double>>(item.value).get();
        return Util::format_time(seconds);
    }
    std::ostringstream value;
    if (item.part == ValuePart::HoleMagnitude) value << holes(*item.owner);
    else if (item.part == ValuePart::GravitySeconds) value << gravity_seconds(*item.owner);
    else std::visit([&](auto reference) { value << reference.get(); }, item.value);
    if(item.id==Id::SoftDropFactor && item.owner->softDropFixed) value << 's';
    if(item.id==Id::ForcedSdf && item.owner->forcedSdfFixed) value << 's';
    return value.str();
}
static bool assign_scalar_value(SettingItem& item, const std::string& text) {
    if (item.bothReadOnly || item.readOnly) return false;
    if (item.secondaryOwner) {
        auto first=*item.owner,second=*item.secondaryOwner;
        auto a=rebind_item(item,first),b=rebind_item(item,second);
        if (!assign_scalar(a,text) || !assign_scalar(b,text)) return false;
        *item.owner=std::move(first); *item.secondaryOwner=std::move(second);
        return true;
    }
    struct RecordEdits {
        UserConfig::Settings& user;
        ~RecordEdits(){UserConfig::Presets::record_edits(user);}
    } record{*item.owner};
    const auto seconds_input=[&] {
        auto number=std::string_view{text};
        const auto end=number.find_last_not_of(" \t\r\n");
        if(end!=std::string_view::npos) number=number.substr(0,end+1);
        if(!number.empty() && (number.back()=='s' || number.back()=='S')) number.remove_suffix(1);
        return Util::parse_time(number);
    };
    if(item.id==Id::BlitzTime || item.id==Id::GarbageInterval || item.id==Id::GarbageInTime ||
       item.id==Id::GarbageRiseInterval || item.id==Id::LockDelay || item.id==Id::LineClearDelay || item.id==Id::EntryDelay) {
        const auto value=seconds_input();
        if(!value || *value<0 || *value>UserConfig::setting_descriptor(item.id).range.maximum) return false;
        std::get<std::reference_wrapper<double>>(item.value).get()=*value;return true;
    }
    if (item.id==Id::GravityLevelLines) {
        const auto value=Util::Text::parse_integer<int>(text);
        if(!value || *value<1 || *value>1000000) return false;
        item.owner->gravityLevelLines=*value;return true;
    }
    if (item.id==Id::GravityAcceleration) {
        const auto value=Util::Text::parse_finite_real(text);
        if(!value || *value<0.01 || *value>100) return false;
        item.owner->gravityAcceleration=*value;return true;
    }
    if (item.id == Id::Das || item.id == Id::Arr || item.id == Id::Dcd || item.id == Id::SoftDropFactor) {
        return UserConfig::assign_setting_text(*item.owner,item.id,text);
    }
    if (item.part == ValuePart::HoleMagnitude) {
        const auto value = Util::Text::parse_integer<int>(text);
        const int width = std::clamp(item.owner->fieldWidth, 1, 100);
        if (!value || *value < 1 || *value > width) return false;
        item.owner->holeCount = (item.owner->holeCount < 0 ? -1 : 1) * *value;
        return true;
    }
    if (item.part == ValuePart::GravitySeconds || item.id == Id::SimulationPps || item.id == Id::TurnPieces) {
        const auto value = item.part==ValuePart::GravitySeconds?seconds_input():Util::Text::parse_finite_real(text);
        const double upper = item.part == ValuePart::GravitySeconds ? 1e9 : 1e6;
        if (!value || *value <= (item.part == ValuePart::GravitySeconds ? 1e-7 : 0.0) || *value > upper) return false;
        if (item.part == ValuePart::GravitySeconds) item.owner->gravityInterval = *value;
        else UserConfig::assign_setting_text(*item.owner, item.id, text);
        return true;
    }
    return UserConfig::assign_setting_text(*item.owner, item.id, text);
}
bool assign_scalar(SettingItem& item,const std::string& text) {
    if(item.bothReadOnly || item.readOnly) return false;
    if(item.owner->presetDefined && (text=="undefined" || text=="未定义" || text=="?")) {
        for(auto* owner:{item.owner,item.secondaryOwner}) if(owner) {
            const auto rebound=rebind_item(item,*owner);
            for(const auto id:item_fields(rebound)) UserConfig::Presets::undefine(*owner,id);
        }
        return true;
    }
    if(!assign_scalar_value(item,text)) return false;
    for(const auto id:item_fields(item)) UserConfig::Presets::define(*item.owner,id);
    UserConfig::Presets::record_edits(*item.owner);
    return true;
}
} // namespace App::Settings::Model
