#include "tetr/util/runtime_directory_lock.h"
#include "session_scene.h"
#include "board_layout.h"
#include "tetr/frontend/graphical/session.h"
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/client_loop.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/app/settings_menu.h"
#include "tetr/config/normalization.h"
#include "tetr/config/profile_store.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/network/network_services.h"
#include "tetr/engine/multiplayer_scene.h"
#include "../../app/ui_text.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/pause.h"
#include "tetr/engine/undo.h"
#include "tetr/input/services.h"
#include "tetr/presentation/context_view.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/recent.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/runtime/game_context.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Graphical {
namespace {
struct LeaveSession {};
ActionAvailability action_availability(const Runtime::GameContext& c) {
    ActionAvailability result{};
    const auto& p=c.session.player;
    const bool finished=c.session.roomScene.enabled?c.session.roomScene.finished:p.timing.completedGameTime.has_value();
    const bool manual=!finished && !c.config.mode.replayMode && !Runtime::autoplay_enabled(c.config.autoplay.mode) &&
        !(c.session.roomScene.enabled && (c.session.roomScene.spectator || !c.session.roomScene.selfAlive));
    const auto& movement=c.config.movement;
    result[0]=manual && movement.allows_horizontal(-1);result[1]=manual && movement.allows_horizontal(1);
    result[2]=manual && !movement.disableSoftDrop;result[3]=manual && !movement.disableHardDrop;
    result[4]=manual && movement.allows_rotation(1);result[5]=manual && movement.allows_rotation(-1);
    result[6]=manual && movement.allows_rotation(2);
    result[7]=manual && Runtime::hold_available(c.config.field,p.gameplay.hold.used);
    result[8]=!c.session.roomScene.enabled || finished ||
        (!c.session.roomScene.spectator && c.session.roomScene.selfAlive);
    result[9]=true;result[10]=finished;
    result[11]=!finished && Engine::undo_enabled(c);
    result[12]=!finished && Engine::pause_enabled(c);
    return result;
}
std::vector<Frontend::UiEntry> control_hints(const UserConfig::Settings& settings) {
    static constexpr const char* labels[]={"Left","Right","Soft drop","Hard drop","CW","CCW","180°","Hold","Retry","Settings","Save","Undo","Pause"};
    static constexpr const char* chinese[]={"左移","右移","软降","硬降","顺转","逆转","180°","暂存","重试","设置","保存","撤销","暂停"};
    std::vector<Frontend::UiEntry> result;
    for(std::size_t i=0;i<13;++i) {
        std::string keys;
        for(const int key:UserConfig::control_keys(settings,i)) {
            if(!keys.empty())keys+=" / ";keys+=App::UiText::key_label(key);
        }
        result.push_back({std::to_string(i),App::UiText::language()==App::UiText::Language::Chinese?chinese[i]:labels[i],true,keys.empty()?App::UiText::tr("Unbound"):keys});
    }
    return result;
}
// Existing replay encoder/hash and directory rules, with a unique destination.
std::string save_finished_recording(Runtime::GameContext& context) {
    const auto data=Replay::recording_for_save(context);
    if(data.empty() || data.size()>20000000) throw std::runtime_error("Replay is empty or too large to save");
    const auto directory=Replay::replay_directory();
    std::filesystem::create_directories(directory);
    Util::FileWriteLock writeLock(directory/".gl-recording");
    if(!writeLock.acquired())throw std::runtime_error("Could not save replay");
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    auto path=directory/("gl-"+std::to_string(stamp)+".txt");
    for(unsigned suffix=1;std::filesystem::exists(path);++suffix)
        path=directory/("gl-"+std::to_string(stamp)+"-"+std::to_string(suffix)+".txt");
    std::ofstream file(path,std::ios::binary);
    file<<data<<Replay::replay_hash_suffix(data);
    file.close();
    if(!file) throw std::runtime_error("Could not save replay");
    auto& state=Replay::session(context);
    if(!context.config.mode.replayMode && !state.recentPath.empty() &&
       Replay::rename_recent(std::filesystem::u8path(state.recentPath),path))state.recentPath=path.generic_u8string();
    return "Saved: "+path.generic_u8string();
}
}

