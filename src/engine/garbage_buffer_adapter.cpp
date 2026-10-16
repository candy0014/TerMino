#include "tetr/engine/garbage_service.h"
#include "tetr/app/application_state.h"
#include "tetr/engine/live_piece_periodic_events.h"
#include "tetr/engine/movement_controller.h"
#include "garbage_rendering.h"
#include "piece_replay.h"

#include "tetr/network/multiplayer_channel.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/rules/opener_phase.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/rules/attack.h"
#include "tetr/rules/quick_play.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Engine::GarbageService {
namespace {

std::vector<bool> buffer_segment_tops(const Runtime::PlayerSessionState& player,
    const std::vector<int>& states) {
    std::vector<bool> tops(states.size(),false);
    std::int64_t top=0;
    for (const auto& packet:player.garbage.buffer) {
        if (packet.line_count()<=0) continue;
        top+=packet.line_count();
        // A packet extending above the display remains one continuous
        // segment; clipping must not create a false boundary at the ceiling.
        if (top>=static_cast<std::int64_t>(states.size())) break;
        tops[static_cast<std::size_t>(top)]=states[static_cast<std::size_t>(top)]!=0;
    }
    return tops;
}

Runtime::TimePoint garbage_clock_now(Runtime::GameContext& context) {
    if (!context.config.mode.replayMode) return context.now();
    return Runtime::TimePoint::from_seconds(
        Replay::playback_game_elapsed(context,context.now()).seconds());
}

Runtime::Duration buffer_delay_in_clock(const Runtime::GameContext& context) {
    const double multiplier=context.config.mode.replayMode ? 1.0 : context.config.timeMultiplier;
    return Runtime::current_incoming_garbage_delay(context) / (multiplier>0.0 ? multiplier : 1.0);
}

Runtime::Duration rise_interval_in_clock(const Runtime::GameContext& context) {
    auto interval=context.config.garbage.riseInterval;
    if (!context.config.mode.replayMode && context.config.garbage.mode==Runtime::GarbageMode::QuickPlay)
        interval=Rules::qp_garbage_rise_interval(context.config.quickPlay,interval);
    const double multiplier=context.config.mode.replayMode ? 1.0 : context.config.timeMultiplier;
    return std::isfinite(interval.seconds()) && interval.seconds()>0
        ? interval/(multiplier>0 ? multiplier : 1.0) : Runtime::Duration{};
}

bool deadline_reached(Runtime::TimePoint now,Runtime::TimePoint deadline) {
    const double tolerance=4*std::numeric_limits<double>::epsilon()*
        std::max({1.0,std::abs(now.seconds()),std::abs(deadline.seconds())});
    return (deadline-now).seconds()<=tolerance;
}

std::int64_t pending_lines(const Runtime::PlayerSessionState& player) {
    std::int64_t result=0;
    for (const auto& packet:player.garbage.buffer) result+=std::max(0,packet.line_count());
    return result;
}

bool arm_ready_rise(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field) {
    const auto interval=rise_interval_in_clock(context);
    if (context.config.mode.replayMode || interval<=Runtime::Duration{} || !has_ready_buffer(context,player))
        return false;
    auto& rise=player.garbage.rising;
    int available=std::min(context.config.garbage.cap,field.height()+Runtime::kHiddenRowCapacity);
    const auto now=garbage_clock_now(context);
    const auto delay=buffer_delay_in_clock(context);
    for (const auto& packet:player.garbage.buffer) {
        if (now-packet.arrival_time()<delay || available<=0) break;
        const int taken=std::min(available,std::max(0,packet.line_count()));
        rise.remainingLines+=taken; available-=taken;
    }
    if (!rise.remainingLines) return false;
    const auto previousDeadline=rise.nextRow;
    rise.interval=interval;
    const bool quickPlay=context.config.garbage.mode==Runtime::GarbageMode::QuickPlay;
    rise.nextRow=now+(quickPlay ? interval : Runtime::Duration{});
    // Preserve pacing across a cap boundary within the same attack, and QP's
    // post-clear hesitation even when the previous roll was cancelled.
    if (previousDeadline && (quickPlay || player.garbage.buffer.front().preserve_hole()))
        rise.nextRow=std::max(*rise.nextRow,*previousDeadline);
    update_buffer(context,player,true);
    return true;
}

int advance_armed_rows(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,const GarbageRiseCallback& afterRise) {
    if (context.config.mode.replayMode) return 0;
    auto& rise=player.garbage.rising;
    const auto now=garbage_clock_now(context);
    int risen=0;
    while (rise.remainingLines>0 && rise.nextRow && deadline_reached(now,*rise.nextRow)) {
        while (!player.garbage.buffer.empty() && player.garbage.buffer.front().line_count()<=0)
            player.garbage.buffer.pop_front();
        if (player.garbage.buffer.empty()) { rise.clear(); break; }
        const int packetLines=player.garbage.buffer.front().line_count();
        // These rows were already eligible when armed. A later QP delay
        // change must not make the same attack wait again.
        const auto plan=Rules::consume_ready_garbage(player.garbage.buffer,now,Runtime::Duration{},1);
        if (plan.chunks.empty()) break;
        const auto chunk=plan.chunks.front();
        if (chunk.lines<=0) continue;
        if(chunk.sourceSlot>=0) player.multiplayer.lastDamageSource=chunk.sourceSlot;
        Replay::add_replay(context,Replay::OperationKind::RaiseBuffer,1);
        add_garbage(context,player,field,1,chunk.preserveHole,Runtime::BoardCell::garbage(),false);
        if (player.garbage.tetrioRandom && packetLines==1)
            player.garbage.tetrioRandom->finish_packet(field.width(),context.config.garbage.avoidSameColumn);
        ++risen;
        --rise.remainingLines;
        *rise.nextRow+=rise.interval;
        if (afterRise && !afterRise(1)) break;
    }
    if (player.garbage.buffer.empty()) {
        player.garbage.cancellation.cancellationFraction=0;
        if (context.config.garbage.mode!=Runtime::GarbageMode::QuickPlay) rise.clear();
    }
    return risen;
}

Rules::GarbageBufferDisplayMode buffer_display_mode(Runtime::GarbageMode garbageMode) {
    if (Runtime::uses_garbage_buffer(garbageMode)) {
        return Rules::GarbageBufferDisplayMode::ThreeStage;
    }
    return Rules::GarbageBufferDisplayMode::Hidden;
}

void render_buffer_cell(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int displayIndex,
    int state,
    bool segmentTop) {
    Render::OutputGuard output(context);
    if (displayIndex > player.gameplay.playfield.visibleHeight + Runtime::kDisplayHiddenRows) return;
    Render::move_to(
        context,
        player,
        player.gameplay.playfield.visibleHeight - displayIndex,
        0,
        -3);
    if (state == 0) {
        Render::draw_square(context.config.display.squareStyle, Render::SquareFill::Empty);
        return;
    }

    if (state == 1) Render::set_ui_color(Render::ColorRole::Accent, context.config.display.obscuredColors);
    if (state == 2) Render::set_ui_color(Render::ColorRole::IncomingGarbage, context.config.display.obscuredColors);
    if (state == 3) Render::set_ui_color(Render::ColorRole::Danger, context.config.display.obscuredColors);
    Render::draw_square(context.config.display.squareStyle,
        segmentTop ? Render::SquareFill::LowerSevenEighths : Render::SquareFill::Filled);
}

void send_multiplayer_attack(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    int attack) {
    if (attack <= 0) return;
    auto& multiplayer = player.multiplayer;
    ++multiplayer.sendId;
    multiplayer.pendingSends.push_back({multiplayer.sendId, attack});
    Network::send_attack_frame(
        context,
        {multiplayer.sendId, multiplayer.receiveId, attack});
}

} // namespace

