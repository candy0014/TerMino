#include "tetr/frontend/graphical/session.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/map_store.h"
#include "tetr/config/presets.h"
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/input/services.h"
#include "tetr/network/network_services.h"
#include "tetr/network/relay_server.h"
#include "tetr/network/lobby_channel.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/local_versus_codec.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/recent.h"
#include "tetr/runtime/game_context.h"
#include "input.h"
#include "field_effects.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace TetrTests::graphical_application_test {
namespace {
using namespace std::chrono_literals;
using Id=UserConfig::SettingId;
const char* phase="start";
void stage(const char* name) {phase=name;std::cerr<<"Application test: "<<name<<std::endl;}
std::function<void()> pump=[]{};
template<class Predicate> Graphical::SessionView wait(Graphical::Session& session,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+10s;
    for(;;) {
        pump();
        auto view=session.view();if(predicate(view))return view;
        if(view.status==Graphical::SessionStatus::Error || std::chrono::steady_clock::now()>=deadline) {
            std::cerr<<"Failed in "<<phase<<": status="<<static_cast<int>(view.status)<<" peers="<<view.peers.size()<<" pieces="<<view.board.pieces<<" "<<view.message;
            if(view.ui) {std::cerr<<" UI="<<view.ui->metadata.id<<" path="<<view.ui->metadata.path<<" selected="<<view.ui->metadata.selected<<" notice="<<view.ui->metadata.notice;
                for(const auto& entry:view.ui->metadata.entries)std::cerr<<" ["<<entry.id<<"]";}
            std::cerr<<'\n';assert(false);
        }
        std::this_thread::sleep_for(2ms);
    }
}
void tap(Graphical::Session& session,SDL_Scancode key) {
    const int native=Graphical::native_key(key);assert(native>0);
    session.input().update(native,true);std::this_thread::sleep_for(25ms);
    session.input().update(native,false);std::this_thread::sleep_for(30ms);
}
auto ui(Graphical::Session& s,const std::string& id) {
    return wait(s,[&](const auto& v){return v.ui && v.ui->metadata.id==id;});
}
void choose(Graphical::Session& s,const std::string& id) {
    for(int i=0;i<200;++i) {
        const auto v=s.view();assert(v.ui);const auto& m=v.ui->metadata;
        const auto found=std::find_if(m.entries.begin(),m.entries.end(),[&](const auto& e){return e.id==id;});
        if(found==m.entries.end()) {std::cerr<<"Missing entry "<<id<<" in "<<m.id<<'\n';assert(false);}
        assert(found->enabled);
        if(m.selected==static_cast<std::size_t>(found-m.entries.begin())) {tap(s,SDL_SCANCODE_RETURN);return;}
        tap(s,SDL_SCANCODE_DOWN);
    }
    assert(false);
}
void click(Graphical::Session& s,const std::string& id,std::optional<std::size_t> ancestor={}) {
    const auto v=s.view();assert(v.ui);const auto& m=v.ui->metadata;
    const auto& entries=!ancestor?m.entries:*ancestor<m.ancestors.size()?m.ancestors[*ancestor].entries:m.parent->entries;
    const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.id==id;});
    assert(found!=entries.end() && found->enabled);
    s.ui_command({Frontend::UiCommandKind::Activate,static_cast<std::size_t>(found-entries.begin()),ancestor,m.page});
}
std::string setting(Id id) {return "setting-"+std::to_string(static_cast<int>(id));}
void text(Graphical::Session& s,const std::string& value) {
    wait(s,[&](const auto&){return s.text_input_active();});
    s.text_event({Frontend::TextEditKind::Insert,value});s.text_event({Frontend::TextEditKind::Confirm});
    wait(s,[&](const auto&){return !s.text_input_active();});
}
UserConfig::Settings defaults() {
    UserConfig::Settings s;s.keyCodes=UserConfig::default_key_codes();s.uiLanguage=1;s.gravityInterval=10;return s;
}
void root(Graphical::Session& s) {
    for(int i=0;i<10;++i) {const auto v=s.view();if(v.ui && v.ui->metadata.id=="root")return;tap(s,SDL_SCANCODE_LEFT);}
    assert(false);
}
void local_replay_pause(bool touch) {
    stage(touch?"local replay touch pause freezes both boards":"local replay keyboard pause freezes both boards");
    std::filesystem::create_directories(".tetr/replay");
    const auto recording=Replay::encode_local_versus_replay(
        "{{Model,6}}{end_all,40}","{{Model,6}}{end_all,60}");
    {std::ofstream file(".tetr/replay/dual-pause.txt");file<<recording<<Replay::replay_hash_suffix(recording);}
    auto a=defaults(),b=defaults();a.persistenceEnabled=b.persistenceEnabled=false;
    a.gameMode=5;a.replayPath=".tetr/replay/dual-pause.txt";
    b.keyCodes[12]=Graphical::native_key(SDL_SCANCODE_F3);
    Graphical::LaunchOptions launch;launch.startScreen=false;launch.touchControls=touch;
    Graphical::Session s(a,b,launch);
    wait(s,[](const auto& v){return v.replay && v.status==Graphical::SessionStatus::Playing &&
        v.peers.size()==1 && v.board.elapsed.seconds()>.1 && v.peers[0].board.elapsed.seconds()>.1;});
    const std::array<int,3> keys{touch?Input::virtual_action_key(12):a.keyCodes[12],
        touch?Input::virtual_action_key(12,1):b.keyCodes[12],Graphical::native_key(SDL_SCANCODE_SPACE)};
    const auto press=[&](int key) {
        s.input().update(key,true);std::this_thread::sleep_for(25ms);
        s.input().update(key,false);std::this_thread::sleep_for(25ms);
    };
    const auto frozen=[&] {
        wait(s,[](const auto& v){return v.replayPaused;});
        std::this_thread::sleep_for(30ms);const auto paused=s.view();
        std::this_thread::sleep_for(180ms);const auto held=s.view();
        const auto ownDelta=(held.board.elapsed-paused.board.elapsed).seconds();
        const auto peerDelta=(held.peers[0].board.elapsed-paused.peers[0].board.elapsed).seconds();
        std::cerr<<"Local replay pause touch="<<touch<<" own delta="<<ownDelta<<" peer delta="<<peerDelta<<'\n';
        assert(std::abs(ownDelta)<.02 && std::abs(peerDelta)<.02);
        assert(std::abs((held.board.elapsed-held.peers[0].board.elapsed).seconds())<.02);
    };
    for(std::size_t i=0;i<keys.size();++i) {
        press(keys[i]);frozen();
        const auto paused=s.view();press(keys[(i+1)%keys.size()]);
        wait(s,[&](const auto& v){return !v.replayPaused &&
            v.board.elapsed>paused.board.elapsed+Runtime::Duration::from_seconds(.04) &&
            v.peers[0].board.elapsed>paused.peers[0].board.elapsed+Runtime::Duration::from_seconds(.04);});
    }
    press(keys[0]);frozen();
    tap(s,SDL_SCANCODE_EQUALS);wait(s,[](const auto& v){return v.replaySpeed==2;});
    tap(s,SDL_SCANCODE_RIGHT);
    wait(s,[](const auto& v){return v.replayPaused && v.board.elapsed.seconds()>=10 &&
        v.peers.size()==1 && v.peers[0].board.elapsed.seconds()>=10;});frozen();
    tap(s,SDL_SCANCODE_LEFT);
    wait(s,[](const auto& v){return v.replayPaused && v.board.elapsed.seconds()<5 &&
        v.peers.size()==1 && v.peers[0].board.elapsed.seconds()<5;});frozen();
    assert(s.view().replaySpeed==2);
    press(keys[1]);wait(s,[](const auto& v){return !v.replayPaused;});
    const auto resumed=s.view();std::this_thread::sleep_for(200ms);const auto advanced=s.view();
    for(const double delta:{(advanced.board.elapsed-resumed.board.elapsed).seconds(),
        (advanced.peers[0].board.elapsed-resumed.peers[0].board.elapsed).seconds()})assert(delta>.32 && delta<.52);
    tap(s,SDL_SCANCODE_MINUS);wait(s,[](const auto& v){return v.replaySpeed==1;});
}
}
int run(int argc,char** argv) {
    const auto original=std::filesystem::current_path();
    const auto directory=std::filesystem::temp_directory_path()/("termino-application-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);std::filesystem::current_path(directory);
    if(argc>1 && std::string(argv[1])=="local-replay-pause") {
        local_replay_pause(argc>2 && std::string(argv[2])=="touch");
        std::filesystem::current_path(original);std::filesystem::remove_all(directory);return 0;
    }
    local_replay_pause(false);local_replay_pause(true);
    auto first=defaults(),second=defaults();first.persistencePath="player1.ini";second.persistencePath="player2.ini";
    UserConfig::save(first);UserConfig::save(second);
    {
        stage("direct mouse actions and ancestor navigation without keyboard stepping");
        auto mouse=defaults();mouse.persistenceEnabled=false;
        Graphical::Session s(mouse,mouse,{});ui(s,"start");click(s,"settings");
        const auto oldPage=ui(s,"root").ui->metadata.page;
        click(s,"NETWORK");ui(s,"NETWORK");
        s.ui_command({Frontend::UiCommandKind::Activate,0,{},oldPage}); // Stale page cannot activate its replacement.
        click(s,setting(Id::Username));
        wait(s,[&](const auto&){return s.text_input_active();});
        click(s,"GAME",0);
        auto ancestor=ui(s,"root");assert(!s.text_input_active());
        assert(ancestor.ui->metadata.entries[ancestor.ui->metadata.selected].id=="GAME");
        click(s,"GAME");ui(s,"GAME");click(s,"playfield");ui(s,"playfield");
        click(s,setting(Id::FieldWidth));wait(s,[&](const auto&){return s.text_input_active();});
        s.text_event({Frontend::TextEditKind::Insert,"27"});
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.input=="27";});
        const auto ownerDepth=s.view().ui->metadata.ancestors.size();
        click(s,setting(Id::FieldHeight),ownerDepth);
        ancestor=ui(s,"playfield");assert(!s.text_input_active());
        assert(ancestor.ui->metadata.entries[ancestor.ui->metadata.selected].id==setting(Id::FieldHeight));
        assert(std::find_if(ancestor.ui->metadata.entries.begin(),ancestor.ui->metadata.entries.end(),
            [](const auto& e){return e.id==setting(Id::FieldWidth);})->value=="10");
        click(s,"pieces",1);ancestor=ui(s,"GAME");
        assert(ancestor.ui->metadata.entries[ancestor.ui->metadata.selected].id=="pieces");
        click(s,"pieces");ui(s,"pieces");click(s,setting(Id::RotationSystem));
        auto choices=wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Choices;});
        const auto option=std::find_if(choices.ui->metadata.entries.rbegin(),choices.ui->metadata.entries.rend(),[](const auto& e){return e.enabled;});
        const auto choiceId=option->id;click(s,choiceId);ui(s,"pieces");
        click(s,setting(Id::RotationSystem));
        choices=wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Choices;});
        const auto chosen=std::find_if(choices.ui->metadata.entries.begin(),choices.ui->metadata.entries.end(),[&](const auto& e){return e.id==choiceId;});
        assert(chosen->checked.value_or(false));
        click(s,"CONTROLS",0);ancestor=ui(s,"root");click(s,"CONTROLS");ui(s,"CONTROLS");
        click(s,setting(Id::ControlHardDrop));wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::KeyCapture;});
        click(s,"NETWORK",0);ancestor=ui(s,"root");
        assert(ancestor.ui->metadata.entries[ancestor.ui->metadata.selected].id=="NETWORK");
        std::uint64_t cursor=0;assert(s.input().events().read(cursor).empty());
    }
    for(int height:{0,1,2,3}) {
        stage("short field full application countdown and drop");
        auto small=defaults();small.persistenceEnabled=false;small.fieldHeight=height;small.goal=4;small.pieceTarget=1;
        Graphical::Session s(small,small,{false});
        const auto playing=wait(s,[](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing && v.board.elapsed.seconds()>.1;});
        assert(playing.board.bottom==height);
        tap(s,SDL_SCANCODE_SPACE);
        wait(s,[](const auto& v){return v.board.pieces>0 || v.status==Graphical::SessionStatus::Finished;});
    }
    for(int height:{0,1,2,3}) {
        stage("local versus short second field");
        auto a=defaults(),b=defaults();a.gameMode=b.gameMode=6;
        a.persistenceEnabled=b.persistenceEnabled=false;b.fieldHeight=height;
        b.keyCodes[3]=Graphical::native_key(SDL_SCANCODE_KP_ENTER);
        Graphical::Session s(a,b,{false});
        const auto playing=wait(s,[](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing && v.peers.size()==1 && v.peers[0].board.active;});
        assert(playing.board.bottom==20 && playing.peers[0].board.bottom==height);
        tap(s,SDL_SCANCODE_KP_ENTER);
        wait(s,[](const auto& v){return v.peers.size()==1 && (v.peers[0].board.pieces>0 || v.peers[0].board.finished);});
    }
    {
        stage("change short field height in settings and restart");
        auto small=defaults();small.persistenceEnabled=false;small.goal=2;small.racingDistance=10;
        Graphical::Session s(small,small,{false});
        wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Playing;});
        for(int height:{3,2,1,0,20}) {
            tap(s,SDL_SCANCODE_P);wait(s,[](const auto& v){return bool(v.ui);});root(s);choose(s,"GAME");choose(s,"playfield");
            choose(s,setting(Id::FieldHeight));text(s,std::to_string(height));ui(s,"playfield");
            tap(s,SDL_SCANCODE_R);
            const auto playing=wait(s,[&](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing && v.board.bottom==height;});
            assert(playing.board.cells.size()==static_cast<std::size_t>((height-playing.board.firstRow)*playing.board.width));
        }
    }
    for(const auto key:{SDL_SCANCODE_R,SDL_SCANCODE_RETURN}) {
        stage("fresh start screen R and Enter launch gameplay");
        auto fresh=defaults();fresh.persistenceEnabled=false;
        Graphical::Session s(fresh,fresh,{});
        assert(ui(s,"start").ui->metadata.selected==0);
        tap(s,SDL_SCANCODE_TAB);tap(s,SDL_SCANCODE_RETURN);ui(s,"root");
        tap(s,SDL_SCANCODE_ESCAPE);
        assert(ui(s,"start").ui->metadata.selected==0);
        tap(s,key);
        wait(s,[](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing;});
    }
    for(const auto key:{SDL_SCANCODE_R,SDL_SCANCODE_RETURN}) {
        stage("failed online start explains error without opening settings");
        auto broken=defaults();broken.persistenceEnabled=false;broken.gameMode=4;
        broken.serverIp="127.0.0.1:0"; // Invalid endpoint: deterministic, no external traffic.
        Graphical::Session s(broken,broken,{});ui(s,"start");tap(s,key);
        const auto result=wait(s,[](const auto& v){return v.ui &&
            (v.ui->metadata.id=="root" || (v.ui->metadata.id=="start" && !v.ui->metadata.notice.empty()));});
        assert(result.ui->metadata.id=="start" && !result.ui->metadata.notice.empty());
        assert(result.ui->metadata.selected==0);
        tap(s,SDL_SCANCODE_P);ui(s,"root");
    }
    {
        stage("start screen configured terminal shortcuts");
        auto remapped=defaults();remapped.persistenceEnabled=false;
        UserConfig::set_control_keys(remapped,8,{Graphical::native_key(SDL_SCANCODE_F6),Graphical::native_key(SDL_SCANCODE_F7)});
        UserConfig::set_control_keys(remapped,9,{Graphical::native_key(SDL_SCANCODE_F8),Graphical::native_key(SDL_SCANCODE_F9)});
        Graphical::Session s(remapped,remapped,{});
        auto start=ui(s,"start");
        assert(start.ui->metadata.entries.at(0).value=="[F6 / F7]");
        assert(start.ui->metadata.entries.at(1).value=="[F8 / F9]");
        tap(s,SDL_SCANCODE_R);ui(s,"start");
        tap(s,SDL_SCANCODE_F9);ui(s,"root");
        tap(s,SDL_SCANCODE_ESCAPE);ui(s,"start");
        tap(s,SDL_SCANCODE_TAB);assert(s.view().ui->metadata.selected==1);
        tap(s,SDL_SCANCODE_F7);
        wait(s,[](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing;});
    }
    {
        Graphical::Session s(first,second,{});ui(s,"start");
        stage("start keyboard focus");
        s.input().update(Graphical::native_key(SDL_SCANCODE_RSHIFT),true);tap(s,SDL_SCANCODE_TAB);
        s.input().update(Graphical::native_key(SDL_SCANCODE_RSHIFT),false);assert(s.view().ui->metadata.selected==2);
        tap(s,SDL_SCANCODE_TAB);assert(s.view().ui->metadata.selected==0);
        tap(s,SDL_SCANCODE_TAB);assert(s.view().ui->metadata.selected==1);
        tap(s,SDL_SCANCODE_RETURN);ui(s,"root");
        stage("Unicode username persistence");choose(s,"NETWORK");
        auto networkUi=ui(s,"NETWORK");
        assert(!networkUi.ui->metadata.ancestors.empty());
        const auto usernameEntry=std::find_if(networkUi.ui->metadata.entries.begin(),networkUi.ui->metadata.entries.end(),
            [](const auto& e){return e.id==setting(Id::Username);});
        assert(usernameEntry!=networkUi.ui->metadata.entries.end() && !usernameEntry->value.empty());
        assert(usernameEntry->control==Frontend::UiControl::Value && !usernameEntry->description.empty());
        choose(s,setting(Id::Username));text(s,"图形 测试");ui(s,"NETWORK");
        UserConfig::Settings saved;UserConfig::load(saved,"player1.ini");assert(saved.username=="图形 测试");
        stage("effect intensity keyboard controls and persistence");root(s);choose(s,"OTHERS");
        const auto appearance=ui(s,"OTHERS");
        assert(std::none_of(appearance.ui->metadata.entries.begin(),appearance.ui->metadata.entries.end(),
            [](const auto& e){return e.id==setting(Id::EffectsIntensity);}));
        choose(s,"effects");ui(s,"effects");
        choose(s,setting(Id::EffectsIntensity));text(s,"150");ui(s,"effects");
        choose(s,setting(Id::BoardBounciness));text(s,"175");ui(s,"effects");
        choose(s,setting(Id::FieldShake));text(s,"0");ui(s,"effects");
        choose(s,setting(Id::ParticleIntensity));text(s,"200");ui(s,"effects");
        UserConfig::load(saved,"player1.ini");assert(saved.effectsIntensity==150 && saved.fieldShake==0 && saved.particleIntensity==200 && saved.boardBounciness==175);
        tap(s,SDL_SCANCODE_LEFT);
        const auto returned=ui(s,"OTHERS");
        assert(returned.ui->metadata.entries.at(returned.ui->metadata.selected).id=="effects");
        choose(s,"effects");ui(s,"effects");
        choose(s,"reset");ui(s,"effects");
        UserConfig::load(saved,"player1.ini");assert(saved.effectsIntensity==150); // First Enter only confirms.
        tap(s,SDL_SCANCODE_RETURN);ui(s,"effects");
        UserConfig::load(saved,"player1.ini");
        assert(saved.effectsIntensity==100 && saved.boardBounciness==100 && saved.fieldShake==35 && saved.particleIntensity==35);
        assert(saved.landingEffect==5 && saved.lineClearEffect==5 && saved.hardDropTrail==5);
        assert(saved.username=="图形 测试" && saved.uiLanguage==1);
        stage("numeric field validation");root(s);choose(s,"GAME");choose(s,"playfield");
        choose(s,setting(Id::FieldWidth));text(s,"12");
        ui(s,"playfield");UserConfig::load(saved,"player1.ini");assert(saved.fieldWidth==12);
        stage("map editor create/paint/save");root(s);choose(s,"maps");choose(s,"map-new");tap(s,SDL_SCANCODE_RETURN);ui(s,"map-editor");
        tap(s,SDL_SCANCODE_3);
        auto editor=ui(s,"map-editor");assert(editor.ui->metadata.editor);
        const auto originalRow=editor.ui->metadata.editor->row;
        assert(editor.ui->metadata.editor->field.colors.at(originalRow*editor.ui->metadata.editor->field.width)==1);
        tap(s,SDL_SCANCODE_RIGHT);tap(s,SDL_SCANCODE_7);
        editor=ui(s,"map-editor");assert(editor.ui->metadata.editor->column==1 && editor.ui->metadata.editor->brush==5);
        tap(s,SDL_SCANCODE_ESCAPE);ui(s,"map-editor-exit");tap(s,SDL_SCANCODE_ESCAPE);
        editor=ui(s,"map-editor");assert(editor.ui->metadata.editor->dirty && editor.ui->metadata.editor->column==1);
        tap(s,SDL_SCANCODE_TAB);editor=ui(s,"map-editor");assert(editor.ui->metadata.editor->sequenceFocus);
        assert(!editor.ui->metadata.editor->candidates.empty() && editor.ui->metadata.editor->detail);
        tap(s,SDL_SCANCODE_F3);
        tap(s,SDL_SCANCODE_SLASH);ui(s,"map-editor-search");text(s,"#");
        editor=ui(s,"map-editor");assert(editor.ui->metadata.editor->query=="#" && !editor.ui->metadata.editor->candidates.empty());
        const auto columns=editor.ui->metadata.editor->candidateColumns;
        tap(s,SDL_SCANCODE_DOWN);editor=ui(s,"map-editor");assert(editor.ui->metadata.editor->candidate==static_cast<std::size_t>(columns));
        tap(s,SDL_SCANCODE_RETURN);editor=ui(s,"map-editor");assert(editor.ui->metadata.editor->sequenceSize==1);
        tap(s,SDL_SCANCODE_TAB);tap(s,SDL_SCANCODE_S);
        UserConfig::Maps::Collection maps;assert(UserConfig::Maps::load(maps) && maps.maps.size()==1);
        tap(s,SDL_SCANCODE_ESCAPE);ui(s,"maps");
        stage("preset editor save");root(s);choose(s,"presets");choose(s,"preset-new");
        ui(s,"root");click(s,"preset-rename");
        wait(s,[&](const auto&){return s.text_input_active();});
        s.text_event({Frontend::TextEditKind::Insert,"discarded name"});
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.input=="discarded name";});
        click(s,"preset-save",0);
        const auto presetOwner=ui(s,"root");assert(!s.text_input_active());
        assert(presetOwner.ui->metadata.entries[presetOwner.ui->metadata.selected].id=="preset-save");
        choose(s,"preset-rename");text(s,"中文预设");ui(s,"root");
        choose(s,"preset-save");
        UserConfig::Presets::Collection presets;assert(UserConfig::Presets::load(presets));
        assert(std::any_of(presets.presets.begin(),presets.presets.end(),[](const auto& p){return p.name=="中文预设";}));
        tap(s,SDL_SCANCODE_ESCAPE);ui(s,"presets");
        stage("key capture and binding persistence");root(s);choose(s,"CONTROLS");choose(s,setting(Id::ControlHardDrop));
        auto capture=wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::KeyCapture;});
        assert(!capture.ui->metadata.currentValue.empty() && !capture.ui->metadata.hint.empty());
        assert(capture.ui->metadata.parent && !capture.ui->metadata.parent->entries.empty());
        const auto& owner=*capture.ui->metadata.parent;
        assert(owner.entries.at(owner.selected).id==setting(Id::ControlHardDrop));
        tap(s,SDL_SCANCODE_F7);ui(s,"CONTROLS");UserConfig::load(saved,"player1.ini");
        const auto drops=UserConfig::control_keys(saved,3);
        assert(std::find(drops.begin(),drops.end(),Graphical::native_key(SDL_SCANCODE_F7))!=drops.end());
        stage("challenge catalog");root(s);choose(s,"challenges");
        auto catalog=ui(s,"challenges");assert(!catalog.ui->metadata.entries.empty());
        stage("full app gameplay and menu return");tap(s,SDL_SCANCODE_R);
        const auto playing=wait(s,[](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing && v.board.width==12;});
        assert(playing.controlHints.size()==13 && playing.controlHints[3].value.find("F7")!=std::string::npos);
        tap(s,SDL_SCANCODE_C);tap(s,SDL_SCANCODE_F7);
        wait(s,[](const auto& v){return v.board.pieces==1 && v.board.hold.valid();});
        tap(s,SDL_SCANCODE_U);wait(s,[](const auto& v){return v.board.pieces==0;});
        tap(s,SDL_SCANCODE_F2);wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Paused;});
        tap(s,SDL_SCANCODE_F2);tap(s,SDL_SCANCODE_ESCAPE);ui(s,"start");
    }
    stage("Bot through original full client loop");
    first=defaults();first.autoplayMode=1;first.autoplayPps=20;first.goal=4;first.pieceTarget=2;
    Graphical::LaunchOptions play;play.startScreen=false;
    {Graphical::Session s(first,second,play);const auto finished=wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Finished && v.board.pieces==2;});
        assert(finished.roundRevision>0);tap(s,SDL_SCANCODE_R);
        wait(s,[&](const auto& v){return v.roundRevision>finished.roundRevision && v.status==Graphical::SessionStatus::Finished && v.board.pieces==2;});
        stage("board save dialog cancellation");tap(s,SDL_SCANCODE_S);
        auto prompt=wait(s,[&](const auto& v){return s.text_input_active() && v.ui && v.ui->metadata.id=="replay-save";});
        assert(prompt.ui->overlay && !prompt.ui->metadata.parent && !prompt.ui->metadata.chinese);
        assert(!prompt.ui->metadata.currentValue.empty() && !prompt.ui->metadata.shortcuts.empty());
        s.text_event({Frontend::TextEditKind::Insert,"cancelled-name.txt"});
        s.text_event({Frontend::TextEditKind::Cancel});
        wait(s,[&](const auto& v){return !s.text_input_active() && v.ui && v.ui->metadata.kind==Frontend::UiKind::Notice;});
        assert(!std::filesystem::exists(".tetr/replay/cancelled-name.txt"));
        stage("statistics and original save dialog");tap(s,SDL_SCANCODE_TAB);
        auto stats=wait(s,[](const auto& v){return v.ui && v.ui->metadata.statistics.has_value();});
        assert(stats.ui->metadata.statistics->page==Frontend::UiStatisticsPage::Summary && stats.ui->metadata.statistics->metrics.size()>=18);
        const auto metrics=stats.ui->metadata.statistics->metrics;
        // Repeated Down visits the complete shared order; no overview metric
        // is omitted from the selectable list or steals focus out of order.
        for(int i=1;i<int(metrics.size());++i) {
            tap(s,SDL_SCANCODE_DOWN);
            stats=wait(s,[&](const auto& v){return v.ui && v.ui->metadata.statistics && v.ui->metadata.statistics->selectedMetric==i;});
            assert(stats.ui->metadata.statistics->metrics[i].id==metrics[i].id);
        }
        tap(s,SDL_SCANCODE_DOWN);assert(s.view().ui->metadata.statistics->selectedMetric==int(metrics.size())-1);
        tap(s,SDL_SCANCODE_TAB);stats=wait(s,[](const auto& v){return v.ui && v.ui->metadata.statistics && v.ui->metadata.statistics->page==Frontend::UiStatisticsPage::Clears;});
        assert(stats.ui->metadata.statistics->pieces.size()>=7);
        tap(s,SDL_SCANCODE_DOWN);stats=s.view();assert(stats.ui->metadata.statistics->selectedRow==1);
        tap(s,SDL_SCANCODE_TAB);
        tap(s,SDL_SCANCODE_S);
        prompt=wait(s,[&](const auto& v){return s.text_input_active() && v.ui && v.ui->metadata.id=="replay-save";});
        assert(!prompt.ui->overlay && !prompt.ui->metadata.parent);
        const auto automatic=Replay::recent_replays().front().path;
        text(s,"中文录像.txt");
        wait(s,[](const auto&){return std::filesystem::exists(std::filesystem::u8path(".tetr/replay/中文录像.txt"));});
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Statistics;});
        assert(!std::filesystem::exists(automatic));
        assert(Replay::recent_replays().front().path==std::filesystem::u8path(".tetr/replay/中文录像.txt"));
    }
    stage("full client loop replay with Unicode path");
    first=defaults();first.gameMode=5;first.replayPath=".tetr/replay/中文录像.txt";
    {Graphical::Session s(first,second,play);
        wait(s,[](const auto& v){return v.replay && v.status==Graphical::SessionStatus::Finished && v.board.pieces==2;});
        stage("playback Save As retains original recent file");
        const auto originalRecent=Replay::recent_replays().front().path;
        tap(s,SDL_SCANCODE_S);text(s,"playback-extra.txt");
        wait(s,[](const auto&){return std::filesystem::exists(".tetr/replay/playback-extra.txt");});
        assert(std::filesystem::exists(originalRecent) && Replay::recent_replays().front().path==originalRecent);
        const auto clearCandidate=Replay::save_recent("{{Model,0}}{end_all,0}","clear by menu");assert(clearCandidate);
        tap(s,SDL_SCANCODE_P);root(s);choose(s,"MODEL");choose(s,"recent-replays");
        const auto recent=ui(s,"recent-replays");
        assert(recent.ui->metadata.entries.front().id=="clear-replays");
        assert(std::any_of(recent.ui->metadata.entries.begin(),recent.ui->metadata.entries.end(),[&](const auto& e){return e.id==originalRecent.generic_u8string();}));
        assert(std::none_of(recent.ui->metadata.entries.begin(),recent.ui->metadata.entries.end(),[](const auto& e){return e.id.find("playback-extra")!=std::string::npos;}));
        stage("recent replay keyboard action menu playback");
        choose(s,originalRecent.generic_u8string());const auto actions=ui(s,originalRecent.generic_u8string());
        assert(actions.ui->metadata.entries.size()==2 && actions.ui->metadata.entries.front().id=="play" && actions.ui->metadata.entries.back().id=="delete");
        const auto revision=actions.roundRevision;choose(s,"play");
        wait(s,[&](const auto& v){return v.roundRevision>revision && v.replay && v.status==Graphical::SessionStatus::Finished && v.board.pieces==2;});
        stage("recent replay direct click and fresh Enter deletion");
        tap(s,SDL_SCANCODE_P);root(s);choose(s,"MODEL");choose(s,"recent-replays");ui(s,"recent-replays");
        click(s,originalRecent.generic_u8string());ui(s,originalRecent.generic_u8string());click(s,"delete");
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.notice.find("Press Enter again to delete this replay file")!=std::string::npos;});
        click(s,"delete");std::this_thread::sleep_for(60ms);
        assert(std::filesystem::exists(originalRecent)); // Repeated clicks cannot confirm deletion.
        tap(s,SDL_SCANCODE_UP);
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.entries[v.ui->metadata.selected].id=="play";});
        assert(std::filesystem::exists(originalRecent));choose(s,"delete");
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.notice.find("Press Enter again to delete this replay file")!=std::string::npos;});
        assert(std::filesystem::exists(originalRecent));tap(s,SDL_SCANCODE_RETURN);
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.id=="recent-replays" && v.ui->metadata.notice=="Replay deleted.";});
        assert(!std::filesystem::exists(originalRecent) && std::filesystem::exists(".tetr/replay/playback-extra.txt"));
        stage("recent replay clear confirmation and extra-copy preservation");
        const auto remaining=Replay::recent_replays();assert(!remaining.empty());
        click(s,"clear-replays");
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.notice.find("Press Enter again to delete all recent")!=std::string::npos;});
        click(s,"clear-replays");std::this_thread::sleep_for(60ms);assert(std::filesystem::exists(*clearCandidate));
        tap(s,SDL_SCANCODE_RIGHT);
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.notice=="Confirmation cancelled.";});
        assert(Replay::recent_replays().size()==remaining.size());click(s,"clear-replays");
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.notice.find("Press Enter again to delete all recent")!=std::string::npos;});
        tap(s,SDL_SCANCODE_RETURN);
        const auto cleared=wait(s,[](const auto& v){return v.ui && v.ui->metadata.id=="recent-replays" && v.ui->metadata.notice=="Recent replays cleared.";});
        assert(cleared.ui->metadata.entries.size()==2 && cleared.ui->metadata.entries.back().id=="empty");
        for(const auto& entry:remaining)assert(!std::filesystem::exists(entry.path));
        assert(Replay::recent_replays().empty() && std::filesystem::exists(".tetr/replay/playback-extra.txt"));
    }
    stage("local versus two independently controlled boards");
    first=defaults();second=defaults();first.gameMode=6;second.gameMode=6;
    second.keyCodes[3]=Graphical::native_key(SDL_SCANCODE_KP_ENTER);
    {Graphical::Session s(first,second,play);
        for(int digit:{3,2,1}) {
            const auto ready=wait(s,[&](const auto& v){return v.countdownDigit==digit && v.peers.size()==1;});
            assert(ready.board.pieces==0 && ready.peers[0].board.pieces==0 && !ready.ui);
        }
        const auto before=wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Playing && v.peers.size()==1 && v.peers[0].board.active;});
        assert(s.view().countdownDigit==0);
        tap(s,SDL_SCANCODE_SPACE);tap(s,SDL_SCANCODE_KP_ENTER);
        const auto after=wait(s,[](const auto& v){return v.board.pieces==1 && v.peers.size()==1 && v.peers[0].board.pieces==1;});
        assert(after.peers[0].slot==-2 && !after.peers[0].miniature);
        assert(after.board.placement && after.peers[0].board.placement);
        Graphical::FieldEffects left,right;left.update(before.board,0);right.update(before.peers[0].board,0);
        left.update(after.board,.1);right.update(after.peers[0].board,.1);
        left.update(after.board,.16);right.update(after.peers[0].board,.16);
        assert(!left.particles().empty() && !right.particles().empty());
        assert(left.transform().y>0 && right.transform().y>0);
    }
    stage("graphical room UI and original terminal peer");
    {
        Network::RelayServer relay(341);relay.bind(3197);std::atomic<bool> relayRunning=true;
        std::thread relayThread([&]{while(relayRunning)relay.poll(1ms);});
        struct RelayStop {std::atomic<bool>& running;std::thread& thread;~RelayStop(){running=false;thread.join();}} relayStop{relayRunning,relayThread};
        for(const auto key:{SDL_SCANCODE_R,SDL_SCANCODE_RETURN,SDL_SCANCODE_F7}) {
            stage("active player two starts online without opening preferences");
            auto a=defaults(),b=defaults();a.gameMode=6;b.gameMode=4;
            a.persistenceEnabled=b.persistenceEnabled=false;b.serverIp="127.0.0.1:3197";
            if(key==SDL_SCANCODE_F7)UserConfig::set_control_keys(b,8,{Graphical::native_key(key)});
            Graphical::LaunchOptions launch;launch.playerTwoActive=true;
            Graphical::Session online(a,b,launch);ui(online,"start");tap(online,key);
            const auto route=wait(online,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Menu;});
            assert(route.ui->metadata.id=="rooms" && route.ui->metadata.path=="play-rooms");
            if(key==SDL_SCANCODE_R) {
                tap(online,SDL_SCANCODE_ESCAPE);ui(online,"start");tap(online,key);ui(online,"rooms");
                tap(online,SDL_SCANCODE_P);ui(online,"root");
                tap(online,SDL_SCANCODE_ESCAPE);ui(online,"start");tap(online,key);
                assert(ui(online,"rooms").ui->metadata.path=="play-rooms");
            }
            choose(online,"create");
            wait(online,[](const auto& v){return v.ui && std::any_of(v.ui->metadata.entries.begin(),v.ui->metadata.entries.end(),
                [](const auto& e){return e.id=="submit" && e.enabled;});});
            choose(online,"name");wait(online,[&](const auto&){return online.text_input_active();});
            // Shortcut letters in a room name must not become menu actions.
            online.input().update(Graphical::native_key(SDL_SCANCODE_P),true);std::this_thread::sleep_for(40ms);
            const auto roomName=key==SDL_SCANCODE_R?"RPRO":key==SDL_SCANCODE_RETURN?"EPRO":"FPRO";
            online.text_event({Frontend::TextEditKind::Insert,roomName});online.text_event({Frontend::TextEditKind::Confirm});
            wait(online,[&](const auto& v){return !online.text_input_active() && v.ui && v.ui->metadata.kind==Frontend::UiKind::Menu;});
            assert(online.view().ui->metadata.path=="play-rooms");
            online.input().update(Graphical::native_key(SDL_SCANCODE_P),false);
            choose(online,"submit");
            wait(online,[](const auto& v){return (v.ui && v.ui->metadata.kind==Frontend::UiKind::Lobby) ||
                (!v.ui && v.room && (v.status==Graphical::SessionStatus::Countdown || v.status==Graphical::SessionStatus::Playing));});
            tap(online,SDL_SCANCODE_ESCAPE);ui(online,"start");
        }
        auto state=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
        Runtime::GameContext context(*state,services);Network::NetworkServices network;Input::Services input;
        Input::FocusState focus;Replay::ReplaySessionState replay;App::ApplicationState app;
        auto peer=defaults();peer.gameMode=4;peer.username="Terminal peer";peer.serverIp="127.0.0.1:3197";
        context.attach_user_settings(peer);context.attach_input(input);context.attach_network(network);context.attach_app(app);context.attach_replay(replay);
        app.primaryContext=&context;app.headlessLocalMatch=true;
        input.initialized=true;input.focusSource=&focus;input.keyEventSource=&focus.events();Input::refresh_bindings(input,peer.keyCodes);
        first=defaults();first.serverIp=peer.serverIp;first.username="Graphical host";first.ghost=0;
        Graphical::Session s(first,second,{});ui(s,"start");tap(s,SDL_SCANCODE_P);ui(s,"root");choose(s,"NETWORK");choose(s,"connect");
        const auto has=[&](const auto& v,const std::string& id){return v.ui && std::any_of(v.ui->metadata.entries.begin(),v.ui->metadata.entries.end(),[&](const auto& e){return e.id==id && e.enabled;});};
        wait(s,[&](const auto& v){return has(v,"room-create");});choose(s,"room-create");ui(s,"rooms");
        wait(s,[&](const auto& v){return has(v,"name");});choose(s,"name");text(s,"GLUI");ui(s,"rooms");choose(s,"submit");
        wait(s,[&](const auto& v){return has(v,"room-users");});
        Network::initialize_network_services(context,peer.serverIp);
        pump=[&]{Network::poll_connection(network);};
        wait(s,[&](const auto&){return network.connection==Network::ConnectionState::Connected;});
        Network::request_room(network,Network::RoomOperation::Join,"GLUI");
        wait(s,[&](const auto&){return !network.pendingRoomRequest && network.room.players==2;});
        choose(s,"room-users");
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.id=="rooms" && v.ui->metadata.entries.size()==3;});
        tap(s,SDL_SCANCODE_LEFT);ui(s,"NETWORK");choose(s,"room-start");
        pump=[]{};
        std::atomic<bool> stopPeer=false,peerReady=false,canStart=false,peerFailed=false;
        struct StopPeer {};
        const auto observePeer=[&]{
            if(stopPeer)throw StopPeer{};
            const auto ready=std::count_if(network.roomMembers.begin(),network.roomMembers.end(),[](const auto& member){return member.ready;});
            if(ready==2)canStart=true;
        };
        services.clock.waitInterrupt=observePeer;
        input.controlInterrupt=observePeer;
        std::thread terminal([&]{try {App::initialize_game(context);peerReady=true;App::run(context);}catch(const StopPeer&){}catch(const std::exception& e){std::cerr<<"Terminal peer: "<<e.what()<<'\n';peerFailed=true;}});
        struct PeerStop {std::atomic<bool>& stop;std::thread& thread;~PeerStop(){stop=true;thread.join();}} peerStop{stopPeer,terminal};
        stage("mixed frontend match start");
        // Both fighters must be ready before the host confirms. Setup and
        // countdown acknowledgements continue through the original protocol.
        const auto lobby=wait(s,[&](const auto& v){return canStart.load() && v.ui && v.ui->metadata.lobby && v.ui->metadata.lobby->ready==2;});
        assert(lobby.ui->metadata.lobby->members.size()==2 && lobby.ui->metadata.lobby->startEnabled && lobby.ui->metadata.lobby->canStart);
        assert(std::any_of(lobby.ui->metadata.lobby->members.begin(),lobby.ui->metadata.lobby->members.end(),[](const auto& member){return member.host && member.name=="Graphical host";}));
        tap(s,SDL_SCANCODE_RETURN);
        stage("mixed frontend countdown completion");
        for(int digit:{3,2,1}) {
            const auto countdown=wait(s,[&](const auto& v){return v.countdownDigit==digit && !v.ui && !v.peers.empty();});
            assert(countdown.actionEnabled[0][8] && countdown.actionEnabled[0][9]);
            assert(!countdown.actionEnabled[0][11] && !countdown.actionEnabled[0][12]);
        }
        // Countdown now has roster-backed boards before remote snapshots.
        // Wait for the peer's actual spawn before injecting its first input.
        const auto beforeOnline=wait(s,[&](const auto& v){return v.status==Graphical::SessionStatus::Playing && !v.ui && !v.peers.empty() && v.peers[0].board.active && peerReady.load();});
        assert(beforeOnline.endMatch && beforeOnline.actionEnabled[0][8]);
        assert(s.view().countdownDigit==0);
        stage("mixed frontend board propagation");
        focus.update(peer.keyCodes[3],true);std::this_thread::sleep_for(30ms);focus.update(peer.keyCodes[3],false);
        tap(s,SDL_SCANCODE_SPACE);
        const auto afterOnline=wait(s,[](const auto& v){return v.board.pieces==1 && !v.peers.empty() && v.peers[0].board.pieces==1;});
        assert(!afterOnline.peers[0].miniature && afterOnline.board.placement && afterOnline.peers[0].board.placement);
        assert(!beforeOnline.board.ghost && !beforeOnline.peers[0].board.ghost);
        Graphical::FieldEffects left,right;left.update(beforeOnline.board,0);right.update(beforeOnline.peers[0].board,0);
        left.update(afterOnline.board,.1);right.update(afterOnline.peers[0].board,.1);
        left.update(afterOnline.board,.16);right.update(afterOnline.peers[0].board,.16);
        assert(!left.particles().empty() && !right.particles().empty());
        assert(left.transform().y>0 && right.transform().y>0);
        assert(!peerFailed);
        stage("mixed frontend clean shutdown");
    }
    std::filesystem::current_path(original);std::filesystem::remove_all(directory);
    std::cerr<<"Full graphical application: settings/Unicode/maps/presets/key-capture/challenges/Bot/statistics/save/local-versus/mixed-frontend-network OK\n";
    return 0;
}
} // namespace TetrTests::graphical_application_test
