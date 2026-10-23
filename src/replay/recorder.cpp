#include "tetr/replay/recorder.h"
#include "tetr/compat/board_encoding.h"
#include "tetr/compat/piece_encoding.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/challenge_prefix.h"
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
    if(context.config.mode.replayMode && !state.loadedRecording.empty())return state.loadedRecording;
    std::string recording = state.data;
    const auto headerEnd = Util::Text::matching_brace(recording, 0);
    if (headerEnd != std::string::npos) {
        std::ostringstream metadata;
        metadata.imbue(std::locale::classic());
        metadata<<"{undo_count,"<<state.undoCount<<"}{pause_count,"<<state.pauseCount<<'}';
        if(state.onlineIdentity[2])metadata<<"{online_room,"<<state.onlineIdentity[0]<<"}{online_epoch,"<<state.onlineIdentity[1]<<"}{online_actor,"<<state.onlineIdentity[2]<<'}';
        if(state.onlineProfile)metadata<<"{online_profile,"<<state.onlineProfile<<'}';
        const auto& timing=context.session.player.timing;
        auto gameTime=timing.completedGameTime,realTime=timing.completedRealTime;
        const auto* app=context.app_component();
        const auto* round=app && app->challenge_round(&context)?&*app->challenge_round(&context):nullptr;
        if(round && round->completedResult) {
            // Preserve the full source's times when the visible challenge
            // result has already been replaced with its earlier prefix.
            gameTime=Runtime::Duration::from_seconds(round->completedResult->gameSeconds);
            realTime=Runtime::Duration::from_seconds(round->completedResult->realSeconds);
        }
        metadata<<std::setprecision(std::numeric_limits<double>::max_digits10);
        if (gameTime) metadata<<"{result_game_time,"<<gameTime->seconds()<<'}';
        if (realTime) metadata<<"{result_real_time,"<<realTime->seconds()<<'}';
        if(round) {
            // Undo can replace the operation stream. Such attempts are not
            // eligible, and their old placement ordinals must not be cached.
            if(!state.undoCount && !state.pauseCount && round->evaluated)
                metadata<<encode_challenge_prefixes(round->replayPrefixes);
        }
        recording.insert(headerEnd,metadata.str());
    }
    return recording;
}

void append_record(Runtime::GameContext& context, const std::string& record) {
    auto& state = session(context);
    if (state.tetrioRecorder) return; // The tournament has its own uncapped recording.
    const auto previousSize=state.data.size();
    if (state.recordingStart && !context.config.mode.replayMode) {
        const auto* app = context.app_component();
        const auto& clock = app && app->primaryContext ? *app->primaryContext : context;
        const auto elapsed = clock.real_now() - *state.recordingStart;
        state.data += "{wall_time," + std::to_string(elapsed.seconds()) + "}";
    }
    state.data += record;
    if(state.recordObserver)state.recordObserver(std::string_view(state.data).substr(previousSize));
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
    if (session(context).data.length() >= session(context).recordingLimit) return;
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
    Piece::Pose pose,int inputActions) {
    if(inputActions>=0){add_replay(context,OperationKind::EndMoveData,Compat::to_legacy(lastRotation),score,
        pose.position.row,pose.position.col,Compat::to_legacy(pose.rotation),inputActions);return;}
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
    if (session(context).data.length() >= session(context).recordingLimit) return;
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