void update_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player, bool finalState) {
    auto& deferred=player.presentation.garbageBufferDisplayDeferred;
    if (finalState) deferred=false;
    else if (deferred) return;
    const auto nextStates = Rules::compute_garbage_buffer_display(
        player.garbage.buffer,
        garbage_clock_now(context),
        context.config.mode.replayMode ? 1.0 : context.config.timeMultiplier,
        Runtime::current_incoming_garbage_delay(context),
        buffer_display_mode(context.config.garbage.mode),
        static_cast<int>(player.presentation.garbageBufferDisplay.size()));
    const auto nextTops=buffer_segment_tops(player,nextStates);
    auto& previousTops=player.presentation.garbageBufferSegmentTops;
    previousTops.resize(nextStates.size(),false);

    for (int index = 1; index < static_cast<int>(player.presentation.garbageBufferDisplay.size()); ++index) {
        if (player.presentation.garbageBufferDisplay[index] == nextStates[index] &&
            previousTops[index]==nextTops[index]) continue;
        player.presentation.garbageBufferDisplay[index] = nextStates[index];
        previousTops[index]=nextTops[index];
        render_buffer_cell(context, player, index, nextStates[index],nextTops[index]);
    }
}

void redraw_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    if (player.presentation.garbageBufferDisplayDeferred) {
        const auto& previous=player.presentation.garbageBufferDisplay;
        const auto& tops=player.presentation.garbageBufferSegmentTops;
        for (std::size_t index=1;index<previous.size();++index)
            render_buffer_cell(context,player,static_cast<int>(index),previous[index],
                index<tops.size() && tops[index]);
        return;
    }
    const auto states = Rules::compute_garbage_buffer_display(
        player.garbage.buffer,
        garbage_clock_now(context),
        context.config.mode.replayMode ? 1.0 : context.config.timeMultiplier,
        Runtime::current_incoming_garbage_delay(context),
        buffer_display_mode(context.config.garbage.mode),
        static_cast<int>(player.presentation.garbageBufferDisplay.size()));
    player.presentation.garbageBufferDisplay=states;
    const auto tops=buffer_segment_tops(player,states);
    player.presentation.garbageBufferSegmentTops=tops;
    for(int index=1;index<static_cast<int>(states.size());++index){
        render_buffer_cell(context,player,index,states[index],tops[index]);
    }
}