Session::Session(UserConfig::Settings settings,std::string replayPath)
    :worker_([this,settings=std::move(settings),replayPath=std::move(replayPath)]() mutable {
        run(std::move(settings),std::move(replayPath));
    }) {}
Session::Session(UserConfig::Settings first,UserConfig::Settings second,LaunchOptions options)
    :worker_([this,first=std::move(first),second=std::move(second),options]() mutable {
        run_full(std::move(first),std::move(second),options);
    }) {}
void Session::begin_text_input() {std::lock_guard<std::mutex> lock(mutex_);textEvents_.clear();textMode_=true;}
void Session::end_text_input() {std::lock_guard<std::mutex> lock(mutex_);textMode_=false;textEvents_.clear();}
std::optional<Frontend::TextEditEvent> Session::poll_text_event() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(textEvents_.empty())return std::nullopt;
    auto event=std::move(textEvents_.front());textEvents_.pop_front();return event;
}
void Session::text_event(Frontend::TextEditEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(textMode_ && textEvents_.size()<512)textEvents_.push_back(std::move(event));
}
void Session::ui_command(Frontend::UiCommand command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(fileDialog_ && !fileDialog_->result())return;
    if(!input_.accepting_input() || !view_.ui || command.page!=view_.ui->metadata.page || uiCommands_.size()>=64)return;
    if(!Frontend::valid_ui_command(view_.ui->metadata,command))return;
    uiCommands_.push_back(command);
}
std::shared_ptr<Frontend::FileDialog> Session::open_file(std::string directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(fileDialog_ && !fileDialog_->result())return {};
    uiCommands_.clear();textEvents_.clear();fileDialogDispatched_=false;
    fileDialog_=std::make_shared<Frontend::FileDialog>(std::move(directory));return fileDialog_;
}
std::shared_ptr<Frontend::FileDialog> Session::take_file_dialog() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!fileDialog_ || fileDialogDispatched_ || fileDialog_->result())return {};
    fileDialogDispatched_=true;return fileDialog_;
}
Session::~Session() {
    stop_=true;
    {std::lock_guard<std::mutex> lock(mutex_);if(fileDialog_)fileDialog_->complete();}
    if(worker_.joinable()) worker_.join();
}
SessionView Session::view() const { std::lock_guard<std::mutex> lock(mutex_);return view_; }
SessionControls Session::controls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {view_.status,view_.ui,view_.message,view_.board.pieces,view_.board.hold,view_.replay,view_.spectator,view_.room,
        view_.actionEnabled,view_.switchTarget,view_.endMatch};
}
void Session::request_menu() { menu_=true; }
void Session::request_save() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(view_.status==SessionStatus::Finished) save_=true;
}

