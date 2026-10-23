#include "tetr/legacy/bot_adapter.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/app/place.h"
#include "tetr/bot/bot_chatgpt.h"
#include "tetr/bot/quick_play_search.h"
#include "tetr/autoplay/availability.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/autoplay_path.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/periodic_events.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/input/services.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/rules/quick_play.h"
#include "tetr/runtime/dynamic_rules.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace TetrTests::bot_quick_play_altitude_benchmark {
namespace {
using Mode=Runtime::AutoplayMode;
using Duration=Runtime::Duration;
struct Modifier {const char* name;int UserConfig::Settings::* field;int value;};
constexpr Modifier modifiers[]{
    {"EX",&UserConfig::Settings::qpExpert,1},{"rEX",&UserConfig::Settings::qpExpert,2},
    {"NH",&UserConfig::Settings::qpNoHold,1},{"rNH",&UserConfig::Settings::qpNoHold,2},
    {"MS",&UserConfig::Settings::qpMessy,1},{"rMS",&UserConfig::Settings::qpMessy,2},
    {"GV",&UserConfig::Settings::qpGravity,1},{"rGV",&UserConfig::Settings::qpGravity,2},
    {"VL",&UserConfig::Settings::qpVolatile,1},{"rVL",&UserConfig::Settings::qpVolatile,2},
    {"DH",&UserConfig::Settings::qpDoubleHole,1},{"rDH",&UserConfig::Settings::qpDoubleHole,2},
    {"IN",&UserConfig::Settings::qpInvisible,1},{"rIN",&UserConfig::Settings::qpInvisible,2},
    {"AS",&UserConfig::Settings::qpAllSpin,1},{"rAS",&UserConfig::Settings::qpAllSpin,2},
    {"AS3",&UserConfig::Settings::qpAllSpin,3},
    {"BF",&UserConfig::Settings::qpBackfire,1},{"rBF",&UserConfig::Settings::qpBackfire,2},
    {"SE",&UserConfig::Settings::qpSnakeEyes,1},{"rSE",&UserConfig::Settings::qpSnakeEyes,2},
    {"SB",&UserConfig::Settings::qpSnowball,1},{"rSB",&UserConfig::Settings::qpSnowball,2},
    {"PN",&UserConfig::Settings::qpFool,1},{"rPN",&UserConfig::Settings::qpFool,2},
    {"F26",&UserConfig::Settings::qpFool,3},{"rF26",&UserConfig::Settings::qpFool,4}
};
constexpr Mode bots[]{Mode::While1,Mode::Candy0014,Mode::Mech,Mode::ZzztojBotris,
    Mode::ZzztojToj,Mode::ZzztojDig,Mode::ZzztojQq,Mode::ZzztojC2,Mode::ChatGPT,Mode::ColdClear};
UserConfig::Settings settings(const std::string& name) {
    UserConfig::Settings user;if(name=="BASE" || name=="NORMAL")return user;
    if(name=="FULL" || name=="NORMAL_FULL") {user.spinBonuses=int(Runtime::SpinBonuses::AllSpinPlus);return user;}
    std::istringstream stream(name);std::string part;
    while(std::getline(stream,part,'+')) {
        const auto found=std::find_if(std::begin(modifiers),std::end(modifiers),[&](const auto& m){return part==m.name;});
        if(found==std::end(modifiers))throw std::runtime_error("Unknown modifier: "+part);
        if(user.*found->field)throw std::runtime_error("Repeated modifier family: "+name);
        user.*found->field=found->value;
    }
    return user;
}
std::vector<std::string> cases(const std::string& group) {
    if(group!="singles" && group!="pairs")return {group};
    std::vector<std::string> result{"BASE"};
    for(const auto& m:modifiers)result.emplace_back(m.name);
    if(group=="pairs")for(std::size_t i=0;i<std::size(modifiers);++i)for(std::size_t j=i+1;j<std::size(modifiers);++j)
        if(modifiers[i].field!=modifiers[j].field)result.push_back(std::string(modifiers[i].name)+'+'+modifiers[j].name);
    return result;
}
struct Game {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay;
    Input::Services input;
    double time=0,peak=0;
    Game(const UserConfig::Settings& user,Mode bot,unsigned seed,int width,bool quickPlay,bool live=false) {
        context.config=Runtime::normalize_user_config(user,quickPlay?Runtime::GameMode::QuickPlay:Runtime::GameMode::Zen);
        context.config.autoplay.mode=bot;context.config.autoplay.searchWidth=width;context.config.autoplay.pps=10;
        context.attach_replay(replay);context.attach_input(input);input.keyStateProvider=[](int){return false;};
        // A controlled wall-clock model: all real Rules/Engine paths run, while
        // only presentation sleeps are skipped. Delay costs are advanced below.
        context.pause.paused=!live;context.pause.started={};
        replay.playback.rebuilding=!live;
        auto& p=session.player;auto& b=p.gameplay.playfield;
        b.width=context.config.field.width;b.visibleHeight=context.config.field.height;
        b.nextCount=context.config.field.nextCount;b.holdEnabled=context.config.field.holdEnabled;
        p.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem,false,context.config.quickPlay.defaultFloor);
        p.pieces.random.seed(seed);p.garbage.random.seed(seed^0x3819u);p.attackRoundingRandom.seed(seed^0x9e3779b9u);
        services.random.seed(seed^0x4827u);
        Engine::initialize_board(context,p);Engine::GarbageService::initialize(p,context.config.garbage.holePattern);
        Engine::initialize_quick_play(context,p);Engine::initialize_board_timers(context,p);
    }
    bool advance(double seconds) {
        auto& p=session.player;const double end=time+seconds;
        while(time+1e-9<end) {
            time=std::min(end,time+.05);context.pause.started=Runtime::TimePoint::from_seconds(time);
            if(Engine::run_frame(context,p.gameplay.playfield.field,p)!=Rules::PlayResult::ContinueFrame)return false;
            if(context.config.garbage.mode==Runtime::GarbageMode::QuickPlay && Rules::qp_gravity_increases(context.config.quickPlay) &&
                time+1e-9>=p.timing.qpGravity.seconds()+1) {
                Runtime::set_current_gravity_interval(context,Engine::advance_qp_gravity_speed(Runtime::current_gravity_interval(context),true));
                p.timing.qpGravity+=Duration::from_seconds(1);
            }
            peak=std::max(peak,session.quickPlay.altitude);
        }
        return true;
    }
};
void play_live(const std::string& name,Mode bot,int limit,unsigned seed,int width,double timeLimit,double pps) {
    auto game=std::make_unique<Game>(settings(name),bot,seed,width,name!="NORMAL" && name!="NORMAL_FULL",true);
    auto& g=*game;auto& c=g.context;auto& p=g.session.player;
    c.config.autoplay.pps=pps;
    Legacy::BotStrategy strategy;c.attach_autoplay(strategy);
    if(!strategy.initialize(c))throw std::runtime_error("Live benchmark BOT is unavailable");
    App::PieceLoopState loop;
    const auto exit=Engine::run_piece_loop(c,p,[&](Runtime::Playfield& field,Piece::Definition piece,
        Runtime::PlayerSessionState& player,bool continuing) {
        if(player.gameplay.scoring.placedBlocks>=limit ||
            Runtime::elapsed_game_time(c.session,c.now()).seconds()>=timeLimit)return Rules::PlayResult::Retry;
        const auto result=App::run_piece(c,field,piece,player,loop,continuing);
        g.peak=std::max(g.peak,c.session.quickPlay.altitude);
        return result;
    },&loop);
    const auto& s=p.gameplay.scoring;
    std::uint64_t spins=0,nonTSpins=0,zeroSpins=0;
    for(const auto& [operation,count]:s.clearStatistics) {
        const auto spin=count[1]+count[2];
        if(operation.second>0) {
            spins+=spin;
            if(Piece::metadata(Piece::Type::from_index(operation.first)).familyIndex!=5)nonTSpins+=spin;
        } else zeroSpins+=spin;
    }
    std::cerr<<std::setprecision(12)<<"{\"case\":"<<std::quoted(name)<<",\"bot\":"<<int(bot)
        <<",\"seed\":"<<seed<<",\"live\":true,\"width\":"<<width<<",\"pps_cap\":"<<pps
        <<",\"pieces\":"<<s.placedBlocks<<",\"seconds\":"<<Runtime::elapsed_game_time(c.session,c.now()).seconds()
        <<",\"spin_clears\":"<<spins<<",\"non_t_spin_clears\":"<<nonTSpins<<",\"zero_spins\":"<<zeroSpins
        <<",\"attack\":"<<s.totalAttack<<",\"lines\":"<<s.totalLines<<",\"height\":"<<g.peak
        <<",\"ended\":"<<std::quoted(exit==Engine::GameLoopExit::Dead?"dead":"limit")<<"}\n";
}
void play(const std::string& name,Mode bot,int limit,unsigned seed,int width,double timeLimit) {
    auto game=std::make_unique<Game>(settings(name),bot,seed,width,name!="NORMAL" && name!="NORMAL_FULL");auto& g=*game;
    auto& c=g.context;auto& p=g.session.player;auto& b=p.gameplay.playfield;
    const auto available=Autoplay::availability(c.config,bot);
    const auto prefix=[&] {std::cerr<<std::setprecision(12)<<"{\"case\":"<<std::quoted(name)<<",\"bot\":"<<int(bot)<<",\"seed\":"<<seed;};
    if(!available || !Bot::init(c)) {prefix();std::cerr<<",\"supported\":false,\"reason\":"<<std::quoted(available.reason)<<"}\n";return;}
    std::vector<double> timings;std::string ended="limit";int placed=0,depth=0;
    int spinClears=0,zeroSpins=0,quads=0,ordinaryClears=0,penaltyRows=0;
    int nonTSpins=0,fullSpinClears=0,spinChain=0,maxSpinChain=0,nonTZeroSpins=0;
    int evaluated=0,peakFrontier=0,retainedWidth=0;
    std::array<std::array<int,5>,7> spinDistribution{};
    for(int turn=0;turn<limit;++turn) {
        while(p.pieces.bag.size()<=std::size_t(b.nextCount+1))Engine::add_bag(c,p);
        auto piece=Piece::get_definition(Engine::current_piece(p));Engine::MovementState movement;
        movement.pose=Piece::spawn_pose(b.width,piece.type());
        if(Engine::handle_spawn_collision(c,b.field,piece,p,movement)){ended="spawn";break;}
        const auto gravity=Runtime::current_gravity_interval(c);
        if(gravity.is_instant())Engine::apply_presentation_instant_gravity(movement,[&](auto pose){return piece.can_place(pose,b.field);});
        std::vector<Piece::Type> next;for(int i=0;i<=b.nextCount;++i)next.push_back(p.pieces.bag[i].type);
        auto answer=movement.pose;Runtime::RotationRecord rotation;const auto current=piece.type();
        const auto began=std::chrono::steady_clock::now();
        const auto selected=Bot::getpos(c,p,b.field,next,Engine::unseen_pieces_in_preview_endpoint_bag(c,p),movement.pose,
            {c.config.field.holdEnabled,p.gameplay.hold.piece},answer,rotation);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();timings.push_back(ms);
        if(bot==Mode::ChatGPT) {
            const auto d=Bot_chatgpt::last_diagnostics();depth+=d.projectedDepth;
            evaluated+=d.evaluated;peakFrontier=std::max(peakFrontier,d.peakFrontier);retainedWidth=std::max(retainedWidth,d.searchWidth);
        }
        if(!selected.valid()){ended="selection";break;}
        if(selected!=current) {
            const auto old=p.gameplay.hold.piece;p.gameplay.hold.piece=current;
            Engine::consume_current_piece(p);if(old.valid())Engine::prepend_piece(p,old);
            piece=Piece::get_definition(selected);movement.pose=Piece::spawn_pose(b.width,selected);
            if(Engine::handle_spawn_collision(c,b.field,piece,p,movement)){ended="hold_spawn";break;}
            if(gravity.is_instant())Engine::apply_presentation_instant_gravity(movement,[&](auto pose){return piece.can_place(pose,b.field);});
        }
        const auto path=Engine::search_autoplay_path(b.field,piece,movement,answer,rotation,c.config.handling,c.config.movement,
            gravity,false,{},Engine::autoplay_spin_rules(c.config));
        if(!path)throw std::runtime_error("Unreachable benchmark target: "+name+" bot="+std::to_string(int(bot)));
        movement=path->finalMovement;const int oldLines=p.gameplay.scoring.totalLines;
        auto env=Bot::make_environment(c.config);
        env.quickPlayProgress=g.session.quickPlay.progress;env.quickPlayAltitude=g.session.quickPlay.altitude;
        env.quickPlayBooster=g.session.quickPlay.speed;env.quickPlayFloor=g.session.quickPlay.floor;
        env.nowSeconds=g.time;env.elapsedGameSeconds=g.time;
        const auto projection=Bot::QuickPlaySearch::forecast_placement(env,p,b.field,piece.type(),movement.pose,movement.lastRotation);
        const int lines=projection.placement.clear.clearedLines;
        const bool spin=projection.placement.spin.spin;
        spinClears+=spin && lines>0;zeroSpins+=spin && !lines;
        ordinaryClears+=!spin && lines>0;quads+=!spin && lines>=4;
        penaltyRows+=projection.application.effects.solidGarbageLines;
        const int family=Piece::metadata(piece.type()).familyIndex;
        nonTSpins+=spin && lines>0 && family!=5;nonTZeroSpins+=spin && lines==0 && family!=5;
        fullSpinClears+=spin && !projection.placement.spin.mini && lines>0;
        if(lines>0) {spinChain=spin?spinChain+1:0;maxSpinChain=std::max(maxSpinChain,spinChain);}
        if(spin && family>=0 && family<7)++spinDistribution[family][std::clamp(lines,0,4)];
        const auto outcome=Engine::finalize_locked_piece(c,b.field,piece,p,movement,0,[&]{return Engine::run_frame(c,b.field,p);});
        Engine::consume_current_piece(p);++placed;
        const bool cleared=p.gameplay.scoring.totalLines>oldLines;
        const double elapsed=std::max(.1,ms/1000)+c.config.field.entryDelay.seconds()+(cleared?c.config.field.lineClearDelay.seconds():0);
        g.peak=std::max(g.peak,g.session.quickPlay.altitude);
        if(outcome==Rules::PlayResult::Dead || !g.advance(std::min(elapsed,std::max(0.,timeLimit-g.time)))){ended="dead";break;}
        if(outcome!=Rules::PlayResult::Locked)throw std::runtime_error("Unexpected benchmark result");
        if(g.time+1e-9>=timeLimit){ended="time";break;}
        g.replay.data.clear();
    }
    double total=0;for(double t:timings)total+=t;std::sort(timings.begin(),timings.end());
    const double p95=timings.empty()?0:timings[std::min(timings.size()-1,std::size_t(.95*timings.size()))];
    const auto& s=p.gameplay.scoring;prefix();
    std::cerr<<",\"supported\":true,\"pieces\":"<<placed<<",\"limit\":"<<limit<<",\"height\":"<<g.peak
        <<",\"final_height\":"<<g.session.quickPlay.altitude<<",\"seconds\":"<<g.time<<",\"attack\":"<<s.totalAttack
        <<",\"dug\":"<<s.totalGarbageLines<<",\"lines\":"<<s.totalLines<<",\"mean_ms\":"<<total/std::max<std::size_t>(1,timings.size())
        <<",\"mean_depth\":"<<double(depth)/std::max<std::size_t>(1,timings.size())<<",\"p95_ms\":"<<p95<<",\"max_ms\":"<<(timings.empty()?0:timings.back())
        <<",\"spin_clears\":"<<spinClears<<",\"zero_spins\":"<<zeroSpins<<",\"quads\":"<<quads
        <<",\"ordinary_clears\":"<<ordinaryClears<<",\"penalty_rows\":"<<penaltyRows<<",\"spin_distribution\":[";
    for(int family=0;family<7;++family) {
        if(family)std::cerr<<',';std::cerr<<'[';
        for(int lines=0;lines<5;++lines) {if(lines)std::cerr<<',';std::cerr<<spinDistribution[family][lines];}
        std::cerr<<']';
    }
    std::cerr<<"],\"non_t_spin_clears\":"<<nonTSpins<<",\"non_t_zero_spins\":"<<nonTZeroSpins
        <<",\"full_spin_clears\":"<<fullSpinClears<<",\"max_spin_chain\":"<<maxSpinChain
        <<",\"retained_width\":"<<retainedWidth<<",\"peak_frontier\":"<<peakFrontier
        <<",\"mean_evaluated\":"<<double(evaluated)/std::max(1,placed)
        <<",\"ended\":"<<std::quoted(ended)<<"}\n";
}
}
int run(int argc,char** argv) {
    Render::begin_output_suppression();
    const int pieces=argc>1?std::stoi(argv[1]):300;const unsigned seed=argc>2?std::stoul(argv[2]):123;
    const int only=argc>3?std::stoi(argv[3]):9;const std::string group=argc>4?argv[4]:"singles";const int width=argc>5?std::stoi(argv[5]):300;
    const double timeLimit=argc>6?std::stod(argv[6]):30;
    const bool live=argc>7 && std::string(argv[7])=="--live";
    const double pps=argc>8?std::stod(argv[8]):10;
    for(const auto& name:cases(group))for(auto bot:bots)if(!only || only==int(bot)) {
        if(live)play_live(name,bot,pieces,seed,width,timeLimit,pps);
        else play(name,bot,pieces,seed,width,timeLimit);
    }
    Render::end_output_suppression();return 0;
}
}