int resolve_received_attack(Runtime::GameContext& context, Runtime::PlayerSessionState& player, double attack) {
    const auto& config=context.config.garbage;
    const double multiplier=std::isfinite(config.hitMultiplier)
        ? std::clamp(config.hitMultiplier,0.0,1000.0) : 1.0;
    const double exact=std::max(0.0,attack)*multiplier +
        (config.legacyIncomingRounding ? 0.0 : player.garbage.incomingFraction);
    const double rounded=std::floor(exact+(config.legacyIncomingRounding ? 0.0 : 1e-9));
    player.garbage.incomingFraction=config.legacyIncomingRounding ? 0.0 : std::max(0.0,exact-rounded);
    return static_cast<int>(std::min<double>(rounded,std::numeric_limits<int>::max()));
}

void add_buffer(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Playfield&,
    int attack,
    bool instant,
    int resolvedLines, bool deferDisplay,std::optional<int> receivedAttack) {
    const bool qpRoll = context.config.garbage.mode == Runtime::GarbageMode::QuickPlay &&
        (!context.config.mode.replayMode || resolvedLines >= 0);
    const double roll = qpRoll ? static_cast<double>(player.attackRoundingRandom()) /
        (static_cast<double>(std::mt19937::max()) + 1) : 1.0;
    if (resolvedLines < 0) {
        double received = std::max(0,attack);
        if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay && !context.config.mode.replayMode) {
            const auto& qp = context.session.quickPlay;
            // The local opponent simulation has no remote altitude; equal
            // altitude is the neutral adaptation of the height penalty.
            const double raw = Rules::qp_receive_attack(attack,qp.altitude,qp.altitude,qp.floor,
                context.config.quickPlay,qp.progress,Runtime::elapsed_game_time(context.session,context.now()).seconds());
            // Preserve Quick Play's normal stochastic rounding, then apply the
            // player's reception setting to the received packet.
            received = std::floor(raw);
            received += roll < raw - received;
        }
        resolvedLines=resolve_received_attack(context,player,received);
    }
    if (!context.config.mode.replayMode) {
        Replay::record_add_buffer(context,attack,instant,resolvedLines,player.garbage.incomingFraction,deferDisplay,
            std::max(0,receivedAttack.value_or(resolvedLines)));
    }
    player.gameplay.scoring.totalReceivedAttack+=static_cast<std::uint64_t>(std::max(0,receivedAttack.value_or(resolvedLines)));
    if (resolvedLines > 0) {
        if ((deferDisplay || context.config.garbage.entryMode==Runtime::GarbageEntryMode::Immediate) &&
            Runtime::uses_garbage_buffer(context.config.garbage.mode) &&
            Runtime::current_incoming_garbage_delay(context)==Runtime::Duration{} && context.config.garbage.cap>0)
            player.presentation.garbageBufferDisplayDeferred=true;
        if (context.config.garbage.mode == Runtime::GarbageMode::QuickPlay) {
            auto& p = context.session.quickPlay.progress;
            p.receiveProtection = std::min(18,p.receiveProtection +
                (Runtime::reverse(context.config.quickPlay.volatileMode) ? resolvedLines / 3 : resolvedLines));
            p.sinceReceived = 0;
        }
        if (auto* recorder = Replay::tetrio_recorder(context))
            recorder->incoming(context, resolvedLines);
        player.garbage.buffer.push_back(Rules::make_garbage_packet(
            resolvedLines,
            1.0,
            garbage_clock_now(context),
            instant,
            buffer_delay_in_clock(context)));
    }
    update_buffer(context, player);
}

