#include "tetr/app/start_screen_navigation.h"
#include "tetr/input/services.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/app/application_state.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/engine/game_end_presentation.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/input/action.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/constants.h"
#include "tetr/app/settings_menu.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/local_versus_codec.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/recorder.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/network/network_services.h"
#include "tetr/util/terminal_text.h"
#include "settings_input.h"
#include "settings_navigation.h"
#include "ui_text.h"
#include "post_game_statistics.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include <termios.h>
#include <unistd.h>
#endif

#include <iostream>

namespace App {

enum class ReplaySaveResult {
    Saved,
    Cancelled,
    Failed,
};

static std::string default_replay_name(Runtime::GameContext& context) {
    std::string path;
    for(int i=0;i<10;i++){
        const int value=context.services.random()%36;
        if(value<10) path+=char(value+'0');
        else path+=char(value-10+'a');
    }
    return path+".txt";
}

static bool write_replay_file(
    const std::string& replay,
    const std::filesystem::path& path) {
    std::ofstream rep(path,std::ios::out|std::ios::trunc);
    rep<<replay<<Replay::replay_hash_suffix(replay);
    return static_cast<bool>(rep);
}

static int replay_prompt_position(const Runtime::GameContext& context,
                                  const Runtime::PlayerSessionState& player) {
    const auto& field = player.gameplay.playfield;
    const auto& presentation = player.presentation;
    int bottom = std::max({field.visibleHeight, field.activeHeight,
        presentation.hold_panel_height() + 15});
    if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) {
        for (const auto& span : presentation.altitudeDisplay)
            if (span.width) bottom = std::max(bottom, span.row - presentation.topMargin);
    } else {
        bottom = std::max(bottom, field.visibleHeight + 1); // Score below the board.
    }
    // Compact text ends before NEXT, so it can sit directly below the field
    // and statistics even when the preview column extends farther down.
    return bottom + 1;
}

static void poll_server(Runtime::GameContext& context) {
    if (context.network_component()) Network::poll_connection(*context.network_component());
}

static void clear_replay_prompt_lines(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player) {
    Render::OutputGuard output(context);
    Render::set_ui_color(
        context.config.display.obscuredColors ? Render::ColorRole::Ghost : Render::ColorRole::Text,
        context.config.display.obscuredColors);
    for (const auto& span:player.presentation.replayPromptDisplay) {
        Render::move_cursor(span.row,span.column);
        std::cout<<std::string(span.width,' ');
    }
    player.presentation.replayPromptDisplay.clear();
}

static void draw_replay_prompt_rows(Runtime::GameContext& context,
    Runtime::PlayerSessionState& player, int replayPos, const std::vector<std::string>& rows) {
    Render::OutputGuard output(context);
    clear_replay_prompt_lines(context,player);
    const auto layout=Render::board_text_layout(context,player);
    for (std::size_t index=0;index<rows.size();++index) {
        const int row=player.presentation.topMargin+replayPos+static_cast<int>(index);
        Render::move_cursor(row,layout.column);
        std::cout<<Util::TerminalText::clip(rows[index],layout.columns,"");
        player.presentation.replayPromptDisplay.push_back({row,layout.column,
            static_cast<std::size_t>(layout.columns)});
    }
    Render::flush();
}

static void print_replay_prompt(Runtime::GameContext& context,
    Runtime::PlayerSessionState& player, int replayPos, const std::string& message) {
    const auto layout=Render::board_text_layout(context,player);
    auto rows=Util::TerminalText::wrap_words(UiText::tr(message),layout.columns);
    const auto hint=Util::TerminalText::wrap_words(UiText::tr("[Tab] Results / Clear statistics / Board"),layout.columns);
    rows.insert(rows.end(),hint.begin(),hint.end());
    draw_replay_prompt_rows(context,player,replayPos,rows);
}

static Render::TerminalSize results_terminal_size(const Runtime::GameContext& context) {
    const auto& p=context.session.player.presentation;
    return Render::terminal_size().value_or(Render::TerminalSize{
        p.terminalColumns>0 ? p.terminalColumns : 80,p.terminalRows>0 ? p.terminalRows : 24});
}

