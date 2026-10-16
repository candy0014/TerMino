#include "single_player_challenges.h"
#include "tetr/app/settings_menu.h"
#include "tetr/app/application_state.h"
#include "settings_input.h"
#include "settings_model.h"
#include "settings_navigation.h"
#include "settings_view.h"
#include "settings_targets.h"
#include "settings_presets.h"
#include "settings_preset_editor.h"
#include "settings_maps.h"
#include "map_editor.h"
#include "tetr/config/map_store.h"
#include "tetr/config/presets.h"
#include "ui_text.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/input/services.h"
#include "tetr/input/action.h"
#include "tetr/config/user_config.h"
#include "tetr/config/normalization.h"
#include "tetr/config/multiplayer_config.h"
#include "tetr/replay/file_path.h"
#include "tetr/network/network_services.h"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace App::Settings {
namespace {
using Model::SettingItem;
using Model::SettingNode;
using Model::NodeKind;
enum class MenuExit { Restart, StartScreen, Root, CancelEdit };

void print_action_confirmation(const TerminalSettingsView& view, const SettingNode& node) {
    if(node.key=="copy-host-config") {view.print_message("Press Enter again to copy the host configuration. Arrow keys cancel.");return;}
    if(node.confirmTwice) {
        view.print_message(node.kind==NodeKind::ChallengeReplay || node.kind==NodeKind::RecentReplay ? "Press Enter again to play this replay. Arrow keys cancel." :
            node.kind==NodeKind::ChallengeStart ? "Press Enter again to start this challenge. Arrow keys cancel." :
            node.kind==NodeKind::PresetEdit ? "Press Enter again to edit this preset. Arrow keys cancel." :
            node.kind==NodeKind::PresetDelete ? "Press Enter again to delete this preset. Arrow keys cancel." :
            node.kind==NodeKind::MapDelete ? "Press Enter again to delete this map. Arrow keys cancel." :
            "Press Enter again to confirm this map option. Arrow keys cancel.");return;
    }
    switch (node.kind) {
        case NodeKind::CopyOtherPlayer:
            view.print_message("Press Enter again to copy the other player's settings. Arrow keys cancel."); break;
        case NodeKind::PresetApply:
            view.print_message("Press Enter again to apply this preset. Arrow keys cancel."); break;
        case NodeKind::PresetUndo:
            view.print_message("Press Enter again to undo the latest preset layer. Arrow keys cancel."); break;
        case NodeKind::PresetClear:
            view.print_message("Press Enter again to undo all presets. Arrow keys cancel."); break;
        case NodeKind::PresetRemove:
            view.print_message("Press Enter again to remove this preset layer. Arrow keys cancel."); break;
        case NodeKind::PresetCommit:
            view.print_message("Press Enter again to keep current values and discard preset recovery. Arrow keys cancel."); break;
        default: view.print_reset_confirmation(true); break;
    }
}

bool action_pressed(Runtime::GameContext& context, Input::Action action,
                    const std::set<int>* suppressed = nullptr) {
    for (const auto& [key, bound] : context.input_services().bindings.entries())
        if (bound == action && (!suppressed || !suppressed->count(key)) &&
            context.input_services().key_down(key)) return true;
    return false;
}
bool local_versus_available(Runtime::GameContext& context) {
    const auto* app = context.app_component();
    return app && app->primaryContext && app->localPlayerTwoContext &&
        Runtime::selected_game_mode(app->primaryContext->user_settings()) == Runtime::GameMode::LocalVersus;
}
bool restart_pressed(Runtime::GameContext& context, const std::set<int>* suppressed = nullptr) {
    if (action_pressed(context, Input::Action::Retry, suppressed)) return true;
    if (!local_versus_available(context)) return false;
    const auto* app = context.app_component();
    auto* other = app->primaryContext == &context ? app->localPlayerTwoContext : app->primaryContext;
    return other && action_pressed(*other, Input::Action::Retry, suppressed);
}
App::SettingsMenuState& menu_position(Runtime::GameContext& context, App::SettingsMenuState& fallback) {
    auto* app = context.app_component();
    if (!app) return fallback;
    return app->localPlayerTwoContext == &context ||
        (!local_versus_available(context) && app->singlePlayerOwner == 1)
        ? app->playerTwoSettingsMenu : app->settingsMenu;
}
bool persist(Runtime::GameContext& context) { return context.user_settings().presetDefined || UserConfig::save(context.user_settings()); }
void poll_server(Runtime::GameContext& context) {
    auto* app = context.app_component();
    auto& primary = app && app->primaryContext ? *app->primaryContext : context;
    if (primary.network_component()) {
        if (primary.user_settings_component()) Network::set_online_username(*primary.network_component(),primary.user_settings().username);
        Network::poll_connection(*primary.network_component());
        if(primary.user_settings_component()) Network::synchronize_room_player_mode(*primary.network_component(),
            Runtime::selected_game_mode(primary.user_settings())==Runtime::GameMode::Multiplayer,primary.user_settings().gameMode);
        auto& network=*primary.network_component();
        const auto now=std::chrono::steady_clock::now();
        if(Network::is_room_host(network) && network.roomPolicy.forceRules && now-network.lastPreviewBuild>std::chrono::milliseconds(250)) {
            auto config=Runtime::normalize_user_config(primary.user_settings(),Runtime::selected_game_mode(primary.user_settings()));
            if(const auto map=UserConfig::Maps::find(primary.user_settings().mapId)) Runtime::apply_map_config(config,*map);
            config.quickPlay.continuousCancellation=app ? app->qpContinuousCancellation : primary.config.quickPlay.continuousCancellation;
            Network::set_room_config_preview(network,UserConfig::encode_multiplayer_config(primary.user_settings(),config,true));
            network.lastPreviewBuild=now;
        }
    }
}
bool is_player_two(const Runtime::GameContext& context) {
    return context.app_component() && (context.app_component()->localPlayerTwoContext == &context ||
        context.app_component()->singlePlayerOwner == 1);
}

} // namespace

bool synchronize_shared_appearance(Runtime::GameContext& displayed, bool save) {
    auto source=displayed.user_settings();
    source.uiLanguage=UserConfig::clamped_int(source,UserConfig::SettingId::UiLanguage);
    UiText::set_language(source.uiLanguage==1 ? UiText::Language::English : UiText::Language::Chinese);
    std::vector<Runtime::GameContext*> contexts{&displayed};
    std::vector<UserConfig::Settings*> settings{&displayed.user_settings()};
    UserConfig::Settings* uninitializedSecond=nullptr;
    if (auto* app=displayed.app_component()) {
        for (auto* context:{app->primaryContext,app->localPlayerTwoContext}) {
            if (!context) continue;
            contexts.push_back(context);
            settings.push_back(&context->user_settings());
        }
        if (app->localPlayerTwoContext && !app->localVersusSettingsAvailable && !local_versus_available(displayed))
            uninitializedSecond=&app->localPlayerTwoContext->user_settings();
        if (app->savedPlayerOneSettings) settings.push_back(&*app->savedPlayerOneSettings);
    }
    bool saved=true;
    std::set<std::string> savedPaths;
    for (auto* user:settings) {
        for (const auto& descriptor:UserConfig::setting_schema()) {
            if (descriptor.section!=UserConfig::Section::Others) continue;
            if(source.presetDefined && user->presetDefined) {
                if(UserConfig::Presets::defined(source,descriptor.id)) UserConfig::Presets::define(*user,descriptor.id);
                else UserConfig::Presets::undefine(*user,descriptor.id);
            }
            std::visit([&](auto member) {
                if constexpr (!std::is_same_v<decltype(member),std::monostate>)
                    user->*member=source.*member;
            },descriptor.member);
        }
        // Save the displayed copy first: promoted Player 2 can share a path
        // with the secondary context, whose gameplay settings may be stale.
        // Keep an unused Player 2 profile absent until it is initialized.
        if (save && !user->presetDefined && user!=uninitializedSecond && savedPaths.insert(user->persistencePath).second)
            saved=UserConfig::save(*user) && saved;
    }
    for (auto* context:contexts) {
        context->config.display=Runtime::normalize_user_config(context->user_settings(),
            Runtime::selected_game_mode(context->user_settings())).display;
        if (auto* input=context->input_component())
            input->set_continue_input_unfocused(UserConfig::clamped_int(context->user_settings(),UserConfig::SettingId::ContinueInputUnfocused)!=0);
    }
    return saved;
}