void add_qp_opponent_attack(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,int attack) {
    if (context.config.mode.replayMode || attack<=0) return;
    auto& qp=context.session.quickPlay;
    const double elapsed=Runtime::elapsed_game_time(context.session,context.now()).seconds();
    // Equal-altitude opponents provide a neutral height penalty. Their APM
    // is empirical; target selection is the project's local simulation.
    const double exact=Rules::qp_receive_attack(attack,qp.altitude,qp.altitude,qp.floor,
        context.config.quickPlay,qp.progress,elapsed);
    const double roll=double(player.attackRoundingRandom())/(double(std::mt19937::max())+1);
    const double rounded=std::floor(exact)+(roll<exact-std::floor(exact));
    const int received=resolve_received_attack(context,player,rounded);
    // Record the original reception/rounding independently of delayed parts.
    // Playback consumes the same roll; the segments carry resolved line counts.
    player.gameplay.scoring.totalReceivedAttack+=static_cast<std::uint64_t>(received);
    Replay::record_add_buffer(context,attack,false,0,player.garbage.incomingFraction,false,received);
    if (!received) return;
    const bool volatileMode=Runtime::enabled(context.config.quickPlay.volatileMode);
    if (Rules::qp_queue_windup(qp.progress,received,qp.altitude,volatileMode,elapsed)) {
        const auto& warning=qp.progress.windupWarnings.back();
        Replay::add_replay(context,Replay::OperationKind::QuickPlayWindup,
            warning.start,warning.end,warning.segments);
    } else add_buffer(context,player,field,attack,false,received,false,0);
}

