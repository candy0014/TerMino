#include "tetr/app/application_state.h"
#include "tetr/app/init.h"
#include "tetr/app/game.h"
#include "tetr/app/settings_menu.h"
#include "tetr/challenges/challenges.h"
#include "tetr/config/map_store.h"
#include "tetr/config/normalization.h"
#include "tetr/config/presets.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/runtime/construction.h"
#include "tetr/runtime/piece_turn.h"
#include "tetr/input/services.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/network/network_services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/local_versus_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/session_state.h"
#include "../src/app/single_player_challenges.h"
#include "../src/app/settings_view.h"
#include "../src/app/local_versus.h"
#include "../src/app/ui_text.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using namespace Challenges;
using Id=UserConfig::SettingId;
std::filesystem::path directory;
void environment(const char* key,const std::string& value) {
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
struct Player {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings settings;Input::Services input;Replay::ReplaySessionState replay;Network::NetworkServices network;
    Player(App::ApplicationState& app,const std::string& name) {
        context.attach_app(app);context.attach_user_settings(settings);context.attach_input(input);context.attach_replay(replay);context.attach_network(network);
        settings.keyCodes=UserConfig::default_key_codes();settings.persistencePath=(directory/(name+".ini")).string();
        input.keyStateProvider=[](int){return false;};
    }
};
struct Pair {
    App::ApplicationState app;Player first{app,"first"},second{app,"second"};
    Pair(){app.primaryContext=&first.context;app.localPlayerTwoContext=&second.context;Input::share_key_state(second.input,first.input);}
    void configure(){first.context.config=preview_config(first.settings);second.context.config=preview_config(second.settings);}
};
const App::Settings::Model::SettingNode& child(const App::Settings::Model::SettingNode& node,const std::string& key) {
    for(const auto& row:node.children)if(row.key==key)return row;
    assert(false);return node;
}
void rollback_and_runtime() {
    UserConfig::Settings s;s.fieldWidth=13;s.arr=.037;s.replayPath="before.rep";
    const auto baseline=s;assert(find("sprint-40")->change(s));
    s.arr=.2;s.das=.5;s.replayPath="after.rep";s.username="changed";s.keyCodes[0]=123;
    s.forceDas=1;s.forcedDas=.01;s.mapId=99;
    auto saved=persistent_settings(s);assert(saved.fieldWidth==13 && saved.arr==baseline.arr && saved.replayPath==baseline.replayPath);
    restore_adjustment(s);assert(s.username==baseline.username && s.keyCodes==baseline.keyCodes && !s.forceDas && !s.mapId && s.das==baseline.das);
    assert(find("sprint-40")->change(s));auto config=preview_config(s);
    Result r;r.config=config;r.gameSeconds=10;r.statistics.totalLines=40;r.statistics.clearStatistics[{0,4}][0]=10;
    s.fieldWidth=99;s.autoplayMode=9;s.goal=0;
    assert(find("sprint-40")->check(config) && find("sprint-40")->val(s,r));
    r.config.field.width=11;assert(!find("sprint-40")->val(baseline,r));
    Definition forced;forced.constraints={{Id::Das,Restriction::Fixed,{.05}}};
    UserConfig::Settings handling;handling.das=.3;handling.forceDas=1;handling.forcedDas=.05;
    assert(forced.check(handling));auto actual=preview_config(handling);actual.handling.das=Runtime::Duration::from_seconds(.3);
    assert(!forced.check(actual));
}
void bot_levels_and_ownership() {
    for(bool turns:{false,true})for(int speed:turns?std::vector<int>{300,500,750,1000,1200}:std::vector<int>{700,1200,1500,2000,2500}) {
        Pair p;const auto id="local-bot-chatgpt-"+std::string(turns?"turn-":"")+std::to_string(speed);
        const auto d=lookup(id);assert(d);
        p.first.settings.fieldWidth=15;p.second.settings.fieldHeight=25;
        const auto a=p.first.settings,b=p.second.settings;
        assert(d->change(p.first.settings,&p.second.settings));
        const double pps=turns?2:speed/1000.0;
        assert(p.second.settings.autoplayMode==9 && p.second.settings.autoplayPps==pps);
        assert(p.first.settings.turnPieces==(turns?1.:0.) && p.second.settings.turnPieces==(turns?speed/1000.0:0.0));
        if(turns)assert(Runtime::completed_piece_turns(speed,p.second.settings.turnPieces)==1000);
        p.configure();assert(d->check(p.first.context.config,&p.second.context.config));
        assert(!d->check(p.second.context.config,&p.first.context.config));
        Result r;r.config=p.first.context.config;r.opponent=p.second.context.config;r.won=true;r.gameSeconds=20;
        const auto win=d->val(p.first.settings,r);assert(win && win->progress==1);
        r.won=false;const auto loss=d->val(p.first.settings,r);assert(loss && loss->progress==0 && loss->value==20);
        assert(d->better(*win,*loss) && d->better({0,30},{0,20}) && !d->better({0,10},{0,20}) && d->better({1,10},{1,20}));
        assert(App::challenge_score(*d,*loss).find("20.000")!=std::string::npos);r.won=true;
        r.opponent->autoplay.pps+=.1;assert(d->val(p.first.settings,r));
        r.opponent->autoplay.searchWidth=299;assert(!d->val(p.first.settings,r));
        r.opponent->autoplay.searchWidth=400;assert(d->val(p.first.settings,r));
        r.opponent->autoplay.pps=pps-.01;assert(!d->val(p.first.settings,r));
        r.opponent->autoplay.pps=pps;
        if(turns){r.opponent->turnPieces=speed/1000.0-.01;assert(!d->val(p.first.settings,r));r.opponent->turnPieces=speed/1000.0+.1;assert(d->val(p.first.settings,r));}
        r.opponent->movement.disableLeft=true;assert(!d->val(p.first.settings,r));
        // Both player files retain their pre-selection configuration.
        assert(UserConfig::save(p.first.settings) && UserConfig::save(p.second.settings));
        UserConfig::Settings disk;UserConfig::load(disk,p.second.settings.persistencePath);assert(disk.autoplayMode==b.autoplayMode && disk.fieldHeight==b.fieldHeight);
        p.first.settings.arr=.7;p.second.settings.arr=.8;
        restore_adjustment(p.first.settings,&p.second.settings);
        assert(p.first.settings.fieldWidth==a.fieldWidth && p.first.settings.arr==a.arr && p.second.settings.fieldHeight==b.fieldHeight && p.second.settings.arr==b.arr);
    }
    // Player 2 can own the human side; changing a menu profile after start must
    // not replace either actual participant's round configuration.
    Pair p;const auto d=lookup("local-bot-chatgpt-1500");assert(d->change(p.second.settings,&p.first.settings));p.configure();
    App::begin_single_player_challenges(p.first.context);App::begin_single_player_challenges(p.second.context);
    assert(p.app.challengeRound && p.app.playerTwoChallengeRound);
    p.second.settings.autoplayMode=9;p.first.settings.autoplayMode=0;
    p.first.session->player.timing.completedGameTime=Runtime::Duration::from_seconds(12);
    p.second.session->player.timing.completedGameTime=Runtime::Duration::from_seconds(12);
    App::finish_single_player_challenges(p.first.context,p.first.session->player,true);
    App::finish_single_player_challenges(p.second.context,p.second.session->player,true);
    assert(p.app.challengeRound->outcomes.empty() && !p.app.playerTwoChallengeRound->outcomes.empty());
    assert(std::any_of(p.app.playerTwoChallengeRound->outcomes.begin(),p.app.playerTwoChallengeRound->outcomes.end(),[&](const auto& o){return o.challengeId==d->id;}));
    const auto file=directory/"dual.rep";std::ofstream(file)<<"saved";
    assert(App::attach_challenge_replay(p.second.context,file) && !App::attach_challenge_replay(p.first.context,file));
    History history;assert(load(history,history_path(p.second.settings)) && history.best(*d) && replay_exists(*history.best(*d)));
    Pair lost;assert(d->change(lost.first.settings,&lost.second.settings));lost.configure();
    App::begin_single_player_challenges(lost.first.context);lost.first.session->player.timing.completedGameTime=Runtime::Duration::from_seconds(37);
    App::finish_single_player_challenges(lost.first.context,lost.first.session->player,false);
    assert(lost.app.challengeRound && !lost.app.challengeRound->outcomes.empty() && lost.app.challengeRound->runId);
    assert(load(history,history_path(lost.first.settings)));bool found=false;
    for(const auto& record:history.records)if(record.challengeId==d->id && record.score.progress==0 && record.score.value==37)found=true;
    assert(found);
}
void maps_and_menu() {
    std::vector<Definition> maps;
    for(int i=0;i<3;++i) {
        Runtime::MapDefinition map;map.name="Map "+std::to_string(i+1);Runtime::resize_map(map,6,6);
        map.rows.back()="GG  GG";map.goal=Runtime::MapGoal::ClearInitial;
        UserConfig::Maps::SaveUndo undo;assert(UserConfig::Maps::save(map,undo));
    }
    for(const auto& d:available_definitions())if(d.id.rfind("map-",0)==0)maps.push_back(d);assert(maps.size()==3);
    UserConfig::Settings settings;settings.persistencePath=(directory/"maps-player.ini").string();settings.fieldWidth=87;settings.rotationSystem=3;
    assert(maps[1].change(settings));const auto config=preview_config(settings);
    assert(config.field.width==6 && maps[1].check(config) && !maps[0].check(config));
    settings.fieldWidth=87;assert(maps[1].check(settings)); // The map still owns the effective width.
    Result r;r.config=config;r.goalReached=true;r.gameSeconds=4.25;
    assert(maps[1].val(UserConfig::Settings{},r));r.goalReached=false;assert(!maps[1].val(settings,r));r.goalReached=true;
    auto wrongSize=config;wrongSize.field.width+=1;assert(!maps[1].check(wrongSize));
    History h;std::uint64_t run=0;assert(h.add(settings,r,1000,run).size()==1 && save(h,history_path(settings)));
    auto tree=App::make_challenge_menu(settings);const auto& group=child(tree,"challenges/Maps");
    assert(group.children.size()==3 && group.children[0].mapId==2 && group.children[1].mapId==1 && group.children[2].mapId==3);
    assert(group.children[0].summary==App::challenge_score(maps[1],{1,4.25}) && group.children[1].summary==App::UiText::tr("No records yet"));
    // The selected item's PB appears in the active list, then disappears from
    // the ancestor list after opening that challenge.
    Pair p;p.first.settings=settings;
    App::Settings::TerminalSettingsView view(p.first.context,false,0);
    const auto surface=App::Settings::make_settings_surface();
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    view.set_menu_path(tree,{"challenges/Maps"},0);view.print_menu(group,0,"challenges / Maps");
    Test::TerminalScreen active;active.feed(output.str());assert(active.text().find("04.250")!=std::string::npos);
    output.str({});view.invalidate();
    const std::vector<std::string> path{"challenges/Maps",group.children[0].key};
    view.set_menu_path(tree,path,1);view.print_menu(group.children[0],1,"challenges / Maps / selected");
    Test::TerminalScreen inside;inside.feed(output.str());std::cout.rdbuf(previous);
    // There is still one PB on the active Score records row, with none beside
    // any of the ancestor challenge rows.
    const auto screen=inside.text();const auto pos=screen.find("04.250");assert(pos!=std::string::npos && screen.find("04.250",pos+1)==std::string::npos);
    // Editing a map never reuses scores from the previous puzzle content.
    auto changed=*maps[1].map;changed.rows.front()[0]='G';UserConfig::Maps::SaveUndo undo;assert(UserConfig::Maps::save(changed,undo));
    assert(!maps[1].check(preview_config(settings)));
    const auto newTree=App::make_challenge_menu(settings);assert(child(newTree,"challenges/Maps").children[0].mapId==1);
    UserConfig::Settings rollback;rollback.mapId=2;assert(find("sprint-40")->change(rollback));
    UserConfig::Maps::remap_selection(rollback,{{1,0},{2,1},{3,2}});restore_adjustment(rollback);assert(rollback.mapId==1);
}
void map_construction() {
    Runtime::MapDefinition map;map.name="Build target";Runtime::resize_map(map,6,6);
    map.rows[4]="IJ    ";map.rows[5]="LO    ";map.generator=Runtime::MapGenerator::CustomSequence;
    map.sequence={Piece::types_for_rotation_system(Runtime::RotationSystem::Pentomino).front()};
    map.rotationSystem=Runtime::RotationSystem::Pentomino;map.goal=Runtime::MapGoal::AllClear;map.foolFloor=10;
    UserConfig::Maps::SaveUndo undo;assert(UserConfig::Maps::save(map,undo));
    Definition d;for(const auto& candidate:available_definitions())if(candidate.id.rfind("construct-map-"+std::to_string(map.id)+"-",0)==0)d=candidate;
    assert(!d.id.empty());Pair p;auto& user=p.first.settings;user.randomPieces=6;user.mapRotationSystem=8;user.qpDefaultFloor=5;
    const auto original=user;assert(d.change(user));const auto config=preview_config(user);
    assert(d.check(config) && config.field.width==6 && config.field.height==6 && config.rotationSystem==Runtime::RotationSystem::SrsPlus);
    assert(config.bagRandomizer==Runtime::BagRandomizer::Classic && config.quickPlay.defaultFloor==5 && Runtime::active_goal(config)==Runtime::GameGoal::None);
    assert(config.initialMap && config.initialMap->sequence.empty() && config.initialMap->goal==Runtime::MapGoal::Disabled && config.initialMap->rows[4]=="GG    ");
    auto metadata=map;metadata.sequence.clear();metadata.rotationSystem=Runtime::RotationSystem::Ars;metadata.goal=Runtime::MapGoal::ClearInitial;metadata.rows[4]="GG    ";
    assert(UserConfig::Maps::save(metadata,undo));bool sameId=false;for(const auto& current:available_definitions())if(current.id==d.id)sameId=true;assert(sameId && d.check(user));
    Render::begin_output_suppression();App::initialize_game(p.first.context);
    auto& context=p.first.context;auto& player=p.first.session->player;auto& field=player.gameplay.playfield.field;
    assert(field.width()==6 && field.height()==6 && field.remaining_initial_cells()==0 && !Engine::finite_map_sequence(context));
    for(int row=0;row<6;++row)for(int col=0;col<6;++col)assert(!field.cell(row,col).occupied());
    const auto header=Replay::encode_config_prefix(context.config)+"{block_seed,42}{garbage_seed,24}}";
    Runtime::RuntimeConfig decoded;Replay::ConfigPrefixResult prefix;assert(Replay::decode_config_prefix(header,decoded,prefix));
    assert(decoded.challengeRules==Runtime::ChallengeRules::ConstructMap && decoded.rotationSystem==config.rotationSystem && decoded.initialMap->rows==config.initialMap->rows);
    unsigned seed=0;do {player.pieces.random.seed(++seed);Engine::initialize_board(context,player);}while(Piece::metadata(Engine::current_piece(player)).familyIndex!=3 && seed<100);
    assert(Piece::metadata(Engine::current_piece(player)).familyIndex==3);
    p.first.replay.data=Replay::encode_config_prefix(context.config)+"{block_seed,"+std::to_string(seed)+"}{garbage_seed,24}}";
    App::begin_single_player_challenges(context);
    auto piece=Piece::get_definition(Engine::current_piece(player));const auto cells=piece.cells(Runtime::Rotation{0});int maxRow=-100,minCol=100;
    for(const auto& cell:cells){maxRow=std::max(maxRow,cell[0]);minCol=std::min(minCol,cell[1]);}
    Engine::MovementState movement;movement.pose={{5-maxRow,-minCol},Runtime::Rotation{0}};
    Replay::add_replay(context,Replay::OperationKind::StartPlace);Replay::record_pose_event(context,Replay::OperationKind::Move,movement.pose);
    Replay::add_replay(context,Replay::OperationKind::EndMove);Replay::record_end_move_data(context,{},0,movement.pose);
    assert(Engine::finalize_locked_piece(context,field,piece,player,movement,0,[]{return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Won);
    assert(Runtime::construction_map_matches(context.config,field));
    field.set_cell(-1,0,Runtime::BoardCell::piece(0));assert(!Runtime::construction_map_matches(context.config,field));field.set_cell(-1,0,Runtime::BoardCell::empty());
    field.set_cell(4,0,Runtime::BoardCell::empty());assert(!Runtime::construction_map_matches(context.config,field));field.set_cell(4,0,Runtime::BoardCell::piece(6));
    field.set_cell(0,5,Runtime::BoardCell::piece(0));assert(!Runtime::construction_map_matches(context.config,field));field.set_cell(0,5,Runtime::BoardCell::empty());
    assert(Runtime::construction_map_matches(context.config,field));
    player.timing.completedGameTime=Runtime::Duration::from_seconds(1);App::finish_single_player_challenges(context,player,true);
    assert(p.app.challengeRound && std::any_of(p.app.challengeRound->outcomes.begin(),p.app.challengeRound->outcomes.end(),[&](const auto& outcome){return outcome.challengeId==d.id;}));
    const auto tree=App::make_challenge_menu(user);const auto& maps=child(child(tree,"challenges/Construction"),"challenges/Construction/Maps");
    assert(!maps.children.empty() && maps.children.front().challengeId==d.id);
    Replay::add_replay(context,Replay::OperationKind::EndAll);
    const auto recording=Replay::recording_for_save(context);const auto file=directory/"construction.rep";
    std::ofstream(file)<<recording<<Replay::replay_hash_suffix(recording);
    // The target is embedded; playback must still work after the local map is removed.
    assert(UserConfig::Maps::remove(map.id));p.app.replayPreview=App::ReplayPreviewRequest{file.generic_u8string(),true};
    environment("TETR_TEST_BATCH","1");environment("TETR_TEST_CLOCK_SCALE","1000");
    App::initialize_game(context);assert(context.config.mode.replayMode && !field.cell(5,0).occupied());
    App::run(context);assert(Runtime::construction_map_matches(context.config,field) && Replay::recording_for_save(context)==recording);
    auto fullTarget=std::make_shared<Runtime::MapDefinition>();Runtime::resize_map(*fullTarget,4,4);fullTarget->rows.back()="GGGG";
    context.config.mode.replayMode=false;context.config.initialMap=fullTarget;
    context.config.field.width=player.gameplay.playfield.width=4;context.config.field.height=player.gameplay.playfield.visibleHeight=4;
    Engine::initialize_board(context,player);
    auto line=Piece::get_definition(Piece::Type::from_index(0));const auto lineCells=line.cells(Runtime::Rotation{0});maxRow=-100;minCol=100;
    for(const auto& cell:lineCells){maxRow=std::max(maxRow,cell[0]);minCol=std::min(minCol,cell[1]);}
    movement.pose={{3-maxRow,-minCol},Runtime::Rotation{0}};
    assert(Engine::finalize_locked_piece(context,field,line,player,movement,0,[]{return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Won);
    assert(player.gameplay.scoring.totalLines==0 && Runtime::construction_map_matches(context.config,field));
    environment("TETR_TEST_BATCH","");environment("TETR_TEST_CLOCK_SCALE","");
    Render::end_output_suppression();
    restore_adjustment(user);assert(user.mapId==original.mapId && user.rotationSystem==original.rotationSystem && user.randomPieces==original.randomPieces && user.mapRotationSystem==original.mapRotationSystem);
}
void local_replay_and_owner_restore() {
    Pair p;auto d=lookup("local-bot-chatgpt-1200");assert(d && d->change(p.first.settings,&p.second.settings));p.configure();
    const auto stream=[](const Runtime::RuntimeConfig& config){return Replay::encode_config_prefix(config)+"{block_seed,123}{garbage_seed,456}}{end_all,0.01}";};
    const auto recording=Replay::encode_local_versus_replay(stream(p.first.context.config),stream(p.second.context.config));
    const auto file=directory/"versus.rep";std::ofstream(file)<<recording<<Replay::replay_hash_suffix(recording);
    p.app.localVersusSettingsAvailable=true;p.first.settings.replayPath="previous.rep";
    p.first.settings.challengeReplay=UserConfig::ChallengeReplayRestore{6,"previous.rep",true,d->id};
    p.first.settings.gameMode=5;p.first.settings.replayPath=file.generic_u8string();
    Render::begin_output_suppression();App::initialize_game(p.first.context);Render::end_output_suppression();
    assert(p.first.replay.localVersusReplay && p.second.replay.localVersusReplay && Replay::recording_for_save(p.first.context)==recording);
    App::restore_challenge_replay(p.first.context);assert(p.first.settings.gameMode==6 && p.first.settings.replayPath=="previous.rep");
    // Starting from Player 2's solo profile must configure the saved Player 1
    // profile, which will become the actual opponent upon entering Local Versus.
    Pair secondOwner;
    secondOwner.app.singlePlayerOwner=1;secondOwner.app.savedPlayerOneSettings=secondOwner.first.settings;
    secondOwner.app.savedPlayerOneSettings->fieldWidth=19;
    auto* partner=App::challenge_partner_settings(secondOwner.first.context);
    assert(partner==&*secondOwner.app.savedPlayerOneSettings);
    d=lookup("local-bot-chatgpt-1200");assert(d->change(secondOwner.first.settings,partner));
    secondOwner.app.localVersusSettingsAvailable=true;
    App::Settings::initialize_local_versus_player_settings(secondOwner.first.context);
    assert(secondOwner.second.settings.autoplayMode==0 && secondOwner.first.settings.autoplayMode==9 && secondOwner.first.settings.autoplayPps==1.2);
    restore_adjustment(secondOwner.second.settings,&secondOwner.first.settings);assert(secondOwner.first.settings.fieldWidth==19);
}
void human_round_has_no_challenges() {
    environment("TETR_LOCAL_VERSUS_BATCH","1");environment("TETR_TEST_CLOCK_SCALE","1000");
    assert(!lookup("local-human"));
    Pair p;p.first.settings.gameMode=p.second.settings.gameMode=6;
    p.app.localVersusSettingsAvailable=true;
    p.first.context.attach_output_mutex(p.app.terminalOutputMutex);p.second.context.attach_output_mutex(p.app.terminalOutputMutex);
    Render::begin_output_suppression();App::initialize_game(p.first.context);
    auto& board=p.second.session->player.gameplay.playfield;
    // An existing human-vs-human game can still end normally, but neither
    // participant is eligible for the single-player Bot challenges.
    for(int row=-Runtime::kHiddenRowCapacity;row<board.visibleHeight;++row)
        for(int col=0;col<board.width;++col)board.field.set_cell(row,col,Runtime::BoardCell::garbage());
    App::run_local_versus(p.first.context,p.second.context);Render::end_output_suppression();
    assert(p.app.challengeRound && p.app.playerTwoChallengeRound);
    assert(p.app.challengeRound->outcomes.empty());
    assert(p.app.playerTwoChallengeRound->outcomes.empty());
    environment("TETR_LOCAL_VERSUS_BATCH","");environment("TETR_TEST_CLOCK_SCALE","");
}
void lost_bot_round_records() {
    environment("TETR_LOCAL_VERSUS_BATCH","1");environment("TETR_TEST_CLOCK_SCALE","1000");
    Pair p;const auto d=*lookup("local-bot-chatgpt-700");assert(d.change(p.first.settings,&p.second.settings));
    Legacy::BotStrategy bot;p.second.context.attach_autoplay(bot);
    p.app.localVersusSettingsAvailable=true;
    p.first.context.attach_output_mutex(p.app.terminalOutputMutex);p.second.context.attach_output_mutex(p.app.terminalOutputMutex);
    Render::begin_output_suppression();App::initialize_game(p.first.context);
    assert(p.second.context.config.autoplay.mode==Runtime::AutoplayMode::ChatGPT && d.check(p.first.context.config,&p.second.context.config));
    auto& field=p.first.session->player.gameplay.playfield;
    for(int row=-Runtime::kHiddenRowCapacity;row<field.visibleHeight;++row)
        for(int col=0;col<field.width;++col)field.field.set_cell(row,col,Runtime::BoardCell::garbage());
    App::run_local_versus(p.first.context,p.second.context);Render::end_output_suppression();
    assert(p.app.challengeRound && p.app.challengeRound->runId && !p.app.challengeRound->outcomes.empty());
    assert(p.app.playerTwoChallengeRound && p.app.playerTwoChallengeRound->outcomes.empty());
    History history;assert(load(history,history_path(p.first.settings)));bool found=false;
    for(const auto& record:history.records)if(record.runId==p.app.challengeRound->runId && record.challengeId==d.id) {
        found=true;assert(record.score.progress==0 && record.score.value>=0 && replay_exists(record));
    }
    assert(found);
    environment("TETR_LOCAL_VERSUS_BATCH","");environment("TETR_TEST_CLOCK_SCALE","");
}
}
int main() {
    directory=std::filesystem::temp_directory_path()/("termino_challenge_modes_"+std::to_string(utc_now_milliseconds()));std::filesystem::create_directories(directory);
    const auto* old=std::getenv("TETR_MAP_FILE");const std::string previous=old?old:"";environment("TETR_MAP_FILE",(directory/"maps.txt").string());
    App::UiText::set_language(App::UiText::Language::English);
    rollback_and_runtime();bot_levels_and_ownership();maps_and_menu();map_construction();local_replay_and_owner_restore();human_round_has_no_challenges();lost_bot_round_records();
    environment("TETR_MAP_FILE",previous);std::filesystem::remove_all(directory);
    std::cerr<<"runtime challenge checks, full rollback, ChatGPT levels, per-player records, maps and versus replays passed\n";
}
