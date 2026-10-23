#include "single_player_challenges.h"
#include "online_replay.h"
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
#include "tetr/replay/recent.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include "tetr/replay/recorder.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/network/network_services.h"
#include "tetr/network/room_replay.h"
#include "tetr/util/terminal_text.h"
#include "tetr/util/runtime_directory_lock.h"
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
    Util::FileWriteLock writeLock(path);if(!writeLock.acquired())return false;
    auto temporary=path;temporary+=".tmp";
    std::ofstream rep(temporary,std::ios::out|std::ios::trunc|std::ios::binary);
    rep<<replay<<Replay::replay_hash_suffix(replay);
    rep.close();if(!rep)return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code error;std::filesystem::rename(temporary,path,error);return !error;
#endif
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
    if(Frontend::ui_surface_active()) {
        TerminalCanvas canvas;
        const int start=std::max(0,36-static_cast<int>(rows.size()));
        for(std::size_t i=0;i<rows.size();++i)canvas.write(start+static_cast<int>(i),4,rows[i],120,{{210,230,248}},{{9,17,28}});
        Frontend::UiFrame frame{std::move(canvas.pending),{},false,true};
        frame.metadata={Frontend::UiKind::Notice,"replay-notice",UiText::tr("Replay")};
        frame.metadata.chinese=UiText::language()!=UiText::Language::English;
        for(const auto& row:rows) {if(!frame.metadata.hint.empty())frame.metadata.hint+='\n';frame.metadata.hint+=row;}
        for(const std::size_t action:{8u,9u}) {
            std::string keys;
            for(const int key:UserConfig::control_keys(context.user_settings(),action)) {
                if(!keys.empty())keys+=" / ";keys+=UiText::key_label(key);
            }
            frame.metadata.entries.push_back({std::to_string(action),UiText::tr(action==8?"Restart":"Settings"),true,keys});
        }
        Frontend::present_ui(std::move(frame));return;
    }
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
    const auto hint=Util::TerminalText::wrap_words(UiText::tr("[Tab] Results / Clear statistics"),layout.columns);
    rows.insert(rows.end(),hint.begin(),hint.end());
    draw_replay_prompt_rows(context,player,replayPos,rows);
}

static Render::TerminalSize results_terminal_size(const Runtime::GameContext& context) {
    const auto& p=context.session.player.presentation;
    return Render::terminal_size().value_or(Render::TerminalSize{
        p.terminalColumns>0 ? p.terminalColumns : 80,p.terminalRows>0 ? p.terminalRows : 24});
}