static void restore_finished_boards(Runtime::GameContext& context,Runtime::GameContext* second=nullptr) {
    Render::OutputGuard output(context);
    if (const auto size=Render::terminal_size()) Engine::center_game_boards(context,*size,second);
    Render::clear_screen();
    const auto restore=[](Runtime::GameContext& owner) {
        auto& p=owner.session.player;
        p.presentation.replayPromptDisplay.clear();
        Render::redraw_player_board(owner,p);
        Engine::GarbageService::redraw_buffer(owner,p);
        Engine::render_final_mode_result(owner,p,p.timing.completedGameTime.value_or(Runtime::Duration{}));
        if (!p.presentation.roundEndHeadline.empty()) {
            Render::set_ui_color(Render::ColorRole::Text,owner.config.display.obscuredColors);
            Render::move_cursor(1,p.presentation.margin);std::cout<<p.presentation.roundEndHeadline;
            Render::move_cursor(2,p.presentation.margin);std::cout<<"Press \"Retry\" to wait for a new game.";
            Render::move_cursor(3,p.presentation.margin);std::cout<<"Press \"Settings\" to open settings.";
        }
        using N=Settings::NavigationInput;using K=Settings::NavigationKey;
        for (const auto key:{K::Up,K::Down,K::Left,K::Right,K::PageUp,K::PageDown})
            owner.input_services().suppress_control_key_until_release(N::key_code(key));
    };
    restore(context);if (second) restore(*second);
    if (context.config.mode.replayMode) Engine::render_replay_playback_progress(context,context.session.player);
    Render::flush();
}

static std::vector<int> replay_save_keys(Runtime::GameContext& context) {
    std::vector<int> keys;
    for (const auto& [key, action] : context.input_services().bindings.entries()) {
        if (action == Input::Action::SaveReplay) keys.push_back(key);
    }
    return keys;
}

static std::string replay_save_prompt(Runtime::GameContext& context,
    Runtime::GameContext* other=nullptr,bool cancelled=false) {
    auto keys=replay_save_keys(context);
    if (other) for (int key:replay_save_keys(*other))
        if (std::find(keys.begin(),keys.end(),key)==keys.end()) keys.push_back(key);
    std::string labels;
    for (int key:keys) {
        if (!labels.empty()) labels+=" / ";
        labels+=UiText::key_label(key);
    }
    if (labels.empty()) labels=UiText::tr("Unbound");
    const auto prefix=other
        ? (cancelled ? "Replay save cancelled. Press either player's Save Replay " : "Press either player's Save Replay ")
        : (cancelled ? "Replay save cancelled. Press Save Replay " : "Press Save Replay ");
    return UiText::tr(prefix)+"["+labels+"]"+
        UiText::tr(cancelled ? " to try again." : " to save this game.");
}

