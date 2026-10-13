#include "game_terminal.h"
#include "ui_text.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/app/application_state.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/render/board_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/terminal_text.h"

#include <algorithm>
#include <iostream>
#include <vector>
#include <map>

namespace App::InitDetail {
namespace {
bool fits(Render::TerminalSize actual, Render::TerminalSize required) {
    return actual.columns>=required.columns && actual.rows>=required.rows;
}

std::string binding_label(const std::vector<Runtime::GameContext*>& contexts, Input::Action action) {
    std::vector<int> keys;
    for (const auto* context:contexts)
        for (const auto& [key,bound]:context->input_services().bindings.entries())
            if (bound==action && std::find(keys.begin(),keys.end(),key)==keys.end()) keys.push_back(key);
    std::string label;
    for (const int key:keys) {
        if (!label.empty()) label+=" / ";
        label+=UiText::key_label(key);
    }
    return "["+(label.empty() ? UiText::tr("Unbound") : label)+"]";
}

void draw_check(Runtime::GameContext& context, Render::TerminalSize current,
    Engine::GameTerminalRequirements required, const std::vector<Runtime::GameContext*>& contexts) {
    Render::OutputGuard output(context);
    const bool obscured=context.config.display.obscuredColors;
    Render::clear_screen();
    // Union the line directions, including coincident edges/corners. This
    // creates correct T/cross junctions instead of overwriting one rectangle.
    std::map<std::pair<int,int>,unsigned> edges;
    const auto mark=[&](int row,int column,unsigned mask) {
        if (row>=0 && row<current.rows-1 && column>=0 && column<current.columns-1)
            edges[{row,column}]|=mask;
    };
    const auto rectangle=[&](Render::TerminalSize size) {
        const int right=size.columns-2,bottom=size.rows-2;
        for (int col=0;col<=std::min(right,current.columns-2);++col) {
            mark(0,col,col==0 ? 6u : col==right ? 12u : 10u);
            mark(bottom,col,col==0 ? 3u : col==right ? 9u : 10u);
        }
        for (int row=1;row<std::min(bottom,current.rows-1);++row) {
            mark(row,0,5); mark(row,right,5);
        }
    };
    rectangle(required.full);
    if (required.minimum!=required.full) rectangle(required.minimum);
    static constexpr const char* shapes[16]={" ","│","─","└","│","│","┌","├",
        "─","┘","─","┴","┐","┤","┬","┼"};
    Render::set_ui_color(obscured ? Render::ColorRole::Ghost : Render::ColorRole::Orange,obscured);
    for (const auto& [position,mask]:edges) {
        Render::move_cursor(position.first,position.second); std::cout<<shapes[mask];
    }
    const int contentWidth=std::min({44,required.minimum.columns-5,current.columns-4});
    if (contentWidth>0) {
        std::vector<std::string> lines;
        const auto append=[&](const std::string& text) {
            const auto wrapped=Util::TerminalText::wrap(text,contentWidth);
            lines.insert(lines.end(),wrapped.begin(),wrapped.end());
        };
        const bool fullLayoutFits=fits(current,required.full);
        append(UiText::tr(fullLayoutFits ? "Terminal size is sufficient." :
            "Enlarge the terminal until the outer frame is fully visible."));
        append(UiText::tr("Minimum layout: ")+std::to_string(required.minimum.columns)+" x "+std::to_string(required.minimum.rows));
        if (required.minimum!=required.full)
            append(UiText::tr("Full layout: ")+std::to_string(required.full.columns)+" x "+std::to_string(required.full.rows));
        append(UiText::tr("Current size: ")+std::to_string(current.columns)+" x "+std::to_string(current.rows));
        if (!fullLayoutFits)
            append(UiText::tr(fits(current,required.minimum) ? "Minimum layout fits; NEXT may be clipped." : "Some information will be clipped."));
        append(binding_label(contexts,Input::Action::Retry)+" "+UiText::tr(fullLayoutFits ?
            "Press the restart key to start the game." : "Press the restart key to start anyway."));
        append(binding_label(contexts,Input::Action::OpenSettings)+" "+UiText::tr("Settings")+
            "   [Esc] "+UiText::tr("Return to start screen"));
        Render::set_ui_color(obscured ? Render::ColorRole::Ghost : Render::ColorRole::Text,obscured);
        for (std::size_t i=0;i<lines.size() && static_cast<int>(i)+2<std::min(required.minimum.rows-2,current.rows-1);++i) {
            Render::move_cursor(static_cast<int>(i)+2,2); std::cout<<lines[i];
        }
    }
    Render::move_cursor(0,0); Render::flush();
}

struct WatchedKey {
    Input::Services* input;
    int key;
    Input::Action action;
    bool held;
    bool suppressed;
};
}

Render::TerminalSize required_game_terminal_size(Runtime::GameContext& context,
    Runtime::GameContext* secondContext, bool compact) {
    return Engine::required_game_terminal_size(context,secondContext,compact);
}

TerminalCheckResult wait_for_game_terminal(Runtime::GameContext& context,
    Runtime::GameContext* secondContext, TerminalSizeQuery query) {
    if (Render::output_suppressed()) return TerminalCheckResult::Ready;
    if (!query) query=Render::terminal_size;
    auto current=query();
    if (!current) return TerminalCheckResult::Ready;
    const auto required=Engine::game_terminal_requirements(context,secondContext);
    auto* app=context.app_component();
    if (app) app->observe_terminal_size(current->columns,current->rows);
    const auto confirmation=[&] {
        return std::array<int,2>{{current->columns,current->rows}};
    };
    const auto accept=[&] {
        Engine::center_game_boards(context,*current,secondContext);
        if (app) {
            app->acceptedGameTerminalSize=confirmation();
            app->acceptedGameTerminalRequirements={{required.full.columns,required.full.rows}};
        }
    };
    const bool confirmed=app && app->acceptedGameTerminalSize==confirmation() &&
        required.full.columns<=app->acceptedGameTerminalRequirements[0] &&
        required.full.rows<=app->acceptedGameTerminalRequirements[1];
    if (fits(*current,required.full) || confirmed) {
        accept();
        return TerminalCheckResult::Ready;
    }

    std::vector<Runtime::GameContext*> contexts{&context};
    if (secondContext) contexts.push_back(secondContext);
    Input::ControlInputScope controls(context.input_component(),secondContext ? secondContext->input_component() : nullptr);
    Input::GameplayInputScope firstCapture(context.input_component());
    Input::GameplayInputScope secondCapture(secondContext ? secondContext->input_component() : nullptr);
    std::vector<WatchedKey> keys;
    for (auto* target:contexts) {
        auto& input=target->input_services();
        input.discard_control_events();
        const auto watch=[&](int key,Input::Action action) {
            const bool held=input.raw_key_down(key);
            keys.push_back({&input,key,action,held,held});
        };
        for (const auto& [key,action]:input.bindings.entries())
            if (action==Input::Action::Retry || action==Input::Action::OpenSettings) watch(key,action);
        watch(Input::kNativeKeyPlatform==Input::KeyPlatform::Windows ? 0x1b : 1,Input::Action::Unknown);
    }
    std::optional<Render::TerminalSize> shown;
    bool startRequested=false;
    for (;;) {
        current=query();
        if (!current) return TerminalCheckResult::Ready;
        if (app) app->observe_terminal_size(current->columns,current->rows);
        if (current!=shown) { draw_check(context,*current,required,contexts); shown=current; }
        bool restartHeld=false;
        for (auto& key:keys) {
            const bool pressed=key.input->control_key_pressed(key.key);
            const bool held=key.input->raw_key_down(key.key);
            bool fresh=pressed && !key.held && !key.suppressed;
            key.held=held;
            if (!held) key.suppressed=false;
            if (key.action==Input::Action::Retry) restartHeld|=held;
            if (!fresh) continue;
            if (key.action==Input::Action::Unknown) request_start_screen(context);
            if (key.action==Input::Action::OpenSettings) return TerminalCheckResult::OpenSettings;
            if (key.action==Input::Action::Retry) startRequested=true;
        }
        // Once shown, the size check requires confirmation even after resizing.
        if (startRequested && !restartHeld) {
            accept();
            return TerminalCheckResult::Ready;
        }
        if (context.network_component()) Network::poll_connection(*context.network_component());
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.01));
    }
}
} // namespace App::InitDetail