static void restore_finished_boards(Runtime::GameContext& context,Runtime::GameContext* second=nullptr) {
    for(auto* owner:{&context,second}) if(owner) {
        using N=Settings::NavigationInput;using K=Settings::NavigationKey;
        owner->input_services().discard_control_events();
        for(const auto key:{K::Back,K::Backspace})
            owner->input_services().suppress_control_key_until_release(N::key_code(key));
    }
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

// Statistics own Escape while open, just like the online result viewer.
// The finished board keeps its normal application navigation afterwards.
struct StatisticsControlScope {
    Input::Services& input;
    std::function<void()> saved;
    StatisticsControlScope(Runtime::GameContext& context,PostGameStatisticsView& statistics)
        :input(context.input_services()),saved(std::move(input.controlInterrupt)) {
        input.controlInterrupt=[this,&statistics] {
            if(statistics.page()==PostGamePage::Board && saved) saved();
        };
    }
    ~StatisticsControlScope() {input.controlInterrupt=std::move(saved);}
};

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
    const std::vector<int>& saveKeys,Runtime::GameContext* second=nullptr,PostGameStatisticsView* statistics=nullptr,bool updateRecent=true,bool incomplete=false) {
    struct RestoreCapture {
        Input::Services& input;bool active;
        explicit RestoreCapture(Input::Services& source):input(source),active(source.gameplay_input_active()) {if(active)input.end_gameplay_input();}
        ~RestoreCapture(){if(active)input.begin_gameplay_input();}
    } firstCapture(context.input_services());
    std::optional<RestoreCapture> secondCapture;if(second)secondCapture.emplace(second->input_services());
    Input::ControlInputScope inputScope(context.input_component(),second ? second->input_component() : nullptr);
    Render::OutputGuard output(context);
    Render::set_ui_color(
        context.config.display.obscuredColors ? Render::ColorRole::Ghost : Render::ColorRole::Text,
        context.config.display.obscuredColors);
    const std::string defaultName=default_replay_name(context);
    const auto saveTitle=UiText::tr(incomplete?"Save incomplete replay":"Save Replay");
    std::string draft;
    std::size_t draftCaret=0;
    const auto redraw=[&](const std::string& value,std::size_t caret) {
        draft=value; draftCaret=caret;
        if(Frontend::ui_surface_active() && !statistics) {
            TerminalCanvas canvas;
            canvas.box(11,12,100,7,saveTitle,{{58,222,240}},{{230,240,255}},{{9,17,28}});
            canvas.write(13,15,".tetr/replay/"+defaultName,94,{{230,240,255}},{{9,17,28}});
            canvas.write(14,15,UiText::tr("Enter Default name   Esc Cancel"),94,{{130,165,190}},{{9,17,28}});
            const auto prompt=UiText::tr("Custom replay name: ");
            const int at=static_cast<int>(Util::TerminalText::width(prompt+value.substr(0,caret)));
            const int scroll=std::max(0,at-92);
            canvas.write(16,15,Util::TerminalText::slice(prompt+value,scroll,94),94,{{230,240,255}},{{14,48,60}});
            Frontend::UiFrame frame{std::move(canvas.pending),std::pair{16,15+at-scroll},false,true};
            frame.metadata=replay_save_metadata(defaultName,value,caret);
            frame.metadata.title=saveTitle;
            if(incomplete)frame.metadata.hint=UiText::tr("Some player recordings are missing. This copy keeps the local personal replay.");
            Frontend::present_ui(std::move(frame));return;
        }
        if (statistics) {
            statistics->show_replay_save_prompt(defaultName,value,caret);
            statistics->render(context,std::string{},results_terminal_size(context));
            return;
        }
        const auto layout=Render::board_text_layout(context,player);
        auto rows=Util::TerminalText::wrap_words(UiText::tr("Replay file [.tetr/replay/")+defaultName+"]",layout.columns);
        if(incomplete)rows.push_back(UiText::tr("Save incomplete replay"));
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
        feedback(UiText::tr("Invalid file name or path."));
        return ReplaySaveResult::Failed;
    }

    std::error_code error;
    if(!path->parent_path().empty())std::filesystem::create_directories(path->parent_path(),error);
    if (error || !write_replay_file(replay,*path)) {
        feedback(UiText::tr("Could not save the replay."));
        return ReplaySaveResult::Failed;
    }
    auto& state=Replay::session(context);
    if(updateRecent && !context.config.mode.replayMode && !state.recentPath.empty() &&
       Replay::rename_recent(std::filesystem::u8path(state.recentPath),*path))state.recentPath=path->generic_u8string();
    attach_challenge_replay(context,*path);
    if(second)attach_challenge_replay(*second,*path);
    const std::string message=UiText::tr("Replay saved: ")+path->generic_u8string();
    feedback(message);
    return ReplaySaveResult::Saved;
}

void remember_finished_replay(Runtime::GameContext& context,const std::string& recording) {
    auto& state=Replay::session(context);
    if(context.config.mode.replayMode || !state.recentPath.empty() || state.tetrioRecorder ||
       !context.session.player.timing.completedGameTime)return;
    const auto data=recording.empty()?Replay::recording_for_save(context):recording;
    if(data.empty() || data.size()>std::max<std::size_t>(20000000,state.recordingLimit+4096) || data.find("{end_all,")==std::string::npos)return;
    const auto now=std::time(nullptr);std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&now);
#else
    localtime_r(&now,&local);
#endif
    const auto mode=context.config.mode.mode;
    const auto& score=context.session.player.gameplay.scoring;
    std::ostringstream label;label<<std::put_time(&local,"%Y-%m-%d %H:%M:%S")<<"  "
        <<(mode==Runtime::GameMode::LocalVersus?"VS":mode==Runtime::GameMode::Multiplayer?"ONLINE":mode==Runtime::GameMode::QuickPlay?"QP":"SOLO")
        <<"  "<<score.placedBlocks<<"P / "<<score.totalLines<<"L";
    if(const auto path=Replay::save_recent(data,label.str())) {
        state.recentPath=path->generic_u8string();
        attach_challenge_replay(context,*path);
        if(mode==Runtime::GameMode::LocalVersus) {
            if(const auto* app=context.app_component();app && app->primaryContext && app->localPlayerTwoContext) {
                auto* partner=&context==app->primaryContext?app->localPlayerTwoContext:app->primaryContext;
                attach_challenge_replay(*partner,*path);
            }
        }
    }
}