static void wait_for_replay_save_key_release(
    Runtime::GameContext& context,
    const std::vector<int>& keys) {
    for (;;) {
        poll_server(context);
        bool anyDown = false;
        for (int key : keys) {
            if (context.input_services().key_down(key)) {
                anyDown = true;
                break;
            }
        }
        if (!anyDown) return;
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}

static ReplaySaveResult save_replay_interactively(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int replayPos,
    const std::string& replay,
    const std::vector<int>& saveKeys,Runtime::GameContext* second=nullptr,PostGameStatisticsView* statistics=nullptr) {
    Input::ControlInputScope inputScope(context.input_component(),second ? second->input_component() : nullptr);
    Render::OutputGuard output(context);
    Render::set_ui_color(
        context.config.display.obscuredColors ? Render::ColorRole::Ghost : Render::ColorRole::Text,
        context.config.display.obscuredColors);
    const std::string defaultName=default_replay_name(context);
    std::string draft;
    std::size_t draftCaret=0;
    const auto redraw=[&](const std::string& value,std::size_t caret) {
        draft=value; draftCaret=caret;
        if (statistics) {
            statistics->show_replay_save_prompt(defaultName,value,caret);
            statistics->render(context,std::string{},results_terminal_size(context));
            return;
        }
        const auto layout=Render::board_text_layout(context,player);
        auto rows=Util::TerminalText::wrap_words(UiText::tr("Replay file [replay/")+defaultName+"]",layout.columns);
        const auto instructions=Util::TerminalText::wrap_words(
            UiText::tr("Enter: default name   Esc / /cancel: cancel"),layout.columns);
        rows.insert(rows.end(),instructions.begin(),instructions.end());
        const std::string prompt=UiText::tr("Custom replay name: ");
        const int caretColumns=static_cast<int>(Util::TerminalText::width(prompt+value.substr(0,caret)));
        const int scroll=std::max(0,caretColumns-layout.columns+1);
        const int inputRow=player.presentation.topMargin+replayPos+static_cast<int>(rows.size());
        rows.emplace_back();
        draw_replay_prompt_rows(context,player,replayPos,rows);
        // Keep filename editing on its own row, including long UTF-8 names.
        Render::move_cursor(inputRow,layout.column);
        std::cout<<Util::TerminalText::slice(prompt+value,scroll,layout.columns);
        Render::move_cursor(inputRow,layout.column+caretColumns-scroll);
        Render::flush();
    };
    redraw(draft,draftCaret);

    // The key that opened this prompt is read by the low-level monitor.  Wait
    // for its physical release before switching stdin back to canonical text
    // mode, otherwise the save key's repeat can leak into the file name.
    wait_for_replay_save_key_release(context,saveKeys);
    bool savedByEscape = false;
    const std::string enteredNameInput = Settings::read_scalar_text(context, [&] {
        poll_server(context);
        if (statistics) statistics->render(context,std::string{},results_terminal_size(context));
        else if (Engine::refresh_game_terminal_layout(context)) {
            replayPos=replay_prompt_position(context,player);
            redraw(draft,draftCaret);
        }
    }, redraw, &savedByEscape);
    if(savedByEscape) {
        const auto escape=Settings::NavigationInput::key_code(Settings::NavigationKey::Back);
        context.input_services().suppress_control_key_until_release(escape);
        if(second) second->input_services().suppress_control_key_until_release(escape);
    }
    std::string enteredName = enteredNameInput;
    const bool receivedName = !std::cin.fail();
    if (!receivedName) std::cin.clear();

    const auto feedback=[&](const std::string& message) {
        if (statistics) {
            statistics->finish_replay_save_prompt(message);
            statistics->render(context,std::string{},results_terminal_size(context));
        } else print_replay_prompt(context,player,replayPos,message.c_str());
    };
    if (!statistics) clear_replay_prompt_lines(context,player);
    if (savedByEscape || !receivedName || enteredName == "/cancel" || enteredName == "cancel" ||
        (!enteredName.empty() && enteredName.front() == '\x1b')) {
        if (statistics) feedback(replay_save_prompt(context,second,true));
        return ReplaySaveResult::Cancelled;
    }

    if (enteredName.empty()) enteredName=defaultName;
    const auto path=Replay::replay_path_for_saving(enteredName);
    if (!path) {
        feedback(UiText::tr("Invalid file name. Replays must stay inside replay/."));
        return ReplaySaveResult::Failed;
    }

    std::error_code error;
    std::filesystem::create_directories(path->parent_path(),error);
    if (error || !write_replay_file(replay,*path)) {
        feedback(UiText::tr("Could not save the replay."));
        return ReplaySaveResult::Failed;
    }
    const std::string message=UiText::tr("Replay saved: ")+path->string();
    feedback(message);
    return ReplaySaveResult::Saved;
}

void run_post_game_interaction(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    freeze_post_game_times(context,player);
    if (!context.config.mode.replayMode) context.undoHistory.reset();
    if(!context.config.mode.replayMode) Replay::add_replay(context,Replay::OperationKind::EndAll);
    if (std::getenv("TETR_TEST_BATCH")) {
        if (const char* path = std::getenv("TETR_TEST_REPLAY"); path && *path) {
            const auto replayPath = Replay::replay_path_for_saving(path);
            std::error_code error;
            if (replayPath) std::filesystem::create_directories(replayPath->parent_path(), error);
            if (replayPath && !error && write_replay_file(Replay::recording_for_save(context), *replayPath))
                std::cerr << "TETR_TEST_REPLAY path=" << replayPath->string() << '\n';
        }
        return;
    }
    int replaySavedOrDisabled=0;
    const auto completedElapsed=*player.timing.completedGameTime;
    int replayPos=replay_prompt_position(context,player);
    if(Replay::session(context).data.length()>10000000){
        replaySavedOrDisabled=1;
        print_replay_prompt(context,player,replayPos,"The replay was too long to save.");
    }
    if(!context.config.mode.replayMode&&!replaySavedOrDisabled){
        print_replay_prompt(context,player,replayPos,replay_save_prompt(context));
    }
    const std::vector<int> saveKeys=replay_save_keys(context);
    Settings::NavigationInput navigation(context);
    PostGameStatisticsView statistics(make_post_game_statistics(context,player));

    while(1){
        poll_server(context);
        const auto previousPage=statistics.page();statistics.poll(navigation);
        if (previousPage!=PostGamePage::Board && statistics.page()==PostGamePage::Board) {
            restore_finished_boards(context);replayPos=replay_prompt_position(context,player);
            if (!context.config.mode.replayMode) print_replay_prompt(context,player,replayPos,
                replaySavedOrDisabled ? "Replay saved or disabled." : replay_save_prompt(context));
        }
        if (statistics.page()==PostGamePage::Board && !context.config.mode.replayMode && Engine::refresh_game_terminal_layout(context)) {
            replayPos=replay_prompt_position(context,player);
            Engine::render_final_mode_result(context,player,completedElapsed);
            print_replay_prompt(context,player,replayPos,replaySavedOrDisabled ? "Replay saved or disabled."
                : replay_save_prompt(context));
        }
        if(context.config.mode.replayMode && statistics.page()==PostGamePage::Board){
            Engine::update_replay_playback_controls(context,player);
        }
        statistics.render(context,context.config.mode.replayMode ? std::string{} :
            (replaySavedOrDisabled ? UiText::tr("Replay saved or disabled.") : replay_save_prompt(context)),
            results_terminal_size(context));
        int retry=0;
        for(const auto& [key, action]:context.input_services().bindings.entries()){
            if (statistics.reserves(key)) continue;
            if(action==Input::Action::Retry&&context.input_services().key_down(key)){retry=1;break;}
            if(action==Input::Action::OpenSettings&&context.input_services().key_down(key)){Settings::run_menu(context);return;}
            if(!context.config.mode.replayMode&&!replaySavedOrDisabled&&action==Input::Action::SaveReplay&&context.input_services().key_down(key)){
                const bool inStatistics=statistics.page()!=PostGamePage::Board;
                const ReplaySaveResult result=save_replay_interactively(
                    context,player,replayPos,Replay::recording_for_save(context),saveKeys,nullptr,inStatistics ? &statistics : nullptr);
                replaySavedOrDisabled=result==ReplaySaveResult::Saved;
                if(result==ReplaySaveResult::Cancelled && !inStatistics){
                    print_replay_prompt(
                        context,player,replayPos,
                        replay_save_prompt(context,nullptr,true));
                }
                navigation.suppress_held_keys();
            }
        }
        if(retry) break;
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}

void run_local_versus_post_game_interaction(
    Runtime::GameContext& first,
    Runtime::GameContext& second) {
    if (first.app_component() && first.app_component()->automaticLocalMatch) return;
    const bool replayMode = first.config.mode.replayMode;
    freeze_post_game_times(first,first.session.player);
    freeze_post_game_times(second,second.session.player);
    if (!replayMode) {
        Replay::add_replay(first, Replay::OperationKind::EndAll);
        Replay::add_replay(second, Replay::OperationKind::EndAll);
    }

    auto& firstPlayer = first.session.player;
    int replayPos = std::max(
        replay_prompt_position(first,firstPlayer),
        replay_prompt_position(second,second.session.player));
    const std::string replay = replayMode ? std::string{} :
        Replay::encode_local_versus_replay(
            Replay::recording_for_save(first),
            Replay::recording_for_save(second));
    bool replaySavedOrDisabled = replay.size() > 20000000;
    if (!replayMode && !replaySavedOrDisabled) {
        if (const char* path = std::getenv("TETR_LOCAL_VERSUS_REPLAY");
            path && *path != '\0') {
            const auto replayPath=Replay::replay_path_for_saving(path);
            std::error_code error;
            if (replayPath) {
                std::filesystem::create_directories(replayPath->parent_path(),error);
            }
            if (replayPath && !error && write_replay_file(replay,*replayPath)) {
                replaySavedOrDisabled = true;
                std::cerr << "TETR_LOCAL_VERSUS_REPLAY path="
                          << replayPath->string() << '\n';
            }
        }
    }
    if (std::getenv("TETR_LOCAL_VERSUS_BATCH")) return;
    if (!replayMode) {
        print_replay_prompt(
            first, firstPlayer, replayPos,
            replaySavedOrDisabled
                ? "The replay was too long to save."
                : replay_save_prompt(first,&second));
    }

    std::vector<int> saveKeys=replay_save_keys(first);
    const std::vector<int> secondSaveKeys=replay_save_keys(second);
    saveKeys.insert(saveKeys.end(),secondSaveKeys.begin(),secondSaveKeys.end());
    Settings::NavigationInput firstNavigation(first),secondNavigation(second);
    PostGameStatisticsView statistics(make_post_game_statistics(first,firstPlayer),
        make_post_game_statistics(second,second.session.player));
    const auto available_action=[&](Runtime::GameContext& owner,Input::Action requested) {
        for (const auto& [key,action]:owner.input_services().bindings.entries())
            if (action==requested && !statistics.reserves(key) && owner.input_services().key_down(key)) return true;
        return false;
    };

    for (;;) {
        const auto previousPage=statistics.page();statistics.poll(firstNavigation,&secondNavigation);
        if (previousPage!=PostGamePage::Board && statistics.page()==PostGamePage::Board) {
            restore_finished_boards(first,&second);
            replayPos=std::max(replay_prompt_position(first,firstPlayer),replay_prompt_position(second,second.session.player));
            if (!replayMode) print_replay_prompt(first,firstPlayer,replayPos,
                replaySavedOrDisabled ? "Replay saved or disabled." : replay_save_prompt(first,&second));
        }
        const bool firstResized=statistics.page()==PostGamePage::Board && Engine::refresh_game_terminal_layout(first);
        const bool secondResized=statistics.page()==PostGamePage::Board && Engine::refresh_game_terminal_layout(second);
        if (!replayMode && (firstResized || secondResized)) {
            replayPos=std::max(replay_prompt_position(first,firstPlayer),replay_prompt_position(second,second.session.player));
            print_replay_prompt(first,firstPlayer,replayPos,replaySavedOrDisabled ? "Replay saved or disabled."
                : replay_save_prompt(first,&second));
        }
        if(replayMode && statistics.page()==PostGamePage::Board){
            // The shared timeline remains interactive on the final frame, so
            // Left can seek back into a completed Local Versus replay.
            Engine::update_replay_playback_controls(first,firstPlayer);
        }
        statistics.render(first,replayMode ? std::string{} : (replaySavedOrDisabled ?
            UiText::tr("Replay saved or disabled.") : replay_save_prompt(first,&second)),results_terminal_size(first));
        if (available_action(first,Input::Action::OpenSettings) ||
            available_action(second,Input::Action::OpenSettings)) {
            Settings::run_menu(first);
            return;
        }
        if (available_action(first,Input::Action::Retry) ||
            available_action(second,Input::Action::Retry)) {
            return;
        }
        if (!replayMode && !replaySavedOrDisabled &&
            (available_action(first,Input::Action::SaveReplay) ||
             available_action(second,Input::Action::SaveReplay))) {
            const bool inStatistics=statistics.page()!=PostGamePage::Board;
            const ReplaySaveResult result=save_replay_interactively(
                first,firstPlayer,replayPos,replay,saveKeys,&second,inStatistics ? &statistics : nullptr);
            replaySavedOrDisabled=result==ReplaySaveResult::Saved;
            if(result==ReplaySaveResult::Cancelled && !inStatistics){
                print_replay_prompt(
                    first,firstPlayer,replayPos,
                    replay_save_prompt(first,&second,true));
            }
            firstNavigation.suppress_held_keys();secondNavigation.suppress_held_keys();
        }
        first.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}

} // namespace App
