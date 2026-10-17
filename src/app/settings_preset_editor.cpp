#include "settings_preset_editor.h"
#include "settings_input.h"
#include "settings_navigation.h"
#include "ui_text.h"
#include "tetr/app/application_state.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/runtime/game_context.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif

namespace App::Settings {
int preset_save_key() {
#ifdef _WIN32
    return 'S';
#else
    return KEY_S;
#endif
}
int preset_undefined_key() {
#ifdef _WIN32
    return 'U';
#else
    return KEY_U;
#endif
}
namespace {
struct Drafts {const UserConfig::Settings* first;const UserConfig::Settings* second;};
Drafts drafts(const Runtime::GameContext& context,bool hasPlayerTwo) {
    const auto* app=context.app_component();
    const auto* first=app && app->primaryContext ? &app->primaryContext->user_settings() : &context.user_settings();
    const UserConfig::Settings* second=nullptr;
    if(app && app->singlePlayerOwner==1 && app->savedPlayerOneSettings) {
        if(!hasPlayerTwo) return {first,nullptr};
        second=first;first=&*app->savedPlayerOneSettings;
    } else if(app && app->localPlayerTwoContext && app->localPlayerTwoContext->user_settings().presetDefined)
        second=&app->localPlayerTwoContext->user_settings();
    return {first,second};
}
std::optional<UserConfig::PresetFields> second_fields(const Drafts& users,bool hasPlayerTwo) {
    if(!users.second || !users.second->presetDefined || !hasPlayerTwo) return {};
    return UserConfig::Presets::draft_fields(*users.second);
}
struct Snapshot {UserConfig::PresetFields first;std::optional<UserConfig::PresetFields> second;};
Snapshot snapshot(const Runtime::GameContext& context,bool hasPlayerTwo) {
    const auto users=drafts(context,hasPlayerTwo);
    Snapshot result{UserConfig::Presets::draft_fields(*users.first),second_fields(users,hasPlayerTwo)};
    if(result.second) {
        // After P2 leaves Local Versus, P1 is parked in the recovery snapshot.
        // Mode belongs to the active match, so saving must use the displayed
        // single-player mode rather than the parked profile's old Local mode.
        const auto* app=context.app_component();
        const auto& active=app && app->primaryContext ? app->primaryContext->user_settings() : context.user_settings();
        for(auto* fields:{&result.first,&*result.second}) {
            fields->erase(UserConfig::SettingId::GameMode);
            if(UserConfig::Presets::defined(active,UserConfig::SettingId::GameMode))
                (*fields)[UserConfig::SettingId::GameMode]=active.gameMode;
        }
    }
    return result;
}
}
bool PresetEditorSession::dirty(const Runtime::GameContext& context) const {
    const auto values=snapshot(context,hasPlayerTwo);
    return preset.name!=baselineName || values.first!=baseline || values.second!=baselineTwo;
}
bool PresetEditorSession::save(const Runtime::GameContext& context) {
    const auto values=snapshot(context,hasPlayerTwo);
    auto saved=preset;saved.fields=values.first;saved.playerTwoFields=values.second;
    if(!UserConfig::Presets::save(saved)) return false;
    preset=std::move(saved);baseline=preset.fields;baselineTwo=preset.playerTwoFields;baselineName=preset.name;return true;
}
namespace {
void run_editor(Runtime::GameContext& source,UserConfig::Presets::SavedPreset preset,bool existing,PresetEditorSession* parent) {
    PresetEditorSession editor;editor.sourceContext=&source;editor.parent=parent;editor.original=source.user_settings();
    UserConfig::Settings second;second.keyCodes=UserConfig::default_player_two_key_codes();second.presetDefined.emplace();second.keybindingPath.clear();
    editor.originalTwo=UserConfig::Settings{};editor.originalTwo->keyCodes=UserConfig::default_player_two_key_codes();
    if(const auto* owner=source.app_component()) {
        editor.playerTwo=owner->localPlayerTwoContext==&source || owner->singlePlayerOwner==1;
        if(owner->singlePlayerOwner==1 && owner->savedPlayerOneSettings) {
            editor.original=*owner->savedPlayerOneSettings;
            if(owner->primaryContext) editor.originalTwo=owner->primaryContext->user_settings();
        } else {
            if(owner->primaryContext) editor.original=owner->primaryContext->user_settings();
            if(owner->localPlayerTwoContext) editor.originalTwo=owner->localPlayerTwoContext->user_settings();
        }
    }
    if(parent) {editor.original=parent->original;editor.originalTwo=parent->originalTwo;}
    editor.preset=std::move(preset);editor.baselineName=editor.preset.name;
    UserConfig::Settings user;user.keyCodes=UserConfig::default_key_codes();user.presetDefined.emplace();
    // UI language and color are presentation fallbacks, still undefined in the
    // saved preset until the author explicitly assigns those variables.
    user.uiLanguage=source.user_settings().uiLanguage;user.obscuredColors=source.user_settings().obscuredColors;
    user.continueInputUnfocused=source.user_settings().continueInputUnfocused;
    user.keybindingPath.clear();second.uiLanguage=user.uiLanguage;second.obscuredColors=user.obscuredColors;
    second.continueInputUnfocused=user.continueInputUnfocused;
    if(existing) {
        const auto populate=[](UserConfig::Settings& target,const UserConfig::PresetFields& fields) {
            for(const auto& [id,value]:fields) {
                UserConfig::Presets::write_value(target,id,value);UserConfig::Presets::define(target,id);
            }
        };
        populate(user,editor.preset.fields);
        populate(second,editor.preset.playerTwoFields ? *editor.preset.playerTwoFields : editor.preset.fields);
        editor.hasPlayerTwo=editor.preset.playerTwoFields.has_value() || user.gameMode==6;
        // Saved records always open with their logical P1 table. New records
        // created by P2 still start with that player's independent draft.
        if(user.gameMode==6) second.gameMode=6;
    }
    Input::Services input;Input::share_key_state(input,source.input_services());
    Input::refresh_bindings(input,user.keyCodes,user.additionalKeyCodes);
    Input::Services secondInput;Input::share_key_state(secondInput,source.input_services());
    Input::refresh_bindings(secondInput,second.keyCodes,second.additionalKeyCodes);
    Network::NetworkServices network;
    struct Disconnect {
        Network::NetworkServices& network;
        ~Disconnect(){Network::disconnect_online(network);}
    } disconnect{network};
    App::ApplicationState app;
    if(source.app_component()) app.textInputSession=source.app_component()->textInputSession;
    Runtime::GameContext context(source.session,source.services);
    context.config=source.config;context.attach_user_settings(user);context.attach_input(input);context.attach_app(app);
    context.attach_network(network);
    Runtime::GameContext secondContext(source.session,source.services);
    secondContext.config=source.config;secondContext.attach_user_settings(second);secondContext.attach_input(secondInput);secondContext.attach_app(app);
    app.primaryContext=&context;app.localPlayerTwoContext=&secondContext;app.localVersusSettingsAvailable=true;
    if(editor.playerTwo && !existing) {
        app.savedPlayerOneSettings=user;user=second;app.singlePlayerOwner=1;
        refresh_runtime_bindings(context);
    }
    if(source.output_mutex()) context.attach_output_mutex(*source.output_mutex());
    struct RestoreLanguage {
        UiText::Language language=UiText::language();
        ~RestoreLanguage(){UiText::set_language(language);}
    } restore;
    synchronize_shared_appearance(context);
    const auto initial=snapshot(context,editor.hasPlayerTwo);
    editor.baseline=initial.first;editor.baselineTwo=initial.second;
    run_preset_menu(context,editor);
}
}
void run_new_preset(Runtime::GameContext& source) {
    UserConfig::Presets::Collection collection;
    if(!UserConfig::Presets::load(collection)) return;
    UserConfig::Presets::SavedPreset preset;preset.name=UserConfig::Presets::default_name(collection);
    run_editor(source,std::move(preset),false,nullptr);
}
bool run_saved_preset(Runtime::GameContext& source,int id,PresetEditorSession* parent) {
    auto preset=UserConfig::Presets::find(id);if(!preset) return false;
    run_editor(source,std::move(*preset),true,parent);return true;
}
namespace {
int dialog(Runtime::GameContext& context,const TerminalSettingsView& view,const std::string& title,
    std::vector<std::string> labels,const std::string& hint={}) {
    UserConfig::Settings values;int selected=0;
    Model::SettingItem item{UserConfig::SettingId::UiLanguage,title,std::ref(values.uiLanguage),{},false,false,&values};
    item.choice.labels=std::move(labels);
    for(std::size_t i=0;i<item.choice.labels.size();++i) item.choice.values.push_back(static_cast<int>(i));
    const auto draw=[&] {view.print_choices(item,selected,"PRESET SETTINGS");if(!hint.empty()) view.print_message(hint);};
    NavigationInput input(context);draw();
    for(;;) {
        if(input.update(NavigationKey::Back,false) || input.update(NavigationKey::Left,false) || input.update(NavigationKey::Backspace,false)) return -1;
        const bool up=input.update(NavigationKey::Up,true),down=input.update(NavigationKey::Down,true);
        if(up && input.active_vertical_key()==NavigationKey::Up) selected=(selected+item.choice.values.size()-1)%item.choice.values.size();
        if(down && input.active_vertical_key()==NavigationKey::Down) selected=(selected+1)%item.choice.values.size();
        if(up || down || view.terminal_size_changed()) draw();
        const bool right=input.update(NavigationKey::Right,false);
        if(input.update(NavigationKey::Confirm,false) || right) return selected;
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
    }
}
}
bool request_preset_exit(Runtime::GameContext& context,const TerminalSettingsView& view,PresetEditorSession& editor) {
    if(!editor.dirty(context)) return true;
    std::string message="Save before exiting?";
    for(;;) {
        const int choice=dialog(context,view,"Unsaved changes",{"Save and exit","Discard and exit","Keep editing"},message);
        if(choice==1) return true;
        if(choice!=0) return false;
        if(editor.save(context)) return true;
        message="Could not save the preset. Choose another option or try again.";
    }
}
bool reset_preset_entry(Runtime::GameContext& context,const TerminalSettingsView& view,
    const Model::SettingNode& node,const PresetEditorSession& editor,bool both) {
    const int choice=dialog(context,view,"Reset this entry",{"All undefined","Use original player settings","Use default settings","Cancel reset"});
    if(choice<0 || choice==3) return false;
    const auto reset=[&](Runtime::GameContext& target) {
        const auto* app=target.app_component();
        const bool second=app && (app->localPlayerTwoContext==&target || app->singlePlayerOwner==1);
        UserConfig::Settings defaults;defaults.keyCodes=second ? UserConfig::default_player_two_key_codes() : UserConfig::default_key_codes();
        const auto& original=second && editor.originalTwo ? *editor.originalTwo : editor.original;
        Model::reset_draft(target.user_settings(),node,choice==0 ? nullptr : choice==1 ? &original : &defaults);
        refresh_runtime_bindings(target);
    };
    reset(context);
    if(both && context.app_component() && context.app_component()->localPlayerTwoContext)
        reset(*context.app_component()->localPlayerTwoContext);
    return true;
}
bool rename_preset(Runtime::GameContext& context,const TerminalSettingsView& view,PresetEditorSession& editor) {
    UserConfig::Settings values;values.replayPath=editor.preset.name;
    Model::SettingItem item{UserConfig::SettingId::ReplayPath,"Rename preset",std::ref(values.replayPath),{},false,false,&values};
    item.hint="Enter confirms the preset name. Esc cancels.";
    bool cancelled=false;
    const auto entered=read_scalar_text(context,[]{},[&](const auto& text,std::size_t caret){view.print_edit(item,"PRESET SETTINGS",text,caret);},&cancelled);
    if(cancelled || entered.empty()) return true;
    if(entered.size()>256 || entered.find_first_of("\r\n")!=std::string::npos) return false;
    editor.preset.name=entered;return true;
}
}