void Session::run(UserConfig::Settings settings,std::string replayPath) {
    try {
        Render::DetachedOutputScope output;
        auto state=std::make_unique<Runtime::SessionState>();
        Runtime::RuntimeServices services;
        Runtime::GameContext context(*state,services);
        Input::Services input;
        App::ApplicationState app;
        Replay::ReplaySessionState replay;
        context.attach_input(input);context.attach_app(app);context.attach_replay(replay);
        context.attach_user_settings(settings);context.attach_output_mutex(app.terminalOutputMutex);
        app.primaryContext=&context;
        input.focusSource=&input_;input.keyEventSource=&input_.events();input.initialized=true;
        // No evdev monitor, console session, native key polling or terminal IO.
        settings.persistencePath.clear();
        const bool requestedReplay=!replayPath.empty();
        if(requestedReplay) {settings.gameMode=5;settings.replayPath=replayPath;}
        bool initialized=false,finished=false;
        std::uint64_t roundRevision=0;
        std::string message;
        auto lastPublish=std::chrono::steady_clock::time_point{};
        const auto publish=[&](bool force=false) {
            const auto now=std::chrono::steady_clock::now();
            if(!initialized || (!force && now-lastPublish<std::chrono::milliseconds(4))) return;
            lastPublish=now;
            SessionView next;
            next.roundRevision=roundRevision;
            next.controlHints=control_hints(settings);
            next.actionEnabled[0]=action_availability(context);
            const auto& field=state->player.gameplay.playfield;
            next.board=capture_graphical_board(context,state->player,solo_geometry(field.field.width(),field.visibleHeight));
            next.countdownDigit=state->countdownDigit;
            next.replay=context.config.mode.replayMode;
            next.status=finished?SessionStatus::Finished:context.pause.paused?SessionStatus::Paused:
                !state->gameStartTime?SessionStatus::Countdown:SessionStatus::Playing;
            if(next.replay) {
                next.replayPosition=Replay::playback_progress_elapsed(context,context.now()).seconds();
                next.replayDuration=Replay::playback_duration(context).seconds();
                next.replaySpeed=Replay::playback_speed(context);
                next.replayPaused=Replay::playback_paused(context);
                if(!finished && Replay::playback_paused(context)) next.status=SessionStatus::Paused;
            }
            next.message=message;
            std::lock_guard<std::mutex> lock(mutex_);
            next.revision=view_.revision+1;view_=std::move(next);
        };
        const auto pump=[&] {
            if(stop_ || menu_) throw LeaveSession{};
            publish();
        };
        input.controlInterrupt=[&] {pump();Engine::poll_game_pause(context,pump);};
        services.clock.waitInterrupt=[&] {input.poll_control_interrupt();pump();};
        app.openSettings=[] { throw LeaveSession{}; };
        app.roundFinished=[&] { finished=true;publish(true); };
        for(;;) {
            initialized=false;finished=false;
            App::initialize_game(context);
            ++roundRevision;
            if(requestedReplay && !context.config.mode.replayMode)
                throw std::runtime_error("Invalid/unsupported replay: verify its path and checksum. Two-player replay UI is scheduled for phase 3.");
            initialized=true;publish(true);
            App::run(context);
            if(app.replayResume.pending) continue;
            if(!finished) continue; // existing Retry outcome; same initialization
            // Preserve the final frame. Replay controls can still seek backwards.
            try {
                for(;;) {
                    pump();
                    if(save_.exchange(false)) {
                        try {message=save_finished_recording(context);}
                        catch(const std::exception& error) {message=error.what();}
                        publish(true);
                    }
                    if(context.config.mode.replayMode)
                        Engine::update_replay_playback_controls(context,state->player);
                    bool retry=false;
                    for(const auto& [key,action]:input.bindings.entries()) {
                        if(action==Input::Action::Retry && input.key_down(key)) retry=true;
                        if(action==Input::Action::OpenSettings && input.key_down(key)) throw LeaveSession{};
                    }
                    if(retry) break;
                    services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
                }
            } catch(const Engine::ReplaySeekRequested&) {}
        }
    } catch(const LeaveSession&) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Menu;view_.countdownDigit=0;++view_.revision;
    } catch(const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Error;view_.countdownDigit=0;view_.message=error.what();++view_.revision;
    } catch(...) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Error;view_.countdownDigit=0;view_.message="Unexpected session failure";++view_.revision;
    }
}

