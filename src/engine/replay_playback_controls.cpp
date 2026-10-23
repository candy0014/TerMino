#include "tetr/engine/replay_playback_controls.h"

#include "tetr/app/application_state.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/undo.h"
#include "tetr/engine/game_terminal_layout.h"
#include "tetr/input/services.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/movement_event.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/constants.h"
#include "tetr/util/numeric.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/util/terminal_text.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif

namespace Engine {
namespace {

std::vector<int> pause_keys(const Runtime::GameContext& context) {
    std::vector<int> keys;
    for (const auto& [key, action] : context.input_services().bindings.entries())
        if (action == Input::Action::Pause) keys.push_back(key);
    return keys;
}

bool pause_key_down(Input::Services& input, const std::vector<int>& keys, bool disabled) {
    if (disabled) return false;
    bool down=false;
    for (int key:keys) down|=input.control_key_pressed(key);
#ifdef _WIN32
    return down || input.key_down(VK_SPACE);
#else
    return down || input.key_down(KEY_SPACE);
#endif
}

bool slower_key_down(Input::Services& input) {
#ifdef _WIN32
    return input.key_down(VK_OEM_MINUS) || input.key_down(VK_SUBTRACT);
#else
    return input.key_down(KEY_MINUS) || input.key_down(KEY_KPMINUS);
#endif
}

bool faster_key_down(Input::Services& input) {
#ifdef _WIN32
    return input.key_down(VK_OEM_PLUS) || input.key_down(VK_ADD);
#else
    return input.key_down(KEY_EQUAL) || input.key_down(KEY_KPPLUS);
#endif
}

bool backward_key_down(Input::Services& input) {
#ifdef _WIN32
    return input.key_down(VK_LEFT);
#else
    return input.key_down(KEY_LEFT);
#endif
}

bool forward_key_down(Input::Services& input) {
#ifdef _WIN32
    return input.key_down(VK_RIGHT);
#else
    return input.key_down(KEY_RIGHT);
#endif
}

std::array<bool,5> read_controls(Input::Services& input,
    const std::vector<int>& pauseKeys, bool disablePause) {
    return {pause_key_down(input,pauseKeys,disablePause),slower_key_down(input),
        faster_key_down(input),backward_key_down(input),forward_key_down(input)};
}

bool rising_edge(bool down, bool& wasDown) {
    const bool pressed=down&&!wasDown;
    wasDown=down;
    return pressed;
}

bool replay_seek_pending(Runtime::GameContext& context) {
    auto* app=context.app_component();
    if (!app) return false;
    std::lock_guard<std::recursive_mutex> lock(app->terminalOutputMutex);
    return app->replayResume.pending;
}

bool request_replay_seek(Runtime::GameContext& context, double offsetSeconds) {
    auto* app=context.app_component();
    if (!app) return false;

    std::lock_guard<std::recursive_mutex> lock(app->terminalOutputMutex);
    if (app->replayResume.pending) return true;

    Runtime::GameContext* timeline=app->primaryContext;
    if (!timeline || !timeline->config.mode.replayMode ||
        !timeline->replay_component()) {
        timeline=&context;
    }
    const Runtime::TimePoint now=timeline->now();
    const Runtime::Duration current=Replay::playback_progress_elapsed(*timeline,now);
    const Runtime::Duration duration=Replay::playback_duration(*timeline);
    const double targetSeconds=std::clamp(
        current.seconds()+offsetSeconds,0.0,std::max(0.0,duration.seconds()));
    if (std::fabs(targetSeconds-current.seconds())<1e-8) return false;

    app->replayResume.pending=true;
    app->replayResume.targetSeconds=targetSeconds;
    app->replayResume.speed=Replay::playback_speed(*timeline);
    app->replayResume.paused=Replay::playback_paused(*timeline);
    Render::begin_output_suppression();
    return true;
}

bool rebuild_has_due_operation(Runtime::GameContext& context) {
    const auto& operations=Replay::session(context).operations;
    return !operations.empty()&&Replay::event_due(
        operations.front(),Replay::playback_elapsed(context,context.now()));
}

void redraw_rebuilt_player(Runtime::GameContext& context) {
    auto& player=context.session.player;
    synchronize_field_visibility(context,player);
    Render::redraw_player_board(context,player);
    GarbageService::redraw_buffer(context,player);
}

void finish_rebuild_display(
    Runtime::GameContext& primary,
    Runtime::GameContext* second) {
    Replay::complete_playback_resume(primary,primary.now());
    if(second) Replay::complete_playback_resume(*second,second->now());

    Render::end_output_suppression();
    if (const auto size=Render::terminal_size()) center_game_boards(primary,*size,second);
    Render::clear_screen();
    redraw_rebuilt_player(primary);
    if(second) redraw_rebuilt_player(*second);
    render_replay_playback_progress(primary,primary.session.player);
}

bool complete_rebuild_if_ready(Runtime::GameContext& context) {
    auto& playback=Replay::session(context).playback;
    if(!playback.rebuilding) return true;

    auto finish=[&]() {
        if(!playback.rebuildComplete){
            if(rebuild_has_due_operation(context)) return false;
            playback.rebuildComplete=true;
        }

        Runtime::GameContext* primary=&context;
        Runtime::GameContext* second=nullptr;
        if(auto* app=context.app_component()){
            if(app->primaryContext&&app->primaryContext->config.mode.replayMode){
                primary=app->primaryContext;
            }
            if(Replay::session(*primary).localVersusReplay){
                second=app->localPlayerTwoContext;
            }
        }
        if(!Replay::session(*primary).playback.rebuildComplete) return false;
        if(second&&!Replay::session(*second).playback.rebuildComplete) return false;
        finish_rebuild_display(*primary,second);
        return true;
    };

    if(auto* app=context.app_component()){
        std::lock_guard<std::recursive_mutex> lock(app->terminalOutputMutex);
        return finish();
    }
    return finish();
}

std::string format_replay_time(Runtime::Duration value) {
    return Util::format_time(std::max(0.0,value.seconds()));
}

std::string progress_bar(
    Runtime::Duration elapsed,
    Runtime::Duration duration,
    int width,
    bool finished) {
    width=std::max(12,width);
    double ratio=duration>Runtime::Duration{}
        ? elapsed.seconds()/duration.seconds()
        : (finished ? 1.0 : 0.0);
    ratio=std::clamp(ratio,0.0,1.0);
    const int marker=std::min(
        width-1,static_cast<int>(ratio*static_cast<double>(width-1)));
    std::string bar(static_cast<std::size_t>(width),'-');
    for (int index=0;index<marker;++index) bar[static_cast<std::size_t>(index)]='=';
    bar[static_cast<std::size_t>(marker)]='|';
    return '['+bar+']';
}

int progress_row(
    Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool compact=false, bool maximum=false) {
    const int configured=Replay::session(context).playback.progressRow;
    if ((compact || player.presentation.dynamicLayout) && Replay::session(context).localVersusReplay) {
        auto* app=context.app_component();
        if (app && app->primaryContext && app->localPlayerTwoContext) {
            const auto bottom=[&](Runtime::GameContext& owner) {
                return Render::board_vertical_extent(owner,owner.session.player,maximum).footer+
                    (owner.config.garbage.mode==Runtime::GarbageMode::QuickPlay ? 4 : 2);
            };
            return std::max(bottom(*app->primaryContext),bottom(*app->localPlayerTwoContext));
        }
    }
    if (configured>=0) return configured;
    // Reserve the QP footer's full height even while its short altitude fits
    // directly below the field, so later booster levels cannot hit the timeline.
    int row=Render::board_vertical_extent(context,player,compact && maximum).footer+
        (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay ? 4 : 2);
    if (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay)
        for (const auto& span:player.presentation.altitudeDisplay)
            if (span.width) row=std::max(row,span.row-player.presentation.topMargin+1);
    return row;
}

struct ProgressText { std::vector<std::string> rows; int counters=-1; };

bool chinese_interface(const Runtime::GameContext& context) {
    const auto* settings=context.user_settings_component();
    return settings && settings->uiLanguage==0;
}

ProgressText progress_text(Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, Runtime::Duration elapsed) {
    const Runtime::Duration duration=Replay::playback_duration(context);
    const int barWidth=std::clamp(player.gameplay.playfield.width*2,12,20);
    const std::string progressLine=progress_bar(elapsed,duration,barWidth,
        Replay::playback_finished(context))+' '+
        format_replay_time(elapsed)+" / "+format_replay_time(duration);
    std::ostringstream speedText;
    speedText<<std::setprecision(6)<<std::defaultfloat
             <<Replay::playback_speed(context);
    std::ostringstream controls;
    const bool paused=Replay::playback_paused(context);
    if (chinese_interface(context)) {
        controls<<(paused ? "已暂停" : "播放中")<<"  倍速："<<speedText.str()
            <<"×  暂停键/空格："<<(paused ? "继续" : "暂停")
            <<"\n-/+：倍速  ←/→：±10秒";
    } else {
        controls<<(paused?"PAUSE":"PLAY")<<' '<<speedText.str()
            <<"x  Pause/Space: "<<(paused?"Play":"Pause")
            <<"\n-/+: Speed  Left/Right: -/+10s";
    }
    const auto layout=Render::board_text_layout(context,player);
    ProgressText result;
    const auto append=[&](const std::string& text) {
        const auto rows=Util::TerminalText::wrap_words(text,layout.columns);
        result.rows.insert(result.rows.end(),rows.begin(),rows.end());
    };
    append(progressLine);
    append(controls.str());
    if (context.config.mode.mode!=Runtime::GameMode::Multiplayer &&
        context.config.mode.mode!=Runtime::GameMode::LocalVersus &&
        context.config.garbage.mode!=Runtime::GarbageMode::Multiplayer &&
        !Replay::session(context).localVersusReplay) {
        result.counters=static_cast<int>(result.rows.size());
        const bool chinese=chinese_interface(context);
        append((chinese ? "撤回：" : "UNDO: ")+std::to_string(Replay::session(context).undoCount)+
            (chinese ? "  暂停：" : "  PAUSE: ")+std::to_string(Replay::session(context).pauseCount));
    }
    return result;
}

void render_progress(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::TimePoint now) {
    const auto text=progress_text(context,player,Replay::playback_progress_elapsed(context,now));
    const int row=progress_row(context,player);
    const int column=Render::board_text_layout(context,player).column;
    // Local Versus can place the second board only 40 columns after the
    // first.  Clear exactly one compact panel rather than a fixed wide span,
    // which could erase the other player's board or timeline.
    auto& playback=Replay::session(context).playback;
    std::size_t width=0;
    for (const auto& line:text.rows) width=std::max(width,Util::TerminalText::width(line));
    const std::size_t clearWidth=std::max(width,playback.lastProgressWidth);
    const std::size_t clearRows=std::max(text.rows.size(),playback.lastProgressRows);
    playback.lastProgressWidth=width;
    playback.lastProgressRows=text.rows.size();

    Render::OutputGuard output(context);
    Render::set_ui_color(
        context.config.display.obscuredColors
            ? Render::ColorRole::Ghost
            : Render::ColorRole::Text,
        context.config.display.obscuredColors);
    auto& undoDisplay=player.presentation.undoCountDisplay;
    if (undoDisplay.width) {
        Render::move_cursor(undoDisplay.row,undoDisplay.column);
        std::cout<<std::string(undoDisplay.width,' ');
        undoDisplay={};
    }
    for (std::size_t index=0;index<clearRows;++index) {
        const int y=row+player.presentation.topMargin+static_cast<int>(index);
        Render::move_cursor(y,column);
        std::cout<<std::string(clearWidth,' ');
        if (index<text.rows.size()) {
            Render::move_cursor(y,column);
            std::cout<<text.rows[index];
        }
    }
    if (text.counters>=0) undoDisplay={row+player.presentation.topMargin+text.counters,
        column,Util::TerminalText::width(text.rows[text.counters])};
    Render::flush();
}

} // namespace

void share_replay_playback_controls(Runtime::GameContext& first, Runtime::GameContext& second) {
    auto shared=Replay::session(first).playback.shared;
    if (!shared || shared!=Replay::session(second).playback.shared) return;
    auto input=std::make_shared<Input::Services>();
    Input::share_key_state(*input,first.input_services());
    auto keys=pause_keys(first);
    for (int key:pause_keys(second))
        if (std::find(keys.begin(),keys.end(),key)==keys.end()) keys.push_back(key);
    input->begin_gameplay_input();input->discard_control_events();
    shared->readControls=[input,keys=std::move(keys),disabled=first.config.disablePause || second.config.disablePause] {
        return read_controls(*input,keys,disabled);
    };
    shared->controlsDown=shared->readControls();
}

Render::TerminalSize replay_playback_terminal_size(Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player, bool compact, bool maximum) {
    if (!context.config.mode.replayMode || !context.replay_component() ||
        !Replay::session(context).playback.progressVisible) return {0,0};
    // The elapsed time can eventually have as many minute digits as duration.
    const auto text=progress_text(context,player,Replay::playback_duration(context));
    const int row=progress_row(context,player,compact,maximum);
    std::size_t width=0;
    for (const auto& line:text.rows) width=std::max(width,Util::TerminalText::width(line));
    return {Render::board_text_layout(context,player).column+static_cast<int>(width),
        row+(compact ? 0 : player.presentation.topMargin)+static_cast<int>(text.rows.size())};
}

void render_replay_playback_progress(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player) {
    if (!context.config.mode.replayMode) return;
    auto& playback=Replay::session(context).playback;
    if (!playback.progressVisible) return;
    const Runtime::TimePoint now=context.now();
    render_progress(context,player,now);
    playback.lastProgressRender=now;
    playback.progressRendered=true;
}

void update_replay_playback_controls(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    bool forceRender) {
    if (!context.config.mode.replayMode) return;
    if (context.input_component()) context.input_services().poll_control_interrupt();
    refresh_game_terminal_layout(context);
    poll_undo_key(context);
    if (replay_seek_pending(context)) throw ReplaySeekRequested{};
    if (Replay::playback_rebuilding(context)&&
        !complete_rebuild_if_ready(context)) return;
    auto& playback=Replay::session(context).playback;
    const Runtime::TimePoint now=context.now();
    std::array<bool,5> pressed{};
    if (playback.shared) {
        // The dedicated reader has no navigation callback and is only polled
        // under this lock. Neither worker touches the other's input cursor.
        auto& shared=*playback.shared;
        std::lock_guard<std::recursive_mutex> lock(shared.mutex);
        const auto down=shared.readControls();
        for (std::size_t i=0;i<pressed.size();++i)
            pressed[i]=rising_edge(down[i],shared.controlsDown[i]);
        const auto at=context.now();
        if (pressed[0]) Replay::toggle_playback_pause(context,at);
        if (pressed[1]) Replay::set_playback_speed(context,at,shared.speed/2.0);
        if (pressed[2]) Replay::set_playback_speed(context,at,shared.speed*2.0);
        context.config.timeMultiplier=shared.speed;
    } else {
        const auto down=read_controls(context.input_services(),pause_keys(context),context.config.disablePause);
        std::array<bool*,5> held{&playback.pauseKeyDown,&playback.slowerKeyDown,&playback.fasterKeyDown,
            &playback.backwardKeyDown,&playback.forwardKeyDown};
        for (std::size_t i=0;i<pressed.size();++i) {
            if (playback.controlsInitialized) pressed[i]=rising_edge(down[i],*held[i]);
            else *held[i]=down[i];
        }
        if (pressed[0]) Replay::toggle_playback_pause(context,now);
        if (pressed[1]) Replay::set_playback_speed(context,now,Replay::playback_speed(context)/2.0);
        if (pressed[2]) Replay::set_playback_speed(context,now,Replay::playback_speed(context)*2.0);
    }
    if (!playback.controlsInitialized) {
        playback.controlsInitialized=true;
        forceRender=true;
    }
    forceRender=forceRender || pressed[0] || pressed[1] || pressed[2];
    if (pressed[3] && request_replay_seek(context,-10.0)) throw ReplaySeekRequested{};
    if (pressed[4] && request_replay_seek(context,10.0)) throw ReplaySeekRequested{};

    if (!playback.progressVisible) return;
    const bool renderDue=!playback.progressRendered || forceRender ||
        (!Replay::playback_paused(context) &&
         now-playback.lastProgressRender>=Runtime::Duration::from_seconds(0.05));
    if (!renderDue) return;
    render_progress(context,player,now);
    playback.lastProgressRender=now;
    playback.progressRendered=true;
}

void complete_replay_rebuild_at_game_end(Runtime::GameContext& context) {
    if(!context.config.mode.replayMode||!Replay::playback_rebuilding(context)) return;

    const auto finish=[&]() {
        Runtime::GameContext* primary=&context;
        Runtime::GameContext* second=nullptr;
        if(auto* app=context.app_component()){
            if(app->primaryContext&&app->primaryContext->config.mode.replayMode){
                primary=app->primaryContext;
            }
            if(Replay::session(*primary).localVersusReplay){
                second=app->localPlayerTwoContext;
            }
        }
        Replay::session(*primary).playback.rebuildComplete=true;
        if(second) Replay::session(*second).playback.rebuildComplete=true;
        finish_rebuild_display(*primary,second);
    };

    if(auto* app=context.app_component()){
        std::lock_guard<std::recursive_mutex> lock(app->terminalOutputMutex);
        finish();
        return;
    }
    finish();
}

} // namespace Engine
