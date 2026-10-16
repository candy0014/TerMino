#include "tetr/challenges/challenges.h"
#include "tetr/app/application_state.h"
#include "tetr/config/normalization.h"
#include "tetr/config/presets.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/finesse.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/challenge_puzzles.h"
#include "tetr/runtime/field_metrics.h"
#include "tetr/input/services.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/audio_cues.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/challenge_prefix.h"
#include "tetr/replay/session_state.h"
#include "../src/app/challenge_replay_analysis.h"
#include "../src/app/bot_battle_royale.h"
#include "../src/app/single_player_challenges.h"
#include "../src/app/ui_text.h"
#include "../src/engine/next_preview_presentation.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
namespace {
using namespace Challenges;using Id=UserConfig::SettingId;
void intervals() {
    const auto inf=std::numeric_limits<double>::infinity();
    Constraint c{Id::NextCount,Restriction::Interval,{},1,inf,true,false};
    assert(c.accepts(1) && c.accepts(1000000000) && !c.accepts(0) && !c.accepts(inf) && !c.accepts(std::nan("")));
    assert(c.nearest(0)==UserConfig::PresetValue{1});
    c={Id::NextCount,Restriction::Interval,{},1,3,false,false};
    assert(!c.accepts(1) && c.accepts(2) && !c.accepts(3) && c.nearest(10)==UserConfig::PresetValue{2});
    c={Id::Das,Restriction::Interval,{},0,.5,false,true};
    assert(!c.accepts(0.0) && c.accepts(c.nearest(0.0)) && c.accepts(.5));
    c={Id::NextCount,Restriction::Interval,{},-inf,3,false,false};assert(c.accepts(-123) && c.nearest(5)==UserConfig::PresetValue{2});
}
void catalog_and_ranges() {
    const auto all=available_definitions();assert(all.size()>=120);std::set<std::string> ids;
    for(const auto& d:all) {
        assert(ids.insert(d.id).second);UserConfig::Settings s,opponent;
        if(!d.change(s,d.opponentConstraints.empty()?nullptr:&opponent)) {std::cerr<<"Cannot apply "<<d.id<<'\n';assert(false);}
        assert(d.check(s,d.opponentConstraints.empty()?nullptr:&opponent));
        if(std::find(d.categories.begin(),d.categories.end(),"Dig")!=d.categories.end()) {
            assert(s.cheeseMessiness==1.0);s.cheeseMessiness=.8;assert(!d.check(s));s.cheeseMessiness=1;
        }
        if(d.id=="audio-200" || d.id.find("bot-royale-")==0) {
            assert(d.testing);const auto name=App::challenge_name(d);
            assert(name.find("【测试】")!=std::string::npos || name.find("[TEST]")!=std::string::npos);
        }else assert(!d.testing);
        const auto runtime=preview_config(s);const auto encoded=Replay::encode_config_prefix(runtime)+"{block_seed,42}{garbage_seed,24}}";
        Runtime::RuntimeConfig decoded;Replay::ConfigPrefixResult prefix;assert(Replay::decode_config_prefix(encoded,decoded,prefix));
        assert(decoded.challengeRules==runtime.challengeRules && Runtime::effective_bag_randomizer(decoded)==Runtime::effective_bag_randomizer(runtime));
        restore_adjustment(s,&opponent);assert(!s.challengeAdjustment && s.challengeRules==0);
    }
    auto sprint=*find("sprint-40");UserConfig::Settings s;sprint.change(s);
    s.qpNoHold=1;s.nextCount=0;s.ghost=0;s.disableLeft=1;assert(sprint.check(s));
    s.gravityInterval=1.001;assert(!sprint.check(s));s.gravityInterval=1;assert(sprint.check(s));
    s.nextCount=6;assert(!sprint.check(s));
    auto efficiency=*lookup("efficiency-40");s={};assert(efficiency.change(s));s.gravityInterval=1e9+5;assert(efficiency.check(s));
    auto master=*lookup("master-20g-1");s={};assert(master.change(s));s.nextCount=0;s.lockDelay=.1;assert(master.check(s));s.lockDelay=.6;assert(!master.check(s));
    s.challengeRules=10;
    assert(Runtime::normalize_user_config(s,Runtime::GameMode::Multiplayer).challengeRules==Runtime::ChallengeRules::None);
    assert(Runtime::normalize_user_config(s,Runtime::GameMode::LocalVersus).challengeRules==Runtime::ChallengeRules::None);
}
void classic_handling_rules() {
    constexpr double hz=60.0988;
    for(int tier=0;tier<4;++tier) {
        const auto d=*lookup("classic-200-"+std::to_string(tier));UserConfig::Settings s;
        assert(d.change(s));const auto config=preview_config(s);assert(d.check(config));
        const std::vector<std::pair<Id,double>> timings{
            {Id::Das,16/hz},{Id::Arr,6/hz},{Id::SoftDropFactor,2/hz},
            {Id::EntryDelay,10/hz},{Id::LineClearDelay,17/hz},
            {Id::GravityInterval,std::vector<double>{.8,.3,.1,.03}[tier]},
            {Id::LockDelay,std::vector<double>{.5,.3,.15,.08}[tier]}};
        for(const auto& [id,value]:timings) {
            assert(runtime_value(config,id)==std::optional<UserConfig::PresetValue>{value});
            // Both faster and slower timing are outside the fixed standard.
            for(double factor:{.5,2.0}) {
                auto edited=s;UserConfig::Presets::write_value(edited,id,value*factor);assert(!d.check(edited));
            }
        }
        assert(config.rotationSystem==Runtime::RotationSystem::Nrs && Runtime::effective_bag_randomizer(config)==Runtime::BagRandomizer::Classic);
        assert(config.handling.softDropFixed && config.handling.dcd==Runtime::Duration{} && !config.handling.preMove && !config.handling.dasPrecharge);
        assert(config.movement.disableHardDrop && config.movement.disable180 && !config.handling.infiniteMovement);
        assert(config.handling.lockResetLimit==(tier==3?0:5));
        for(const auto& [id,wrong]:std::vector<std::pair<Id,int>>{
            {Id::SoftDropFixed,0},{Id::PreMove,1},{Id::DasPrecharge,1},{Id::DisableHardDrop,0},
            {Id::Disable180,0},{Id::InstantGravity,1},{Id::LockResetLimit,tier==3?1:0},{Id::InfiniteMovement,1}}) {
            auto edited=s;UserConfig::Presets::write_value(edited,id,wrong);assert(!d.check(edited));
        }
        auto dcd=s;dcd.dcd=.01;assert(!d.check(dcd));
        auto fewerNext=s;fewerNext.nextCount=0;assert(d.check(fewerNext));
        // Active mechanism overrides must be corrected without losing personal
        // handling; the SDF unit flag belongs to that same override.
        UserConfig::Settings forced;forced.instantGravity=1;forced.gravityInterval=2;
        forced.das=.07;forced.arr=.003;forced.softDropFactor=15;forced.softDropFixed=0;
        forced.forceSdf=forced.forceDas=forced.forceArr=forced.forceDcd=forced.forcePreMove=forced.forceDasPrecharge=1;
        forced.forcedSdf=0;forced.forcedSdfFixed=0;forced.forcedDas=forced.forcedArr=0;forced.forcedDcd=.1;
        const auto original=forced;assert(d.change(forced));const auto actual=preview_config(forced);
        for(const auto& [id,value]:timings)assert(runtime_value(actual,id)==runtime_value(config,id));
        assert(!forced.instantGravity && forced.forcedSdfFixed==1 && actual.handling.softDropFixed);
        assert(forced.das==original.das && forced.arr==original.arr && forced.softDropFactor==original.softDropFactor && forced.softDropFixed==original.softDropFixed);
        auto bypass=forced;bypass.forcedDas=0;assert(!d.check(bypass));
        restore_adjustment(forced);
        for(const auto id:{Id::Das,Id::Arr,Id::SoftDropFactor,Id::SoftDropFixed,Id::InstantGravity,Id::GravityInterval,
            Id::ForcedSdf,Id::ForcedSdfFixed,Id::ForcedDas,Id::ForcedArr,Id::ForcedDcd,Id::ForcedPreMove,Id::ForcedDasPrecharge})
            assert(UserConfig::Presets::read_value(forced,id)==UserConfig::Presets::read_value(original,id));
        Result result;result.config=config;result.gameSeconds=100;
        result.statistics.totalLines=200;result.statistics.placedBlocks=500;result.statistics.score=12345;
        result.statistics.clearStatistics[{0,4}][0]=50;
        assert(d.val(s,result));result.config.handling.das=Runtime::Duration{};assert(!d.val(s,result));
    }
}
struct Rig {
    std::unique_ptr<Runtime::SessionState> owned=std::make_unique<Runtime::SessionState>();
    Runtime::SessionState& session=*owned;Runtime::RuntimeServices services;Runtime::GameContext c{session,services};
    App::ApplicationState app;UserConfig::Settings user;Input::Services input;Replay::ReplaySessionState replay;
    explicit Rig(const Definition& d) {
        c.attach_app(app);app.primaryContext=&c;c.attach_user_settings(user);c.attach_input(input);c.attach_replay(replay);
        assert(d.change(user));c.config=preview_config(user);auto& p=session.player;auto& b=p.gameplay.playfield;
        b.width=c.config.field.width;b.visibleHeight=c.config.field.height;b.nextCount=c.config.field.nextCount;b.holdEnabled=c.config.field.holdEnabled;
        p.pieces.allowedPieces=Piece::types_for_rotation_system(c.config.rotationSystem);p.pieces.random.seed(42);p.garbage.random.seed(24);
        Engine::initialize_board(c,p);Engine::GarbageService::initialize(p,c.config.garbage.holePattern);Engine::initialize_board_timers(c,p);
        replay.data=Replay::encode_config_prefix(c.config)+"{block_seed,42}{garbage_seed,24}}";
    }
    Rules::PlayResult lock(Piece::Type type,Piece::Pose pose,int inputs=-1) {
        auto piece=Piece::get_definition(type);Engine::MovementState movement;movement.pose=pose;movement.inputActions=inputs;
        Replay::add_replay(c,Replay::OperationKind::StartPlace);Replay::record_end_move_data(c,{},session.player.gameplay.scoring.score,pose,inputs);
        return Engine::finalize_locked_piece(c,session.player.gameplay.playfield.field,piece,session.player,movement,0,[]{return Rules::PlayResult::ContinueFrame;});
    }
    Piece::Pose floor(Piece::Type type,int rotation=0,int col=3) {
        auto piece=Piece::get_definition(type);auto pose=Piece::spawn_pose(c.config.field.width,type);pose.rotation=Runtime::Rotation(rotation);pose.position.col=col;
        assert(piece.can_place(pose,session.player.gameplay.playfield.field));while(piece.can_place(pose.translated(1,0),session.player.gameplay.playfield.field))++pose.position.row;return pose;
    }
};
void digging_finish() {
    Rig dig(*find("dig-10"));auto& p=dig.session.player;auto& b=p.gameplay.playfield;
    Engine::GarbageService::add_garbage(dig.c,p,b.field,10);
    assert(b.garbageHeight==10);
    // A straight well lets real consecutive locks clear the final ten rows.
    b.field.clear();
    for(int row=10;row<20;++row)for(int col=1;col<10;++col)b.field.set_cell(row,col,Runtime::BoardCell::garbage());
    const auto type=Piece::Type::from_index(0);const auto piece=Piece::get_definition(type);
    const auto cells=piece.cells(Runtime::Rotation{1});int column=100;
    for(const auto& cell:cells)column=std::min(column,cell[1]);
    for(int expected:{4,8,10}) {
        assert(dig.lock(type,dig.floor(type,1,-column))==Rules::PlayResult::Locked);
        assert(p.gameplay.scoring.totalGarbageLines==expected && b.garbageHeight==10-expected);
    }
    Engine::GarbageService::add_garbage(dig.c,p,b.field,10);assert(b.garbageHeight==0);
    // Partial replenishment: 32 cleared + 6 on board only needs two more.
    p.gameplay.scoring.totalGarbageLines=32;b.garbageHeight=6;dig.c.config.mode.garbageTarget=40;
    Engine::GarbageService::add_garbage(dig.c,p,b.field,4);assert(b.garbageHeight==8);
    Engine::GarbageService::add_garbage(dig.c,p,b.field,4);assert(b.garbageHeight==8);
    // Initial generation also honors a goal below the configured layer height.
    b.field.clear();b.garbageHeight=0;p.gameplay.scoring.totalGarbageLines=0;dig.c.config.mode.garbageTarget=5;
    Engine::GarbageService::add_garbage(dig.c,p,b.field,10);assert(b.garbageHeight==5);
    // Old recordings apply their exact historical garbage events unchanged.
    dig.c.config.mode.replayMode=true;
    Engine::GarbageService::add_garbage(dig.c,p,b.field,3);assert(b.garbageHeight==8);
    dig.c.config.mode.replayMode=false;dig.c.config.mode.goal=Runtime::GameGoal::None;
    Engine::GarbageService::add_garbage(dig.c,p,b.field,2);assert(b.garbageHeight==10);
}
void placement_rules() {
    for(const auto& id:{"research-1","research-plus-1","pure-quads","pure-quads-extreme","tsd-20-0","tsd-20-1","tsd-20-2","singles-marathon-200","pc-consecutive","c4w-2","symmetry-40"}) {
        auto d=*lookup(id);UserConfig::Settings s;assert(d.change(s));Result initial;initial.config=preview_config(s);
        PrefixTracker tracker({d},s,initial,d.id);assert(tracker.find(d.id));
        Placement p;p.pieceType=5;p.lines=2;p.spin=true;p.column=4;
        if(d.id=="research-1")p.b2bBroken=true;
        if(d.id=="research-plus-1"){p.spin=false;p.lines=4;}
        if(d.id.find("pure-quads")!=std::string::npos)p.lines=1;
        if(d.id=="tsd-20-0")p.mini=true;
        if(d.id=="pc-consecutive")initial.statistics.stackHeight=5;
        if(d.id=="symmetry-40")p.symmetric=false;
        if(d.id=="c4w-2" || d.id=="singles-marathon-200"){p.lines=1;p.spin=false;assert(!tracker.observe(p,initial));p.lines=d.id=="c4w-2"?0:1;}
        if(d.id=="tsd-20-1"){for(int i=0;i<3;++i)assert(!tracker.observe(p,initial));}
        if(d.id=="tsd-20-2")assert(!tracker.observe(p,initial));
        assert(tracker.observe(p,initial));
    }
}
void puzzles_and_stack() {
    Rig columns(*find("sprint-40"));std::vector<int> physicalColumns;
    columns.app.challengePlacementObserver=[&](const Placement& p,auto&){physicalColumns.push_back(p.column);return false;};
    assert(columns.lock(Piece::Type::from_index(0),columns.floor(Piece::Type::from_index(0),1,3))==Rules::PlayResult::Locked);
    columns.session.player.gameplay.playfield.field.clear();
    assert(columns.lock(Piece::Type::from_index(0),columns.floor(Piece::Type::from_index(0),3,4))==Rules::PlayResult::Locked);
    assert(physicalColumns==std::vector<int>({4,4}));
    Rig series(*lookup(Runtime::challenge_training_id(true)));
    for(int stage=0;stage<3;++stage) {
        const auto map=Runtime::challenge_puzzle(stage);const auto type=Engine::current_piece(series.session.player);
        assert(type==map.sequence.front());assert(series.lock(type,series.floor(type,stage==2?2:0,stage==0?2:stage==1?4:5))==Rules::PlayResult::Locked);
        Engine::consume_current_piece(series.session.player);
        if(series.session.player.gameplay.scoring.allClearCount!=std::uint64_t(stage+1))std::cerr<<"PC stage "<<stage<<" lines "<<series.session.player.gameplay.scoring.totalLines<<" PC "<<series.session.player.gameplay.scoring.allClearCount<<'\n';
        assert(series.session.player.gameplay.scoring.allClearCount==std::uint64_t(stage+1));
    }
    Replay::add_replay(series.c,Replay::OperationKind::EndAll);
    auto seriesRule=*lookup(Runtime::challenge_training_id(true));seriesRule.stopRule=[] {return [](const Placement&,const Result&,const Result& r){return r.statistics.placedBlocks>=3;};};
    const auto seriesPrefix=App::analyze_challenge_replay(series.replay.data,seriesRule);assert(seriesPrefix && seriesPrefix->placements==2);
    Rig stack(*lookup("stack-100-v1"));auto& field=stack.session.player.gameplay.playfield.field;
    const auto type=Piece::Type::from_index(0);auto piece=Piece::get_definition(type);
    for(int round=0;round<4;++round) {
        const int row=19-round;for(int x=0;x<10;++x)field.set_cell(row,x,Runtime::BoardCell::piece(3));
        auto pose=stack.floor(type);int maxRow=-100;for(auto d:piece.cells(pose.rotation))maxRow=std::max(maxRow,d[0]);pose.position.row=row-maxRow;
        for(auto d:piece.cells(pose.rotation))field.set_cell(pose.position.row+d[0],pose.position.col+d[1],Runtime::BoardCell::empty());
        assert(piece.can_place(pose,field));assert(stack.lock(type,pose)==Rules::PlayResult::Locked);
        assert(stack.session.player.gameplay.scoring.totalLines==(round==3?4:0));
        assert(stack.session.player.gameplay.scoring.allClearCount==std::uint64_t(round==3?1:0));
    }
    Rig master(*lookup("stage-master-v1"));auto& task=master.session.player.gameplay.challenge;auto& board=master.session.player.gameplay.playfield.field;
    task.stage=1;task.stageQuads=1;
    const auto vertical=Piece::Type::from_index(0);auto shape=Piece::get_definition(vertical);auto pose=master.floor(vertical,1,3);
    for(int y=16;y<20;++y)for(int x=0;x<10;++x)board.set_cell(y,x,Runtime::BoardCell::piece(3));
    for(const auto& d:shape.cells(pose.rotation))board.set_cell(pose.position.row+d[0],pose.position.col+d[1],Runtime::BoardCell::empty());
    assert(master.lock(vertical,pose)==Rules::PlayResult::Locked && task.stage==2 && master.session.player.gameplay.scoring.challengeStages==2);
    Engine::initialize_board(master.c,master.session.player);assert(task.stage==0);
}
void replay_analysis_and_finesse() {
    auto d=*find("sprint-40");d.id="test-first-lock";d.stopRule=[] {return [](const Placement&,const Result&,const Result& r){return r.statistics.placedBlocks>=2;};};
    for(bool legacy:{false,true}) {
    Rig rig(d);
    if(legacy) {
        rig.c.config.legacyRotationRules=true;rig.c.config.legacyRandomizerRules=true;
        rig.session.player.pieces.allowedPieces=Piece::types_for_rotation_system(rig.c.config.rotationSystem,true);
        rig.session.player.pieces.random.seed(42);rig.session.player.garbage.random.seed(24);Engine::initialize_board(rig.c,rig.session.player);
        rig.replay.data=Replay::encode_config_prefix(rig.c.config)+"{block_seed,42}{garbage_seed,24}}";
        const std::string marker="{RotationRulesVersion,0}";const auto at=rig.replay.data.find(marker);assert(at!=std::string::npos);rig.replay.data.erase(at,marker.size());
    }
    for(int i=0;i<2;++i) {Engine::prepare_next_preview(rig.c,rig.session.player);const auto type=Engine::current_piece(rig.session.player);
        assert(rig.lock(type,rig.floor(type))==Rules::PlayResult::Locked);Engine::consume_current_piece(rig.session.player);}
    Replay::add_replay(rig.c,Replay::OperationKind::EndAll);
    const auto boundary=App::analyze_challenge_replay(rig.replay.data,d);assert(boundary && boundary->placements==1);
    const auto prepared=Replay::prepare_challenge_replay(rig.replay.data,d.id,d.ruleVersion,true,[&](const auto& data){return App::analyze_challenge_replay(data,d);});
    assert(prepared.status==Replay::PrefixPreparationStatus::Ready && prepared.boundary->placements==1);
    }
    Rig finesse(*lookup("finesse-sprint-40-v1"));const auto type=Engine::current_piece(finesse.session.player);auto pose=Piece::spawn_pose(10,type);auto piece=Piece::get_definition(type);
    while(piece.can_place(pose.translated(1,0),finesse.session.player.gameplay.playfield.field))++pose.position.row;
    const int minimal=Engine::minimal_input_count(finesse.session.player.gameplay.playfield.field,piece,pose,{});assert(minimal==1);
    bool fault=false;finesse.app.challengePlacementObserver=[&](const Placement& p,auto&){fault=p.finesseFault;return fault;};
    assert(finesse.lock(type,pose,2)==Rules::PlayResult::Dead && fault);
    Replay::add_replay(finesse.c,Replay::OperationKind::EndAll);
    const auto clipped=App::analyze_challenge_replay(finesse.replay.data,*lookup("finesse-sprint-40-v1"));assert(clipped && clipped->placements==0);
}
void scores_and_history() {
    UserConfig::Settings settings;auto efficiency=*lookup("efficiency-40");assert(efficiency.change(settings));
    Result r;r.config=preview_config(settings);r.gameSeconds=30;r.statistics.totalLines=44;r.statistics.totalAttack=110;r.statistics.placedBlocks=50;r.statistics.clearStatistics[{0,4}][0]=11;
    auto score=efficiency.val(settings,r);assert(score && score->progress==40 && score->value==2.5 && score->tie==30);
    assert(efficiency.better({40,3,60},{40,2,10}) && efficiency.better({40,2.5,20},*score));
    r.statistics.totalLines=36;r.statistics.clearStatistics[{0,4}][0]=9;assert(!efficiency.val(settings,r));
    auto dig=*lookup("dig-efficiency-10");assert(dig.change(settings));r={};r.config=preview_config(settings);r.gameSeconds=99;r.statistics.totalLines=12;r.statistics.totalGarbageLines=12;r.statistics.placedBlocks=10;r.statistics.clearStatistics[{0,4}][0]=3;
    score=dig.val(settings,r);assert(score && score->value==10 && dig.better({10,9,100},*score) && dig.better({10,10,98},*score));
    auto strict=*lookup("c4w-2");assert(strict.change(settings));r={};r.config=preview_config(settings);r.statistics.maxCombo=7;r.gameSeconds=8;score=strict.val(settings,r);assert(score && score->progress==7);
    auto secret=*lookup("construct-secret");assert(secret.change(settings));r={};r.config=preview_config(settings);r.statistics.placedBlocks=100;r.statistics.secretGrade=3;r.statistics.secretGradePieces=12;r.statistics.secretGradeTime=Runtime::Duration::from_seconds(20);r.gameSeconds=200;
    score=secret.val(settings,r);assert(score && score->progress==3 && score->value==12 && score->tie==20);
    const auto path=std::filesystem::temp_directory_path()/("termino_catalog_history_"+std::to_string(utc_now_milliseconds()));
    std::ofstream(path)<<"TETR_CHALLENGES 1 2 1\n1 1 \"sprint-40\" 40 30 1 \"\"\n";
    History history;assert(load(history,path) && history.records.front().score.tie==0);
    history.nextRunId=3;history.records.push_back({2,2,efficiency.id,{40,2.5,20},true,{}});
    assert(save(history,path));History roundTrip;assert(load(roundTrip,path) && roundTrip.best(efficiency)->score.tie==20);std::filesystem::remove(path);
    Runtime::Playfield field;field.initialize(10,20);
    field.set_cell(19,0,Runtime::BoardCell::piece(Piece::metadata(Piece::Type::from_index(1)).colorId));
    field.set_cell(19,9,Runtime::BoardCell::piece(Piece::metadata(Piece::Type::from_index(2)).colorId));
    assert(Runtime::field_metrics(field,Runtime::RotationSystem::SrsPlus).symmetric);
    field.set_cell(19,9,Runtime::BoardCell::piece(Piece::metadata(Piece::Type::from_index(1)).colorId));assert(!Runtime::field_metrics(field,Runtime::RotationSystem::SrsPlus).symmetric);
}
void drought_and_display() {
    Rig drought(*lookup("drought-100"));for(int bag=0;bag<50;++bag){for(const auto& p:drought.session.player.pieces.bag)assert(Piece::metadata(p.type).familyIndex!=0);drought.session.player.pieces.bag.clear();Engine::add_bag(drought.c,drought.session.player);assert(drought.session.player.pieces.bag.size()==6);}
    Rig adverse(*lookup("drought-plus-100"));adverse.session.player.pieces.bag.clear();Engine::add_bag(adverse.c,adverse.session.player);assert(adverse.session.player.pieces.bag.size()==1);
    Rig blind(*lookup("blind-200"));assert(!Render::active_piece_visible(blind.c,blind.session.player));
    auto blindConfig=blind.c.config;blindConfig.display.showSpawnPosition=true;assert(!lookup("blind-200")->check(blindConfig));
    Rig instinct(*lookup("instinct-200"));instinct.session.player.presentation.activePieceBorn=Runtime::Duration::from_seconds(0);instinct.session.player.timing.completedGameTime=Runtime::Duration::from_seconds(.19);assert(Render::active_piece_visible(instinct.c,instinct.session.player));instinct.session.player.timing.completedGameTime=Runtime::Duration::from_seconds(.21);assert(!Render::active_piece_visible(instinct.c,instinct.session.player));
    assert(Render::cue_frequencies(Render::AudioCue::Piece,0)!=Render::cue_frequencies(Render::AudioCue::Piece,1));
    for(int population:{49,99}) {
        Rig royale(*lookup("bot-royale-"+std::to_string(population)+"-v1"));Legacy::BotStrategy strategy;royale.c.attach_autoplay(strategy);
        auto arena=App::start_bot_battle_royale(royale.c);assert(arena && royale.session.player.gameplay.scoring.battleRank==population);
        assert(royale.app.challengeFrame && royale.app.challengeSendAttack);royale.app.challengeSendAttack(4);
        royale.session.gameStartTime=royale.c.now()-Runtime::Duration::from_seconds(.5);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(App::bot_battle_status(arena).placements<6 && std::chrono::steady_clock::now()<deadline){assert(!royale.app.challengeFrame());std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        const auto status=App::bot_battle_status(arena);
        assert(status.population==population && status.alive==population && status.placements>=6);
    }
}
}
int main(int argc,char** argv){
    if(argc>1 && std::string(argv[1])=="--catalog") {
        for(const auto& d:available_definitions()) {for(const auto& category:d.categories)std::cout<<category<<" / ";std::cout<<'\t'<<d.id<<'\t'<<d.name<<(d.testing?"【测试】":"")<<'\n';}return 0;
    }
    Render::begin_output_suppression();intervals();catalog_and_ranges();classic_handling_rules();digging_finish();placement_rules();puzzles_and_stack();replay_analysis_and_finesse();scores_and_history();drought_and_display();Render::end_output_suppression();std::cerr<<"Full challenge catalog, interval checks, real settlement, legacy replay analysis and new mechanics passed\n";
}