int offset_buffer(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,
    int attack,
    int clearedLines,
    int b2bReleaseCharge,
    int perfectClearAttack) {
    if (!context.config.mode.replayMode) {
        if (perfectClearAttack) Replay::add_replay(context,Replay::OperationKind::OffsetBuffer,attack,clearedLines,perfectClearAttack);
        else Replay::add_replay(context,Replay::OperationKind::OffsetBuffer,attack,clearedLines);
    }

    if (clearedLines) {
        if (auto* recorder = Replay::tetrio_recorder(context); recorder && context.config.garbage.legacyOpenerPhase) recorder->cancellation(
            player.gameplay.scoring.placedBlocks,
            Rules::opener_phase_double_cancel(player.gameplay.scoring.placedBlocks,
                attack, Rules::pending_garbage(player.garbage.buffer), true));
        const auto packetCount = player.garbage.buffer.size();
        const auto pendingBefore=pending_lines(player);
        const Rules::GarbageCancelResult cancellation = Rules::cancel_incoming_garbage(
            player.garbage.buffer,
            Rules::GarbageCancelInput{
                attack,
                player.gameplay.scoring.placedBlocks,
                context.config.garbage.mode == Runtime::GarbageMode::Multiplayer,
                context.config.garbage.openerPhase,
                player.gameplay.scoring.totalOutgoingAttack,
                context.config.garbage.legacyOpenerPhase,
                perfectClearAttack,
                context.config.garbage.cancelMultiplier
            },&player.garbage.cancellation);
        attack = cancellation.outgoingAttack;
        auto& rising=player.garbage.rising;
        const auto cancelled=std::max<std::int64_t>(0,pendingBefore-pending_lines(player));
        rising.remainingLines-=static_cast<int>(std::min<std::int64_t>(rising.remainingLines,cancelled));
        if (player.garbage.buffer.empty()) rising.clear();
        if (!context.config.mode.replayMode && context.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
            rise_interval_in_clock(context)>Runtime::Duration{}) {
            // The continuous-entry client pauses its roll for twelve frames
            // after a clear, without requiring another non-clearing lock.
            const double multiplier=context.config.timeMultiplier>0 ? context.config.timeMultiplier : 1.0;
            const auto deadline=garbage_clock_now(context)+Runtime::Duration::from_seconds(12.0/60.0)/multiplier;
            rising.nextRow=rising.nextRow ? std::max(*rising.nextRow,deadline) : deadline;
        }
        if (player.garbage.tetrioRandom)
            for (auto i = player.garbage.buffer.size(); i < packetCount; ++i)
                player.garbage.tetrioRandom->finish_packet(field.width(),context.config.garbage.avoidSameColumn);

        if (attack) {
            auto packets = cancellation.outgoingPackets;
            if (packets.empty()) packets.push_back(attack);
            for (std::size_t i = 0; i < packets.size(); ++i) {
                const auto* app=context.app_component();
                if(!context.config.mode.replayMode && app && app->challengeSendAttack)app->challengeSendAttack(packets[i]);
                if (!context.config.mode.replayMode && context.config.garbage.mode == Runtime::GarbageMode::Backfire && !(app && app->challengeSendAttack))
                    add_buffer(context,player,field,packets[i],false,-1,
                        context.config.garbage.entryMode==Runtime::GarbageEntryMode::AfterLock &&
                        Runtime::current_incoming_garbage_delay(context)==Runtime::Duration{} &&
                        context.config.field.lineClearDelay<=Runtime::Duration::from_seconds(1e-7) &&
                        context.config.garbage.cap>0);
                if (!context.config.mode.replayMode && context.config.garbage.mode == Runtime::GarbageMode::Multiplayer) {
                    const auto release = Rules::split_b2b_release_garbage(packets[i],i==0 ? b2bReleaseCharge : 0);
                    send_multiplayer_attack(context,player,release.ordinaryAttack);
                    for (const int packet : release.chargePackets) send_multiplayer_attack(context,player,packet);
                }
            }
        }
        update_buffer(context, player);
        return attack;
    }

    if (context.config.garbage.entryMode == Runtime::GarbageEntryMode::AfterNonClear &&
        (!context.config.mode.replayMode || rise_interval_in_clock(context)==Runtime::Duration{}))
        raise_ready_buffer(context,player,field,true,false);
    return 0;
}

bool has_ready_buffer(Runtime::GameContext& context, const Runtime::PlayerSessionState& player) {
    if (context.config.garbage.cap <= 0 || player.garbage.buffer.empty() ||
        player.garbage.rising.remainingLines>0) return false;
    return garbage_clock_now(context)-player.garbage.buffer.front().arrival_time() >=
        buffer_delay_in_clock(context);
}

