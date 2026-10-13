#include "tetr/replay/recorder.h"
#include "tetr/compat/board_encoding.h"
#include "tetr/compat/piece_encoding.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/app/application_state.h"
#include "tetr/util/text_codec.h"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <algorithm>

namespace Replay {

std::string recording_for_save(const Runtime::GameContext& context) {
    const auto& state = session(context);
    std::string recording = state.data;
    const auto headerEnd = Util::Text::matching_brace(recording, 0);
    if (headerEnd != std::string::npos) {
        std::ostringstream metadata;
        metadata.imbue(std::locale::classic());
        metadata<<"{undo_count,"<<state.undoCount<<"}{pause_count,"<<state.pauseCount<<'}';
        const auto& timing=context.session.player.timing;
        metadata<<std::setprecision(std::numeric_limits<double>::max_digits10);
        if (timing.completedGameTime) metadata<<"{result_game_time,"<<timing.completedGameTime->seconds()<<'}';
        if (timing.completedRealTime) metadata<<"{result_real_time,"<<timing.completedRealTime->seconds()<<'}';
        recording.insert(headerEnd,metadata.str());
    }
    return recording;
}

void append_record(Runtime::GameContext& context, const std::string& record) {
    auto& state = session(context);
    if (state.tetrioRecorder) return; // The tournament has its own uncapped recording.
    if (state.recordingStart && !context.config.mode.replayMode) {
        const auto* app = context.app_component();
        const auto& clock = app && app->primaryContext ? *app->primaryContext : context;
        const auto elapsed = clock.real_now() - *state.recordingStart;
        state.data += "{wall_time," + std::to_string(elapsed.seconds()) + "}";
    }
    state.data += record;
}

std::string replay_base(const Runtime::GameContext& context, OperationKind op) {
    return encode_operation_base(op, Runtime::elapsed_game_time(context.session, context.now()));
}

std::string replay_base(const Runtime::GameContext& context, const std::string& op) {
    return encode_operation_base(op, Runtime::elapsed_game_time(context.session, context.now()));
}

void record_add_garbage(Runtime::GameContext& context, int height, bool preserveHole, Runtime::BoardCell cell) {
    if (context.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        const auto& qp=context.session.quickPlay;
        add_replay(context,OperationKind::AddGarbage,height,preserveHole ? 2 : 0,Compat::to_legacy(cell),
            qp.floor,qp.progress.receiveProtection,Runtime::elapsed_game_time(context.session,context.now()).seconds());
        return;
    }
    add_replay(context, OperationKind::AddGarbage, height, preserveHole ? 2 : 0, Compat::to_legacy(cell));
}

void record_add_solid_garbage(Runtime::GameContext& context, int height, Runtime::BoardCell cell) {
    add_replay(context, OperationKind::AddSolidGarbage, height, Compat::to_legacy(cell));
}

void record_add_buffer(Runtime::GameContext& context, int attack, bool instant, int resolvedLines,
                       double incomingFraction, bool deferDisplay,std::optional<int> receivedAttack) {
    if (session(context).data.length() >= 10000000) return;
    std::ostringstream record;
    record.imbue(std::locale::classic());
    record << '{' << replay_base(context,OperationKind::AddBuffer)
           << ',' << attack << ',' << instant << ',' << resolvedLines
           << ',' << std::setprecision(std::numeric_limits<double>::max_digits10) << incomingFraction;
    if (deferDisplay || receivedAttack) record << ',' << int(deferDisplay);
    if (receivedAttack) record << ',' << std::max(0,*receivedAttack);
    record << '}';
    append_record(context,record.str());
}

void record_pose_event(
    Runtime::GameContext& context,
    OperationKind operation,
    Piece::Pose pose) {
    add_replay(
        context,
        operation,
        pose.position.row,
        pose.position.col,
        Compat::to_legacy(pose.rotation));
}

void record_end_move_data(
    Runtime::GameContext& context,
    Runtime::RotationRecord lastRotation,
    long long score,
    Piece::Pose pose) {
    add_replay(
        context,
        OperationKind::EndMoveData,
        Compat::to_legacy(lastRotation),
        score,
        pose.position.row,
        pose.position.col,
        Compat::to_legacy(pose.rotation));
}

void record_quick_play_state(Runtime::GameContext& context, const Runtime::PlayerSessionState& player) {
    if (session(context).data.length() >= 10000000) return;
    const auto& qp = context.session.quickPlay;
    const auto& p = qp.progress;
    std::ostringstream record;
    record.imbue(std::locale::classic());
    // A downgraded XP bar can be arbitrarily close to the upgrade boundary.
    // Six decimal places would round it to a full bar and spuriously re-upgrade on playback.
    record << std::setprecision(std::numeric_limits<double>::max_digits10)
           << '{' << replay_base(context, OperationKind::EndPlaceData)
           << ',' << qp.speed << ',' << qp.altitude << ',' << qp.floor
           << ',' << qp.garbageMultiple << ',' << qp.garbageInTime.seconds()
           << ',' << player.pieces.qpBagBreather
           << ',' << p.experience << ',' << p.pendingAltitude << ',' << p.decayProtection
           << ',' << p.upgradeFatigue << ',' << p.upgraded << ',' << p.cancellation
           << ',' << p.sinceRelief << ',' << p.piecesSinceRelief << ',' << p.receiveProtection
           << ',' << p.sinceReceived << ',' << p.floorTime << ',' << p.stallMultiplier
           << ',' << p.fatigueStage << ',' << p.windupUntil
           << ',' << p.fatigueGarbageLines << '}';
    append_record(context,record.str());
}

} // namespace Replay
