#include "multiplayer_battle.h"
#include "online_replay.h"
#include "settings_navigation.h"
#include "terminal_canvas.h"
#include "post_game_statistics.h"
#include "tetr/app/application_state.h"
#include "ui_text.h"
#include "ui_style.h"
#include "tetr/app/settings_menu.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/engine/countdown_presentation.h"
#include "tetr/engine/input_precharge.h"
#include "tetr/network/network_services.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/network/transport.h"
#include "tetr/network/multiplayer_channel.h"
#include "tetr/network/round_statistics.h"
#include "tetr/input/services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <map>
#include <iostream>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

namespace App {
namespace {
class SpectatorFramePacer {
#ifdef _WIN32
    // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is available at runtime on
    // Windows 10 1803+, including with SDKs targeting older Windows versions.
    HANDLE timer_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_MODIFY_STATE|SYNCHRONIZE);
#endif
public:
    ~SpectatorFramePacer() {
#ifdef _WIN32
        if(timer_)CloseHandle(timer_);
#endif
    }
    void wait(Runtime::GameContext& c) {
        auto remaining=Runtime::Duration::from_seconds(.002);
        if(c.session.roomScene.lastPaint!=Runtime::TimePoint{})remaining=std::min(remaining,
            c.session.roomScene.lastPaint+Runtime::Duration::from_seconds(1./120)-c.real_now());
        remaining=std::max(remaining,Runtime::Duration::from_seconds(.0005));
        c.input_services().poll_control_interrupt();
#ifdef _WIN32
        LARGE_INTEGER due;due.QuadPart=-std::max<LONGLONG>(1,c.services.clock.wall_duration(remaining).count()*10);
        if(timer_ && SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE) &&
           WaitForSingleObject(timer_,INFINITE)==WAIT_OBJECT_0)return;
#endif
        c.services.clock.sleep_for(remaining);
    }
};
bool action(Runtime::GameContext& c,Input::Action action,std::map<int,bool>& held) {
    bool pressed=false;
    for(const auto& [key,a]:c.input_services().bindings.entries()) if(a==action) {
        const bool down=c.input_services().key_down(key);if(down && !held[key]) pressed=true;held[key]=down;
    }
    return pressed;
}
std::map<int,bool> held_actions(Runtime::GameContext& c) {
    std::map<int,bool> held;for(const auto& [key,a]:c.input_services().bindings.entries()) held[key]=c.input_services().key_down(key);return held;
}
}
Engine::CountdownResult wait_for_room_round(Runtime::GameContext& c,Engine::MovementInputState& movement) {
    auto& n=Network::network_services(c);auto held=held_actions(c);
    auto last=std::chrono::steady_clock::time_point{};
    for(;;) {
        Network::poll_connection(n);
        if(n.connection!=Network::ConnectionState::Connected) throw Network::ConnectionError(n.connectionMessage);
        if(n.matchEpoch!=c.session.roomScene.epoch || n.matchCanceled || n.matchPhase==Network::RoomMatchPhase::Idle)
            throw Network::LobbyInterrupted("Match changed before start");
        if(n.matchPhase==Network::RoomMatchPhase::Finished) throw Network::LobbyInterrupted("Match ended before start");
        if(n.matchPhase==Network::RoomMatchPhase::Playing) return Engine::CountdownResult::Completed;
        const auto now=std::chrono::steady_clock::now();
        if(now-last>=std::chrono::milliseconds(25)) {
            Network::send_lobby_signal(c,Network::ClientLobbySignal::CountdownComplete);last=now;
        }
        Engine::precharge_movement_input(c,movement);
        for(auto actionId:{Input::Action::Retry,Input::Action::OpenSettings})
            if(action(c,actionId,held) && Network::handle_room_game_control(c,actionId==Input::Action::Retry))
                return actionId==Input::Action::Retry ? Engine::CountdownResult::Retry : Engine::CountdownResult::OpenSettings;
        c.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}
void paint_room_waiting(Runtime::GameContext& c,TerminalCanvas& surface,Render::TerminalSize size,
    std::size_t choice,std::size_t& offset,bool configuring) {
    Render::OutputGuard output(c);
    auto& n=Network::network_services(c);using UiText::tr;
    const bool host=Network::is_room_host(n);
    const auto style=ui_style(c.config.display.obscuredColors);
    const Render::TerminalColors text{style.text,style.background},accent{style.inactiveSelection,style.background},heading{style.heading,style.background};
    const auto selection=style.selectedText,selectedBackground=style.selectedBackground,muted=style.muted;
    const bool controls=host && n.matchPhase==Network::RoomMatchPhase::Preparing;
    const auto insets=ui_insets(size);
    const int left=insets.left,top=insets.top+3,footerTop=size.rows-insets.bottom-3;
    const int width=std::max(1,std::min(76,size.columns-left-insets.right-4));
    const int panelGap=size.rows<24?0:1,footerGap=size.rows<20?0:1;
    const int page=std::clamp(footerTop-top-3-footerGap-(controls?4+panelGap:0),1,15);
    offset=std::min(offset,n.roomMembers.size()>std::size_t(page)?n.roomMembers.size()-page:0);
    int ready=0,battles=0;
    for(const auto& m:n.roomMembers) if(m.role==Network::PlayerRole::Battle) {++battles;if(n.matchPhase==Network::RoomMatchPhase::Preparing && m.ready)++ready;}
    surface.columnsLimit=std::max(0,size.columns-insets.right);surface.rowsLimit=size.rows;surface.begin_frame();
    surface.metadata={Frontend::UiKind::Lobby,"room-waiting",n.room.name};
    if(Frontend::ui_surface_active()) {
        auto& metadata=surface.metadata;
        metadata.chinese=UiText::language()!=UiText::Language::English;
        metadata.hint=tr(configuring?"Preparing player configuration...":"Waiting for the host to start the match...");
        metadata.notice=tr(ready<2?"Need two ready players":"Waiting for host confirmation.");
        metadata.shortcuts={"PgUp/PgDn "+tr("Room users"),
            UiText::binding_label(c.user_settings(),9)+" "+tr("Settings")+"   "+UiText::binding_label(c.user_settings(),8)+" "+tr("Restart")};
        if(controls)metadata.shortcuts[0]+="   "+tr("Enter: Start match   Ctrl+Shift+F12: End current match");
        metadata.lobby.emplace();auto& model=*metadata.lobby;
        model.offset=offset;model.pageSize=page;model.ready=ready;model.battles=battles;
        model.players=n.room.players;model.capacity=n.room.capacity;model.canStart=controls;model.startEnabled=ready>=2;
        for(const auto& member:n.roomMembers)model.members.push_back({member.username,
            tr(member.role==Network::PlayerRole::Offline?"Offline":member.role==Network::PlayerRole::Spectator?"Spectator":"Battle"),
            member.host,member.role==Network::PlayerRole::Battle && n.matchPhase==Network::RoomMatchPhase::Preparing && member.ready});
    }
    const auto line=[&](int row,const std::string& label,TerminalCanvas::Color color) {
        surface.write(row,left,label,std::max(1,size.columns-left-insets.right),color,text.background);
    };
    line(insets.top,tr("Match start waiting")+" / "+Engine::compact_player_name(n.room.name,std::max(1,width-24)),heading.foreground);
    line(insets.top+1,tr(configuring?"Preparing player configuration...":"Waiting for the host to start the match..."),text.foreground);
    surface.box(top,left,width,page+1,tr("Room users"),heading.foreground,text.foreground,text.background);
    surface.write(top+1,left+2,tr("Ready")+" "+std::to_string(ready)+"/"+std::to_string(battles)+
        "   "+tr("Player")+" "+std::to_string(n.room.players)+"/"+std::to_string(n.room.capacity),width,accent.foreground,text.background);
    for(int index=0;index<page && offset+index<n.roomMembers.size();++index) {
        const auto& m=n.roomMembers[offset+index];
        const auto status=tr(m.role==Network::PlayerRole::Offline?"Offline":m.role==Network::PlayerRole::Spectator?"Spectator":"Battle")+
            (m.role==Network::PlayerRole::Battle?" / "+tr(n.matchPhase==Network::RoomMatchPhase::Preparing && m.ready?"Ready":"Not ready"):"");
        const auto suffix=(m.host?" [HOST]":"")+std::string("  ")+status;
        const int nameWidth=std::max(1,width-int(Util::TerminalText::width(suffix))-2);
        surface.write(top+2+index,left+2,Engine::compact_player_name(m.username,nameWidth)+suffix,width,
            m.ready?accent.foreground:m.role!=Network::PlayerRole::Battle?muted:text.foreground,text.background);
    }
    if(controls) {
        const int x=left,y=top+page+3+panelGap,w=width;
        surface.box(y,x,w,1,tr("Match controls"),heading.foreground,text.foreground,text.background);
        surface.write(y+1,x+2,"> "+tr("Start match now"),w,ready<2?muted:selection,selectedBackground);
    }
    const auto shortcuts=UiText::binding_label(c.user_settings(),9)+" "+tr("Settings")+"   "+
        UiText::binding_label(c.user_settings(),8)+" "+tr("Restart");
    line(footerTop,tr(ready<2?"Need two ready players":"Waiting for host confirmation."),muted);
    line(footerTop+1,controls?tr("Enter: Start match   Ctrl+Shift+F12: End current match"):"PgUp/PgDn "+tr("Room users"),muted);
    line(footerTop+2,shortcuts,muted);
    surface.present({},c.config.display.obscuredColors);
}
bool wait_for_room_ready(Runtime::GameContext& c) {
    using namespace Settings;auto& n=Network::network_services(c);
    NavigationInput input(c);auto held=held_actions(c);std::size_t choice=0,offset=0;
    std::uint64_t revision=~std::uint64_t{};auto last=std::chrono::steady_clock::time_point{};
    auto size=Render::terminal_size();
    TerminalCanvas surface;
    for(;;) {
        Network::poll_connection(n);Engine::update_multiplayer_scene(c);
        if(n.connection!=Network::ConnectionState::Connected) throw Network::ConnectionError(n.connectionMessage);
        if(n.matchCanceled || n.matchPhase==Network::RoomMatchPhase::Idle) throw RoomSettingsRequested{};
        if(n.matchEpoch!=c.session.roomScene.epoch) throw Network::LobbyInterrupted("New match started");
        if(n.matchPhase==Network::RoomMatchPhase::Countdown || n.matchPhase==Network::RoomMatchPhase::Playing) {
            const bool missingSetup=std::any_of(n.roomMembers.begin(),n.roomMembers.end(),[&](const auto& m) {
                return m.participating && m.slot!=n.roomPlayerIndex && !n.playerSetups.count(m.slot);
            });
            if(missingSetup) {c.services.clock.sleep_for(Runtime::Duration::from_seconds(.005));continue;}
            // Finish clock negotiation before showing the countdown. Mixed
            // versions explicitly report the legacy completion barrier.
            if(n.matchPhase==Network::RoomMatchPhase::Countdown && n.countdownClockAvailable &&
               !n.countdownDeadline && !n.countdownLegacy) {c.services.clock.sleep_for(Runtime::Duration::from_seconds(.005));continue;}
            bool participant=false;for(const auto& m:n.roomMembers) if(m.slot==n.roomPlayerIndex) participant=m.participating && m.alive;
            Engine::draw_multiplayer_scene(c,true);return participant;
        }
        if(n.matchPhase==Network::RoomMatchPhase::Finished) return false;
        const auto now=std::chrono::steady_clock::now();
        if(now-last>=std::chrono::milliseconds(250)) {
            Network::send_lobby_signal(c,Network::ClientLobbySignal::ReadyForConfig);
            Network::send(n,n.localPlayerSetup);
            for(const auto& m:n.roomMembers) if(m.slot==n.roomPlayerIndex && m.role==Network::PlayerRole::Battle && !m.ready)
                Network::request_room(n,Network::RoomOperation::SetReady,{},true,true,1);
            last=now;
        }
        const bool host=Network::is_room_host(n);int ready=0,battles=0;
        for(const auto& m:n.roomMembers) if(m.role==Network::PlayerRole::Battle) {++battles;if(m.ready) ++ready;}
        const auto current=Render::terminal_size();
        if(revision!=n.roomRevision || current!=size) {
            revision=n.roomRevision;size=current;
            paint_room_waiting(c,surface,size.value_or(Render::TerminalSize{80,40}),choice,offset);
        }
        if(input.update(NavigationKey::PageDown,true)) {offset+=8;revision=~std::uint64_t{};}
        if(input.update(NavigationKey::PageUp,true)) {offset=offset>8?offset-8:0;revision=~std::uint64_t{};}
        if(host && (input.update(NavigationKey::Confirm,false) || input.update(NavigationKey::Right,false))) {
            if(ready>=2) Network::request_room(n,Network::RoomOperation::ForceStart);
        }
        if(action(c,Input::Action::OpenSettings,held)) {
            Network::send_lobby_signal(c,Network::ClientLobbySignal::Cancel);Settings::run_menu(c);
            c.session.roomScene.enabled=false;throw Network::LobbyInterrupted("Settings changed");
        }
        if(action(c,Input::Action::Retry,held)) {
            Network::send_lobby_signal(c,Network::ClientLobbySignal::Cancel);
            throw Network::LobbyInterrupted("Prepare again");
        }
        c.services.clock.sleep_for(Runtime::Duration::from_seconds(0.005));
    }
}
void watch_room_match(Runtime::GameContext& c,bool localLost) {
    using namespace Settings;auto& n=Network::network_services(c);const auto epoch=c.session.roomScene.epoch;
    NavigationInput input(c);auto held=held_actions(c);Engine::draw_multiplayer_scene(c,true);
    SpectatorFramePacer framePacer;
    auto lastLoss=std::chrono::steady_clock::time_point{};
    std::string shownStatus;std::uint64_t shownRevision=~std::uint64_t{};
    Render::TerminalSize shownSize{0,0};
    std::unique_ptr<PostGameStatisticsView> results;
    std::vector<int> actors;
    std::size_t shownResults=0;
    auto lastRequest=std::chrono::steady_clock::time_point{};
    auto savedInterrupt=c.input_services().controlInterrupt;
    struct RestoreInterrupt {Input::Services& input;std::function<void()> saved;~RestoreInterrupt(){input.controlInterrupt=std::move(saved);}} restore{c.input_services(),savedInterrupt};
    c.input_services().controlInterrupt=[&] {
        if(results)return;
        if(savedInterrupt)savedInterrupt();
    };
    const auto refresh_results=[&] {
        auto& cache=n.roundResultWires[epoch];
        if(results && cache.size()==shownResults)return;
        const int selected=results?results->player_index():0;
        const auto page=results?results->page():PostGamePage::Summary;
        const int row=results?results->selected_row():0,column=results?results->selected_column():0,metric=results?results->selected_metric():0;
        actors=n.roundParticipants[epoch];
        if(actors.empty())for(const auto& entry:cache)actors.push_back(entry.first);
        if(actors.empty())return;
        std::vector<PostGameStatistics> data;std::vector<std::string> names;
        for(int slot:actors) {
            names.push_back(n.roundResultNames[epoch][slot]);
            const auto found=cache.find(slot);const auto result=found==cache.end()?std::optional<Network::RoundStatistics>{}:Network::decode_round_statistics(found->second);
            if(!result) {PostGameStatistics waiting;waiting.metrics={{"STATUS",UiText::tr("Waiting for player statistics...")}};data.push_back(std::move(waiting));continue;}
            auto session=std::make_unique<Runtime::SessionState>();Runtime::GameContext view(*session,c.services);
            view.config.mode.mode=Runtime::GameMode::Multiplayer;view.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
            view.config.simulationPps=result->pieceTime?1:0;session->player.gameplay.scoring=result->score;
            session->player.timing.completedGameTime=Runtime::Duration::from_seconds(result->gameSeconds);
            session->player.timing.completedRealTime=Runtime::Duration::from_seconds(result->realSeconds);
            data.push_back(make_post_game_statistics(view,session->player));
        }
        const bool first=!results;
        results=std::make_unique<PostGameStatisticsView>(std::move(data),std::move(names));
        int choose=selected;
        if(first) {auto own=std::find(actors.begin(),actors.end(),c.session.roomScene.ownSlot);if(own!=actors.end())choose=static_cast<int>(own-actors.begin());}
        results->select_player(choose);
        if(page==PostGamePage::Clears) {results->next_page();results->move(row,column);}else results->move(metric,0);
        shownResults=cache.size();lastRequest={};
    };
    for(;;) {
        Network::poll_connection(n);
        // Keep the completed round visible until this player chooses Retry.
        // Receiving a new roster must not revive its eliminated players or
        // mix the next round's frames into their frozen boards.
        if(n.matchEpoch==epoch) {
            if(results) {
                Render::begin_output_suppression();
                try {Engine::poll_multiplayer_scene(c,false);}catch(...){Render::end_output_suppression();throw;}
                Render::end_output_suppression();
            } else Engine::poll_multiplayer_scene(c,false);
        }
        const bool nextMatch=n.matchEpoch!=epoch;
        if(nextMatch && !c.session.roomScene.finished) {
            if(!c.session.roomScene.started)return;
            Engine::finish_multiplayer_scene(c,true);
        }
        if(n.connection!=Network::ConnectionState::Connected) throw Network::ConnectionError(n.connectionMessage);
        if(n.matchCanceled || n.matchPhase==Network::RoomMatchPhase::Idle) throw RoomSettingsRequested{};
        const auto now=std::chrono::steady_clock::now();
        const auto own=std::find_if(n.roomMembers.begin(),n.roomMembers.end(),[&](const auto& m){return m.slot==c.session.roomScene.ownSlot;});
        if(!nextMatch && localLost && own!=n.roomMembers.end() && own->alive && n.matchPhase==Network::RoomMatchPhase::Playing && now-lastLoss>=std::chrono::milliseconds(250)) {
            Network::send_round_signal(c,Network::OutgoingRoundSignal::LocalLoss);lastLoss=now;
        }
        if(action(c,Input::Action::SaveReplay,held) && (c.session.roomScene.finished || c.session.player.timing.completedGameTime)) {
            save_finished_online_replay(c,results.get());
            if(results)results->invalidate();else Engine::draw_multiplayer_scene(c,true);
            shownStatus.clear();input.suppress_held_keys();
        }
        if(results) {
            refresh_results();results->poll(input);
            if(results->page()==PostGamePage::Board || input.update(NavigationKey::Back,false) || input.update(NavigationKey::Backspace,false)) {
                c.input_services().discard_control_events();
                c.input_services().suppress_control_key_until_release(NavigationInput::key_code(NavigationKey::Back));
                results.reset();Engine::draw_multiplayer_scene(c,true);shownStatus.clear();input.suppress_held_keys();continue;
            }
            const int slot=actors[results->player_index()];
            if(!n.roundResultWires[epoch].count(slot) && now-lastRequest>=std::chrono::milliseconds(250)) {
                Network::request_round_statistics(n,epoch,slot);lastRequest=now;
            }
            results->render(c,UiText::tr("Left/Right: Player  Tab: Summary/Clears  Up/Down: Scroll  PgUp/PgDn: Page  Esc: Board")+"  "+UiText::binding_label(c.user_settings(),10)+" "+UiText::tr("Save Replay"),
                Render::terminal_size().value_or(Render::TerminalSize{120,40}));
            c.services.clock.sleep_for(Runtime::Duration::from_seconds(0.01));continue;
        }
        if(input.update(NavigationKey::Tab,false)) {
            if(c.session.roomScene.finished) {refresh_results();if(results){input.suppress_held_keys();continue;}}
            else Engine::cycle_multiplayer_focus(c);
        }
        if(action(c,Input::Action::Retry,held) && (nextMatch || c.session.roomScene.finished || n.matchPhase==Network::RoomMatchPhase::Finished)) {
            n.startFromMenu=true;c.session.roomScene.enabled=false;return;
        }
        if(action(c,Input::Action::OpenSettings,held)) {
            Network::handle_room_game_control(c,false);Settings::run_menu(c);return;
        }
        const auto dimensions=Render::terminal_size().value_or(Render::TerminalSize{80,35});
        std::string status="Tab "+UiText::tr(c.session.roomScene.finished?"Result details":"Player")+"   "+
            UiText::binding_label(c.user_settings(),8)+" "+UiText::tr("Restart")+"   "+
            UiText::binding_label(c.user_settings(),9)+" "+UiText::tr("Settings");
        if(nextMatch) {
            status=UiText::tr("The host has started the match.")+" / "+UiText::tr("Retry: Prepare match.");
        }
        if(c.session.roomScene.finished || c.session.player.timing.completedGameTime)status+="   "+UiText::binding_label(c.user_settings(),10)+" "+UiText::tr("Save Replay");
        if(status!=shownStatus || shownRevision!=c.session.roomScene.revision || dimensions!=shownSize) {
            Render::set_ui_color(Render::ColorRole::Text,c.config.display.obscuredColors);Render::move_cursor(dimensions.rows-1,0);
            const auto clipped=Util::TerminalText::clip(status,dimensions.columns);
            std::cout<<clipped<<std::string(dimensions.columns-Util::TerminalText::width(clipped),' ');Render::flush();
            shownStatus=status;shownRevision=c.session.roomScene.revision;shownSize=dimensions;
        }
        if(c.session.roomScene.finished)c.services.clock.sleep_for(Runtime::Duration::from_seconds(0.01));
        else framePacer.wait(c);
    }
}
} // namespace App
