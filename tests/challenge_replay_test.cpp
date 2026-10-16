#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/app/start_screen_navigation.h"
#include "tetr/config/normalization.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/recent.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/single_player_challenges.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>

namespace {
void environment(const char* name,const char* value) {
#ifdef _WIN32
    _putenv_s(name,value);
#else
    setenv(name,value,1);
#endif
}
struct Rig {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    App::ApplicationState app;UserConfig::Settings settings;Input::Services input;Replay::ReplaySessionState replay;
    Rig() {
        context.attach_app(app);context.attach_user_settings(settings);context.attach_input(input);context.attach_replay(replay);
        app.primaryContext=&context;settings.keyCodes=UserConfig::default_key_codes();input.keyStateProvider=[](int){return false;};
        settings.fieldWidth=13;settings.replayPath="previous.txt";
    }
};
std::string read(const std::filesystem::path& path) {std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};}
void automatic_challenge_replays(const std::string& contents) {
    Rig r,partner;r.app.localPlayerTwoContext=&partner.context;partner.context.attach_app(r.app);
    r.settings.persistencePath=".tetr/auto-player.ini";partner.settings.persistencePath=".tetr/auto-partner.ini";
    const auto definition=*Challenges::find("sprint-10");assert(definition.change(r.settings));
    r.context.config=Challenges::preview_config(r.settings);
    App::begin_single_player_challenges(r.context);
    auto& player=r.session->player;auto& score=player.gameplay.scoring;
    score.totalLines=10;score.placedBlocks=10;score.clearStatistics[{0,1}][0]=10;
    player.timing.completedGameTime=Runtime::Duration::from_seconds(1);
    App::finish_single_player_challenges(r.context,player);
    assert(r.app.challengeRound && r.app.challengeRound->runId);
    const auto run=r.app.challengeRound->runId;
    App::remember_finished_replay(r.context,contents);
    const auto cached=std::filesystem::u8path(r.replay.recentPath);assert(!cached.empty() && std::filesystem::is_regular_file(cached));
    const auto store=Challenges::history_path(r.settings),otherStore=Challenges::history_path(partner.settings);
    Challenges::History history;assert(Challenges::load(history,store));
    assert(history.best(definition)->runId==run && Challenges::replay_exists(*history.best(definition)));
    const auto tree=App::make_challenge_menu(r.settings);
    const auto& rows=tree.children.front().children.front().children.front().children;
    assert(!rows.empty() && rows.front().enabled && rows.front().title.find("  Re")!=std::string::npos);
    const auto unrelated=std::filesystem::path(".tetr/replay/unrelated.txt");std::ofstream(unrelated)<<"unrelated";
    const auto unrelatedRun=history.nextRunId++;
    history.records.push_back({unrelatedRun,2000,definition.id,{10,20},false,unrelated.generic_u8string()});
    assert(Challenges::save(history,store) && Challenges::save(history,otherStore));
    const auto recordCount=history.records.size();
    const auto verify=[&](const std::filesystem::path& destination) {
        for(const auto& file:{store,otherStore}) {
            Challenges::History loaded;assert(Challenges::load(loaded,file) && loaded.records.size()==recordCount);
            for(const auto& record:loaded.records) {
                if(record.runId==run)assert(record.replayPath==destination.generic_u8string() && Challenges::replay_exists(record));
                if(record.runId==unrelatedRun)assert(record.replayPath==unrelated.generic_u8string());
            }
        }
        assert(read(destination)==contents+Replay::replay_hash_suffix(contents));
    };
    const auto originalMode=r.settings.gameMode;const auto originalPath=r.settings.replayPath;
    r.app.replayPreview=App::ReplayPreviewRequest{cached.generic_u8string(),true};
    App::initialize_game(r.context);assert(r.context.config.mode.replayMode);
    const auto first=std::filesystem::path(".tetr/replay/另存一.txt");
    environment("TETR_TEST_REPLAY",std::filesystem::absolute(first).u8string().c_str());
    App::run(r.context);verify(first);
    assert(!r.replay.challengeRecords.empty() && r.replay.sourcePath==cached.generic_u8string());
    const auto second=std::filesystem::path(".tetr/replay/另存二.txt");
    environment("TETR_TEST_REPLAY",second.u8string().c_str());
    App::run_post_game_interaction(r.context,r.session->player);verify(second);
    // Seeking reloads the original cached source, keeping the same record IDs.
    r.app.replayResume={true,.025,1024,false};App::initialize_game(r.context);
    assert(!r.replay.challengeRecords.empty());
    const auto third=std::filesystem::path(".tetr/replay/跳转后另存.txt");
    environment("TETR_TEST_REPLAY",third.u8string().c_str());
    App::run(r.context);verify(third);
    assert(r.settings.gameMode==originalMode && r.settings.replayPath==originalPath);
    assert(std::filesystem::exists(cached) && std::filesystem::exists(first) && std::filesystem::exists(second));
    environment("TETR_TEST_REPLAY","");
    const auto transient=Replay::save_recent(contents,"unsaved expiry");assert(transient);
    assert(Challenges::load(history,store) && history.attach_replay(unrelatedRun,*transient) && Challenges::save(history,store));
    for(int i=0;i<30;++i)assert(Replay::save_recent(contents,"newer play"));
    assert(!std::filesystem::exists(*transient) && std::filesystem::exists(cached));
    const auto expiredMenu=App::make_challenge_menu(r.settings);
    const auto& expiredRows=expiredMenu.children.front().children.front().children.front().children;
    bool foundExpired=false;
    for(const auto& row:expiredRows)if(row.key=="run-"+std::to_string(unrelatedRun)) {
        foundExpired=true;assert(!row.enabled && row.title.find("  Re")==std::string::npos);
    }
    assert(foundExpired);
}
void play(const std::filesystem::path& path,bool challenge,bool seek,int expectedLines) {
    Rig r;const auto original=r.settings;
    const auto expected=std::filesystem::absolute(challenge?path:*Replay::replay_path_for_loading(path.u8string())).lexically_normal();
    if(challenge)r.settings.challengeReplay=UserConfig::ChallengeReplayRestore{original.gameMode,original.replayPath,true};
    r.settings.gameMode=5;r.settings.replayPath=path.u8string();Render::begin_output_suppression();
    App::initialize_game(r.context);
    assert(r.context.config.mode.replayMode);
    assert(std::filesystem::absolute(std::filesystem::u8path(r.replay.sourcePath)).lexically_normal()==expected);
    if(!challenge)assert(!std::filesystem::u8path(r.settings.replayPath).is_absolute());
    assert(r.context.config.mode.goal==Runtime::GameGoal::Lines && r.context.config.mode.racingDistance==10);
    assert(!r.replay.operations.empty());
    if(challenge)assert(r.settings.challengeReplay && !r.settings.challengeReplay->pending);
    const auto loadedPath=r.settings.replayPath;const auto loadedData=r.replay.data;
    if(seek) {
        r.app.replayResume={true,.025,1024,false};App::initialize_game(r.context);
        assert(r.context.config.mode.replayMode && r.settings.challengeReplay && r.settings.replayPath==loadedPath);
        assert(!r.app.replayResume.pending && r.replay.data==loadedData);
    }
    App::run(r.context);
    assert(!r.app.challengeRound && r.session->player.gameplay.scoring.totalLines==expectedLines);
    if(r.replay.resultGameTime)assert(r.session->player.timing.completedGameTime &&
        std::abs(r.session->player.timing.completedGameTime->seconds()-r.replay.resultGameTime->seconds())<1e-6);
    if(challenge) {
        try{App::request_start_screen(r.context);assert(false);}catch(const App::StartScreenRequested&){}
        assert(r.settings.gameMode==original.gameMode && r.settings.replayPath==original.replayPath && !r.settings.challengeReplay);
        assert(r.settings.fieldWidth==13);
    }
    std::cerr<<"played "<<path.u8string()<<" lines="<<expectedLines<<" seek="<<seek<<'\n';
}
}
int main(int argc,char** argv) {
    const auto originalDirectory=std::filesystem::current_path();
    const auto actual=argc>1?std::optional<std::filesystem::path>{std::filesystem::absolute(std::filesystem::u8path(argv[1]))}:std::nullopt;
    const auto actualContents=actual?read(*actual):std::string{};
    const auto directory=std::filesystem::temp_directory_path()/("termino_challenge_playback_"+std::to_string(Challenges::utc_now_milliseconds()));
    std::filesystem::create_directories(directory/Replay::replay_directory());std::filesystem::current_path(directory);
    environment("TETR_TEST_BATCH","1");environment("TETR_TEST_CLOCK_SCALE","1000");
    UserConfig::Settings settings;settings.goal=2;settings.racingDistance=10;
    const auto config=Runtime::normalize_user_config(settings,Runtime::GameMode::Zen);
    const auto contents=Replay::encode_config_prefix(config)+"{block_seed,123}{garbage_seed,456}}{end_all,0.050000}";
    const auto relative=std::filesystem::u8path(".tetr/replay/10行 练习.txt"),absolute=std::filesystem::absolute(relative);
    std::ofstream(absolute)<<contents<<Replay::replay_hash_suffix(contents);
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    automatic_challenge_replays(contents);
    // Absolute references from older histories remain playable.
    play(absolute,true,true,0);
    play(relative,true,false,0);
    play(relative,false,false,0);
    play(std::filesystem::u8path("replay/10行 练习.txt"),false,false,0);
    for(const auto& file:{std::filesystem::u8path("exports/绝对 路径.txt"),
            std::filesystem::path("replay/explicit.txt"),std::filesystem::path("root replay.txt")}) {
        if(file.has_parent_path())std::filesystem::create_directories(file.parent_path());
        std::ofstream(file)<<contents<<Replay::replay_hash_suffix(contents);
        const auto selected=std::filesystem::absolute(file);
        UserConfig::Settings profile;profile.persistencePath=".tetr/absolute-input.ini";profile.replayPath=selected.generic_u8string();
        assert(UserConfig::save(profile));UserConfig::Settings restored;UserConfig::load(restored,profile.persistencePath);
        assert(!std::filesystem::u8path(restored.replayPath).is_absolute());
        const auto resolved=Replay::replay_path_for_loading(restored.replayPath);assert(resolved);
        assert(std::filesystem::absolute(*resolved).lexically_normal()==selected);
        play(selected,false,false,0);
    }
    {
        Rig r;const auto original=r.settings;
        r.app.replayPreview=App::ReplayPreviewRequest{relative.generic_u8string(),true};
        App::initialize_game(r.context);
        assert(r.context.config.mode.replayMode && r.settings.gameMode==original.gameMode && r.settings.replayPath==original.replayPath);
        assert(Replay::recording_for_save(r.context)==contents);
        r.app.replayResume={true,.025,1024,false};App::initialize_game(r.context);
        assert(r.app.replayPreview && r.context.config.mode.replayMode && r.settings.replayPath==original.replayPath);
        App::run(r.context);assert(Replay::recording_for_save(r.context)==contents);
        try{App::request_start_screen(r.context);assert(false);}catch(const App::StartScreenRequested&){}
        assert(!r.app.replayPreview && r.settings.gameMode==original.gameMode && r.settings.replayPath==original.replayPath);
    }
    assert(Replay::replay_path_for_loading(absolute.u8string())==absolute);
    if(actual) {
        assert(!actualContents.empty());play(*actual,true,false,10);play(*actual,true,true,10);
        assert(read(*actual)==actualContents);
    }
    Challenges::History history;history.nextRunId=2;
    history.records.push_back({1,1000,"sprint-10",{10,1},true,{}});
    assert(history.attach_replay(1,absolute));
    assert(history.records.front().replayPath==relative.generic_u8string());
    const auto historyFile=std::filesystem::path(".tetr/player.ini.challenges");
    assert(Challenges::save(history,historyFile));
    assert(read(historyFile).find(directory.generic_u8string())==std::string::npos);
    // An explicitly selected external project's replay must keep that location,
    // even when the current project has a recording with the same relative name.
    auto external=directory;external+="_external";
    const auto externalFile=external/relative;
    std::filesystem::create_directories(externalFile.parent_path());
    std::ofstream(externalFile)<<contents<<Replay::replay_hash_suffix(contents);
    auto externalHistory=history;externalHistory.records.front().replayPath=externalFile.generic_u8string();
    const auto externalHistoryFile=std::filesystem::path(".tetr/external.ini.challenges");
    assert(Challenges::save(externalHistory,externalHistoryFile));
    Challenges::History loadedExternal;assert(Challenges::load(loadedExternal,externalHistoryFile));
    const auto externalReference=std::filesystem::u8path(loadedExternal.records.front().replayPath);
    assert(!externalReference.is_absolute() && std::filesystem::absolute(externalReference).lexically_normal()==externalFile);
    play(externalFile,false,false,0);play(externalReference,true,true,0);
    // An old history still contains the previous project's absolute location.
    const auto legacyFile=std::filesystem::path(".tetr/legacy.ini.challenges");
    {std::ofstream legacy(legacyFile);legacy<<"TETR_CHALLENGES 1 2 1\n1 1000 \"sprint-10\" 10 1 1 "<<std::quoted(absolute.generic_u8string())<<'\n';}
    std::filesystem::current_path(originalDirectory);
    auto moved=directory;moved+="_moved";std::filesystem::rename(directory,moved);
    std::filesystem::current_path(moved);
    assert(!std::filesystem::exists(absolute));
    assert(Challenges::load(loadedExternal,externalHistoryFile));
    play(std::filesystem::u8path(loadedExternal.records.front().replayPath),true,true,0);
    for(const auto& file:{historyFile,legacyFile}) {
        Challenges::History loaded;assert(Challenges::load(loaded,file));
        const auto& record=loaded.records.front();assert(record.replayPath==relative.generic_u8string());
        assert(Challenges::replay_exists(record));
        play(std::filesystem::u8path(record.replayPath),true,true,0);
        assert(Challenges::save(loaded,file));assert(read(file).find(directory.generic_u8string())==std::string::npos);
    }
    std::cout.rdbuf(previous);Render::end_output_suppression();
    std::filesystem::current_path(originalDirectory);std::filesystem::remove_all(moved);std::filesystem::remove_all(external);
    std::cerr<<"challenge replay load, playback, seek, settings restoration and project relocation passed\n";
}