bool save_finished_online_replay(Runtime::GameContext& context,PostGameStatisticsView* statistics) {
    if(context.session.roomScene.enabled && context.network_component()) {
        auto& n=Network::network_services(context);
        if(n.multiplayerRoom && ((n.replays && n.replays->supported) || !context.session.player.timing.completedGameTime))
            return save_online_room_replay(context,context.session.roomScene.roomInstance?context.session.roomScene.roomInstance:n.roomInstance,context.session.roomScene.epoch,statistics);
    }
    auto& player=context.session.player;
    if(!player.timing.completedGameTime || context.config.mode.replayMode || Replay::session(context).data.size()>10000000) return false;
    return save_replay_interactively(context,player,replay_prompt_position(context,player),
        Replay::recording_for_save(context),replay_save_keys(context),nullptr,statistics)==ReplaySaveResult::Saved;
}
bool save_replay_copy(Runtime::GameContext& context,const std::string& recording,bool updateRecent,PostGameStatisticsView* statistics,bool incomplete) {
    return save_replay_interactively(context,context.session.player,replay_prompt_position(context,context.session.player),
        recording,replay_save_keys(context),nullptr,incomplete?nullptr:statistics,updateRecent,incomplete)==ReplaySaveResult::Saved;
}

void run_post_game_interaction(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    freeze_post_game_times(context,player);
    finish_single_player_challenges(context,player);
    if (!context.config.mode.replayMode) context.undoHistory.reset();
    if(!context.config.mode.replayMode) Replay::add_replay(context,Replay::OperationKind::EndAll);
    remember_finished_replay(context);
    if(auto* app=context.app_component();app && app->roundFinished) {
        app->roundFinished();
        return;
    }
    if (std::getenv("TETR_TEST_BATCH")) {
        if (const char* path = std::getenv("TETR_TEST_REPLAY"); path && *path) {
            const auto replayPath = Replay::replay_path_for_saving(path);
            std::error_code error;
            if (replayPath && !replayPath->parent_path().empty()) std::filesystem::create_directories(replayPath->parent_path(), error);
            if (replayPath && !error && write_replay_file(Replay::recording_for_save(context), *replayPath)) {
                auto& state=Replay::session(context);
                if(!context.config.mode.replayMode && !state.recentPath.empty() &&
                   Replay::rename_recent(std::filesystem::u8path(state.recentPath),*replayPath))state.recentPath=replayPath->generic_u8string();
                attach_challenge_replay(context,*replayPath);
                std::cerr << "TETR_TEST_REPLAY path=" << replayPath->generic_u8string() << '\n';
            }
        }
        return;
    }
    bool replayDisabled=Replay::recording_for_save(context).size()>10000000;
    const auto completedElapsed=*player.timing.completedGameTime;
    int replayPos=replay_prompt_position(context,player);
    if(Replay::session(context).data.length()>10000000){
        replayDisabled=1;
        print_replay_prompt(context,player,replayPos,"The replay was too long to save.");
    }
    if(!replayDisabled){
        print_replay_prompt(context,player,replayPos,replay_save_prompt(context));
    }
    const std::vector<int> saveKeys=replay_save_keys(context);
    Settings::NavigationInput navigation(context);
    PostGameStatisticsView statistics(make_post_game_statistics(context,player));
    if(auto* app=context.app_component();app && app->challengeRound)
        statistics.set_challenges(app->challengeRound->outcomes,app->challengeRound->notice);
    StatisticsControlScope statisticsInput(context,statistics);

    while(1){
        poll_server(context);
        const auto previousPage=statistics.page();statistics.poll(navigation);
        if (previousPage!=PostGamePage::Board && statistics.page()==PostGamePage::Board) {
            restore_finished_boards(context);replayPos=replay_prompt_position(context,player);
            print_replay_prompt(context,player,replayPos,
                replayDisabled ? "The replay was too long to save." : replay_save_prompt(context));
        }
        if (statistics.page()==PostGamePage::Board && Engine::refresh_game_terminal_layout(context)) {
            replayPos=replay_prompt_position(context,player);
            Engine::render_final_mode_result(context,player,completedElapsed);
            print_replay_prompt(context,player,replayPos,replayDisabled ? "The replay was too long to save."
                : replay_save_prompt(context));
        }
        if(context.config.mode.replayMode && statistics.page()==PostGamePage::Board){
            Engine::update_replay_playback_controls(context,player);
        }
        statistics.render(context,
            (replayDisabled ? UiText::tr("The replay was too long to save.") : replay_save_prompt(context)),
            results_terminal_size(context));
        int retry=0;
        for(const auto& [key, action]:context.input_services().bindings.entries()){
            if (statistics.reserves(key)) continue;
            if(action==Input::Action::Retry&&context.input_services().key_down(key)){retry=1;break;}
            if(action==Input::Action::OpenSettings&&context.input_services().key_down(key)){Settings::run_menu(context);return;}
            if(!replayDisabled&&action==Input::Action::SaveReplay&&context.input_services().key_down(key)){
                const bool inStatistics=statistics.page()!=PostGamePage::Board;
                const ReplaySaveResult result=save_replay_interactively(
                    context,player,replayPos,Replay::recording_for_save(context),saveKeys,nullptr,inStatistics ? &statistics : nullptr);

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
    const std::string replay = replayMode ? Replay::recording_for_save(first) :
        Replay::encode_local_versus_replay(
            Replay::recording_for_save(first),
            Replay::recording_for_save(second));
    remember_finished_replay(first,replay);
    bool replayDisabled = replay.size() > 20000000;
    if (!replayMode && !replayDisabled) {
        if (const char* path = std::getenv("TETR_LOCAL_VERSUS_REPLAY");
            path && *path != '\0') {
            const auto replayPath=Replay::replay_path_for_saving(path);
            std::error_code error;
            if (replayPath && !replayPath->parent_path().empty()) {
                std::filesystem::create_directories(replayPath->parent_path(),error);
            }
            if (replayPath && !error && write_replay_file(replay,*replayPath)) {
                auto& state=Replay::session(first);
                if(Replay::rename_recent(std::filesystem::u8path(state.recentPath),*replayPath))state.recentPath=replayPath->generic_u8string();
                attach_challenge_replay(first,*replayPath);attach_challenge_replay(second,*replayPath);
                std::cerr << "TETR_LOCAL_VERSUS_REPLAY path="
                          << replayPath->generic_u8string() << '\n';
            }
        }
    }
    if (std::getenv("TETR_LOCAL_VERSUS_BATCH")) return;
    {
        print_replay_prompt(
            first, firstPlayer, replayPos,
            replayDisabled
                ? "The replay was too long to save."
                : replay_save_prompt(first,&second));
    }

    std::vector<int> saveKeys=replay_save_keys(first);
    const std::vector<int> secondSaveKeys=replay_save_keys(second);
    saveKeys.insert(saveKeys.end(),secondSaveKeys.begin(),secondSaveKeys.end());
    Settings::NavigationInput firstNavigation(first),secondNavigation(second);
    PostGameStatisticsView statistics(make_post_game_statistics(first,firstPlayer),
        make_post_game_statistics(second,second.session.player));
    if(auto* app=first.app_component()) {
        std::vector<Challenges::Outcome> outcomes;std::string notice;
        for(auto* context:{&first,&second})if(const auto& round=app->challenge_round(context);round) {
            for(auto outcome:round->outcomes){outcome.player=context==&first?0:1;outcomes.push_back(std::move(outcome));}
            if(!round->notice.empty()){if(!notice.empty())notice+='\n';notice+=round->notice;}
        }
        statistics.set_challenges(std::move(outcomes),notice);
    }
    StatisticsControlScope firstStatisticsInput(first,statistics),secondStatisticsInput(second,statistics);
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
            print_replay_prompt(first,firstPlayer,replayPos,
                replayDisabled ? "The replay was too long to save." : replay_save_prompt(first,&second));
        }
        const bool firstResized=statistics.page()==PostGamePage::Board && Engine::refresh_game_terminal_layout(first);
        const bool secondResized=statistics.page()==PostGamePage::Board && Engine::refresh_game_terminal_layout(second);
        if (firstResized || secondResized) {
            replayPos=std::max(replay_prompt_position(first,firstPlayer),replay_prompt_position(second,second.session.player));
            print_replay_prompt(first,firstPlayer,replayPos,replayDisabled ? "The replay was too long to save."
                : replay_save_prompt(first,&second));
        }
        if(replayMode && statistics.page()==PostGamePage::Board){
            // The shared timeline remains interactive on the final frame, so
            // Left can seek back into a completed Local Versus replay.
            Engine::update_replay_playback_controls(first,firstPlayer);
        }
        statistics.render(first,(replayDisabled ?
            UiText::tr("The replay was too long to save.") : replay_save_prompt(first,&second)),results_terminal_size(first));
        if (available_action(first,Input::Action::OpenSettings) ||
            available_action(second,Input::Action::OpenSettings)) {
            Settings::run_menu(first);
            return;
        }
        if (available_action(first,Input::Action::Retry) ||
            available_action(second,Input::Action::Retry)) {
            return;
        }
        if (!replayDisabled &&
            (available_action(first,Input::Action::SaveReplay) ||
             available_action(second,Input::Action::SaveReplay))) {
            const bool inStatistics=statistics.page()!=PostGamePage::Board;
            const ReplaySaveResult result=save_replay_interactively(
                first,firstPlayer,replayPos,replay,saveKeys,&second,inStatistics ? &statistics : nullptr);

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