bool has_due_garbage_rise(Runtime::GameContext& context,const Runtime::PlayerSessionState& player) {
    const auto& rise=player.garbage.rising;
    return !context.config.mode.replayMode && rise.remainingLines>0 && rise.nextRow &&
        deadline_reached(garbage_clock_now(context),*rise.nextRow);
}

int advance_garbage_rise(Runtime::GameContext& context,Runtime::PlayerSessionState& player,
    Runtime::Playfield& field,bool renderField,const GarbageRiseCallback& afterRise) {
    if (!has_due_garbage_rise(context,player)) return 0;
    Render::OutputGuard output(context);
    const auto before=renderField ? GarbageRendering::capture_field(context,player,field)
        : GarbageRendering::FieldImage{};
    const int risen=advance_armed_rows(context,player,field,afterRise);
    if (risen && renderField) GarbageRendering::render_field_changes(context,player,field,before);
    update_buffer(context,player,true);
    return risen;
}

int raise_ready_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player,
                      Runtime::Playfield& field, bool renderField, bool recordConsumption,
                      const GarbageRiseCallback& afterRise) {
    if (player.garbage.rising.remainingLines>0)
        return advance_garbage_rise(context,player,field,renderField,afterRise);
    if (!has_ready_buffer(context,player)) {
        if (player.presentation.garbageBufferDisplayDeferred) update_buffer(context,player,true);
        return 0;
    }
    if (arm_ready_rise(context,player,field)) {
        return advance_garbage_rise(context,player,field,renderField,afterRise);
    }
    Render::OutputGuard output(context);
    const auto before=renderField && !context.config.mode.replayMode
        ? GarbageRendering::capture_field(context,player,field) : GarbageRendering::FieldImage{};
    std::vector<int> originalPacketSizes;
    if (player.garbage.tetrioRandom)
        for (const auto& packet : player.garbage.buffer) originalPacketSizes.push_back(packet.line_count());
    const Rules::GarbageRisePlan rise = Rules::consume_ready_garbage(
        player.garbage.buffer,
        garbage_clock_now(context),
        buffer_delay_in_clock(context),
        std::min(context.config.garbage.cap,field.height()+Runtime::kHiddenRowCapacity));
    int consumed=0;
    for (const auto& chunk:rise.chunks) consumed+=chunk.lines;
    if (consumed && recordConsumption && !context.config.mode.replayMode)
        Replay::add_replay(context,Replay::OperationKind::RaiseBuffer,consumed);
    if (player.garbage.buffer.empty()) player.garbage.cancellation.cancellationFraction=0.0;
    int risenLines=0;
    if (!context.config.mode.replayMode) {
        std::size_t packetIndex = 0;
        for (const auto& chunk : rise.chunks) {
            if(chunk.lines>0 && chunk.sourceSlot>=0) player.multiplayer.lastDamageSource=chunk.sourceSlot;
            add_garbage(
                context,
                player,
                field,
                chunk.lines,
                chunk.preserveHole,
                Runtime::BoardCell::garbage(),false);
            risenLines+=chunk.lines;
            if (player.garbage.tetrioRandom && chunk.lines == originalPacketSizes[packetIndex++])
                player.garbage.tetrioRandom->finish_packet(field.width(),context.config.garbage.avoidSameColumn);
            if (afterRise && !afterRise(chunk.lines)) break;
        }
    }
    if (risenLines && renderField) GarbageRendering::render_field_changes(context,player,field,before);
    update_buffer(context, player,true);
    return risenLines;
}

void consume_recorded_buffer(Runtime::GameContext& context, Runtime::PlayerSessionState& player, int lines) {
    auto& queue=player.garbage.buffer;
    while (lines>0 && !queue.empty()) {
        const int taken=std::min(lines,queue.front().line_count());
        lines-=taken;
        if (taken==queue.front().line_count()) queue.pop_front();
        else { queue.front().consume_lines(taken); queue.front().mark_preserve_hole(); }
    }
    if (queue.empty()) player.garbage.cancellation.cancellationFraction=0.0;
    update_buffer(context,player,true);
}

} // namespace Engine::GarbageService

