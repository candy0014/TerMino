#include "room_replay_playback.h"
#include "online_replay.h"
#include "init_player_setup.h"
#include "settings_navigation.h"
#include "post_game_statistics.h"
#include "ui_text.h"
#include "tetr/app/application_state.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/app/settings_menu.h"
#include "tetr/config/normalization.h"
#include "tetr/engine/multiplayer_scene.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/network/round_statistics.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/text_codec.h"
#include "tetr/util/terminal_text.h"
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <stdexcept>
namespace App {
struct RoomReplayPlayback::Impl {
    struct Track {
        const Replay::MultiplayerReplayTrack* source=nullptr;
        Runtime::RuntimeConfig config;
        std::vector<Replay::JournalRecord> frames;
        std::vector<std::size_t> boards;
        std::size_t next=0;
        std::optional<Network::RemoteClockSample> clock;
        std::uint64_t clockTime=0;
    };
    Runtime::GameContext& c;
    const Replay::MultiplayerReplay& archive;
    std::vector<Track> tracks;
    std::uint64_t cursor=0;
    std::size_t nextTarget=0;
    bool ok=true;
    Impl(Runtime::GameContext& context,const Replay::MultiplayerReplay& source):c(context),archive(source) {
        auto& s=c.session.roomScene;s={};s.enabled=true;s.started=true;s.spectator=true;s.selfAlive=false;s.ownSlot=-1;s.epoch=archive.epoch;s.roomInstance=archive.room;
        auto& n=Network::network_services(c);n.boardViewsEnabled=false;n.multiplayerRoom=true;n.matchEpoch=archive.epoch;n.roomInstance=archive.room;n.roomPlayerIndex=-1;
        n.room.name=UiText::tr("Room replay");
        n.roomMessage=UiText::tr(archive.complete()?"Tab: Player / Results   Esc: Back":"Incomplete recording");
        n.playerSetups.clear();n.roomMembers.clear();n.peerMiniBoards.clear();
        for(const auto& sourceTrack:archive.tracks) {
            Track t;t.source=&sourceTrack;t.config=c.config;auto bytes=std::string_view(sourceTrack.journal);
            bool recordedConfig=false;
            while(!bytes.empty()) {
                Replay::JournalRecord record;if(!Replay::take_journal_record(bytes,record)){ok=false;break;}
                if(record.kind=='C') {
                    Replay::ConfigPrefixResult prefix;
                    if(!Replay::decode_config_prefix(record.data,t.config,prefix)){ok=false;break;}
                    recordedConfig=true;
                } else if(record.kind=='B' || record.kind=='M' || record.kind=='F') {
                    if(record.kind=='B')t.boards.push_back(t.frames.size());t.frames.push_back(record);
                }
            }
            if(!recordedConfig)if(const auto setup=Network::decode_lobby_player_setup(sourceTrack.setup)) {
                t.config.field.width=setup->width;t.config.field.height=setup->visibleHeight;t.config.field.nextCount=setup->nextCount;t.config.field.holdEnabled=setup->holdEnabled;
            }
            if(t.config.field.width<1 || t.config.field.width>320 || t.config.field.height<0 || t.config.field.height>512){ok=false;break;}
            auto name=sourceTrack.name+" · #"+std::to_string(sourceTrack.slot+1);
            if(sourceTrack.actor==archive.viewer){name+=" ["+UiText::tr("You")+"]";s.focusSlot=sourceTrack.slot;}
            if(!sourceTrack.complete)name+=" ["+UiText::tr(sourceTrack.journal.empty()?"Missing recording":"Incomplete recording")+"]";
            s.usernames[sourceTrack.slot]=name;s.participants.push_back(sourceTrack.slot);s.killers[sourceTrack.slot]=sourceTrack.killer;
            n.roomMembers.push_back({sourceTrack.slot,name,false,Network::PlayerRole::Battle,false,true,true});
            tracks.push_back(std::move(t));
        }
        if(s.focusSlot<0 && !tracks.empty())s.focusSlot=tracks.front().source->slot;
        for(auto& t:tracks)reset(t);
        if(!tracks.empty())c.session.player=c.session.roomScene.players.at(tracks.front().source->slot);
        c.config.mode.mode=Runtime::GameMode::Multiplayer;c.config.mode.replayMode=true;c.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
        c.config.disableUndo=true;c.config.disablePause=false;c.config.simulationPps=0;
        c.session.gameStartTime=c.now();
    }
    void reset(Track& t) {
        auto& p=c.session.roomScene.players[t.source->slot];p=Runtime::PlayerSessionState{};
        InitDetail::reset_player_board(p,t.config);auto& f=p.gameplay.playfield;
        f.activeHeight=f.visibleHeight;f.field.initialize(f.width,f.visibleHeight);
        p.presentation.blockMaxWidth=p.presentation.blockMaxHeight=4;
        if(const auto setup=Network::decode_lobby_player_setup(t.source->setup)) {
            p.presentation.blockMaxWidth=setup->blockMaxWidth;p.presentation.blockMaxHeight=setup->blockMaxHeight;
        }
        const auto& field=t.config.field;
        p.presentation.remoteFieldVisibility=Runtime::FieldVisibility{field.invisible,field.periodicInvisible,field.invisibilityDelay,field.invisibilityInterval,field.invisibilityDuration};
        p.presentation.username=c.session.roomScene.usernames[t.source->slot];
        p.timing.remoteGameTime=Runtime::Duration{};p.timing.remoteTimeRate=0;p.timing.remoteTimeUpdated=c.services.clock.now();
        t.next=0;t.clock.reset();t.clockTime=0;
    }
    bool apply(Track& t,const Replay::JournalRecord& record) {
        auto& p=c.session.roomScene.players[t.source->slot];
        if(record.kind=='B') {
            auto data=record.data;int dimensions[3]{};
            for(int i=0;i<3;++i){const auto separator=data.find(';');if(separator==std::string_view::npos)return false;
                const auto value=Util::Text::parse_integer<int>(data.substr(0,separator));if(!value)return false;dimensions[i]=*value;data.remove_prefix(separator+1);}
            if(dimensions[0]<1 || dimensions[0]>320 || dimensions[1]<0 || dimensions[1]>512 || dimensions[2]<0 || dimensions[2]>100)return false;
            const auto board=Network::decode_board_state(data,dimensions[0],dimensions[1]);if(!board)return false;
            p.gameplay.playfield.nextCount=dimensions[2];Engine::apply_remote_board_state(c,p,*board,false);
            t.clock=board->clock;t.clockTime=record.time;
        } else if(record.kind=='M') {
            const auto motion=Network::decode_piece_motion(record.data);if(!motion)return false;
            if(!Engine::apply_remote_piece_motion(c,p,*motion,false))return false;
            if(motion->clock){t.clock=motion->clock;t.clockTime=record.time;}
        } else if(record.kind=='F') {
            const auto result=Network::decode_round_statistics(record.data);if(!result)return false;
            p.gameplay.scoring=result->score;p.timing.completedGameTime=Runtime::Duration::from_seconds(result->gameSeconds);
            p.timing.completedRealTime=Runtime::Duration::from_seconds(result->realSeconds);
            p.presentation.activePose.reset();p.presentation.activePieceType.reset();
        }
        return true;
    }
    void scene(std::uint64_t time) {
        auto& s=c.session.roomScene;auto& n=Network::network_services(c);
        const auto old=s.survivors;const bool finished=time>=archive.duration;
        s.finished=finished;s.aborted=finished && archive.reason!=Replay::RoundEndReason::Normal;
        s.winnerSlot=finished?archive.winner:-1;n.winnerSlot=s.winnerSlot;
        n.matchPhase=finished?Network::RoomMatchPhase::Finished:Network::RoomMatchPhase::Playing;
        s.survivors.clear();s.alive.clear();
        while(nextTarget<archive.targets.size() && archive.targets[nextTarget].time<=time) {
            const auto& target=archive.targets[nextTarget++];
            for(auto& member:n.roomMembers)if(member.slot==target.slot)member.target=target.target;
        }
        for(auto& t:tracks) {
            auto& p=s.players[t.source->slot];const bool alive=!t.source->eliminatedAt || time<*t.source->eliminatedAt;
            s.alive[t.source->slot]=alive;if(alive)s.survivors.push_back(t.source->slot);
            for(auto& member:n.roomMembers)if(member.slot==t.source->slot)member.alive=alive;
            if(t.clock && !p.timing.completedGameTime) {
                p.timing.remoteGameTime=Runtime::Duration::from_seconds(t.clock->elapsedSeconds+std::max(0.,double(time-t.clockTime)/1000000.)*t.clock->rate);
                p.timing.remoteTimeRate=0;p.timing.remoteTimeUpdated=c.services.clock.now();
            }
            if(!alive){
                if(!p.timing.completedGameTime)p.timing.completedGameTime=p.timing.remoteGameTime.value_or(Runtime::Duration{});
                p.presentation.activePose.reset();p.presentation.activePieceType.reset();
            }
            if(finished && !p.timing.completedGameTime)p.timing.completedGameTime=p.timing.remoteGameTime.value_or(Runtime::Duration{});
        }
        // Keep the last two finalists visible after the winner is decided.
        if(s.survivors.size()==2){s.finalists=s.survivors;s.finalistLayout=s.finalists;}
        const bool focusAlive=std::find(s.survivors.begin(),s.survivors.end(),s.focusSlot)!=s.survivors.end();
        if(!focusAlive && !finished && !s.survivors.empty()) {
            const int killer=s.killers.count(s.focusSlot)?s.killers.at(s.focusSlot):-1;
            s.focusSlot=std::find(s.survivors.begin(),s.survivors.end(),killer)!=s.survivors.end()?killer:s.survivors.front();
        }
        if(s.finalistLayout.size()==2 && s.finalistLayout.back()==s.focusSlot)std::swap(s.finalistLayout.front(),s.finalistLayout.back());
        n.targetSlot=-1;for(const auto& member:n.roomMembers)if(member.slot==s.focusSlot)n.targetSlot=member.target;
        if(old!=s.survivors){++s.revision;s.lastPaint={};}
        cursor=time;
    }
    bool advance(std::uint64_t time) {
        time=std::min(time,archive.duration);if(time<cursor)return seek(time);
        for(auto& t:tracks)while(t.next<t.frames.size() && t.frames[t.next].time<=time)if(!apply(t,t.frames[t.next++]))return ok=false;
        scene(time);return ok;
    }
    bool seek(std::uint64_t time) {
        time=std::min(time,archive.duration);auto& s=c.session.roomScene;
        const int focus=s.focusSlot;s.finalists.clear();s.finalistLayout.clear();
        nextTarget=0;for(auto& member:Network::network_services(c).roomMembers)member.target=-1;
        for(auto& t:tracks) {
            reset(t);
            const auto it=std::upper_bound(t.boards.begin(),t.boards.end(),time,[&](std::uint64_t stamp,std::size_t index){return stamp<t.frames[index].time;});
            if(it!=t.boards.begin())t.next=*std::prev(it);
            while(t.next<t.frames.size() && t.frames[t.next].time<=time)if(!apply(t,t.frames[t.next++]))return ok=false;
        }
        s.focusSlot=focus;scene(time);s.paintedPlayers.clear();s.lastPaint={};++s.revision;return ok;
    }
};
RoomReplayPlayback::RoomReplayPlayback(Runtime::GameContext& c,const Replay::MultiplayerReplay& r):impl_(std::make_unique<Impl>(c,r)){}
RoomReplayPlayback::~RoomReplayPlayback()=default;
bool RoomReplayPlayback::valid() const{return impl_->ok;}
bool RoomReplayPlayback::advance(std::uint64_t time){return impl_->advance(time);}
bool RoomReplayPlayback::seek(std::uint64_t time){return impl_->seek(time);}
namespace {
class PlaybackScope {
    Runtime::GameContext& c;
    Runtime::SessionState session;
    Runtime::RuntimeConfig config;
    Runtime::GamePauseState pause;
    Replay::ReplaySessionState replay;
    Network::NetworkServices* live;
    Network::NetworkServices recorded;
    std::function<void()> controls;
    ReplayResumeRequest resume;
    std::optional<std::array<int,10>> layout;
    std::optional<std::array<int,2>> acceptedSize;
    std::array<int,2> requirements{};
public:
    explicit PlaybackScope(Runtime::GameContext& context):c(context),session(std::move(c.session)),config(c.config),pause(std::move(c.pause)),
        replay(std::move(Replay::session(c))),live(c.network_component()),controls(std::move(c.input_services().controlInterrupt)) {
        c.session=Runtime::SessionState{};c.pause={};Replay::session(c)=Replay::ReplaySessionState{};
        c.attach_network(recorded);c.input_services().controlInterrupt=[]{};
        if(auto* app=c.app_component()) {
            resume=app->replayResume;app->replayResume={};layout=app->gameTerminalLayout;
            acceptedSize=app->acceptedGameTerminalSize;requirements=app->acceptedGameTerminalRequirements;
        }
    }
    ~PlaybackScope() {
        c.session=std::move(session);c.config=config;c.pause=std::move(pause);Replay::session(c)=std::move(replay);
        if(live)c.attach_network(*live);else c.detach_network();c.input_services().controlInterrupt=std::move(controls);
        c.input_services().discard_control_events();c.input_services().suppress_control_key_until_release(Settings::NavigationInput::key_code(Settings::NavigationKey::Back));
        if(auto* app=c.app_component()) {
            app->replayResume=resume;app->gameTerminalLayout=layout;++app->gameTerminalLayoutRevision;
            app->acceptedGameTerminalSize=acceptedSize;app->acceptedGameTerminalRequirements=requirements;
        }
        Render::end_output_suppression();
    }
    void poll(){if(live)Network::poll_connection(*live);}
};
bool action(Runtime::GameContext& c,Input::Action wanted,std::map<Input::Action,bool>& held) {
    bool down=false;for(const auto& binding:c.input_services().bindings.entries())if(binding.second==wanted)down|=c.input_services().key_down(binding.first);
    const bool pressed=down && !held[wanted];held[wanted]=down;return pressed;
}
std::unique_ptr<PostGameStatisticsView> results_for(Runtime::GameContext& c,const Replay::MultiplayerReplay& archive) {
    std::vector<PostGameStatistics> data;std::vector<std::string> names;
    for(const auto& track:archive.tracks){
        const auto& p=c.session.roomScene.players.at(track.slot);
        if(!track.complete && !p.timing.completedRealTime){PostGameStatistics missing;missing.metrics={{"STATUS",UiText::tr("Incomplete recording")}};data.push_back(std::move(missing));}
        else data.push_back(make_post_game_statistics(c,p));
        names.push_back(c.session.roomScene.usernames.at(track.slot));
    }
    auto result=std::make_unique<PostGameStatisticsView>(std::move(data),std::move(names));
    for(std::size_t i=0;i<archive.tracks.size();++i)if(archive.tracks[i].actor==archive.viewer)result->select_player(int(i));return result;
}
}
RoomReplayExit view_multiplayer_replay(Runtime::GameContext& c,const std::string& recording) {
    auto decoded=Replay::decode_multiplayer_replay(recording);if(!decoded)throw std::runtime_error("Invalid multiplayer replay");
    const auto archive=std::make_shared<Replay::MultiplayerReplay>(std::move(*decoded));
    PlaybackScope saved(c);RoomReplayPlayback scene(c,*archive);if(!scene.valid())throw std::runtime_error("Invalid multiplayer replay configuration");
    auto& replay=Replay::session(c);replay.loadedRecording=recording;replay.multiplayerReplay=archive;
    Replay::configure_playback(c,1,Runtime::Duration::from_seconds(archive->duration/1000000.));Replay::start_playback(c,c.now());
    Settings::NavigationInput input(c);std::map<Input::Action,bool> held;
    for(const auto a:{Input::Action::Retry,Input::Action::OpenSettings,Input::Action::SaveReplay}){held[a]=false;action(c,a,held);}
    std::unique_ptr<PostGameStatisticsView> results;bool force=true;
    for(;;) {
        saved.poll();
        const auto command=Frontend::poll_ui_command([](const auto& c){return c.kind==Frontend::UiCommandKind::Back;});
        if(input.update(Settings::NavigationKey::Back,false)||input.update(Settings::NavigationKey::Backspace,false)||(command && command->kind==Frontend::UiCommandKind::Back)) {
            if(results){results.reset();force=true;input.suppress_held_keys();continue;}return RoomReplayExit::Back;
        }
        if(action(c,Input::Action::Retry,held))return RoomReplayExit::Retry;
        if(action(c,Input::Action::OpenSettings,held))return RoomReplayExit::Settings;
        if(action(c,Input::Action::SaveReplay,held)){save_replay_copy(c,recording,false,results.get());force=true;if(results)results->invalidate();input.suppress_held_keys();}
        if(results) {
            results->poll(input);results->render(c,UiText::tr("Left/Right: Player  Tab: Summary/Clears  Up/Down: Scroll  PgUp/PgDn: Page  Esc: Board"),Render::terminal_size().value_or(Render::TerminalSize{120,40}));
            c.services.clock.sleep_for(Runtime::Duration::from_seconds(.01));continue;
        }
        try {Engine::update_replay_playback_controls(c,c.session.player);}
        catch(const Engine::ReplaySeekRequested&) {
            auto* app=c.app_component();if(!app)throw;const auto request=app->replayResume;app->replayResume={};Render::end_output_suppression();
            const auto time=static_cast<std::uint64_t>(std::max(0.,request.targetSeconds)*1000000.);
            if(!scene.seek(time))throw std::runtime_error("Invalid multiplayer replay frame");
            auto& p=replay.playback;p.finished=false;p.elapsed=Runtime::Duration::from_seconds(time/1000000.);p.progressElapsed=p.elapsed;p.anchor=c.now();p.paused=request.paused;p.speed=request.speed;
            force=true;
        }
        const auto elapsed=Replay::playback_elapsed(c,c.now());
        if(!scene.advance(static_cast<std::uint64_t>(std::max(0.,elapsed.seconds())*1000000.)))throw std::runtime_error("Invalid multiplayer replay frame");
        replay.lastConsumedTimestamp=elapsed;
        if(c.session.roomScene.finished && !Replay::playback_finished(c))Replay::finish_playback(c,c.now());
        if(input.update(Settings::NavigationKey::Tab,false)) {
            if(c.session.roomScene.finished)results=results_for(c,*archive);else Engine::cycle_multiplayer_focus(c);
        }
        Engine::draw_multiplayer_scene(c,force);force=false;
        const auto size=Render::terminal_size().value_or(Render::TerminalSize{120,40});
        replay.playback.progressRow=std::max(0,size.rows-4);
        c.session.player.presentation.topMargin=0;c.session.player.presentation.margin=2;
        Engine::render_replay_playback_progress(c,c.session.player);
        {
            Render::OutputGuard output(c);
            Render::move_cursor(size.rows-1,1);
            const auto hint=UiText::tr(archive->complete()?"Tab: Player / Results   Esc: Back":"Incomplete recording")+"   "+UiText::binding_label(c.user_settings(),10)+" "+UiText::tr("Save Replay");
            std::cout<<Util::TerminalText::clip(hint,std::max(1,size.columns-2));Render::flush();
        }
        if(c.session.roomScene.finished && std::getenv("TETR_TEST_BATCH"))return RoomReplayExit::Back;
        c.services.clock.sleep_for(Runtime::Duration::from_seconds(.005));
    }
}
void run_multiplayer_replay(Runtime::GameContext& c) {
    const auto recording=Replay::session(c).loadedRecording;
    const auto exit=view_multiplayer_replay(c,recording);
    if(exit==RoomReplayExit::Retry || std::getenv("TETR_TEST_BATCH"))return;
    if(auto* app=c.app_component();app && app->openSettings)app->openSettings();else Settings::run_menu(c);
}
}