void Session::run_full(UserConfig::Settings first,UserConfig::Settings second,LaunchOptions options) {
    try {
        Render::DetachedOutputScope output(options.audio);
        auto states=std::make_unique<std::array<Runtime::SessionState,2>>();
        Runtime::RuntimeServices clocks[2];
        Runtime::GameContext a((*states)[0],clocks[0]),b((*states)[1],clocks[1]);
        Runtime::GameContext* contexts[]={&a,&b};
        App::ApplicationState app;
        Input::Services inputs[2];Network::NetworkServices networks[2];
        Replay::ReplaySessionState replays[2];Legacy::BotStrategy bots[2];
        UserConfig::Settings* users[]={&first,&second};
        app.primaryContext=&a;app.localPlayerTwoContext=&b;
        app.qpContinuousCancellation=options.continuousCancellation;
        app.localVersusSettingsAvailable=UserConfig::has_saved_profile(second);
        if(options.playerTwoActive) {app.singlePlayerOwner=1;app.savedPlayerOneSettings=first;first=second;}
        app.textInputSession=this;
        app.fileDialogs=this;
        if(options.touchControls)app.virtualControlsEditor=[this]{touchEditor_=true;};
        for(int i=0;i<2;++i) {
            auto& c=*contexts[i];c.attach_app(app);c.attach_input(inputs[i]);c.attach_network(networks[i]);
            c.attach_replay(replays[i]);c.attach_user_settings(*users[i]);c.attach_autoplay(bots[i]);
            c.attach_output_mutex(app.terminalOutputMutex);
            inputs[i].focusSource=&input_;inputs[i].keyEventSource=&input_.events();inputs[i].initialized=true;
            inputs[i].virtualPlayerSlot=options.touchControls?i:-1;
            Input::refresh_bindings(inputs[i],users[i]->keyCodes,users[i]->additionalKeyCodes);
            App::install_start_screen_navigation(c);
        }
        App::UiText::initialize_language(first.uiLanguage);
        first.uiLanguage=App::UiText::language()==App::UiText::Language::English?1:0;
        App::Settings::synchronize_shared_appearance(a);
        Frontend::UiSurfaceScope ui([&](Frontend::UiFrame frame) {
            std::lock_guard<std::mutex> lock(mutex_);
            view_.ui=frame.rows.empty()?nullptr:std::make_shared<const Frontend::UiFrame>(std::move(frame));
            ++view_.revision;
        },{128,38},[&]() -> std::optional<Frontend::UiCommand> {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!input_.accepting_input())uiCommands_.clear();
            if(uiCommands_.empty())return {};
            const auto command=uiCommands_.front();uiCommands_.pop_front();return command;
        });
        bool ready=false;
        std::uint64_t roundRevision=0;
        std::chrono::steady_clock::time_point lastPublish[2]{};
        const auto publish=[&](int index,bool force=false) {
            const auto now=std::chrono::steady_clock::now();
            if(!ready || (!force && now-lastPublish[index]<std::chrono::milliseconds(8)))return;
            lastPublish[index]=now;auto& c=*contexts[index];
            const bool local=c.config.mode.mode==Runtime::GameMode::LocalVersus;
            const auto& scene=c.session.roomScene;
            const auto& ownField=c.session.player.gameplay.playfield;
            auto geometry=solo_geometry(ownField.field.width(),ownField.visibleHeight);
            BoardGeometry opponentGeometry;
            if(!scene.enabled && (local || c.config.mode.mode==Runtime::GameMode::Multiplayer)) {
                const auto& firstBoard=contexts[0]->session.player.gameplay.playfield;
                const auto& secondBoard=local?contexts[1]->session.player.gameplay.playfield:c.session.opponent.gameplay.playfield;
                const auto layout=multiplayer_geometry({{firstBoard.field.width(),firstBoard.visibleHeight},
                    {secondBoard.field.width(),secondBoard.visibleHeight}},{});
                geometry=layout.full[local?index:0];opponentGeometry=layout.full[1];
            }
            auto board=scene.enabled?Presentation::BoardView{}:capture_graphical_board(c,c.session.player,geometry);
            std::vector<SceneBoard> peers;bool ownBoardVisible=true;
            std::string status,notice,controls;
            std::vector<Frontend::UiEntry> hints;
            if(index==0) {
                if(scene.enabled) {
                    auto roomBoards=capture_room_boards(c);board=std::move(roomBoards.ownBoard);
                    ownBoardVisible=roomBoards.ownBoardVisible;peers=std::move(roomBoards.peers);
                    const auto& roomNetwork=Network::network_services(c);
                    status=roomNetwork.room.name+" / "+roomNetwork.roomMessage;
                } else if(c.config.mode.mode==Runtime::GameMode::Multiplayer) {
                    peers.push_back({-3,false,capture_graphical_board(c,c.session.opponent,opponentGeometry)});
                }
                if(app.challengeRound)notice=app.challengeRound->notice;
                static constexpr const char* labels[]={"L","R","SD","HD","CW","CCW","180","HOLD","RETRY","SET","SAVE","UNDO","PAUSE"};
                for(std::size_t i=0;i<13;++i)controls+=std::string(i?"  ":"")+App::UiText::binding_label(c.user_settings(),i)+labels[i];
                hints=control_hints(c.user_settings());
            }
            const auto available=action_availability(c);
            std::lock_guard<std::mutex> lock(mutex_);
            view_.actionEnabled[index]=available;
            if(index==1) {
                auto found=std::find_if(view_.peers.begin(),view_.peers.end(),[](const auto& peer){return peer.slot==-2;});
                if(found==view_.peers.end())view_.peers.push_back({-2,false,std::move(board)});else found->board=std::move(board);
            } else {
                if(view_.roundRevision!=roundRevision) {view_.peers.clear();view_.ui.reset();}
                view_.roundRevision=roundRevision;
                view_.board=std::move(board);
                view_.countdownDigit=c.session.countdownDigit;
                if(!local)view_.peers=std::move(peers);
                view_.status=(scene.enabled?scene.finished:c.session.player.timing.completedGameTime.has_value())?SessionStatus::Finished:
                    c.pause.paused?SessionStatus::Paused:!c.session.gameStartTime?SessionStatus::Countdown:SessionStatus::Playing;
                view_.replay=c.config.mode.replayMode;
                if(view_.replay) {
                    view_.replayPosition=Replay::playback_progress_elapsed(c,c.now()).seconds();
                    view_.replayDuration=Replay::playback_duration(c).seconds();view_.replaySpeed=Replay::playback_speed(c);
                    view_.replayPaused=Replay::playback_paused(c);
                    if(view_.replayPaused && view_.status!=SessionStatus::Finished)view_.status=SessionStatus::Paused;
                }
                view_.roomStatus=std::move(status);view_.challengeNotice=std::move(notice);view_.controls=std::move(controls);
                view_.controlHints=std::move(hints);
                view_.spectator=scene.enabled && scene.spectator;view_.focusSlot=scene.focusSlot;
                view_.room=scene.enabled;
                view_.switchTarget=scene.enabled && !scene.finished && (scene.spectator || !scene.selfAlive) && scene.survivors.size()>1;
                view_.endMatch=scene.enabled && !scene.finished && !c.config.mode.replayMode &&
                    Network::is_room_host(Network::network_services(c)) &&
                    (Network::network_services(c).matchPhase==Network::RoomMatchPhase::Playing ||
                        Network::network_services(c).matchPhase==Network::RoomMatchPhase::Countdown);
                if(!local)view_.actionEnabled[1]={};
                view_.ownBoardVisible=ownBoardVisible;
            }
            ++view_.revision;
        };
        for(int i=0;i<2;++i) {
            const auto navigate=inputs[i].controlInterrupt;
            const auto interrupt=[&,i] {
                if(stop_) {app.startScreenRequested=true;throw App::StartScreenRequested{};}
                if(menu_.exchange(false))App::request_start_screen(*contexts[i]);
                publish(i);
            };
            inputs[i].controlInterrupt=[navigate,interrupt]{interrupt();navigate();};
            clocks[i].clock.waitInterrupt=[&,i,interrupt]{interrupt();inputs[i].poll_control_interrupt();};
        }
        app.beforeGame=[&] {ready=false;};
        app.gameReady=[&] {
            ++roundRevision;
            ready=true;publish(0,true);
            if(a.config.mode.mode==Runtime::GameMode::LocalVersus)publish(1,true);
        };
        App::run_client(a,!options.startScreen);
        Network::disconnect_online(networks[0]);
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Closed;view_.countdownDigit=0;++view_.revision;
    } catch(const App::StartScreenRequested&) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Closed;view_.countdownDigit=0;++view_.revision;
    } catch(const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);view_.status=SessionStatus::Error;view_.countdownDigit=0;view_.message=error.what();++view_.revision;
    }
}
}
