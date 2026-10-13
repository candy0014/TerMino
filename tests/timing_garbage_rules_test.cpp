#include "tetr/config/normalization.h"
#include "tetr/engine/entry_delay.h"
#include "tetr/engine/movement_controller.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/rules/attack.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/rules/placement_resolution.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/tetrio_random.h"
#include "../src/app/settings_model.h"
#include "../src/app/ui_text.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <string>

namespace {
using Runtime::Duration;
using Runtime::TimePoint;
using Rules::PlayResult;
const App::Settings::Model::SettingNode* find_node(const App::Settings::Model::SettingNode& root,const std::string& key) {
    if(root.key==key) return &root;
    for(const auto& child:root.children) if(const auto* found=find_node(child,key)) return found;
    return nullptr;
}
const App::Settings::Model::SettingItem* find_item(const App::Settings::Model::SettingNode& root,UserConfig::SettingId id) {
    if(root.item && root.item->id==id) return &*root.item;
    for(const auto& child:root.children) if(const auto* found=find_item(child,id)) return found;
    return nullptr;
}
void settings_and_replay() {
    UserConfig::Settings user;
    assert(!user.infiniteMovement && user.entryDelay==0 && user.garbageCap==8 && user.openerPhase==14);
    user.infiniteMovement=1; user.entryDelay=.125; user.cancelMultiplier=1.75; user.hitMultiplier=.123456789;
    user.messinessWithinAttack=.4; user.avoidSameColumn=1; user.garbageCap=3; user.openerPhase=7;
    const auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Multiplayer);
    assert(Runtime::effective_lock_reset_limit(config.handling)==-1);
    assert(config.field.entryDelay.seconds()==.125 && config.garbage.cancelMultiplier==1.75);
    assert(config.garbage.hitMultiplier==user.hitMultiplier && config.garbage.sendMultiplier==1);
    assert(config.garbage.singleMessiness==.4 && config.garbage.avoidSameColumn);
    assert(config.garbage.cap==3 && config.garbage.openerPhase==7 && !config.garbage.legacyOpenerPhase);
    auto tree=App::Settings::Model::make_tree(user,false,0);
    assert(!find_item(tree,UserConfig::SettingId::LockResetLimit));
    const auto* garbage=find_node(tree,"garbage-rules"); assert(garbage);
    int fields=0; for(const auto& child:garbage->children) fields+=child.item.has_value(); assert(fields==8); // Seven shared controls and opener protection.
    using Id=UserConfig::SettingId;
    for(const auto id:{Id::InfiniteMovement,Id::EntryDelay,Id::CancellationMultiplier,
        Id::MessinessWithinAttack,Id::AvoidSameColumn,Id::GarbageCap,Id::OpenerPhase}) {
        const auto* item=find_item(tree,id); assert(item && !item->hint.empty());
        assert(UserConfig::setting_descriptor(id).section==UserConfig::Section::Game);
    }
    Runtime::RuntimeConfig copy;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+'}',copy,prefix));
    assert(copy.handling.infiniteMovement && copy.field.entryDelay==config.field.entryDelay);
    assert(copy.garbage.cancelMultiplier==1.75 && copy.garbage.hitMultiplier==user.hitMultiplier);
    assert(copy.garbage.sendMultiplier==1 && std::abs(copy.garbage.singleMessiness-.4)<1e-12);
    assert(!copy.garbage.legacyIncomingRounding);
    assert(copy.garbage.avoidSameColumn && copy.garbage.cap==3 && copy.garbage.openerPhase==7 && !copy.garbage.legacyOpenerPhase);
    assert(Replay::decode_config_prefix("{{Model,0}}",copy,prefix));
    assert(!copy.handling.infiniteMovement && copy.field.entryDelay==Duration{});
    assert(copy.garbage.sendMultiplier==1 && copy.garbage.singleMessiness==0 && !copy.garbage.avoidSameColumn);
    assert(copy.garbage.cancelMultiplier==1 && copy.garbage.hitMultiplier==1 && copy.garbage.legacyIncomingRounding);
    assert(copy.garbage.cap==8 && copy.garbage.openerPhase==14 && copy.garbage.legacyOpenerPhase);
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(copy)+'}',copy,prefix));
    assert(copy.garbage.legacyOpenerPhase); // Transcoding an old recording retains old cancellation.
    assert(copy.garbage.legacyIncomingRounding);
    assert(Replay::decode_config_prefix("{{GarbageMultiple,0.5}{SendGarbageMultiplier,1.75}}",copy,prefix));
    assert(copy.garbage.hitMultiplier==.5 && std::abs(copy.garbage.sendMultiplier-1.75)<1e-12 && copy.garbage.cancelMultiplier==1);
    assert(copy.garbage.legacyIncomingRounding);
    assert(Replay::decode_operation_fields({"offset_buffer","0","9","4"}).payload_as<Replay::OffsetBufferPayload>().perfectClearAttack==0);
    assert(Replay::decode_operation_fields({"offset_buffer","0","9","4","5"}).payload_as<Replay::OffsetBufferPayload>().perfectClearAttack==5);
    user.messinessWithinAttack=3; user.cancelMultiplier=-1; user.hitMultiplier=-1; user.garbageCap=-1; user.openerPhase=-1; user.entryDelay=-1;
    const auto clamped=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    assert(clamped.garbage.singleMessiness==1 && clamped.garbage.cancelMultiplier==0 && clamped.garbage.hitMultiplier==0);
    assert(clamped.garbage.cap==0 && clamped.garbage.openerPhase==0 && clamped.field.entryDelay==Duration{});
}
void infinite_movement() {
    for(const bool unlimited:{false,true}) {
        Runtime::HandlingConfig handling; handling.infiniteMovement=unlimited;
        Engine::MovementState state; state.pose={{19,4},Runtime::Rotation{}};
        Engine::MovementController movement(state,[](Piece::Pose p){return p.position.row<=19;},{});
        for(int i=1;i<=100;++i) assert(movement.moveHorizontal(i%2 ? -1 : 1,TimePoint::from_seconds(i),Runtime::effective_lock_reset_limit(handling)).changed);
        assert(state.lockResetCount==100 && state.lockTimer.seconds()==(unlimited ? 100 : 15));
        assert(movement.shouldLock(TimePoint::from_seconds(100.1),Duration::from_seconds(.5),handling.gravityInterval,false)==!unlimited);
        assert(movement.shouldLock(TimePoint::from_seconds(101),Duration::from_seconds(.5),handling.gravityInterval,false));
        if(unlimited) {
            state.lockResetCount=std::numeric_limits<int>::max();
            assert(movement.moveHorizontal(1,TimePoint::from_seconds(102),-1).changed);
            assert(state.lockTimer.seconds()==102 && state.lockResetCount==std::numeric_limits<int>::max());
        }
    }
}