void choose_value(Runtime::GameContext& context, const TerminalSettingsView& view,
                  SettingItem& item, const std::string& breadcrumb, const CheckMenuControls& checkControls) {
    auto selected = Model::selected_choice(item);
    if (selected >= item.choice.values.size()) selected = 0;
    if (item.choice.cycleImmediately && !Model::mixed_value(item)) {
        Model::apply_choice(item, item.choice.values[UserConfig::Presets::defined(*item.owner,Model::item_fields(item).front()) ? (selected + 1) % item.choice.values.size() : 0]);
        return;
    }
    view.print_choices(item, selected, breadcrumb);
    NavigationInput input(context);
    for (;;) {
        checkControls();
        if (input.update(NavigationKey::Left, false) || input.update(NavigationKey::Backspace, false)) throw MenuExit::CancelEdit;
        const bool up = input.update(NavigationKey::Up, true);
        const bool down = input.update(NavigationKey::Down, true);
        const auto previous = selected;
        if (up && input.active_vertical_key() == NavigationKey::Up)
            selected = (selected + item.choice.values.size() - 1) % item.choice.values.size();
        if (down && input.active_vertical_key() == NavigationKey::Down)
            selected = (selected + 1) % item.choice.values.size();
        if (input.update(NavigationKey::PageUp, true)) selected = selected > view.page_rows() ? selected - view.page_rows() : 0;
        if (input.update(NavigationKey::PageDown, true)) selected = std::min(selected + view.page_rows(), item.choice.values.size() - 1);
        if (previous != selected || view.terminal_size_changed()) view.print_choices(item, selected, breadcrumb);
        if (input.update(NavigationKey::Confirm, false) || input.update(NavigationKey::Right, false)) {
            Model::apply_choice(item, item.choice.values[selected]);
            return;
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}
namespace {
void show_host_configuration(Runtime::GameContext& primary,Runtime::GameContext& displayed,const TerminalSettingsView& view);
void add_room_entries(SettingNode& root,const Network::NetworkServices& network) {
    auto it=std::find_if(root.children.begin(),root.children.end(),[](const auto& n){return n.key=="NETWORK";});
    if(it==root.children.end()) return;
    auto& menu=*it;
    std::optional<SettingNode> reset;
    for(const auto& child:menu.children) if(child.kind==NodeKind::ResetSection || child.kind==NodeKind::ResetSubtree) reset=child;
    menu.children.erase(std::remove_if(menu.children.begin(),menu.children.end(),[](const auto& child){return child.kind==NodeKind::ResetSection || child.kind==NodeKind::ResetSubtree;}),menu.children.end());
    menu.children.erase(std::remove_if(menu.children.begin(),menu.children.end(),[](const auto& n){return n.kind==NodeKind::RoomAction || n.key=="network-reset-gap";}),menu.children.end());
    const auto gap=[&](const std::string& key) {menu.children.push_back(SettingNode{key,"",{},NodeKind::Separator});};
    const auto append=[&](const std::string& key,const std::string& title,const std::string& summary={},bool enabled=true) {
        SettingNode n{key,title,network.roomMessage,NodeKind::RoomAction};n.summary=summary;
        n.enabled=enabled && network.connection==Network::ConnectionState::Connected;menu.children.push_back(std::move(n));
    };
    if(network.room.name.empty()) {
        append("room-create","Create room");append("room-join","Join by name");append("room-list","Room lobby");
    } else {
        const bool host=Network::is_room_host(network);
        menu.description=network.room.name+" ["+std::to_string(network.room.players)+"/"+std::to_string(network.room.capacity)+"] / "+network.roomMessage;
        if(host) {
            append("room-start","Prepare match");
            gap("room-policy-gap");
            append("room-force-rules","Force host configuration",UiText::tr(network.roomPolicy.forceRules?"ON":"OFF"));
        } else append("room-force-rules","Host configuration",UiText::tr(network.roomPolicy.forceRules?"ON":"OFF"),false);
        append("room-member-limit","Room player limit",std::to_string(network.room.capacity),host && network.multiplayerRoom);
        append("room-battle-limit","Battle player limit",std::to_string(std::min(network.battleLimit,network.room.capacity)),host && network.multiplayerRoom);
        gap("room-role-gap");
        auto self=std::find_if(network.roomMembers.begin(),network.roomMembers.end(),[&](const auto& m){return m.slot==network.roomPlayerIndex;});
        append("room-role","Player status",UiText::tr(self!=network.roomMembers.end() && self->role==Network::PlayerRole::Offline?"Offline":
            self!=network.roomMembers.end() && self->role==Network::PlayerRole::Spectator?"Spectator":"Battle"),network.multiplayerRoom);
        append("room-users","Room users");
        if(!host && network.roomPolicy.forceRules) append("room-view-host","View host configuration");
        gap("room-leave-gap");append("room-leave","Leave room");
    }
    if(reset) {gap("network-reset-gap");menu.children.push_back(std::move(*reset));}
}
void select_room_role_mode(UserConfig::Settings& user,Network::NetworkServices& network,Network::PlayerRole role) {
    if(role!=Network::PlayerRole::Offline)user.gameMode=4;
    else if(user.gameMode==4)user.gameMode=0;
    network.roomOnlineMode=user.gameMode==4;network.roomGameMode=user.gameMode;network.roleRoomInstance=network.roomInstance;
}
bool room_action(Runtime::GameContext& primary,Runtime::GameContext& displayed,const TerminalSettingsView& view,
    const CheckMenuControls& checkControls,const SettingNode& node) {
    auto& n=*primary.network_component();
    const auto& key=node.key;
    if(key=="room-start") {
        primary.user_settings().gameMode=4;
        n.roomOnlineMode=true;n.roomGameMode=primary.user_settings().gameMode;n.roleRoomInstance=n.roomInstance;
        n.startFromMenu=true;
        n.localRoomConfig=UserConfig::encode_multiplayer_config(primary.user_settings(),primary.config);
        // initialize_game confirms the terminal before the lobby sends Prepare.
        return true;
    }
    if(key=="room-leave") {Network::request_room(n,Network::RoomOperation::Leave);return false;}
    if(key=="room-view-host") {show_host_configuration(primary,displayed,view);return false;}
    if(key=="room-role") {
        if(n.pendingRoomRequest) return false;
        int value=0;for(const auto& m:n.roomMembers) if(m.slot==n.roomPlayerIndex) value=static_cast<int>(m.role);
        value=(value+1)%3;
        // Edit the existing row in place; there is no separate choice dialog.
        select_room_role_mode(displayed.user_settings(),n,static_cast<Network::PlayerRole>(value));
        Network::request_room(n,Network::RoomOperation::SetRole,{},true,false,value);
        return false;
    }
    if(key=="room-force-rules") {
        if(!n.pendingRoomRequest)Network::set_room_policy(n,{!n.roomPolicy.forceRules});
        return false;
    }
    if(key=="room-member-limit" || key=="room-battle-limit") {
        int value=key=="room-member-limit"?n.room.capacity:std::min(n.battleLimit,n.room.capacity);
        UserConfig::Settings temporary;temporary.uiLanguage=displayed.user_settings().uiLanguage;
        SettingItem item{Model::SettingId::FieldWidth,node.title,std::ref(value)};item.owner=&temporary;
        for(int i=2;i<=(key=="room-member-limit"?32:n.room.capacity);++i) {item.choice.labels.push_back(std::to_string(i));item.choice.values.push_back(i);}
        try {choose_value(primary,view,item,"SETTINGS > NETWORK",[&]{checkControls();poll_server(primary);});
            Network::request_room(n,Network::RoomOperation::SetLimits,{},true,false,key=="room-member-limit"?value:n.room.capacity,{},
                key=="room-member-limit"?std::min(value,n.battleLimit):std::min(value,n.room.capacity));
        } catch(MenuExit exit) {if(exit!=MenuExit::CancelEdit) throw;}
        return false;
    }
    return false;
}
bool show_rooms(Runtime::GameContext& primary, Runtime::GameContext& displayed,
    const TerminalSettingsView& view, const CheckMenuControls& checkControls, const std::string& action) {
    enum class Page { Main, Create, Join, List, Room, Users, Member };
    auto& network = *primary.network_component();
    Page page = action=="room-create"?Page::Create:action=="room-join"?Page::Join:action=="room-list"?Page::List:Page::Users;
    std::string name;
    bool automaticName=page==Page::Create,nameRequested=false;
    if(automaticName)network.suggestedRoomName.clear();
    int visibility = 0;
    std::size_t selected = 0;
    std::size_t userSelection = 0;
    int memberSlot = -1;
    std::string memberName;
    bool dirty = true;
    auto revision = network.roomRevision;
    auto lastRefresh = std::chrono::steady_clock::time_point{};
    NavigationInput input(primary);
    SettingNode menu;
    SettingNode usersMenu;
    auto roomView = view;
    auto usersView = view;
    const auto row = [](const std::string& key, const std::string& title, const std::string& hint = "") {
        return SettingNode{key, title, hint};
    };
    const auto rebuild = [&] {
        const auto previous = selected<menu.children.size() ? menu.children[selected].key : std::string{};
        menu = SettingNode{"rooms", "Rooms", network.roomMessage};
        if (page == Page::Main) {
            menu.children = {row("create", "Create room"), row("join", "Join by name"),
                row("list", "Room lobby"), row("back", "Back to settings")};
        } else if (page == Page::Create || page == Page::Join) {
            menu.title = page == Page::Create ? "Create room" : "Join by name";
            auto field = row("name", "Room name", page == Page::Create
                ? "Unused four-letter room name; edit to choose your own." : "Enter the exact room name, including private rooms.");
            field.summary = name.empty() ? UiText::tr(page==Page::Create?"Generating room name...":"Empty") : name;
            menu.children.push_back(std::move(field));
            if (page == Page::Create) {
                auto type = row("type", "Room type"); type.summary = UiText::tr(visibility ? "Private" : "Public");
                menu.children.push_back(std::move(type));
            }
            auto submit=row("submit", page == Page::Create ? "Create room" : "Join room");
            submit.enabled=page!=Page::Create || !name.empty();menu.children.push_back(std::move(submit));
            menu.children.push_back(row("back", "Back"));
        } else if (page == Page::List) {
            menu.title = "Room lobby";
            for (const auto& room : network.publicRooms) {
                auto entry = row("room:" + room.name, room.name,
                    room.players >= room.capacity ? "Room is full. Choose another room." : "Select this room to join.");
                entry.literalTitle = true; entry.summary = std::to_string(room.players) + "/" + std::to_string(room.capacity);
                menu.children.push_back(std::move(entry));
            }
            menu.children.push_back(row("refresh", "Refresh rooms", network.publicRooms.empty() ? "No public rooms." : ""));
            menu.children.push_back(row("back", "Back"));
        } else if (page==Page::Users) {
            menu.title="Room users";
            for (const auto& member:network.roomMembers) {
                auto player=row("manage-player:"+std::to_string(member.slot),member.username,"Enter manages this player.");
                player.literalTitle=true; player.summary=UiText::tr(member.role==Network::PlayerRole::Offline?"Offline":member.role==Network::PlayerRole::Battle?"Battle":"Spectator");
                if(member.host) player.summary+=" [HOST]";
                if(member.slot==network.roomPlayerIndex) player.summary+=" ["+UiText::tr("You")+"]";
                player.enabled=Network::is_room_host(network);menu.children.push_back(std::move(player));
            }
            menu.children.push_back(row("back","Back"));
        } else if(page==Page::Member) {
            menu.title=memberName;menu.literalTitle=true;
            const auto member=std::find_if(network.roomMembers.begin(),network.roomMembers.end(),[&](const auto& m){return m.slot==memberSlot && m.username==memberName;});
            if(member!=network.roomMembers.end()) {
                const int battles=static_cast<int>(std::count_if(network.roomMembers.begin(),network.roomMembers.end(),[](const auto& m){return m.role==Network::PlayerRole::Battle;}));
                const bool full=member->role!=Network::PlayerRole::Battle && battles>=std::min(network.room.capacity,network.battleLimit);
                for(int role=0;role<3;++role) {
                    const bool battleFull=role==static_cast<int>(Network::PlayerRole::Battle) && full;
                    auto option=row("state:"+std::to_string(role),role==0?"Battle":role==1?"Spectator":"Offline",
                        battleFull?"Battle player limit reached. Choose spectator or wait for a free place.":"Enter applies this status.");
                    option.enabled=network.multiplayerRoom && !battleFull;
                    if(static_cast<int>(member->role)==role)option.summary=UiText::tr("Current");
                    menu.children.push_back(std::move(option));
                }
                auto transfer=row("transfer","Transfer host",memberSlot==network.roomPlayerIndex?"Select another player.":"Select a player to transfer host immediately.");
                transfer.enabled=memberSlot!=network.roomPlayerIndex;menu.children.push_back(std::move(transfer));
                auto kick=row("kick","Kick player",memberSlot==network.roomPlayerIndex?"The host cannot kick themselves.":"Enter removes this player from the room.");
                kick.enabled=memberSlot!=network.roomPlayerIndex;menu.children.push_back(std::move(kick));
            }
            menu.children.push_back(row("back","Back"));
        } else {
            menu.title = "Current room";
            auto start = row("start", Network::is_room_host(network)?"Prepare match":"Start game", "Start only after creating or joining a room.");
            start.summary = network.room.name + " [" + std::to_string(network.room.players) + "/" + std::to_string(network.room.capacity) + ", " + UiText::tr(network.room.isPublic ? "Public" : "Private") + "]";
            menu.children = {std::move(start)};
            const bool host=Network::is_room_host(network);
            {
                auto policy=row("force-rules",host?"Force host configuration":"Host configuration",
                    "Apply host rules, map and piece sequence before starting. Appearance and handling stay local.");
                policy.summary=UiText::tr(network.roomPolicy.forceRules?"ON":"OFF");
                policy.enabled=host && network.roomPolicyAvailable; menu.children.push_back(std::move(policy));
            }
            if(!host && network.roomPolicy.forceRules && network.roomPolicyAvailable)
                menu.children.push_back(row("view-host-config","View host configuration","Read-only host configuration. Personal handling, appearance and bindings are kept when copying."));
            for (const auto& member : network.roomMembers) {
                auto player = row("member:" + std::to_string(member.slot), member.username);
                player.literalTitle = true;
                player.summary = "#" + std::to_string(member.slot+1);
                if (member.host) player.summary += " [HOST]";
                if (member.slot == network.roomPlayerIndex) player.summary += " [" + UiText::tr("You") + "]";
                menu.children.push_back(std::move(player));
            }
            menu.children.push_back(row("leave", "Leave room"));
            menu.children.push_back(row("back", "Back to settings"));
        }
        selected = std::min(selected, menu.children.size() - 1);
        const auto found = std::find_if(menu.children.begin(), menu.children.end(), [&](const auto& entry) { return entry.key == previous; });
        if (found != menu.children.end()) selected = static_cast<std::size_t>(found - menu.children.begin());
        roomView.set_submenu_path(page==Page::Member?usersView:view,menu,{},selected);
        roomView.prepare_layout(menu.children.size(), "SETTINGS > NETWORK > "+menu.title);
        roomView.print_menu(menu, selected, "SETTINGS > NETWORK > "+menu.title);
        roomView.print_message(network.roomMessage);
        if(page==Page::Users) {usersMenu=menu;usersView.set_submenu_path(view,usersMenu,{},selected);}
        dirty = false; revision = network.roomRevision;
    };
    for (;;) {
        checkControls(); poll_server(primary);
        if (!network.room.name.empty() && (page==Page::Create || page==Page::Join || page==Page::List)) return false;
        if (network.room.name.empty() && (page==Page::Users || page==Page::Member)) return false;
        if(page==Page::Create && automaticName) {
            if(!network.suggestedRoomName.empty() && name!=network.suggestedRoomName) {name=network.suggestedRoomName;dirty=true;}
            if(network.suggestedRoomName.empty() && !network.pendingRoomRequest && !nameRequested) {
                Network::request_room(network,Network::RoomOperation::SuggestName,{},true,true);nameRequested=true;
            }
        }
        if(page==Page::Member && (!Network::is_room_host(network) || std::none_of(network.roomMembers.begin(),network.roomMembers.end(),[&](const auto& m){return m.slot==memberSlot && m.username==memberName;}))) {
            page=Page::Users;selected=userSelection;dirty=true;
        }
        if (page == Page::List && network.connection == Network::ConnectionState::Connected &&
            !network.pendingRoomRequest && std::chrono::steady_clock::now() - lastRefresh > std::chrono::seconds(1)) {
            Network::request_room(network, Network::RoomOperation::List,{},true,true); lastRefresh = std::chrono::steady_clock::now();
        }
        if (dirty || revision != network.roomRevision || roomView.terminal_size_changed()) rebuild();
        if (input.update(NavigationKey::Left, false) || input.update(NavigationKey::Backspace, false)) {
            if(page==Page::Member) {page=Page::Users;selected=userSelection;dirty=true;continue;}
            return false;
        }
        const bool up = input.update(NavigationKey::Up, true), down = input.update(NavigationKey::Down, true);
        const auto previous = selected;
        if (up && input.active_vertical_key() == NavigationKey::Up) selected = (selected + menu.children.size() - 1) % menu.children.size();
        if (down && input.active_vertical_key() == NavigationKey::Down) selected = (selected + 1) % menu.children.size();
        if (input.update(NavigationKey::PageUp, true)) selected = selected > roomView.page_rows() ? selected - roomView.page_rows() : 0;
        if (input.update(NavigationKey::PageDown, true)) selected = std::min(selected + roomView.page_rows(), menu.children.size() - 1);
        if (previous != selected) { dirty = true; rebuild(); }
        if (input.update(NavigationKey::Confirm, false) || input.update(NavigationKey::Right, false)) {
            const auto key = menu.children[selected].key;
            if (!menu.children[selected].enabled) {
                roomView.print_message(page==Page::Create?"Generating room name...":page==Page::Member?menu.children[selected].description:"Only the room host can change this option."); continue;
            }
            if (key == "back") {
                if(page==Page::Member) {page=Page::Users;selected=userSelection;dirty=true;continue;}
                return false;
            } else if (key == "create" || key == "join") {
                page = key == "create" ? Page::Create : Page::Join; selected = 0;
                name.clear();automaticName=page==Page::Create;nameRequested=false;
                if(automaticName)network.suggestedRoomName.clear();
            } else if (key == "list") { page = Page::List; selected = 0; lastRefresh = {}; network.roomMessage = std::string(Network::room_result_message(Network::RoomResult::Listed)); }
            else if (key == "name") {
                SettingItem item{Model::SettingId::ServerIp, "Room name", std::ref(name)};
                item.owner = &displayed.user_settings();
                item.hint = menu.children[selected].description; item.allowEmptyText = page==Page::Create;
                const auto controls = [&] { checkControls(); poll_server(primary); };
                bool escape = false;
                const auto draft = read_scalar_text(primary, controls, [&](const std::string& value, std::size_t caret) {
                    roomView.print_scalar_input(item, "SETTINGS > NETWORK > Rooms", value, caret);
                }, &escape);
                if (!escape && (page==Page::Create || !draft.empty())) {name=draft;automaticName=page==Page::Create && draft.empty();}
                input.suppress_held_keys();
            } else if (key == "type") {
                UserConfig::Settings temporary;
                temporary.uiLanguage = visibility;
                SettingItem item{Model::SettingId::UiLanguage, "Room type", std::ref(temporary.uiLanguage)};
                item.owner = &temporary; item.choice.labels = {"Public", "Private"}; item.choice.values = {0, 1};
                try {
                    choose_value(primary, roomView, item, "SETTINGS > NETWORK > Rooms", [&] { checkControls(); poll_server(primary); });
                    visibility = temporary.uiLanguage;
                } catch (MenuExit exit) { if (exit != MenuExit::CancelEdit) throw; }
                input.suppress_held_keys();
            } else if (key == "submit") Network::request_room(network, page == Page::Create ? Network::RoomOperation::Create : Network::RoomOperation::Join, name, visibility == 0);
            else if (key == "refresh") { Network::request_room(network, Network::RoomOperation::List); lastRefresh = std::chrono::steady_clock::now(); }
            else if (key.rfind("room:", 0) == 0) Network::request_room(network, Network::RoomOperation::Join, key.substr(5));
            else if (key == "leave") Network::request_room(network, Network::RoomOperation::Leave);
            else if(key.rfind("manage-player:",0)==0) {
                memberSlot=std::stoi(key.substr(14));memberName=menu.children[selected].title;
                userSelection=selected;page=Page::Member;selected=0;
            } else if(page==Page::Member && (key=="kick" || key=="transfer" || key.rfind("state:",0)==0)) {
                if(network.pendingRoomRequest) {roomView.print_message(network.roomMessage);continue;}
                // The name guards against a stale selection of a reused slot.
                if(key.rfind("state:",0)==0 && memberSlot==network.roomPlayerIndex)
                    select_room_role_mode(primary.user_settings(),network,static_cast<Network::PlayerRole>(std::stoi(key.substr(6))));
                Network::request_room(network,key=="kick"?Network::RoomOperation::Kick:key=="transfer"?Network::RoomOperation::TransferHost:Network::RoomOperation::SetMemberRole,
                    memberName,true,false,memberSlot,{},network.battleLimit,
                    key=="kick" || key=="transfer"?Network::PlayerRole::Battle:static_cast<Network::PlayerRole>(std::stoi(key.substr(6))));
                page=Page::Users;selected=userSelection;
            } else if(key=="view-host-config") {
                show_host_configuration(primary,displayed,roomView);input.suppress_held_keys();
            } else if (key=="force-rules") {
                if(!network.pendingRoomRequest)Network::set_room_policy(network,{!network.roomPolicy.forceRules});
                input.suppress_held_keys();
            }
            else if (key == "start") {
                if (network.connection == Network::ConnectionState::Connected && !network.room.name.empty() && !network.pendingRoomRequest) return true;
                network.roomMessage = "Create or join a room before starting a game.";
            }
            dirty = true;
        }
        primary.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
    }
}


bool edit_item(Runtime::GameContext& context, const TerminalSettingsView& view,
               SettingItem& item, const std::string& breadcrumb, const CheckMenuControls& checkControls,
               bool& savedByEscape) {
    if (!item.choice.labels.empty()) { choose_value(context, view, item, breadcrumb, checkControls); return true; }
    const auto initialValue=item.id==Model::SettingId::ServerIp ? Model::scalar_value(item) : std::string{};
    view.print_edit(item, breadcrumb,initialValue,initialValue.size());
    if (item.keyBinding) {
        const auto controls=[&] {
            checkControls();
            if (view.terminal_size_changed()) view.print_edit(item,breadcrumb);
        };
        const auto control=UserConfig::setting_descriptor(item.id).controlIndex;
        const int key=capture_key(context,Input::kBindableActions.at(control),controls);
        if(!UserConfig::Presets::defined(context.user_settings(),item.id))
            UserConfig::set_control_keys(context.user_settings(),control,{});
        UserConfig::toggle_control_key(context.user_settings(),control,key);
        UserConfig::Presets::define(context.user_settings(),item.id);
        if(!context.user_settings().presetDefined) UserConfig::save_keybindings(context.user_settings());
        refresh_runtime_bindings(context);
        return true;
    }
    const bool inlineInput = Model::uses_inline_input(item);
    std::string draft=initialValue;
    std::size_t draftCaret=initialValue.size();
    const ScalarInputRedraw redraw = [&](const std::string& value, std::size_t caret) {
        draft=value;
        draftCaret=caret;
        if (inlineInput) view.print_scalar_input(item,breadcrumb,value,caret);
        else view.print_edit(item,breadcrumb,value,caret);
    };
    const auto controls=[&] {
        checkControls();
        if (view.terminal_size_changed()) redraw(draft,draftCaret);
    };
    const std::string value = read_scalar_text(context, controls, redraw, &savedByEscape,initialValue);
    if(savedByEscape && context.user_settings().presetDefined) {savedByEscape=false;throw MenuExit::CancelEdit;}
    if (value.empty()) return true;
    if (item.id == Model::SettingId::Username) {
        const auto name = Network::normalize_room_name(value);
        return Network::valid_room_name(name) && Model::assign_scalar(item,name);
    }
    if (item.id == Model::SettingId::ReplayPath) {
        const auto path = Replay::replay_setting_path(value);
        if (path.empty()) return false;
        context.user_settings().replayPath = path;
        UserConfig::Presets::define(context.user_settings(),item.id);
        return true;
    }
    return Model::assign_scalar(item, value);
}

std::string path_key(const std::vector<std::string>& path) {
    std::string key = "root";
    for (const auto& part : path) key += "/" + part;
    return key;
}
SettingNode& current_menu(SettingNode& root, std::vector<std::string>& path) {
    auto* node = &root;
    std::size_t valid = 0;
    for (const auto& key : path) {
        const auto found = std::find_if(node->children.begin(), node->children.end(), [&](const auto& child) {
            return child.key == key && child.kind == NodeKind::Group;
        });
        // A mutation can leave a remembered group present but empty/disabled
        // (for example after removing the final preset layer). Stay in its
        // parent, whose rows remain valid for selection and rendering.
        if (found == node->children.end() || !found->enabled || found->children.empty()) break;
        node = &*found;
        ++valid;
    }
    path.resize(valid);
    return *node;
}
std::size_t remembered_row(const SettingNode& menu, App::SettingsMenuState& position,
                           const std::vector<std::string>& path, std::size_t fallback = 0) {
    const auto remembered = position.selectedEntries.find(path_key(path));
    if (remembered != position.selectedEntries.end()) {
        for (std::size_t i = 0; i < menu.children.size(); ++i)
            if (menu.children[i].key == remembered->second) return i;
    }
    auto row=std::min(fallback, menu.children.size() - 1);
    while(menu.children[row].kind==NodeKind::Separator) row=(row+1)%menu.children.size();
    return row;
}
std::string breadcrumb(const SettingNode& root, const std::vector<std::string>& path) {
    std::string text = root.title;
    const auto* node = &root;
    for (const auto& key : path) {
        const auto found = std::find_if(node->children.begin(), node->children.end(), [&](const auto& child) { return child.key == key; });
        if (found == node->children.end()) break;
        node = &*found;
        text += " > " + node->title;
    }
    return text;
}
void show_host_configuration(Runtime::GameContext& primary,Runtime::GameContext& displayed,const TerminalSettingsView& sourceView) {
    auto& network=*primary.network_component();
    const auto instance=network.roomInstance;
    std::optional<UserConfig::MultiplayerConfig> snapshot;
    SettingNode root{"host-config","Host configuration (read-only)"};
    root.children.push_back({"loading","Host configuration is loading. Please wait."});
    std::vector<std::string> path;std::size_t selected=0;
    std::uint64_t revision=~network.previewRevision;
    auto view=sourceView;view.set_read_only(true);
    NavigationInput input(primary);input.suppress_held_keys();
    bool dirty=true,confirmation=false;
    for(;;) {
        poll_server(primary);
        if(network.connection!=Network::ConnectionState::Connected || network.roomInstance!=instance ||
           !network.roomPolicy.forceRules || Network::is_room_host(network)) return;
        if(input.update(NavigationKey::Back,false)) return;
        if(revision!=network.previewRevision) {
            revision=network.previewRevision;confirmation=false;
            if(network.roomPreview.incoming.payload) {
                auto incoming=UserConfig::decode_multiplayer_config(*network.roomPreview.incoming.payload);
                if(incoming) {
                    snapshot=std::move(incoming);
                    snapshot->settings=UserConfig::multiplayer_config_view_settings(displayed.user_settings(),*snapshot);
                    root=Model::make_host_tree(snapshot->settings,snapshot->map);
                    dirty=true;
                }
            }
            dirty=true;
        }
        auto& menu=current_menu(root,path);selected=std::min(selected,menu.children.size()-1);
        if(dirty || view.terminal_size_changed()) {
            // Rebuild a settings tree rooted in the received host snapshot.
            // Local Network columns and navigation stay in the parent view.
            view.set_menu_path(root,path,selected);view.prepare_layout(menu.children.size(),breadcrumb(root,path));
            view.print_menu(menu,selected,breadcrumb(root,path));dirty=false;
            if(!network.roomPreview.incoming.payload) view.print_message("Host configuration is loading. Please wait.");
        }
        if(input.update(NavigationKey::Left,false) || input.update(NavigationKey::Backspace,false)) {
            if(path.empty()) return;
            const auto key=path.back();path.pop_back();auto& parent=current_menu(root,path);
            const auto found=std::find_if(parent.children.begin(),parent.children.end(),[&](const auto& child){return child.key==key;});
            selected=found==parent.children.end()?0:static_cast<std::size_t>(found-parent.children.begin());
            dirty=true;confirmation=false;continue;
        }
        const auto previous=selected;
        const bool up=input.update(NavigationKey::Up,true),down=input.update(NavigationKey::Down,true);
        if(up && input.active_vertical_key()==NavigationKey::Up) selected=(selected+menu.children.size()-1)%menu.children.size();
        if(down && input.active_vertical_key()==NavigationKey::Down) selected=(selected+1)%menu.children.size();
        if(input.update(NavigationKey::PageUp,true)) selected=selected>view.page_rows()?selected-view.page_rows():0;
        if(input.update(NavigationKey::PageDown,true)) selected=std::min(selected+view.page_rows(),menu.children.size()-1);
        while(menu.children[selected].kind==NodeKind::Separator)
            selected=(selected+menu.children.size()+(up?-1:1))%menu.children.size();
        if(previous!=selected) {dirty=true;confirmation=false;}
        const bool confirm=input.update(NavigationKey::Confirm,false),right=input.update(NavigationKey::Right,false);
        if(confirm || right) {
            const auto& node=menu.children[selected];
            if(node.key=="copy-host-config") {
                if(!snapshot || !network.roomPreview.incoming.payload) view.print_message("Host configuration is loading. Please wait.");
                else if(!confirmation || !confirm) {confirmation=true;print_action_confirmation(view,node);}
                else {
                    const bool copied=UserConfig::copy_multiplayer_config(displayed.user_settings(),*snapshot);
                    if(copied) {
                        UserConfig::Presets::record_edits(displayed.user_settings());
                        if(auto* app=primary.app_component()) app->qpContinuousCancellation=snapshot->continuousCancellation;
                    }
                    view.print_message(copied && persist(displayed)?"Host configuration copied. Changes saved.":"Could not save settings. Check the settings directory.");
                    confirmation=false;
                }
            } else if(node.kind==NodeKind::Group && !node.children.empty()) {
                path.push_back(node.key);selected=0;dirty=true;confirmation=false;
            } else view.print_message("This configuration is read-only.");
        }
        primary.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
    }
}
} // namespace

void initialize_local_versus_player_settings(Runtime::GameContext& context) {
    auto* app = context.app_component();
    if (!app || !app->primaryContext || !app->localPlayerTwoContext) return;

    Runtime::GameContext& first = *app->primaryContext;
    Runtime::GameContext& second = *app->localPlayerTwoContext;
    synchronize_shared_appearance(first);
    const std::string secondKeybindingPath = second.user_settings().keybindingPath;
    const std::string secondPersistencePath = second.user_settings().persistencePath;
    if (app->singlePlayerOwner == 1 && app->savedPlayerOneSettings) {
        second.user_settings() = first.user_settings();
        first.user_settings() = *app->savedPlayerOneSettings;
    } else if (!app->localVersusSettingsAvailable) {
        second.user_settings() = first.user_settings();
    }
    second.user_settings().keybindingPath = secondKeybindingPath;
    second.user_settings().persistencePath = secondPersistencePath;
    const auto firstMods=UserConfig::snapshot_quick_play_mods(first.user_settings());
    const auto secondMods=UserConfig::snapshot_quick_play_mods(second.user_settings());
    first.user_settings().gameMode = second.user_settings().gameMode = 6;
    UserConfig::reconcile_quick_play_mods(first.user_settings(),firstMods);
    UserConfig::reconcile_quick_play_mods(second.user_settings(),secondMods);
    if(first.user_settings().presetDefined) UserConfig::Presets::define(first.user_settings(),UserConfig::SettingId::GameMode);
    if(second.user_settings().presetDefined) UserConfig::Presets::define(second.user_settings(),UserConfig::SettingId::GameMode);
    app->localVersusSettingsAvailable = true;
    app->singlePlayerOwner = 0;
    persist(first);
    persist(second);
    if (!first.user_settings().persistencePath.empty()) {
        std::ofstream(std::filesystem::path(first.user_settings().persistencePath).parent_path() / "active_player") << 0;
    }

    Input::refresh_bindings(first.input_services(), first.user_settings().keyCodes, first.user_settings().additionalKeyCodes);
    Input::refresh_bindings(second.input_services(), second.user_settings().keyCodes, second.user_settings().additionalKeyCodes);
}

void adopt_single_player_settings(Runtime::GameContext& displayedContext) {
    auto* app = displayedContext.app_component();
    if (!app || !app->primaryContext) return;

    Runtime::GameContext& primary = *app->primaryContext;
    const bool fromSecond = &displayedContext == app->localPlayerTwoContext;
    synchronize_shared_appearance(displayedContext);
    Input::refresh_bindings(displayedContext.input_services(), displayedContext.user_settings().keyCodes, displayedContext.user_settings().additionalKeyCodes);
    if (fromSecond) {
        app->savedPlayerOneSettings = primary.user_settings();
        primary.user_settings() = displayedContext.user_settings();
    }
    app->singlePlayerOwner = fromSecond ? 1 : 0;
    app->localVersusSettingsAvailable = true;
    Input::refresh_bindings(primary.input_services(), primary.user_settings().keyCodes, primary.user_settings().additionalKeyCodes);
    primary.config = Runtime::normalize_user_config(
        primary.user_settings(), Runtime::selected_game_mode(primary.user_settings()));
    primary.config.quickPlay.continuousCancellation = app->qpContinuousCancellation;
    persist(primary);
    if (!primary.user_settings().persistencePath.empty()) {
        std::ofstream(std::filesystem::path(primary.user_settings().persistencePath).parent_path() / "active_player") << app->singlePlayerOwner;
    }

}

MenuResult run_map_menu(Runtime::GameContext& context) {
    auto* app=context.app_component();
    auto& primary=app && app->primaryContext ? *app->primaryContext : context;
    const bool local=local_versus_available(primary);
    const int player=local && app->localPlayerTwoContext==&context ? 1 : 0;
    const auto root=Model::make_tree(context.user_settings(),local,player);
    const auto found=std::find_if(root.children.begin(),root.children.end(),[](const auto& node){return node.key=="maps";});
    if(app && found!=root.children.end()) {
        app->settingsCategoryIndex=static_cast<std::size_t>(player)*root.children.size()+static_cast<std::size_t>(found-root.children.begin());
        auto& position=menu_position(context,app->settingsMenu);
        position.path={"maps"};position.selectedEntries["root/maps"]="map-disable";
    }
    return run_menu(context);
}

MenuResult run_network_menu(Runtime::GameContext& context) {
    auto* app = context.app_component();
    if (app) {
        auto& position = menu_position(context, app->settingsMenu);
        position.path = {"NETWORK"}; position.selectedEntries["root/NETWORK"] = "room-create";
    }
    return run_menu(context);
}

static MenuResult run_menu_impl(Runtime::GameContext& context, bool fromStart,PresetEditorSession* editor) {
    auto* app = context.app_component();
    if(app && app->replayPreview && !app->replayPreview->pending)app->replayPreview.reset();
    auto& primary = app && app->primaryContext ? *app->primaryContext : context;
    if(primary.user_settings().challengeReplay && !primary.user_settings().challengeReplay->pending)
        restore_challenge_replay(primary);
    synchronize_shared_appearance(primary);
    if(editor && local_versus_available(primary)) editor->hasPlayerTwo=true;
    Input::ControlInputScope controls(primary.input_component(),
        app && app->localPlayerTwoContext ? app->localPlayerTwoContext->input_component() : nullptr);
    App::SettingsMenuState fallback[2];
    App::SettingsMenuState bothPosition;
    std::size_t localSelected = 0;
    auto& encodedPosition = app ? app->settingsCategoryIndex : localSelected;
    // Every root contains Player, the root categories, and RESET.
    std::size_t stride = Model::make_tree(primary.user_settings(), local_versus_available(primary), 0).children.size();
    int player = local_versus_available(primary) ? static_cast<int>((encodedPosition / stride) % 2) : 0;
    auto target = [&]() -> Runtime::GameContext& { return player==1 ? *app->localPlayerTwoContext : primary; };
    std::vector<std::string> path;
    const auto make_root=[&] {
        auto tree=player==2 ? Model::make_both_tree(primary.user_settings(),app->localPlayerTwoContext->user_settings()) :
            Model::make_tree(target().user_settings(),local_versus_available(primary),player);
        if(player!=2 && app && app->localPlayerTwoContext) {
            for(auto& node:tree.children)if(node.key=="challenges")node=make_challenge_menu(target().user_settings(),challenge_partner_settings(target()));
        }
        if(primary.network_component() && !editor) add_room_entries(tree,*primary.network_component());
        return tree;
    };
    SettingNode root=make_root();
    const auto update_editor_title=[&] {
        if(editor) {
            root.title=UiText::tr("PRESET SETTINGS")+" / "+editor->preset.name+(editor->dirty(primary) ? " *" : "");
            auto& rename=root.children.front();rename.summary=editor->preset.name;
        }
    };
    update_editor_title();
    std::size_t selected = encodedPosition % stride;
    NavigationInput input(primary);
    bool settingsWasDown = action_pressed(target(), Input::Action::OpenSettings);
    std::set<int> suppressedGameKeys;
    bool confirmationPending = false;
    bool saveWasDown=editor && primary.input_services().key_down(preset_save_key());
    bool undefinedWasDown=editor && primary.input_services().key_down(preset_undefined_key());
    const auto surface = make_settings_surface();
    auto return_to_start = [&]() {
        if (app && !fromStart) {
            app->startScreenRequested = true;
            app->replayResume = {};
        }
        return MenuResult::StartScreen;
    };
    auto position = [&]() -> App::SettingsMenuState& {
        return player==2 ? bothPosition : menu_position(target(),fallback[player]);
    };
    auto remember = [&]() {
        auto& menu = current_menu(root, path);
        position().path = path;
        position().selectedEntries[path_key(path)] = menu.children[selected].key;
        std::size_t rootRow = selected;
        if (!path.empty()) {
            for (std::size_t i = 0; i < root.children.size(); ++i)
                if (root.children[i].key == path.front()) rootRow = i;
        }
        encodedPosition = static_cast<std::size_t>(player==1 ? 1 : 0) * root.children.size() + rootRow;
    };
    auto current_view = [&]() {
        TerminalSettingsView view(target(), local_versus_available(primary), player, fromStart, surface);
        view.set_menu_path(root, path, selected);
        const auto& menu = current_menu(root, path);
        view.prepare_layout(menu.children.size() - first_visible_row(selected), breadcrumb(root, path));
        return view;
    };
    auto redraw = [&](bool rebuild = false) {
        if (rebuild) root=make_root();
        update_editor_title();
        auto& menu = current_menu(root, path);
        selected = remembered_row(menu, position(), path, selected);
        remember();
        current_view().print_menu(menu, selected, breadcrumb(root, path));
    };
    const auto reconcile_modes=[&](Runtime::GameContext& displayed,bool wasLocal) {
        const bool nowLocal=local_versus_available(primary);
        if(!wasLocal && nowLocal) {
            initialize_local_versus_player_settings(primary);player=0;
            if(editor) editor->hasPlayerTwo=true;
        } else if(wasLocal && Runtime::selected_game_mode(displayed.user_settings())!=Runtime::GameMode::LocalVersus) {
            adopt_single_player_settings(displayed);player=0;
        }
    };
    const auto undefine_item=[&](const SettingItem& item) {
        const bool wasLocal=local_versus_available(primary);
        for(auto* owner:{item.owner,item.secondaryOwner}) {
            if(!owner) continue;
            const auto rebound=Model::rebind_item(item,*owner);
            for(const auto id:Model::item_fields(rebound)) UserConfig::Presets::undefine(*owner,id);
        }
        auto& displayed=target();refresh_runtime_bindings(displayed);
        if(item.secondaryOwner) refresh_runtime_bindings(*app->localPlayerTwoContext);
        synchronize_shared_appearance(displayed);reconcile_modes(displayed,wasLocal);
    };
    // Preserve explicit category redirects (for example, network recovery).
    // Otherwise resume the saved chain, trimming branches that no longer exist.
    auto& savedPosition = position();
    if (!savedPosition.path.empty() && savedPosition.path.front() == root.children[selected].key) {
        path = savedPosition.path;
        const auto& menu = current_menu(root,path);
        selected = remembered_row(menu,savedPosition,path);
    }
    remember();
    redraw();
    auto networkRevision=primary.network_component()?primary.network_component()->roomRevision:0;
    auto networkMessage=primary.network_component()?primary.network_component()->connectionMessage:std::string{};
    const auto switch_target=[&](int next) {
        const auto oldPath=path;
        const auto oldKey=current_menu(root,path).children[selected].key;
        remember();
        player=next;
        root=make_root();
        const auto full=Model::make_tree(target().user_settings(),true,player,true);
        const auto location=locate_target_selection(root,full,target().user_settings(),oldPath,oldKey);
        path=location.path; selected=location.selected;
        const auto& menu=current_menu(root,path);
        position().selectedEntries[path_key(path)]=menu.children[selected].key;
        confirmationPending=false;
        settingsWasDown=action_pressed(target(),Input::Action::OpenSettings,&suppressedGameKeys);
        redraw();
    };
    for (;;) {
        poll_server(primary);
        if(primary.network_component() && (networkRevision!=primary.network_component()->roomRevision ||
            networkMessage!=primary.network_component()->connectionMessage)) {
            networkRevision=primary.network_component()->roomRevision;
            networkMessage=primary.network_component()->connectionMessage;redraw(true);
        }
        if (input.update(NavigationKey::Back, false)) {
            remember();
            if(!editor || request_preset_exit(primary,current_view(),*editor)) return return_to_start();
            input.suppress_held_keys();current_view().invalidate();redraw();continue;
        }
        if(editor) {
            const bool saveDown=primary.input_services().key_down(preset_save_key());
            const bool undefinedDown=primary.input_services().key_down(preset_undefined_key());
            const bool savePressed=saveDown && !saveWasDown;
            const bool undefinedPressed=undefinedDown && !undefinedWasDown;
            saveWasDown=saveDown;undefinedWasDown=undefinedDown;
            if(savePressed) {
                const bool saved=editor->save(primary);redraw();
                current_view().print_message(saved ? "Preset saved." : "Could not save the preset.");continue;
            }
            if(undefinedPressed) {
                const auto& node=current_menu(root,path).children[selected];
                if(node.item && !node.item->bothReadOnly) {
                    undefine_item(*node.item);redraw(true);
                    current_view().print_message("Parameter restored to undefined.");
                } else if(node.item && node.item->bothReadOnly) current_view().print_message("Choose Player 1 or Player 2 to edit this item.");
                continue;
            }
        }
        for (auto it = suppressedGameKeys.begin(); it != suppressedGameKeys.end();) {
            if (!primary.input_services().key_down(*it)) it = suppressedGameKeys.erase(it);
            else ++it;
        }
        const bool tab=input.update(NavigationKey::Tab,false);
        if (local_versus_available(primary)) {
            if (input.is_down(NavigationKey::Tab)) suppressedGameKeys.insert(input.key_code(NavigationKey::Tab));
            if (tab) { switch_target((player+1)%3); continue; }
        }
        if (!editor && restart_pressed(primary, &suppressedGameKeys)) { remember(); return MenuResult::StartGame; }
        const bool settingsDown = action_pressed(target(), Input::Action::OpenSettings,&suppressedGameKeys);
        if (settingsDown && !settingsWasDown) {
            remember(); path.clear(); confirmationPending = false; redraw();
        }
        settingsWasDown = settingsDown;
        const bool left = input.update(NavigationKey::Left, false) || input.update(NavigationKey::Backspace, false);
        if (left) {
            const bool cancelled = confirmationPending;
            const auto kind = current_menu(root,path).children[selected].kind;
            confirmationPending = false;
            if (path.empty()) {
                if (cancelled) current_view().print_message(kind==NodeKind::CopyOtherPlayer || preset_action(kind) || map_action(kind) || current_menu(root,path).children[selected].confirmTwice ?
                    "Confirmation cancelled." : "RESET cancelled.");
            }
            else { remember(); path.pop_back(); redraw(); }
        }
        auto& menu = current_menu(root, path);
        auto view = current_view();
        if (view.terminal_size_changed()) {
            view.print_menu(menu,selected,breadcrumb(root,path));
            if (confirmationPending) print_action_confirmation(view,menu.children[selected]);
        }
        const bool up = input.update(NavigationKey::Up, true);
        const bool down = input.update(NavigationKey::Down, true);
        const bool pageUp = input.update(NavigationKey::PageUp, true);
        const bool pageDown = input.update(NavigationKey::PageDown, true);
        const auto previous = selected;
        if (up && input.active_vertical_key() == NavigationKey::Up)
            selected = (selected + menu.children.size() - 1) % menu.children.size();
        if (down && input.active_vertical_key() == NavigationKey::Down)
            selected = (selected + 1) % menu.children.size();
        if (pageUp) selected = selected > view.page_rows() ? selected - view.page_rows() : 0;
        if (pageDown) selected = std::min(selected + view.page_rows(), menu.children.size() - 1);
        while(menu.children[selected].kind==NodeKind::Separator)
            selected=(selected+menu.children.size()+(up || pageUp?-1:1))%menu.children.size();
        if (up || down || pageUp || pageDown) {
            if (confirmationPending) { confirmationPending = false; view.print_reset_confirmation(false); }
            remember();
            view.print_selection(menu, previous, selected, breadcrumb(root, path));
        }
        const bool right = input.update(NavigationKey::Right, false);
        const bool confirm = input.update(NavigationKey::Confirm, false);
        if (right || confirm) {
            auto& node = menu.children[selected];
            if(!node.enabled && node.kind==NodeKind::RoomAction) {
                view.print_message(primary.network_component() && primary.network_component()->connection==Network::ConnectionState::Connected?
                    "Only the room host can change this option.":"Connect to the server before choosing a room.");continue;
            }
            if (!node.enabled) { view.print_message(node.kind==NodeKind::ChallengeReplay || node.key=="challenges" || node.key=="empty" ? node.description : map_action(node.kind) ? "Could not read the map collection. Existing maps will not be overwritten." :
                node.kind==NodeKind::PresetNew ? "Could not read the preset collection. Existing presets will not be overwritten." : "No preset layers to change.");continue; }
            if (player==2 && ((node.item && node.item->bothReadOnly) || node.kind==NodeKind::Connect ||
                node.kind==NodeKind::Disconnect || node.kind==NodeKind::RoomAction || node.kind==NodeKind::CopyOtherPlayer || (node.kind==NodeKind::ResetSection &&
                    (node.section==UserConfig::Section::Controls || node.section==UserConfig::Section::Network)))) {
                view.print_message("Choose Player 1 or Player 2 to edit this item.");
                continue;
            }
            const bool resetting = node.kind == NodeKind::ResetSection || node.kind == NodeKind::ResetSubtree || node.kind == NodeKind::ResetGame;
            const bool preset=preset_action(node.kind);
            const bool presetLibraryOperation=node.kind==NodeKind::PresetNew || node.kind==NodeKind::PresetEdit || node.kind==NodeKind::PresetDelete;
            const bool copying=node.kind==NodeKind::CopyOtherPlayer;
            const bool mapOperation=map_action(node.kind);
            const bool needsConfirmation=(!editor && resetting) || preset || copying || node.confirmTwice;
            if (needsConfirmation && confirmationPending && right) {
                confirmationPending = false;
                view.print_message(resetting ? "RESET cancelled." : "Confirmation cancelled.");
                continue;
            }
            // Only a fresh Enter press confirms; Right requests or cancels confirmation.
            if (needsConfirmation && (!confirmationPending || !confirm)) {
                confirmationPending = true;
                print_action_confirmation(view,node);
                continue;
            }
            confirmationPending = false;
            remember();
            if (node.kind == NodeKind::Group) {
                if (node.key == "player") position().selectedEntries["root/player"] = player==2 ? "both" : player ? "player2" : "player1";
                const auto key=node.key;
                path.push_back(key); selected = 0; redraw();
            }
            else if(node.kind==NodeKind::ChallengeChange || node.kind==NodeKind::ChallengeRestore || node.kind==NodeKind::ChallengeStart) {
                auto& displayed=target();const bool wasLocal=local_versus_available(primary);
                // redraw(true) replaces the tree and invalidates node.
                const bool starting=node.kind==NodeKind::ChallengeStart;
                auto* other=app && app->localPlayerTwoContext?(&displayed==app->localPlayerTwoContext?app->primaryContext:app->localPlayerTwoContext):nullptr;
                auto* partner=challenge_partner_settings(displayed);
                bool applied=true;
                if(node.kind==NodeKind::ChallengeRestore)Challenges::restore_adjustment(displayed.user_settings(),partner);
                else {const auto definition=Challenges::lookup(node.challengeId);applied=definition && definition->change(displayed.user_settings(),partner);}
                if(!applied){redraw(true);continue;}
                if(starting && displayed.user_settings().challengeAdjustment)
                    displayed.user_settings().challengeAdjustment->explicitlyStarted=true;
                if(app && other && displayed.user_settings().challengeAdjustment && displayed.user_settings().challengeAdjustment->paired)
                    app->localVersusSettingsAvailable=true;
                refresh_runtime_bindings(displayed);bool saved=persist(displayed);
                if(partner){saved=UserConfig::save(*partner) && saved;if(other && partner==&other->user_settings())refresh_runtime_bindings(*other);}
                reconcile_modes(displayed,wasLocal);redraw(true);
                if(starting)return MenuResult::StartGame;
                current_view().print_message(saved?"Temporary challenge settings updated.":"Could not save settings. Check the settings directory.");
            }
            else if(node.kind==NodeKind::RecentReplay) {
                std::error_code error;
                if(!std::filesystem::is_regular_file(std::filesystem::u8path(node.replayPath),error) || error) {
                    redraw(true);current_view().print_message("The saved replay file no longer exists.");continue;
                }
                if(app) {app->replayPreview=ReplayPreviewRequest{node.replayPath,true};return MenuResult::StartGame;}
            }
            else if(node.kind==NodeKind::ChallengeReplay) {
                Challenges::Record record;record.replayPath=node.replayPath;
                if(!Challenges::replay_exists(record)){redraw(true);current_view().print_message("The saved replay file no longer exists.");continue;}
                auto& displayed=target();const bool wasLocal=local_versus_available(primary);
                auto& settings=displayed.user_settings();
                Challenges::restore_replay(settings);
                settings.challengeReplay=UserConfig::ChallengeReplayRestore{settings.gameMode,settings.replayPath,true,node.challengeId};
                settings.gameMode=5;settings.replayPath=record.replayPath;
                persist(displayed);reconcile_modes(displayed,wasLocal);return MenuResult::StartGame;
            }
            else if (node.kind == NodeKind::Player) {
                player = node.player;
                path.clear(); selected = 0;
                root=make_root();
                position().selectedEntries["root"] = "player";
                redraw();
                settingsWasDown = action_pressed(target(), Input::Action::OpenSettings);
            } else {
                auto& displayed = target();
                const bool wasLocal = local_versus_available(primary);
                const auto modsBefore=UserConfig::snapshot_quick_play_mods(displayed.user_settings());
                NavigationInput editorInput(primary);
                auto checkControls = [&]() {
                    poll_server(primary);
                    // Key capture accepts navigation and game keys as bindings.
                    const bool capturingKey = node.item && node.item->keyBinding;
                    const bool scalarInput = node.item && !capturingKey && node.item->choice.labels.empty();
                    const bool inlineInput = node.item && Model::uses_inline_input(*node.item);
                    if (wasLocal && !capturingKey && editorInput.is_down(NavigationKey::Tab))
                        suppressedGameKeys.insert(editorInput.key_code(NavigationKey::Tab));
                    // Scalar entry owns every key, including game shortcuts.
                    // Its reader commits Escape before the menu returns to Start.
                    if (scalarInput) {
                        const auto suppress = [&](Runtime::GameContext& source) {
                            for (const auto& [key, action] : source.input_services().bindings.entries())
                                if ((action == Input::Action::Retry || action == Input::Action::OpenSettings) &&
                                    source.input_services().key_down(key)) suppressedGameKeys.insert(key);
                        };
                        suppress(primary);
                        if (local_versus_available(primary)) suppress(*app->localPlayerTwoContext);
                        settingsWasDown = action_pressed(displayed, Input::Action::OpenSettings,&suppressedGameKeys);
                        return;
                    }
                    const bool reservedEscape=capturingKey &&
                        (node.item->id==Model::SettingId::ControlRetry || node.item->id==Model::SettingId::ControlOpenSettings);
                    if ((!capturingKey || reservedEscape) && editorInput.update(NavigationKey::Back, false))
                        throw editor ? MenuExit::CancelEdit : MenuExit::StartScreen;
                    if(editor && !capturingKey) {
                        const bool saveDown=primary.input_services().key_down(preset_save_key());
                        const bool undefDown=primary.input_services().key_down(preset_undefined_key());
                        if(saveDown && !saveWasDown) view.print_message(editor->save(primary) ? "Preset saved." : "Could not save the preset.");
                        saveWasDown=saveDown;
                        if(undefDown && !undefinedWasDown && node.item) {
                            undefine_item(*node.item);
                            undefinedWasDown=undefDown;throw MenuExit::CancelEdit;
                        }
                        undefinedWasDown=undefDown;
                    }
                    if (!editor && !capturingKey && restart_pressed(primary, &suppressedGameKeys)) throw MenuExit::Restart;
                    // Inline cursor and deletion keys are handled by stdin.
                    if (node.item && !capturingKey &&
                        ((!inlineInput && editorInput.update(NavigationKey::Left, false)) ||
                         (!scalarInput && editorInput.update(NavigationKey::Backspace, false))))
                        throw MenuExit::CancelEdit;
                    const bool settingsDown = action_pressed(displayed, Input::Action::OpenSettings,&suppressedGameKeys);
                    const bool pressed = settingsDown && !settingsWasDown;
                    settingsWasDown = settingsDown;
                    if (!capturingKey && pressed) throw MenuExit::Root;
                };
                if(editor && resetting) {
                    const bool changed=reset_preset_entry(displayed,view,node,*editor,player==2);
                    input.suppress_held_keys();current_view().invalidate();synchronize_shared_appearance(displayed);reconcile_modes(displayed,wasLocal);redraw(true);
                    current_view().print_message(changed ? "RESET complete. Preset draft updated." : "RESET cancelled.");continue;
                }
                bool valid = true, saved = true, savedByEscape = false;
                std::string actionMessage;
                try {
                    if(node.kind==NodeKind::PresetNew) {
                        run_new_preset(displayed);input.suppress_held_keys();current_view().invalidate();
                        actionMessage="Preset editor closed.";
                    } else if(node.kind==NodeKind::PresetEdit) {
                        valid=run_saved_preset(displayed,node.presetId,editor);input.suppress_held_keys();current_view().invalidate();
                        actionMessage="Preset editor closed.";
                    } else if(node.kind==NodeKind::PresetDelete) {
                        valid=UserConfig::Presets::remove_saved(node.presetId);
                        if(valid && !path.empty()) path.pop_back();
                        actionMessage="Preset deleted.";
                    } else if(editor && node.kind==NodeKind::PresetRename) {
                        valid=rename_preset(displayed,view,*editor);
                    } else if(editor && node.kind==NodeKind::PresetSave) {
                        saved=editor->save(primary);actionMessage="Preset saved.";
                    } else if(mapOperation) {
                        const auto mapId=node.mapId;
                        if(node.kind==NodeKind::MapNew || node.kind==NodeKind::MapEdit) {
                            auto map=node.kind==NodeKind::MapNew ? std::optional<Runtime::MapDefinition>{Runtime::MapDefinition{}} : UserConfig::Maps::find(mapId);
                            valid=map.has_value();
                            if(map) App::run_map_editor(displayed,*map);
                            input.suppress_held_keys();current_view().invalidate();
                            actionMessage="Map editor closed.";
                        } else if(node.kind==NodeKind::MapDelete) {
                            UserConfig::Maps::IdChanges changes;
                            std::vector<Runtime::GameContext*> sharedOwners;
                            for(auto* session=editor;session;session=session->parent) sharedOwners.push_back(session->sourceContext);
                            valid=delete_map(displayed,mapId,saved,sharedOwners,&changes);
                            if(valid) for(auto* session=editor;session;session=session->parent) {
                                UserConfig::Maps::remap_selection(session->original,changes);
                                if(session->originalTwo) UserConfig::Maps::remap_selection(*session->originalTwo,changes);
                            }
                            if(valid) {
                                if(!path.empty()) path.pop_back();
                            }
                            actionMessage="Map deleted.";
                        } else {
                            const auto map=UserConfig::Maps::find(mapId);
                            valid=node.kind==NodeKind::MapDisable || map.has_value();
                            if(valid) {
                                const auto selectMap=[&](UserConfig::Settings& settings) {
                                    using Id=Model::SettingId;
                                    const int previousGoal=settings.goal;
                                    const auto* selected=node.kind==NodeKind::MapDisable ? nullptr : &*map;
                                    UserConfig::Maps::select(settings,selected);
                                    for(const auto id:{Id::MapId,Id::MapRotationSystem,Id::MapGoal}) UserConfig::Presets::define(settings,id);
                                    if((selected && selected->goal!=Runtime::MapGoal::Disabled) || previousGoal!=settings.goal)
                                        UserConfig::Presets::define(settings,Id::Goal);
                                    if(selected && selected->goal==Runtime::MapGoal::AllClear)
                                        UserConfig::Presets::define(settings,Id::AllClearTarget);
                                };
                                selectMap(displayed.user_settings());
                                if(player==2) {
                                    auto& other=app->localPlayerTwoContext->user_settings();
                                    selectMap(other);
                                    saved=persist(*app->localPlayerTwoContext) && saved;
                                }
                                if(node.kind==NodeKind::MapDisable) {
                                    // Maps only override runtime configuration; the player's
                                    // ordinary field, generator and modifier settings are kept.
                                    const auto restore=[](Runtime::GameContext& owner) {
                                        if(owner.config.mode.replayMode) return;
                                        const bool continuous=owner.app_component() ? owner.app_component()->qpContinuousCancellation : owner.config.quickPlay.continuousCancellation;
                                        owner.config=Runtime::normalize_user_config(owner.user_settings(),Runtime::selected_game_mode(owner.user_settings()));
                                        owner.config.quickPlay.continuousCancellation=continuous;
                                    };
                                    restore(displayed);
                                    if(player==2) restore(*app->localPlayerTwoContext);
                                }
                            }
                            actionMessage="Map selection saved.";
                        }
                    } else if (preset) {
                        const auto savedPreset=node.kind==NodeKind::PresetApply ? UserConfig::Presets::find(node.presetId) : std::nullopt;
                        const bool localPreset=savedPreset && UserConfig::Presets::is_local_versus(*savedPreset);
                        const auto applyAction=[&](UserConfig::Settings& user,bool keepMode,bool playerTwo) {
                            if(node.kind==NodeKind::PresetApply)
                                return savedPreset && UserConfig::Presets::apply(user,*savedPreset,keepMode,playerTwo);
                            return apply_preset_action(user,node,keepMode,playerTwo);
                        };
                        auto first=displayed.user_settings();
                        valid=(!localPreset || (app && app->localPlayerTwoContext)) && applyAction(first,wasLocal,is_player_two(displayed));
                        if(valid && localPreset) {
                            first.gameMode=6;
                            if(first.presetDefined) UserConfig::Presets::define(first,Model::SettingId::GameMode);
                        }
                        const bool enteringLocal=!wasLocal && app && app->localPlayerTwoContext &&
                            Runtime::selected_game_mode(first)==Runtime::GameMode::LocalVersus;
                        std::optional<UserConfig::Settings> restoredFirst;
                        if(enteringLocal && app->singlePlayerOwner==1 && app->savedPlayerOneSettings) {
                            restoredFirst=*app->savedPlayerOneSettings;restoredFirst->gameMode=6;
                            if(restoredFirst->presetDefined) UserConfig::Presets::define(*restoredFirst,Model::SettingId::GameMode);
                            valid=applyAction(*restoredFirst,true,false) && valid;
                        }
                        if (player==2 || (localPreset && app && app->localPlayerTwoContext) || enteringLocal) {
                            auto& other=wasLocal && &displayed==app->localPlayerTwoContext ? primary : *app->localPlayerTwoContext;
                            auto second=other.user_settings();
                            if(enteringLocal) {
                                if(!app->localVersusSettingsAvailable) UserConfig::copy_settings_except_bindings(second,displayed.user_settings());
                                second.gameMode=6;
                                if(second.presetDefined) UserConfig::Presets::define(second,Model::SettingId::GameMode);
                            }
                            valid=applyAction(second,true,&other==app->localPlayerTwoContext) && valid;
                            if(valid) {
                                other.user_settings()=std::move(second);
                                if(restoredFirst) app->savedPlayerOneSettings=std::move(restoredFirst);
                                if(enteringLocal) app->localVersusSettingsAvailable=true;
                                refresh_runtime_bindings(other);
                            }
                        }
                        if(valid) {displayed.user_settings()=std::move(first);refresh_runtime_bindings(displayed);synchronize_shared_appearance(displayed);}
                        if(valid && player==2) refresh_runtime_bindings(*app->localPlayerTwoContext);
                        if(valid && node.kind==NodeKind::PresetRemove && !path.empty()) path.pop_back();
                    } else if (copying) {
                        auto* other=wasLocal ? (&displayed==app->primaryContext ? app->localPlayerTwoContext : app->primaryContext) : nullptr;
                        valid=other!=nullptr;
                        if(valid) UserConfig::copy_settings_except_bindings(displayed.user_settings(),other->user_settings());
                    } else if (node.item) valid = edit_item(displayed, view, *node.item, breadcrumb(root, path), checkControls, savedByEscape);
                    else if (node.kind == NodeKind::ResetSection) {
                        UserConfig::reset_section(displayed.user_settings(), node.section, is_player_two(displayed));
                        Input::refresh_bindings(displayed.input_services(), displayed.user_settings().keyCodes, displayed.user_settings().additionalKeyCodes);
                    } else if (node.kind == NodeKind::ResetSubtree) {
                        Model::reset_subtree(displayed.user_settings(), node, is_player_two(displayed));
                        Input::refresh_bindings(displayed.input_services(), displayed.user_settings().keyCodes, displayed.user_settings().additionalKeyCodes);
                    } else if (node.kind == NodeKind::ResetGame) {
                        UserConfig::reset_settings(displayed.user_settings(), is_player_two(displayed));
                        path.clear();
                    } else if(editor && (node.kind==NodeKind::Connect || node.kind==NodeKind::Disconnect)) {
                        displayed.user_settings().gameMode=node.kind==NodeKind::Connect ? 4 : 0;
                        UserConfig::Presets::define(displayed.user_settings(),Model::SettingId::GameMode);
                        if(primary.network_component()) {
                            if(node.kind==NodeKind::Connect) Network::initialize_network_services(primary,displayed.user_settings().serverIp);
                            else Network::disconnect_online(*primary.network_component());
                        }
                    } else if (node.kind == NodeKind::Connect && primary.network_component()) {
                        displayed.user_settings().gameMode = 4;
                        UserConfig::reconcile_quick_play_mods(displayed.user_settings(),modsBefore);
                        Network::initialize_network_services(primary, displayed.user_settings().serverIp);
                    } else if (node.kind == NodeKind::RoomAction && primary.network_component() && !editor) {
                        const bool dialog=node.key=="room-create" || node.key=="room-join" || node.key=="room-list" || node.key=="room-users";
                        if (dialog?show_rooms(primary,displayed,view,checkControls,node.key):room_action(primary,displayed,view,checkControls,node)) {
                            displayed.user_settings().gameMode = 4; persist(displayed);
                            if (wasLocal) adopt_single_player_settings(displayed);
                            return MenuResult::StartGame;
                        }
                    } else if (node.kind == NodeKind::Disconnect && primary.network_component()) {
                        Network::disconnect_online(*primary.network_component());
                        if (displayed.user_settings().gameMode == 4) displayed.user_settings().gameMode = 0;
                    }
                    if (valid && player==2 && resetting) {
                        auto& other=*app->localPlayerTwoContext;
                        if (node.kind==NodeKind::ResetSection)
                            UserConfig::reset_section(other.user_settings(),node.section,true);
                        else if (node.kind==NodeKind::ResetSubtree)
                            Model::reset_subtree(other.user_settings(),node,true);
                        else if (node.kind==NodeKind::ResetGame)
                            UserConfig::reset_settings(other.user_settings(),true);
                        refresh_runtime_bindings(other);
                    }
                    if (valid && !presetLibraryOperation) {
                        // Individual choices/presets handle their own transitions.
                        // Cover copies, resets and direct connection mode changes too.
                        if (!node.item && !preset) UserConfig::reconcile_quick_play_mods(displayed.user_settings(),modsBefore);
                        UserConfig::Presets::record_edits(displayed.user_settings());
                        if(player==2) UserConfig::Presets::record_edits(app->localPlayerTwoContext->user_settings());
                        const bool appearanceChanged = preset || copying || (node.item &&
                            UserConfig::setting_descriptor(node.item->id).section==UserConfig::Section::Others) ||
                            (node.kind == NodeKind::ResetSection && node.section == UserConfig::Section::Others) ||
                            (node.kind == NodeKind::ResetSubtree && std::any_of(node.resetFields.begin(),node.resetFields.end(),
                                [](auto id) { return UserConfig::setting_descriptor(id).section==UserConfig::Section::Others; }));
                        saved = (appearanceChanged ? synchronize_shared_appearance(displayed,true) : persist(displayed)) && saved;
                        if (!appearanceChanged && player==2 &&
                            (preset || resetting || (node.item && node.item->secondaryOwner)))
                            saved=persist(*app->localPlayerTwoContext) && saved;
                    }
                } catch (MenuExit result) {
                    input.suppress_held_keys();
                    if (result == MenuExit::Restart) return MenuResult::StartGame;
                    if (result == MenuExit::StartScreen) return return_to_start();
                    if (result == MenuExit::Root) path.clear();
                    redraw(editor!=nullptr);
                    continue;
                }
                input.suppress_held_keys();
                displayed.config.display.obscuredColors = std::clamp(displayed.user_settings().obscuredColors, 0, 1) != 0;
                reconcile_modes(displayed,wasLocal);
                const auto actionKind=node.kind;
                redraw(true);
                if (!valid) current_view().print_message(mapOperation ? "Map action failed; existing data is unchanged." :
                    presetLibraryOperation || preset ? "Preset action failed; existing data is unchanged." : "Invalid value; the previous value is unchanged.");
                else if (!saved) current_view().print_message(editor ? "Could not save the preset." : "Could not save settings. Check the settings directory.");
                else if (editor && actionKind==NodeKind::PresetSave) current_view().print_message(actionMessage);
                else if (resetting) current_view().print_message("RESET complete. Changes saved.");
                else if (preset) current_view().print_message(editor ? "Preset draft updated." : "Preset changes saved.");
                else if (copying) current_view().print_message("Other player's settings copied. Changes saved.");
                else if (mapOperation) current_view().print_message(actionMessage);
                else if (presetLibraryOperation) current_view().print_message(actionMessage);
                if (savedByEscape) return return_to_start();
            }
        }
        primary.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}
MenuResult run_menu(Runtime::GameContext& context,bool fromStart) {return run_menu_impl(context,fromStart,nullptr);}
MenuResult run_preset_menu(Runtime::GameContext& context,PresetEditorSession& editor) {return run_menu_impl(context,false,&editor);}
} // namespace App::Settings
