#include "bot_battle_royale.h"
#include "tetr/app/application_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/placement_coordinator.h"
#include "../engine/next_preview_presentation.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/recorder.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include <chrono>
#include <stdexcept>
#include <future>
#include <thread>

namespace App {
namespace {
struct Silent {
    bool restore=!Render::output_suppressed();
    Silent(){if(restore)Render::begin_output_suppression();}
    ~Silent(){if(restore)Render::end_output_suppression();}
};
struct Plan {Piece::Type type;Piece::Pose pose;Runtime::RotationRecord rotation;};
struct Rival {
    Runtime::SessionState session;Runtime::RuntimeServices services;Runtime::GameContext context{session,services};
    ApplicationState app;Replay::ReplaySessionState replay;Input::Services input;std::unique_ptr<Autoplay::Strategy> bot;
    bool alive=true;double next=0;
    std::future<Plan> plan;
};
}
class BotBattleRoyale {
    Runtime::GameContext& human;std::vector<std::unique_ptr<Rival>> rivals;std::mt19937 random;
    std::size_t cursor=0;int remaining=0;bool pendingRank=true;
    unsigned searching=0,maxSearches=std::clamp(std::thread::hardware_concurrency(),1u,6u);
    struct Attack {int target,amount;};std::vector<Attack> pending;
    bool alive(int slot)const{return slot==0 || rivals.at(slot-1)->alive;}
    void send(int source,int amount) {
        std::vector<int> targets;for(int i=0;i<=static_cast<int>(rivals.size());++i)if(i!=source && alive(i))targets.push_back(i);
        if(amount>0 && !targets.empty())pending.push_back({targets[random()%targets.size()],amount});
    }
    void deliver() {
        auto packets=std::move(pending);pending.clear();
        for(const auto& packet:packets) {
            if(!alive(packet.target))continue;
            auto& target=packet.target==0?human:rivals[packet.target-1]->context;
            auto& p=target.session.player;
            if(packet.target==0)Engine::GarbageService::add_buffer(target,p,p.gameplay.playfield.field,packet.amount);
            else{Silent silent;Engine::GarbageService::add_buffer(target,p,p.gameplay.playfield.field,packet.amount);}
        }
    }
    void eliminate(Rival& rival) {if(rival.alive){rival.alive=false;--remaining;pendingRank=true;}}
    void begin_plan(Rival& rival) {
        Silent silent;auto& c=rival.context;auto& p=rival.session.player;auto& field=p.gameplay.playfield.field;
        Engine::GarbageService::raise_ready_buffer(c,p,field);
        if(p.pieces.bag.empty()){eliminate(rival);return;}
        Engine::prepare_next_preview(c,p);
        const auto type=Engine::current_piece(p);const auto piece=Piece::get_definition(type);const auto start=Piece::spawn_pose(field.width(),type);
        if(!piece.can_place(start,field)){eliminate(rival);return;}
        std::vector<Piece::Type> next{type};next.insert(next.end(),p.pieces.nextPreview.begin(),p.pieces.nextPreview.end());
        auto snapshot=std::make_shared<Runtime::SessionState>(rival.session);const auto config=c.config;const auto now=c.now();
        auto* bot=rival.bot.get();const auto unseen=Engine::unseen_pieces_in_preview_endpoint_bag(c,p);
        rival.plan=std::async(std::launch::async,[snapshot,config,now,bot,next,unseen,start] {
            Runtime::RuntimeServices services;Runtime::GameContext context(*snapshot,services);context.config=config;
            snapshot->clockOffset=services.clock.now()-now;
            Plan result;result.pose=start;auto& player=snapshot->player;
            result.type=bot->choose_position(context,player,player.gameplay.playfield.field,next,unseen,start,
                {Runtime::hold_available(config.field),player.gameplay.hold.piece},result.pose,result.rotation);
            return result;
        });++searching;
    }
    void step(Rival& rival,Plan plan) {
        Silent silent;auto& c=rival.context;auto& p=rival.session.player;auto& field=p.gameplay.playfield.field;
        const auto type=Engine::current_piece(p);auto piece=Piece::get_definition(type);
        const auto selected=plan.type;const auto answer=plan.pose;const auto rotation=plan.rotation;
        if(!selected.valid()){eliminate(rival);return;}
        if(selected!=type) {
            Engine::consume_current_piece(p);
            if(p.gameplay.hold.piece.valid())Engine::prepend_piece(p,p.gameplay.hold.piece);
            p.gameplay.hold.piece=type;++p.gameplay.scoring.totalHolds;
            if(p.pieces.bag.empty() || Engine::current_piece(p)!=selected){eliminate(rival);return;}
            piece=Piece::get_definition(selected);
        }
        if(!piece.can_place(answer,field)){eliminate(rival);return;}
        Engine::MovementState movement;movement.pose=answer;movement.lastRotation=rotation;
        const auto signal=Engine::finalize_locked_piece(c,field,piece,p,movement,0,[]{return Rules::PlayResult::ContinueFrame;});
        Engine::consume_current_piece(p);rival.replay.data.clear();
        if(signal!=Rules::PlayResult::Locked)eliminate(rival);
    }
public:
    BotRoyaleStatus status()const {BotRoyaleStatus s{int(rivals.size()+1),remaining,0};for(const auto& rival:rivals)s.placements+=rival->session.player.gameplay.scoring.placedBlocks;return s;}
    explicit BotBattleRoyale(Runtime::GameContext& context):human(context),random(context.services.random()) {
        remaining=Runtime::challenge_population(context.config.challengeRules);
        Silent silent;
        for(int i=1;i<remaining;++i) {
            auto rival=std::make_unique<Rival>();auto& c=rival->context;auto& p=rival->session.player;
            rival->bot=context.autoplay_component()?context.autoplay_component()->clone_for_search():nullptr;
            if(!rival->bot)throw std::runtime_error("Bot royale requires a cloneable BOT strategy");
            c.config=context.config;c.config.challengeRules=Runtime::ChallengeRules::None;
            c.config.handling={};c.config.field={};c.config.movement={};c.config.quickPlay={};c.config.rotationSystem=Runtime::RotationSystem::SrsPlus;
            c.config.autoplay.mode=Runtime::AutoplayMode::ChatGPT;c.config.field.nextCount=5;c.config.field.holdEnabled=true;c.config.field.holdCount=1;
            c.attach_app(rival->app);rival->app.primaryContext=&c;c.attach_replay(rival->replay);c.attach_input(rival->input);c.attach_autoplay(*rival->bot);
            rival->app.challengeSendAttack=[this,i](int amount){send(i,amount);};
            p.pieces.random.seed(random());p.garbage.random.seed(random());p.attackRoundingRandom.seed(random());
            p.pieces.allowedPieces=Piece::types_for_rotation_system(c.config.rotationSystem);
            auto& b=p.gameplay.playfield;b.width=c.config.field.width;b.visibleHeight=c.config.field.height;b.nextCount=5;b.holdEnabled=true;
            Engine::initialize_board(c,p);Engine::GarbageService::initialize(p,c.config.garbage.holePattern);Engine::initialize_board_timers(c,p);
            if(!rival->bot->initialize(c))throw std::runtime_error("Cannot initialize bot_chatgpt for Bot royale");
            rival->next=double(i)/remaining/std::max(.1,context.config.autoplay.pps);rivals.push_back(std::move(rival));
        }
        human.session.player.gameplay.scoring.battleRank=remaining;
    }
    void attack(int amount){send(0,amount);deliver();}
    bool frame() {
        // Search cloned simulation snapshots off the input/rendering thread.
        // Only completed locks and packet delivery mutate the live arena.
        const double elapsed=Runtime::elapsed_game_time(human.session,human.now()).seconds();
        std::size_t checked=0;int committed=0;
        while(checked++<rivals.size()) {
            auto& rival=*rivals[cursor];cursor=(cursor+1)%rivals.size();
            if(!rival.alive)continue;
            rival.session.clockOffset=rival.context.real_now()-(rival.session.gameStartTime.value_or(Runtime::TimePoint{})+Runtime::Duration::from_seconds(elapsed));
            if(rival.plan.valid()) {
                if(elapsed>=rival.next && committed<4 && rival.plan.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                    auto plan=rival.plan.get();--searching;++committed;step(rival,std::move(plan));
                    rival.next=std::max(rival.next+1/std::max(.1,human.config.autoplay.pps),elapsed+.001);deliver();
                }
            } else if(searching<maxSearches && elapsed+.15>=rival.next)begin_plan(rival);
        }
        if(pendingRank) {
            pendingRank=false;human.session.player.gameplay.scoring.battleRank=remaining;
            Replay::add_replay(human,Replay::OperationKind::BattleRank,remaining);
        }
        return remaining<=1;
    }
};
std::shared_ptr<BotBattleRoyale> start_bot_battle_royale(Runtime::GameContext& context) {
    if(context.config.mode.replayMode || context.config.mode.mode!=Runtime::GameMode::Zen ||
       !Runtime::challenge_population(context.config.challengeRules) || !context.app_component())return {};
    auto arena=std::make_shared<BotBattleRoyale>(context);
    context.app_component()->challengeFrame=[weak=std::weak_ptr<BotBattleRoyale>(arena)]{auto a=weak.lock();return a && a->frame();};
    context.app_component()->challengeSendAttack=[weak=std::weak_ptr<BotBattleRoyale>(arena)](int attack){if(auto a=weak.lock())a->attack(attack);};
    return arena;
}
BotRoyaleStatus bot_battle_status(const std::shared_ptr<BotBattleRoyale>& arena){return arena?arena->status():BotRoyaleStatus{};}
}