void fractional_cancellation() {
    for (const bool legacy:{false,true}) {
        Runtime::GarbageCancellationState carry;
        std::deque<Rules::GarbagePacket> queue{{3,TimePoint{},false}};
        Rules::GarbageCancelInput input{4,20,true,0,0,legacy,0,2.0};
        assert(Rules::cancel_incoming_garbage(queue,input,&carry).outgoingAttack==2);
        assert(queue.empty() && carry.outgoingFraction==.5);
        queue.push_back({1,TimePoint{},false});
        input.attack=1;
        assert(Rules::cancel_incoming_garbage(queue,input,&carry).outgoingAttack==1);
        assert(queue.empty() && carry.outgoingFraction==0);

        queue.push_back({2,TimePoint{},false}); input.cancellationMultiplier=.5;
        for (int i=0;i<4;++i) {
            assert(Rules::cancel_incoming_garbage(queue,input,&carry).outgoingAttack==0);
            assert(Rules::pending_garbage(queue)==2-(i+1)/2);
        }
        assert(carry.cancellationFraction==0);

        queue.push_back({10,TimePoint{},false}); input.cancellationMultiplier=0;
        input.attack=4;
        assert(Rules::cancel_incoming_garbage(queue,input,&carry).outgoingAttack==4);
        assert(Rules::pending_garbage(queue)==10);
        input.cancellationMultiplier=.1; input.attack=1;
        for (int i=0;i<100;++i) {
            assert(Rules::cancel_incoming_garbage(queue,input,&carry).outgoingAttack==0);
            assert(Rules::pending_garbage(queue)==10-(i+1)/10);
        }
        assert(queue.empty() && std::abs(carry.cancellationFraction)<1e-9);

        // The opener's extra capacity cannot become outgoing attack, even
        // with a fractional multiplier or separate perfect-clear bonus.
        queue.push_back({6,TimePoint{},false}); input={4,1,true,14,0,legacy,0,1.0};
        assert(Rules::cancel_incoming_garbage(queue,input,&carry).outgoingAttack==0 && queue.empty());
        queue.push_back({3,TimePoint{},false}); input={4,1,true,14,0,legacy,0,.5};
        assert(Rules::cancel_incoming_garbage(queue,input,&carry).outgoingAttack==0);
        assert(Rules::pending_garbage(queue)==(legacy ? 1 : 0));
    }
    std::deque<Rules::GarbagePacket> queue{{1,TimePoint{},false}};
    Runtime::GarbageCancellationState carry;
    const auto result=Rules::cancel_incoming_garbage(queue,{3,20,true,0,0,false,2,2.0},&carry);
    assert(result.outgoingAttack==2 && result.outgoingPackets==std::vector<int>({2}));
    assert(queue.empty() && carry.outgoingFraction==.5);
}
void entry_delay() {
    Runtime::SessionState session; Runtime::RuntimeServices services; Runtime::GameContext context{session,services};
    auto& player=session.player;
    int pumped=0;
    assert(Engine::wait_for_entry_delay(context,player,[&]{++pumped;return PlayResult::ContinueFrame;})==PlayResult::ContinueFrame);
    assert(pumped==0);
    context.config.field.entryDelay=Duration::from_seconds(.02);
    context.config.simulationPps=4; // ARE uses real time even with a stationary per-piece clock.
    const auto start=context.real_now();
    Engine::schedule_entry_delay(context,player);
    assert(player.timing.entryDelayEnd && *player.timing.entryDelayEnd>=start+context.config.field.entryDelay);
    const auto gameTime=context.now();
    assert(Engine::wait_for_entry_delay(context,player,[&]{++pumped;return PlayResult::ContinueFrame;})==PlayResult::ContinueFrame);
    assert(pumped>0 && !player.timing.entryDelayEnd);
    assert(context.real_now()-start>=context.config.field.entryDelay && context.now()==gameTime);
    for(const auto result:{PlayResult::Retry,PlayResult::OpenSettings,PlayResult::OpponentLeft}) {
        Engine::schedule_entry_delay(context,player);
        assert(Engine::wait_for_entry_delay(context,player,[&]{return result;})==result);
        assert(!player.timing.entryDelayEnd);
    }
    Input::Services input; context.attach_input(input);
    Input::refresh_bindings(input,UserConfig::default_key_codes());
    for(const int index:{8,9}) {
        input.keyStateProvider=[&](int key){return key==UserConfig::default_key_codes()[index];};
        Engine::schedule_entry_delay(context,player);
        assert(Engine::wait_for_entry_delay(context,player,[]{assert(false);return PlayResult::ContinueFrame;})==
            (index==8 ? PlayResult::Retry : PlayResult::OpenSettings));
    }
    context.config.mode.replayMode=true;
    Engine::schedule_entry_delay(context,player); assert(!player.timing.entryDelayEnd);
    assert(Engine::wait_for_entry_delay(context,player,[]{assert(false);return PlayResult::Retry;})==PlayResult::ContinueFrame);
}
void placement_delays() {
    Runtime::SessionState session; Runtime::RuntimeServices services; Runtime::GameContext context{session,services};
    Replay::ReplaySessionState replay; context.attach_replay(replay);
    context.config.field.lineClearDelay=Duration::from_seconds(.02);
    context.config.field.entryDelay=Duration::from_seconds(.03);
    auto& player=session.player; auto& playfield=player.gameplay.playfield;
    playfield.width=10; playfield.visibleHeight=playfield.activeHeight=20; playfield.field.initialize(10,20);
    for(int row=16;row<20;++row) for(int col=0;col<10;++col)
        if(col!=5) playfield.field.set_cell(row,col,Runtime::BoardCell::piece(1));
    auto piece=Piece::get_definition(Piece::types_for_rotation_system(context.config.rotationSystem)[0]);
    Engine::MovementState movement; movement.pose={{17,4},Runtime::Rotation::from_index(1)};
    Render::begin_output_suppression();
    const auto start=context.real_now();
    assert(Engine::finalize_locked_piece(context,playfield.field,piece,player,movement,0,
        []{return PlayResult::ContinueFrame;})==PlayResult::Locked);
    assert(player.gameplay.scoring.totalLines==4 && player.gameplay.scoring.placedBlocks==1);
    assert(player.timing.entryDelayEnd && *player.timing.entryDelayEnd>=start+Duration::from_seconds(.05));
    for(int row=16;row<20;++row) for(int col=0;col<10;++col) assert(playfield.field.cell(row,col).empty_cell());
    assert(Engine::wait_for_entry_delay(context,player,[]{return PlayResult::ContinueFrame;})==PlayResult::ContinueFrame);
    assert(context.real_now()-start>=Duration::from_seconds(.05));
    Render::end_output_suppression();
}
void queue_and_multiplier() {
    Rules::AttackInput in; in.linesCleared=4; in.combo=2; in.garbageMultiplier=.5;
    assert(Rules::calculate_attack(in).attack==2); // floor((4 * 1.25) * .5), before rounding.
    in.perfectClear=true; const auto pc=Rules::calculate_attack(in);
    assert(pc.attack==4 && pc.perfectClearAttack==2); // PC is rounded separately.
    in.garbageMultiplier=0; in.clearedGarbageLines=4;
    assert(Rules::calculate_attack(in).attack==1); // Flat garbage-clear bonus is not multiplied.
    using Packet=Rules::GarbagePacket;
    for(const int limit:{0,3,14}) for(const int placed:{1,3,4,14,15}) for(const int sent:{0,5,12}) for(const int pending:{0,3,8,14}) {
        std::deque<Packet> q; if(pending) q.push_back({pending,TimePoint{},false});
        Rules::GarbageCancelInput input{4,placed,true,limit,sent,false};
        const auto result=Rules::cancel_incoming_garbage(q,input);
        const bool protection=limit>0 && placed<=limit && pending>=sent;
        assert(result.outgoingAttack==std::max(0,4-pending));
        assert(Rules::pending_garbage(q)==std::max(0,pending-(protection ? 8 : 4)));
    }
    std::deque<Packet> q{{8,TimePoint{},false}};
    const auto result=Rules::cancel_incoming_garbage(q,{9,1,true,14,0,false,5});
    assert(q.empty() && result.outgoingAttack==5 && result.outgoingPackets==std::vector<int>{5});
    q={{8,TimePoint{},false}};
    assert(Rules::cancel_incoming_garbage(q,{9,1,true,0,0,false,5}).outgoingAttack==1);
    q={{10,TimePoint{},false},{4,TimePoint{},false}};
    assert(Rules::consume_ready_garbage(q,TimePoint::from_seconds(1),Duration{},0).chunks.empty());
    assert(Rules::pending_garbage(q)==14);
    const auto rise=Rules::consume_ready_garbage(q,TimePoint::from_seconds(1),Duration{},3);
    assert(rise.chunks.size()==1 && rise.chunks[0].lines==3 && !rise.chunks[0].preserveHole);
    assert(q.front().line_count()==7 && q.front().preserve_hole() && Rules::pending_garbage(q)==11);
}
void holes() {
    for(const int width:{1,2,10}) for(const double messiness:{0.0,1.0}) {
        Runtime::TetrioGarbageRandom holes(1);
        int previous=holes.row(width,messiness,true);
        for(int i=0;i<50;++i) {
            const int column=holes.row(width,messiness,true);
            assert(column>=0 && column<width);
            if(width>1) assert((column==previous)==(messiness==0));
            previous=column;
        }
        holes.finish_packet(width,true);
        if(width>1) assert(holes.column!=previous);
    }
    for(const bool contiguous:{false,true}) {
        Rules::GarbageGenerationConfig config;
        config.width=10; config.garbageMode=Runtime::GarbageMode::Multiplayer;
        config.singleMessiness=1; config.avoidSameColumn=true;
        Rules::GarbageGenerationState state;
        state.holePattern=contiguous ? Runtime::GarbageHolePattern::contiguous(2) : Runtime::GarbageHolePattern::scattered(1);
        std::mt19937 rng(123); int previous=-1;
        for(int i=0;i<50;++i) {
            const auto row=Rules::generate_garbage_row(config,state,rng);
            assert(state.lastColumn!=previous); previous=state.lastColumn;
            assert(std::count(row.begin(),row.end(),Runtime::BoardCell::empty())==(contiguous ? 2 : 1));
        }
        config.singleMessiness=0;
        const auto column=state.lastColumn;
        for(int i=0;i<10;++i) { (void)Rules::generate_garbage_row(config,state,rng); assert(state.lastColumn==column); }
    }
}
void fixtures() {
    std::cout<<"{\"attacks\":["; bool first=true;
    for(const int lines:{1,2,3,4}) for(const int combo:{1,3,8}) for(const int b2b:{1,4})
        for(const double multiplier:{0.0,.1,.5,.75,1.0,1.5,2.0}) for(const bool pc:{false,true}) {
            if(pc && lines!=4) continue;
            Rules::AttackInput in; in.linesCleared=lines; in.combo=combo; in.b2b=lines==4 ? b2b : 0;
            in.b2bTransition=lines==4 ? Rules::B2BTransition::Increment : Rules::B2BTransition::Break;
            in.perfectClear=pc; in.garbageMultiplier=multiplier;
            if(!first) std::cout<<','; first=false;
            std::cout<<"{\"lines\":"<<lines<<",\"combo\":"<<combo<<",\"b2b\":"<<in.b2b<<",\"multiplier\":"<<multiplier
                <<",\"pc\":"<<int(pc)<<",\"attack\":"<<Rules::calculate_attack(in).attack<<'}';
        }
    std::cout<<"],\"holes\":["; first=true;
    for(const int seed:{1,2,314159}) for(const double messiness:{0.0,.3,1.0}) for(const bool noSame:{false,true}) for(const int cap:{0,1,3,8,12}) {
        Runtime::TetrioGarbageRandom random(seed); std::vector<int> holes;
        for(int i=0;i<std::min(cap,12);++i) holes.push_back(random.row(10,messiness,noSame));
        if(cap>=12) random.finish_packet(10,noSame);
        if(!first) std::cout<<','; first=false;
        std::cout<<"{\"seed\":"<<seed<<",\"messiness\":"<<messiness<<",\"nosame\":"<<int(noSame)<<",\"cap\":"<<cap<<",\"rng\":"<<random.random.state()<<",\"column\":"<<random.column<<",\"holes\":[";
        for(std::size_t i=0;i<holes.size();++i) std::cout<<(i ? "," : "")<<holes[i];
        std::cout<<"]}";
    }
    std::cout<<"],\"openers\":["; first=true;
    for(const int limit:{0,3,14}) for(const int placed:{1,3,4,14,15}) for(const int sent:{0,5,12}) for(const int pending:{0,3,8,14}) for(const bool pc:{false,true}) {
        std::deque<Rules::GarbagePacket> q; if(pending) q.push_back({pending,TimePoint{},false});
        const auto result=Rules::cancel_incoming_garbage(q,{pc ? 9 : 4,placed,true,limit,sent,false,pc ? 5 : 0});
        if(!first) std::cout<<','; first=false;
        std::cout<<"{\"limit\":"<<limit<<",\"placed\":"<<placed<<",\"sent\":"<<sent<<",\"pending\":"<<pending<<",\"pc\":"<<int(pc)
            <<",\"outgoing\":"<<result.outgoingAttack<<",\"remaining\":"<<Rules::pending_garbage(q)<<'}';
    }
    std::cout<<"],\"are\":["; first=true;
    for(const int side:{0,1}) for(const double delay:{0.0,.05,.5}) {
        Runtime::SessionState session; Runtime::RuntimeServices services; Runtime::GameContext context{session,services};
        context.config.field.entryDelay=Duration::from_seconds(delay);
        context.config.field.lineClearDelay=Duration::from_seconds(.1);
        session.player.gameplay.playfield.field.initialize(10,20);
        session.player.garbage.tetrioRandom.emplace(1);
        Replay::TetrioRecorder recorder(1,side);
        for(int i=0;i<3;++i) recorder.lock(context,Rules::PlacementResult{});
        if(!first) std::cout<<','; first=false;
        std::cout<<recorder.round_json(context,true,recorder.last_frame()+1);
    }
    std::cout<<"]}\n";
}
}
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--fixtures") {fixtures(); return 0;}
    settings_and_replay(); fractional_cancellation(); infinite_movement(); entry_delay(); placement_delays(); queue_and_multiplier(); holes();
    std::cout<<"Timing, garbage rules and settings tests passed\n";
}
