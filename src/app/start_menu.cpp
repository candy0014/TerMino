#include "tetr/app/start_menu.h"
#include "tetr/app/settings_menu.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/app/application_state.h"
#include "tetr/util/terminal_text.h"
#include "settings_navigation.h"
#include "terminal_canvas.h"
#include "tetr/frontend/ui_surface.h"
#include "ui_text.h"
#include <algorithm>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif

namespace App {
namespace {
void line(const Runtime::GameContext& context, int row, const std::string& text,
          Render::ColorRole color, Render::TerminalSize size) {
    if (row < 0 || row >= size.rows) return;
    const auto clipped = Util::TerminalText::clip(text, std::max(1, size.columns - 2));
    const int column = (size.columns - static_cast<int>(Util::TerminalText::width(clipped))) / 2;
    const bool obscured = context.user_settings().obscuredColors != 0;
    Render::set_ui_color(obscured ? Render::ColorRole::Ghost : color, obscured);
    Render::move_cursor(row, column);
    std::cout << clipped;
}
Runtime::GameContext* other_player(const Runtime::GameContext& context) {
    const auto* app = context.app_component();
    if (!app || Runtime::selected_game_mode(context.user_settings()) != Runtime::GameMode::LocalVersus) return nullptr;
    return app->primaryContext == &context ? app->localPlayerTwoContext : app->primaryContext;
}
bool key_down(Runtime::GameContext& context, std::size_t binding) {
    for (const int key : UserConfig::control_keys(context.user_settings(), binding))
        if (context.input_services().key_down(key)) return true;
    auto* other = other_player(context);
    if (other)
        for (const int key : UserConfig::control_keys(other->user_settings(), binding))
            if (other->input_services().key_down(key)) return true;
    return false;
}
std::string binding_label(const Runtime::GameContext& context, std::size_t binding) {
    auto keys = UserConfig::control_keys(context.user_settings(), binding);
    if (const auto* other = other_player(context))
        for (const int key : UserConfig::control_keys(other->user_settings(), binding))
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    std::string label;
    for (const int key : keys) {
        if (!label.empty()) label += " / ";
        label += UiText::key_label(key);
    }
    return "[" + (label.empty() ? UiText::tr("Unbound") : label) + "]";
}
}
void draw_start_menu(const Runtime::GameContext& context,std::optional<Render::TerminalSize> requestedSize) {
    using UiText::tr;
    if(Frontend::ui_surface_active()) {
        TerminalCanvas frame;const auto size=Frontend::ui_surface_size().value_or(Frontend::UiSize{128,38});
        frame.columnsLimit=size.first;frame.rowsLimit=size.second;
        const int x=std::max(2,(size.first-64)/2);
        const TerminalCanvas::Color fg{{224,238,255}},bg{{9,17,28}},accent{{58,222,240}},selected{{14,48,60}};
        frame.box(4,x,60,24,"TerMino / OPENGL",accent,fg,bg);
        frame.write(7,x+4,tr("Start")+" / "+context.user_settings().username,54,fg,bg);
        const auto* app=context.app_component();
        const int selection=app?app->startMenuSelection:0;
        frame.metadata={Frontend::UiKind::Start,"start","TerMino",{},{{"play","Start"},{"settings","Settings"},{"quit","Quit"}},static_cast<std::size_t>(selection)};
        if(app)frame.metadata.notice=app->startMenuNotice;
        frame.metadata.chinese=UiText::language()!=UiText::Language::English;
        frame.metadata.context=context.user_settings().username;
        const auto modes=UserConfig::choice_options(UserConfig::ChoiceKind::GameMode);
        const auto mode=std::find(modes.values.begin(),modes.values.end(),context.user_settings().gameMode);
        if(mode!=modes.values.end())frame.metadata.currentValue=tr(modes.labels[mode-modes.values.begin()]);
        for(auto& entry:frame.metadata.entries)entry.label=tr(entry.label);
        // Display the same current bindings used by the terminal and controller,
        // including alternate keys and the second player in local versus.
        frame.metadata.entries[0].value=binding_label(context,8);
        frame.metadata.entries[1].value=binding_label(context,9);
        frame.metadata.entries[2].value="[Esc]";
        const char* labels[]={"Start","Settings","Quit"};
        for(int i=0;i<3;++i) frame.write(12+i*4,x+4,(selection==i?">  ":"   ")+tr(labels[i]),52,
            selection==i?accent:fg,selection==i?selected:bg);
        frame.write(26,x+4,"Tab / Shift+Tab / Up / Down / Enter / Esc",54,accent,bg);
        frame.present({},false);return;
    }
    Render::OutputGuard output(context);
    Render::clear_screen();
    constexpr const char* logo[] = {
        R"( _______           __  __ _             )",
        R"(|__   __|         |  \/  (_)            )",
        R"(   | | ___ _ __   | \  / |_ _ __   ___  )",
        R"(   | |/ _ \ '__|  | |\/| | | '_ \ / _ \ )",
        R"(   | |  __/ |     | |  | | | | | | (_) |)",
        R"(   |_|\___|_|     |_|  |_|_|_| |_|\___/ )"
    };
    const auto actions = binding_label(context, 8) + " " + tr("Start") + "   " +
        binding_label(context, 9) + " " + tr("Settings") + "   [Esc] " + tr("Quit");
    const auto actionWidth = Util::TerminalText::width(actions);
    const auto size = requestedSize.value_or(Render::terminal_size().value_or(Render::TerminalSize{
        static_cast<int>(std::max(std::size_t{52}, actionWidth)) + 8, 14}));
    const int top = std::max(0, (size.rows - 10) / 2);
    for (int row = 0; row < 6; ++row)
        line(context, top + row, logo[row], Render::ColorRole::Orange, size);
    line(context, top + 7, kGameName, Render::ColorRole::Orange, size);
    line(context, top + 9, actions, Render::ColorRole::Accent, size);
    Render::flush();
}
bool run_start_menu(Runtime::GameContext& context) {
    Settings::synchronize_shared_appearance(context);
    auto* app = context.app_component();
    Input::ControlInputScope controls(context.input_component(),
        app && app->localPlayerTwoContext ? app->localPlayerTwoContext->input_component() : nullptr);
    // Seeking suppresses rendering while rebuilding. Escape can interrupt it
    // before completion, so restore output before drawing the Start screen.
    Render::end_output_suppression();
    using namespace Settings;
    NavigationInput input(context);
    bool settingsWasDown = key_down(context, 9);
    bool retryWasDown = key_down(context, 8);
    auto shownSize = Render::terminal_size();
    if(app)app->startMenuSelection=0;
    draw_start_menu(context);
    const auto return_to_start=[&] {
        input.suppress_held_keys();
        settingsWasDown=key_down(context,9);
        retryWasDown=key_down(context,8);
        shownSize=Render::terminal_size();
        if(app)app->startMenuSelection=0;
        draw_start_menu(context);
    };
    auto start_playing = [&] {
        // Release the menu action before the first piece can receive it.
        while (key_down(context, 8) || key_down(context, 9) ||
               input.is_down(NavigationKey::Confirm) || input.is_down(NavigationKey::Right)) {
            if (input.update(NavigationKey::Back, false)) return false;
            context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
        }
        if(app)app->startMenuNotice.clear();
        return true;
    };
    for (;;) {
        const auto size = Render::terminal_size();
        if (size != shownSize) {
            if (app) app->acceptedGameTerminalSize.reset();
            draw_start_menu(context);
            shownSize = size;
        }
        if (input.update(NavigationKey::Back, false)) return false;
        if(Frontend::ui_surface_active()) {
            const auto command=Frontend::poll_ui_command();
            if(command && command->kind==Frontend::UiCommandKind::Back)return false;
            if(command && app)app->startMenuSelection=static_cast<int>(command->index);
            const bool up=input.update(NavigationKey::Up,true);
            const bool down=input.update(NavigationKey::Down,true);
            const bool tab=input.update(NavigationKey::Tab,false);
            const bool shifted=
#ifdef _WIN32
                context.input_services().key_down(VK_LSHIFT) || context.input_services().key_down(VK_RSHIFT);
#else
                context.input_services().key_down(KEY_LEFTSHIFT) || context.input_services().key_down(KEY_RIGHTSHIFT);
#endif
            const int delta=(down?1:0)-(up?1:0)+(tab?(shifted?-1:1):0);
            if(app && delta) {app->startMenuSelection=(app->startMenuSelection+delta+3)%3;draw_start_menu(context);}
            if(input.update(NavigationKey::Confirm,false) || command) {
                const int selected=app?app->startMenuSelection:0;
                if(selected==2)return false;
                if(selected==0)return start_playing();
                if(run_menu(context,true)==MenuResult::StartGame)return start_playing();
                return_to_start();
            }
        }
        const bool retryDown = key_down(context, 8);
        const bool play = retryDown && !retryWasDown;
        retryWasDown = retryDown;
        if (play) return start_playing();
        const bool settingsDown = key_down(context, 9);
        const bool openSettings = settingsDown && !settingsWasDown;
        settingsWasDown = settingsDown;
        if (openSettings) {
            context.config.display.obscuredColors = context.user_settings().obscuredColors != 0;
            if (run_menu(context, true) == MenuResult::StartGame) return start_playing();
            return_to_start();
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}
}
