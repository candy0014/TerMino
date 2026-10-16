#include "tetr/challenges/challenges.h"
#include "tetr/app/application_state.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/app/settings_menu.h"
#include "tetr/input/services.h"
#include "tetr/config/normalization.h"
#include "tetr/config/presets.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/single_player_challenges.h"
#include "../src/app/post_game_statistics.h"
#include "../src/app/settings_view.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {
using namespace Challenges;
using Id=UserConfig::SettingId;
namespace Presets=UserConfig::Presets;
const Definition& sprint(){return *find("sprint-40");}
std::filesystem::path directory;
Result result(const UserConfig::Settings& settings,int lines=40,double time=30) {
    Result r;r.config=Runtime::normalize_user_config(settings,Runtime::GameMode::Zen);r.gameSeconds=time;
    r.statistics.totalLines=lines;r.statistics.placedBlocks=100;r.statistics.totalAttack=20;r.statistics.clearStatistics[{0,1}][0]=lines;return r;
}
void constraints_and_adjustment() {
    Constraint any{Id::HoldCount};assert(any.accepts(-123));
    Constraint fixed{Id::Goal,Restriction::Fixed,{2}};assert(!fixed.accepts(0));assert(fixed.accepts(2.0));assert(fixed.nearest(0)==UserConfig::PresetValue{2});
    Constraint set{Id::RotationSystem,Restriction::Set,{0,1,7}};assert(set.accepts(7));assert(set.nearest(5)==UserConfig::PresetValue{0});
    Constraint range{Id::HoldCount,Restriction::Interval,{},1,3};assert(range.accepts(2));assert(range.nearest(5)==UserConfig::PresetValue{3});assert(range.nearest(-4)==UserConfig::PresetValue{1});
    Constraint real{Id::Das,Restriction::Interval,{},.1,.3};assert(real.nearest(.8)==UserConfig::PresetValue{.3});
    UserConfig::Settings settings;settings.gameMode=3;settings.fieldWidth=17;settings.holdCount=8;settings.das=.213;
    const auto before=settings;assert(!sprint().check(settings));assert(sprint().change(settings));assert(sprint().check(settings));
    assert(settings.fieldWidth==10 && settings.holdCount==1 && settings.das==before.das);
    settings.arr=.017;assert(find("dig-20")->change(settings));assert(find("dig-20")->check(settings));
    assert(settings.challengeAdjustment->challengeId=="dig-20" && settings.arr==before.arr);
    restore_adjustment(settings);assert(settings.gameMode==3 && settings.fieldWidth==17 && settings.holdCount==8 && settings.arr==before.arr);
    for(const auto& d:definitions()) {auto c=before;assert(d.change(c));assert(d.check(c));restore_adjustment(c);assert(c.gameMode==before.gameMode && c.fieldWidth==before.fieldWidth);}
    assert(find("sprint-pentomino-40"));auto p=before;find("sprint-pentomino-40")->change(p);assert(p.rotationSystem==3);
    settings.persistencePath=(directory/"player.ini").string();
    Presets::SavedPreset preset;preset.name="temporary baseline";preset.fields={{Id::FieldWidth,15},{Id::Goal,0}};
    assert(Presets::apply(settings,preset));assert(sprint().change(settings));Presets::record_edits(settings);
    assert(std::get<int>(settings.presets.layers.front().values.at(Id::FieldWidth))==15);
    settings.challengeReplay=UserConfig::ChallengeReplayRestore{0,"original replay",false};settings.gameMode=5;settings.replayPath="temporary replay";
    assert(UserConfig::save(settings));UserConfig::Settings loaded;UserConfig::load(loaded,settings.persistencePath);
    assert(loaded.fieldWidth==15 && loaded.gameMode==3 && loaded.replayPath.empty() && !loaded.challengeAdjustment && !loaded.challengeReplay);
    restore_replay(settings);assert(settings.gameMode==0 && settings.replayPath=="original replay");
    assert(Presets::undo(settings));assert(!settings.challengeAdjustment && settings.fieldWidth==17 && settings.goal==0);
}
void valuation() {
    UserConfig::Settings settings;sprint().change(settings);auto r=result(settings,43,25.5);
    auto score=sprint().val(settings,r);assert(score && score->progress==40 && score->value==25.5);
    auto partial=result(settings,39,10);assert(sprint().better(*score,*sprint().val(settings,partial)));
    assert(sprint().better({40,20},{40,25}) && !sprint().better({40,25},{40,25}));
    r.undoCount=1;assert(!sprint().val(settings,r));r.undoCount=0;r.pauseCount=1;assert(!sprint().val(settings,r));r.pauseCount=0;
    r.statistics.totalLines=99;assert(!sprint().val(settings,r));r=result(settings);r.config.mode.replayMode=true;assert(!sprint().val(settings,r));
    r=result(settings);r.config.simulationPps=1;assert(!sprint().val(settings,r));
    r=result(settings);r.gameSeconds=std::nan("");assert(!sprint().val(settings,r));
    const auto& noRotation=*find("sprint-no-rotation-40");assert(noRotation.change(settings));r=result(settings);assert(noRotation.val(settings,r));
    r.statistics.clearStatistics[{0,1}]={39,1,0};assert(!noRotation.val(settings,r));
    for(const auto& d:definitions()) {
        UserConfig::Settings s;d.change(s);auto v=result(s,44,61);v.statistics.totalGarbageLines=41;
        auto evaluated=d.val(s,v);assert(evaluated && evaluated->progress==std::min(d.target,d.progress==Progress::Lines?44.:d.progress==Progress::Garbage?41.:61.));
        if(d.secondary==Secondary::Attack)assert(evaluated->value==20 && d.better({d.target,30},{d.target,20}));
    }
}
void history() {
    History h;UserConfig::Settings settings;sprint().change(settings);std::uint64_t run=0;
    auto outcomes=h.add(settings,result(settings),1000,run);assert(run==1 && !outcomes.empty() && h.best(sprint())->improved);
    h.add(settings,result(settings),2000,run);assert(h.best(sprint())->runId==1 && !h.recent(sprint()).front().improved);
    const auto replay=directory/"saved replay.rep";std::ofstream(replay)<<"test";
    assert(h.attach_replay(1,replay));assert(replay_exists(*h.best(sprint())));
    const auto relative=replay.lexically_relative(std::filesystem::current_path());
    const auto expected=(relative.empty()?replay:relative).generic_u8string();
    assert(h.best(sprint())->replayPath==expected);
    const auto path=directory/"history";assert(save(h,path));History loaded;assert(load(loaded,path));assert(loaded.best(sprint())->runId==1);
    for(const auto& record:loaded.records)assert(record.replayPath.empty() || record.replayPath==expected);
    // Saving legacy in-memory records must not write absolute references again.
    auto legacy=h;for(auto& record:legacy.records)if(!record.replayPath.empty())record.replayPath=replay.generic_u8string();
    assert(save(legacy,path) && load(loaded,path));
    assert(loaded.best(sprint())->replayPath==h.best(sprint())->replayPath);
#ifdef _WIN32
    auto shared=h;
    for(auto& record:shared.records)record.replayPath=R"(\\server\share\demo.txt)";
    const auto sharedHistory=directory/"shared-history";
    assert(save(shared,sharedHistory) && load(shared,sharedHistory));
    for(const auto& record:shared.records)assert(record.replayPath=="//server/share/demo.txt");
#endif
    std::filesystem::remove(replay);assert(!replay_exists(*loaded.best(sprint())));
    auto invalid=loaded;invalid.records.front().score.value=std::nan("");assert(!save(invalid,path));History intact;assert(load(intact,path) && intact.records.size()==loaded.records.size());
    std::ofstream(path)<<"TETR_CHALLENGES broken";const auto count=loaded.records.size();assert(!load(loaded,path) && loaded.records.size()==count);
    h={};h.nextRunId=101;
    for(int i=1;i<=100;++i)h.records.push_back({std::uint64_t(i),i,sprint().id,{40,double(200-i)},i<=30,i<=60?"saved.rep":""});
    h.retain();assert(h.records.size()==50);std::set<std::uint64_t> kept;for(const auto& r:h.records)kept.insert(r.runId);
    for(int i=21;i<=30;++i)assert(kept.count(i));for(int i=91;i<=100;++i)assert(kept.count(i));for(int i=31;i<=60;++i)assert(kept.count(i));
    auto recent=h.recent(sprint());assert(recent.front().runId==100 && recent.back().runId==21);
    assert(history_path(settings)!=history_path(UserConfig::Settings{} ) || settings.persistencePath.empty());
    UserConfig::Settings noRotation;find("sprint-no-rotation-40")->change(noRotation);History multiple;
    const auto matching=multiple.add(noRotation,result(noRotation),1000,run);assert(matching.size()>=2 && multiple.records.size()==matching.size() && run==1);
    const auto bothReplay=directory/"both.rep";std::ofstream(bothReplay)<<"test";assert(multiple.attach_replay(run,bothReplay));
    for(const auto& record:multiple.records)assert(record.runId==run && replay_exists(record));
}
void app_and_presentation() {
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    App::ApplicationState app;UserConfig::Settings settings;Replay::ReplaySessionState replay;
    settings.persistencePath=(directory/"app-player.ini").string();sprint().change(settings);
    context.attach_app(app);context.attach_user_settings(settings);context.attach_replay(replay);
    context.config=Runtime::normalize_user_config(settings,Runtime::GameMode::Zen);
    App::begin_single_player_challenges(context);auto& p=session->player;p.gameplay.scoring=result(settings).statistics;p.timing.completedGameTime=Runtime::Duration::from_seconds(30);
    App::finish_single_player_challenges(context,p);assert(app.challengeRound && !app.challengeRound->outcomes.empty() && app.challengeRound->outcomes.front().personalBest);
    App::finish_single_player_challenges(context,p);History h;assert(load(h,history_path(settings)) && h.best(sprint()));
    const auto path=directory/"app.rep";std::ofstream(path)<<"test";assert(App::attach_challenge_replay(context,path));
    auto tree=App::make_challenge_menu(settings);auto& leaf=tree.children.front().children[2];assert(leaf.challengeQualified && *leaf.challengeQualified);
    assert(leaf.children.front().summary==App::challenge_score(sprint(),{40,30}));assert(leaf.children.front().children.front().enabled);
    assert(leaf.children.front().children.front().title.find("PB")!=std::string::npos);
    assert(leaf.children.front().children.front().title.find("  Re")!=std::string::npos);
    settings.fieldWidth=15;tree=App::make_challenge_menu(settings);assert(!*tree.children.front().children[2].challengeQualified);
    assert(tree.children.front().children[2].description.find("15")!=std::string::npos);
    App::PostGameStatisticsView view(App::PostGameStatistics{});view.set_challenges(app.challengeRound->outcomes);
    view.next_page();assert(view.page()==App::PostGamePage::Summary);view.next_page();assert(view.page()==App::PostGamePage::Clears);view.next_page();assert(view.page()==App::PostGamePage::Challenges);
    std::ostringstream capture;auto* old=std::cout.rdbuf(capture.rdbuf());view.render(context,"",{80,24});std::cout.rdbuf(old);
    Test::TerminalScreen screen;screen.feed(capture.str());assert(screen.text().find("PB")!=std::string::npos && screen.rows()<=24 && screen.columns()<=80);
    view.next_page();assert(view.page()==App::PostGamePage::Summary);view.close();assert(view.page()==App::PostGamePage::Board);
    settings.challengeReplay=UserConfig::ChallengeReplayRestore{0,"old.rep",false};settings.gameMode=5;settings.replayPath="new.rep";
    try{App::request_start_screen(context);assert(false);}catch(const App::StartScreenRequested&){}
    assert(settings.gameMode==0 && settings.replayPath=="old.rep" && !settings.challengeReplay);
    replay.undoCount=1;App::finish_single_player_challenges(context,p);
    assert(app.challengeRound->outcomes.empty() && !app.challengeRound->runId && !App::attach_challenge_replay(context,path));
    assert(load(h,history_path(settings)) && h.best(sprint()));
    std::filesystem::remove(path);tree=App::make_challenge_menu(settings);
    const auto& missing=tree.children.front().children[2].children.front().children.front();
    assert(!missing.enabled && missing.title.find("  Re")==std::string::npos);
    context.config.mode.replayMode=true;App::begin_single_player_challenges(context);assert(!app.challengeRound);
}
void local_replay_restore() {
    auto a=std::make_unique<Runtime::SessionState>(),b=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices sa,sb;Runtime::GameContext first{*a,sa},second{*b,sb};App::ApplicationState app;
    UserConfig::Settings one,two;Input::Services ia,ib;one.keyCodes=two.keyCodes=UserConfig::default_key_codes();
    first.attach_app(app);second.attach_app(app);first.attach_user_settings(one);second.attach_user_settings(two);first.attach_input(ia);second.attach_input(ib);
    app.primaryContext=&first;app.localPlayerTwoContext=&second;app.localVersusSettingsAvailable=true;
    one.gameMode=two.gameMode=6;one.fieldWidth=13;two.fieldWidth=17;one.replayPath="one.rep";two.replayPath="two.rep";
    two.challengeReplay=UserConfig::ChallengeReplayRestore{6,"two.rep",false};two.gameMode=5;two.replayPath="record.rep";
    App::Settings::adopt_single_player_settings(second);assert(app.singlePlayerOwner==1 && one.gameMode==5);
    App::restore_challenge_replay(first);
    assert(one.gameMode==6 && two.gameMode==6 && one.fieldWidth==13 && two.fieldWidth==17);
    assert(one.replayPath=="one.rep" && two.replayPath=="two.rep" && !one.challengeReplay && !two.challengeReplay && app.singlePlayerOwner==0);
}
}
int main() {
    directory=std::filesystem::temp_directory_path()/("termino_challenges_"+std::to_string(Challenges::utc_now_milliseconds()));std::filesystem::create_directories(directory);
    constraints_and_adjustment();valuation();history();app_and_presentation();local_replay_restore();std::filesystem::remove_all(directory);
    std::cerr<<"challenge constraints, minimal rollback, valuation, persistence, retention, replay association and results passed\n";
}