namespace Engine {

std::optional<Rules::PlayResult> apply_immediate_garbage_to_live_piece(
    Runtime::GameContext& context, Runtime::Playfield& field, Piece::Definition& piece,
    Runtime::PlayerSessionState& player, MovementState& movementState,
    bool pieceVisible, bool* fieldChanged) {
    if (fieldChanged) *fieldChanged=false;
    // Fatigue may raise the solid floor inside the preceding frame callback.
    // Lift a colliding live piece before its next movement or BOT continuation.
    if (!context.config.mode.replayMode && context.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
        !piece.can_place(movementState.pose,field)) {
        const auto before=GarbageRendering::capture_field(context,player,field,
            pieceVisible ? &piece : nullptr,player.presentation.paintedActivePose.value_or(movementState.pose));
        record_piece_pose(context,Replay::OperationKind::PutErase,movementState.pose);
        const auto& cells=piece.cells(movementState.pose.rotation);
        const int minimumRow=-Runtime::kHiddenRowCapacity-std::min_element(cells.begin(),cells.end(),
            [](const auto& a,const auto& b) {return a[0]<b[0];})->at(0);
        while (!piece.can_place(movementState.pose,field) && movementState.pose.position.row>minimumRow)
            --movementState.pose.position.row;
        if (fieldChanged) *fieldChanged=true;
        if (!piece.can_place(movementState.pose,field)) {
            GarbageRendering::render_field_changes(context,player,field,before);
            return Rules::PlayResult::Dead;
        }
        record_piece_pose(context,Replay::OperationKind::PutDraw,movementState.pose);
        GarbageRendering::render_field_changes(context,player,field,before,&piece,movementState.pose);
    }
    bool startImmediate=context.config.garbage.entryMode==Runtime::GarbageEntryMode::Immediate &&
        GarbageService::has_ready_buffer(context,player);
    if (!context.config.mode.replayMode && Runtime::uses_garbage_buffer(context.config.garbage.mode) &&
        (startImmediate || GarbageService::has_due_garbage_rise(context,player))) {
        if (startImmediate && GarbageService::arm_ready_rise(context,player,field)) {
            if (!GarbageService::has_due_garbage_rise(context,player)) return std::nullopt;
            startImmediate=false;
        }
        Render::OutputGuard output(context);
        const auto before=GarbageRendering::capture_field(context,player,field,
            pieceVisible ? &piece : nullptr,player.presentation.paintedActivePose.value_or(movementState.pose));
        record_piece_pose(context,Replay::OperationKind::PutErase,movementState.pose);
        const auto& cells=piece.cells(movementState.pose.rotation);
        const int minimumOffset=std::min_element(cells.begin(),cells.end(),
            [](const auto& a,const auto& b) { return a[0]<b[0]; })->at(0);
        const int minimumRow=-Runtime::kHiddenRowCapacity-minimumOffset;
        bool dead=false;
        const GarbageService::GarbageRiseCallback liftPiece=[&](int) {
            while (!piece.can_place(movementState.pose,field) && movementState.pose.position.row>minimumRow)
                --movementState.pose.position.row;
            dead=!piece.can_place(movementState.pose,field);
            return !dead;
        };
        const int risen=startImmediate
            ? GarbageService::raise_ready_buffer(context,player,field,false,true,liftPiece)
            : GarbageService::advance_garbage_rise(context,player,field,false,liftPiece);
        if (risen && fieldChanged) *fieldChanged=true;
        if (dead) {
            GarbageRendering::render_field_changes(context,player,field,before);
            if (context.config.garbage.mode==Runtime::GarbageMode::Multiplayer)
                Network::send_round_signal(context,Network::OutgoingRoundSignal::LocalLoss);
            return Rules::PlayResult::Dead;
        }
        record_piece_pose(context,Replay::OperationKind::PutDraw,movementState.pose);
        GarbageRendering::render_field_changes(context,player,field,before,&piece,movementState.pose);
        Render::flush();
    }

    return std::nullopt;
}

} // namespace Engine
