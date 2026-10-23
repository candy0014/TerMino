#include "tetr/compat/legacy_encoding.h"
#include "tetr/app/application_state.h"
#include "tetr/engine/replay_playback_controls.h"
#include "tetr/engine/spike_presentation.h"
#include "tetr/input/services.h"
#include "tetr/network/frame_packet.h"
#include "tetr/replay/movement_event.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/recorder.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/file_path.h"
#include "tetr/util/file_path.h"
#include "tetr/replay/local_versus_codec.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/render/board_renderer.h"
#include "tetr/rules/garbage_queue.h"
#include "tetr/runtime/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/runtime_services.h"
#include "support/terminal_screen.h"

#include <cassert>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>

namespace TetrTests::serialization_boundary_test {
namespace {

void replay_is_decoded_once() {
    Replay::ReplaySessionState state;
    using Queue = decltype(state.operations);
    static_assert(std::is_same_v<typename Queue::value_type, Replay::Operation>);

    const std::string text =
        "prefix{move,2.500000,-2,4,3}{end_move_data,3.000000,5,12345,7,2,1}{end_all,4.000000}";
    auto operations = Replay::decode_operation_stream(text, 6, 2.0);
    assert(operations.size() == 3);

    const auto move = operations.front();
    operations.pop();
    assert(move.kind == Replay::OperationKind::Move);
    assert(std::fabs(move.timestamp.seconds() - 1.25) < 1e-12);
    const auto movement = Replay::parse_movement_event(move);
    assert(movement.pose.position.row == -2);
    assert(movement.pose.position.col == 4);
    assert(Compat::to_legacy(movement.pose.rotation) == 3);

    const auto end = operations.front();
    operations.pop();
    const auto endMovement = Replay::parse_movement_event(end);
    assert(Compat::to_legacy(endMovement.lastRotation) == 5);
    assert(endMovement.score == 12345);
    assert(endMovement.pose.position.row == 7);
    assert(endMovement.pose.position.col == 2);
    assert(Compat::to_legacy(endMovement.pose.rotation) == 1);

    assert(operations.front().kind == Replay::OperationKind::EndAll);
    assert(std::fabs(operations.front().timestamp.seconds() - 2.0) < 1e-12);

    assert(Replay::encode_operation_base(
        "move", Runtime::Duration::from_seconds(1.25)) == "move,1.250000");
}


void replay_gameplay_payloads_hide_argument_positions() {
    for (const long long score : {7000000123LL, std::numeric_limits<long long>::max()}) {
        const auto operation = Replay::decode_operation_fields(
            {"end_move_data", "1", "0", std::to_string(score), "2", "3", "0"});
        assert(operation.payload_as<Replay::EndMoveDataPayload>().score==score);
    }
    const auto addGarbage = Replay::decode_operation_fields({"add_garbage", "1.000000", "4", "2", "7"}, 1.0);
    const auto& garbage = addGarbage.payload_as<Replay::AddGarbagePayload>();
    assert(garbage.height == 4);
    assert(garbage.preserveHole);
    assert(garbage.cell == Runtime::BoardCell::garbage());

    const auto addBuffer = Replay::decode_operation_fields({"add_buffer", "1.000000", "6", "1"}, 1.0);
    const auto& buffer = addBuffer.payload_as<Replay::AddBufferPayload>();
    assert(buffer.attack == 6);
    assert(buffer.instant);
    assert(buffer.resolvedLines==-1 && !buffer.incomingFraction);
    const auto fractionalBuffer=Replay::decode_operation_fields({"add_buffer","1","1","0","0","0.123456789"});
    assert(fractionalBuffer.payload_as<Replay::AddBufferPayload>().incomingFraction==.123456789);

    const auto endPlace = Replay::decode_operation_fields({"end_place_data", "1.000000", "2.5", "7.0", "3", "1.25", "0.75", "1"}, 1.0);
    const auto& quickPlay = endPlace.payload_as<Replay::QuickPlayPlacementPayload>();
    assert(std::fabs(quickPlay.speed - 2.5) < 1e-12);
    assert(std::fabs(quickPlay.altitude - 7.0) < 1e-12);
    assert(quickPlay.floor == 3);
    assert(std::fabs(quickPlay.garbageMultiple - 1.25) < 1e-12);
    assert(std::fabs(quickPlay.garbageInTime.seconds() - 0.75) < 1e-12);
    assert(quickPlay.bagBreather);
}

void local_versus_replay_envelope_preserves_both_streams() {
    const std::string first = "{{Model,6}{mapWidth,10}}{move,1.25,-2,4,0}";
    const std::string second = "{{Model,6}{mapWidth,12}}{add_buffer,1.5,4,0}";
    const std::string encoded = Replay::encode_local_versus_replay(first, second);
    assert(Replay::is_local_versus_replay(encoded));

    Replay::LocalVersusReplayStreams decoded;
    assert(Replay::decode_local_versus_replay(encoded, decoded));
    assert(decoded.first == first);
    assert(decoded.second == second);
    assert(!Replay::decode_local_versus_replay(encoded + "x", decoded));
    assert(!Replay::decode_local_versus_replay(
        "TETR_LOCAL_VERSUS_REPLAY_1:999:1:x", decoded));
    assert(!Replay::is_local_versus_replay(first));
}

void recording_metadata_preserves_gameplay_and_hashes() {
    Runtime::SessionState runtime;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtime,services);
    Replay::ReplaySessionState recording;
    context.attach_replay(recording);
    const std::string events="{start_place,0.125000}{end_all,1.000000}";
    recording.data=Replay::encode_config_prefix(context.config)+"{block_seed,123}{garbage_seed,456}}"+events;
    const auto original=recording.data;
    const std::array<std::uint64_t,4> counts={0,1,37,std::numeric_limits<std::uint64_t>::max()};
    std::array<std::string,4> hashes, saved;
    for (std::size_t index=0;index<counts.size();++index) {
        recording.undoCount=counts[index];
        saved[index]=Replay::recording_for_save(context);
        assert(recording.data==original); // Snapshot offsets and the live stream do not change.
        assert(Replay::recording_for_save(context)==saved[index]);
        Runtime::RuntimeConfig decoded;
        Replay::ConfigPrefixResult prefix;
        assert(Replay::decode_config_prefix(saved[index],decoded,prefix));
        assert(prefix.undoCount==recording.undoCount);
        assert(prefix.blockSeed==123 && prefix.garbageSeed==456);
        assert(saved[index].substr(prefix.operationOffset)==events);
        auto operations=Replay::decode_operation_stream(saved[index],prefix.operationOffset,1);
        assert(operations.size()==2 && operations.front().kind==Replay::OperationKind::StartPlace);
        assert(operations.front().timestamp==Runtime::Duration::from_seconds(0.125));
        operations.pop();
        assert(operations.front().kind==Replay::OperationKind::EndAll);
        hashes[index]=Replay::replay_hash_suffix(saved[index]);
    }
    assert(hashes[0]!=hashes[1]);
    Runtime::RuntimeConfig decoded;
    Replay::ConfigPrefixResult legacy;
    assert(Replay::decode_config_prefix(original,decoded,legacy) && legacy.undoCount==0);
    assert(Replay::decode_config_prefix("{{undo_used,0}}",decoded,legacy) && legacy.undoCount==0);
    assert(Replay::decode_config_prefix("{{undo_used,1}}",decoded,legacy) && legacy.undoCount==1);
    assert(Replay::decode_config_prefix("{{undo_used,1}{undo_count,0}}",decoded,legacy) && legacy.undoCount==0);
    assert(Replay::decode_config_prefix("{{undo_count,37}{undo_used,0}}",decoded,legacy) && legacy.undoCount==37);
    assert(!Replay::decode_config_prefix("{{undo_count,-1}}",decoded,legacy));
    assert(!Replay::decode_config_prefix("{{undo_count,1.5}}",decoded,legacy));
    assert(!Replay::decode_config_prefix("{{undo_count,18446744073709551616}}",decoded,legacy));
    Replay::LocalVersusReplayStreams sides;
    assert(Replay::decode_local_versus_replay(Replay::encode_local_versus_replay(saved[0],saved[0]),sides));
    assert(Replay::decode_config_prefix(sides.first,decoded,legacy) && legacy.undoCount==0);
}

void replay_paths_distinguish_filenames_and_project_references() {
    namespace fs=std::filesystem;
    const fs::path defaultFile=".tetr/replay/demo.txt";
    for(const auto* name:{"demo.txt","replay/demo.txt",".tetr/replay/demo.txt"}) {
        assert(Replay::replay_path_for_loading(name)==defaultFile);
        assert(Replay::replay_path_for_saving(name)==defaultFile);
        assert(Replay::replay_setting_path(name)==defaultFile.generic_u8string());
    }
    assert(Replay::replay_path_for_loading("./replay/demo.txt")==fs::path("replay/demo.txt"));
    assert(Replay::replay_setting_path("./.tetr/replay/nested/demo.txt")==".tetr/replay/nested/demo.txt");
    assert(Replay::replay_setting_path("  'demo.txt'  ")==defaultFile.generic_u8string());
    assert(Replay::replay_path_for_saving("replay/nested/demo.txt")==fs::path(".tetr/replay/nested/demo.txt"));
    assert(Replay::replay_path_for_saving("../demo.txt")==fs::path("../demo.txt"));
    assert(Replay::replay_setting_path("../demo.txt")=="../demo.txt");
    assert(Replay::replay_path_for_saving(".tetr/replay/../demo.txt")==fs::path(".tetr/demo.txt"));
    for(const auto* invalid:{"",".","..",".tetr/replay",".tetr/replay/","replay"}) {
        assert(!Replay::replay_path_for_loading(invalid));
        assert(!Replay::replay_path_for_saving(invalid));
    }
    const auto root=fs::current_path();
    // Absolute input selects its actual location. Stored references must round
    // trip without being accidentally rooted under the default replay folder.
    for(const auto& absolute:{root/defaultFile,root/"exports"/fs::u8path("中文 replay.txt"),
            root/"replay/explicit.txt",root/"root replay.txt",root.parent_path()/"external replay.txt"}) {
        assert(Replay::replay_path_for_loading(absolute.u8string())==absolute);
        assert(Replay::replay_path_for_saving(absolute.u8string())==absolute);
        const auto reference=Replay::replay_setting_path("\""+absolute.u8string()+"\"");
        assert(!fs::u8path(reference).is_absolute());
        assert(Replay::replay_setting_path(reference)==reference);
        const auto loaded=Replay::replay_path_for_loading(reference);assert(loaded);
        assert(fs::absolute(*loaded).lexically_normal()==absolute);
    }
#ifdef _WIN32
    assert(Replay::replay_path_for_saving("C:\\demo.txt")==fs::path("C:\\demo.txt"));
    assert(!Replay::replay_path_for_loading("C:demo.txt"));
    assert(Replay::replay_path_for_loading("\\\\server\\share\\demo.txt")==fs::path("\\\\server\\share\\demo.txt"));
    const fs::path unc=R"(\\server\share\demo.txt)";
    assert(Replay::replay_path_for_saving(R"(\\server\share\nested\..\demo.txt)")==unc);
    assert(Replay::replay_setting_path(unc.u8string())=="//server/share/demo.txt");
    assert(Replay::replay_path_for_loading(Replay::replay_setting_path(unc.u8string()))==unc);
    assert(!Replay::replay_path_for_loading(R"(\\server\share)"));
    std::error_code uncError;
    assert(Util::FilePath::absolute(unc,uncError)==unc && !uncError);
    assert(Util::FilePath::relative(unc,fs::path(R"(\\SERVER\SHARE)"))==fs::path("demo.txt"));
    assert(Util::FilePath::relative(unc,fs::path(R"(\\other\share)")).empty());
    assert(!Replay::replay_path_for_loading("\\demo.txt"));
    assert(Replay::replay_path_for_saving("..\\demo.txt")==fs::path("../demo.txt"));
    assert(Replay::replay_setting_path("replay\\demo.txt")==".tetr/replay/demo.txt");
    assert(Replay::replay_setting_path(".tetr\\replay\\demo.txt")==".tetr/replay/demo.txt");
    const auto otherDrive=root.root_name()==fs::path("C:")?fs::path("D:\\demo.txt"):fs::path("C:\\demo.txt");
    assert(Replay::replay_setting_path(otherDrive.u8string())==otherDrive.generic_u8string());
#endif
}

void replay_clock_pauses_and_changes_speed_without_jumping() {
    Runtime::SessionState runtimeSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtimeSession,services);
    Replay::ReplaySessionState replaySession;
    context.attach_replay(replaySession);

    Replay::configure_playback(
        context,2.0,Runtime::Duration::from_seconds(20.0));
    Replay::start_playback(context,Runtime::TimePoint::from_seconds(100.0));
    assert(Replay::playback_elapsed(
        context,Runtime::TimePoint::from_seconds(101.0)).seconds() == 2.0);
    assert(Replay::consume_playback_frame_delta(
        context,Runtime::TimePoint::from_seconds(101.0)).seconds() == 2.0);
    assert(Replay::playback_qp_gravity_due(
        context,
        Runtime::TimePoint::from_seconds(101.0),
        Runtime::Duration::from_seconds(1.0)));

    Replay::toggle_playback_pause(context,Runtime::TimePoint::from_seconds(101.0));
    assert(Replay::playback_elapsed(
        context,Runtime::TimePoint::from_seconds(105.0)).seconds() == 2.0);
    assert(Replay::consume_playback_frame_delta(
        context,Runtime::TimePoint::from_seconds(105.0)).seconds() == 0.0);
    assert(!Replay::playback_qp_gravity_due(
        context,
        Runtime::TimePoint::from_seconds(105.0),
        Runtime::Duration::from_seconds(1.0)));
    Replay::set_playback_speed(context,Runtime::TimePoint::from_seconds(105.0),4.0);
    Replay::toggle_playback_pause(context,Runtime::TimePoint::from_seconds(105.0));
    assert(Replay::playback_elapsed(
        context,Runtime::TimePoint::from_seconds(106.0)).seconds() == 6.0);

    Replay::set_playback_speed(
        context,Runtime::TimePoint::from_seconds(106.0),32.0);
    assert(Replay::playback_speed(context)==32.0);
    Replay::set_playback_speed(
        context,Runtime::TimePoint::from_seconds(106.0),0.03125);
    assert(Replay::playback_speed(context)==0.03125);

    Replay::configure_playback(
        context,1.0,Runtime::Duration::from_seconds(20.0));
    Replay::prepare_playback_resume(
        context,Runtime::Duration::from_seconds(12.0),2.0,true);
    assert(Replay::playback_resuming(context));
    Replay::start_playback(context,Runtime::TimePoint::from_seconds(200.0));
    assert(Replay::playback_paused(context));
    assert(Replay::playback_speed(context)==2.0);
    assert(Replay::playback_elapsed(
        context,Runtime::TimePoint::from_seconds(210.0)).seconds()==12.0);
}

void replay_progress_renders_timeline_and_controls_below_the_board() {
    Runtime::SessionState runtimeSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtimeSession,services);
    Replay::ReplaySessionState replaySession;
    Input::Services input;
    context.attach_replay(replaySession);
    context.attach_input(input);
    context.config.mode.replayMode=true;
    auto& player=context.session.player;
    player.gameplay.playfield.width=10;
    player.gameplay.playfield.visibleHeight=20;
    player.gameplay.playfield.nextCount=5;
    player.presentation.margin=30;

    Replay::configure_playback(
        context,1.0,Runtime::Duration::from_seconds(20.0));
    // Simulate a high-speed replay whose scheduling clock has reached 15s
    // while terminal/gameplay processing has only applied operations to 3.25s.
    replaySession.playback.elapsed=Runtime::Duration::from_seconds(15.0);
    replaySession.playback.paused=true;
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(3.25);
    replaySession.operations.push({
        Replay::OperationKind::EndMove,
        Runtime::Duration::from_seconds(4.0),{}});
    replaySession.operations.push({
        Replay::OperationKind::EndAll,
        Runtime::Duration::from_seconds(20.0),{}});

    const auto progressStart=Replay::playback_progress_elapsed(
        context,Runtime::TimePoint::from_seconds(100.0));
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(3.75);
    const auto progressAfterAppliedWork=Replay::playback_progress_elapsed(
        context,Runtime::TimePoint::from_seconds(100.1));
    const auto progressWhileWaiting=Replay::playback_progress_elapsed(
        context,Runtime::TimePoint::from_seconds(100.15));
    assert(progressStart.seconds()==3.25);
    assert(progressAfterAppliedWork>progressStart);
    assert(progressAfterAppliedWork.seconds()==3.75);
    assert(progressWhileWaiting==progressAfterAppliedWork);

    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(3.25);
    replaySession.playback.progressElapsed={};
    std::ostringstream rendered;
    std::streambuf* original=std::cout.rdbuf(rendered.rdbuf());
    Engine::render_replay_playback_progress(context,player);
    assert(Replay::playback_presented_elapsed(context).seconds()==3.25);
    assert(rendered.str().find("00:03.250")!=std::string::npos);
    assert(rendered.str().find("00:15.000")==std::string::npos);

    rendered.str({});
    rendered.clear();
    replaySession.operations={};
    replaySession.playback.elapsed=Runtime::Duration::from_seconds(20.0);
    Engine::render_replay_playback_progress(context,player);
    assert(rendered.str().find("00:20.000")!=std::string::npos);
    assert(rendered.str().find(
        "["+std::string(19,'=')+"|]")!=std::string::npos);

    replaySession.lastConsumedTimestamp={};
    Replay::start_playback(context,context.now());
    Engine::update_replay_playback_controls(context,player,true);
    Replay::toggle_playback_pause(context,context.now());
    Engine::update_replay_playback_controls(context,player,true);
    std::cout.rdbuf(original);

    assert(rendered.str().find("00:00.000")!=std::string::npos);
    assert(rendered.str().find("PLAY")!=std::string::npos);
    assert(rendered.str().find("PAUSE")!=std::string::npos);
    assert(rendered.str().find("Space: Pause")!=std::string::npos);
    assert(rendered.str().find("Space: Play")!=std::string::npos);
    assert(rendered.str().find("-/+: Speed")!=std::string::npos);
    assert(rendered.str().find("Left/Right: -/+10s")!=std::string::npos);

    rendered.str({});
    rendered.clear();
    replaySession.playback.progressVisible=false;
    original=std::cout.rdbuf(rendered.rdbuf());
    Engine::render_replay_playback_progress(context,player);
    std::cout.rdbuf(original);
    assert(rendered.str().empty());
}

void high_speed_progress_and_controls_follow_the_applied_board() {
    using Runtime::Duration;
    using Runtime::TimePoint;
    Runtime::SessionState runtimeSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtimeSession,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    Replay::configure_playback(context,1024,Duration::from_seconds(400));
    for (double seconds : {0.5,1.0,50.0,60.0,400.0})
        replay.operations.push({Replay::OperationKind::StartPlace,Duration::from_seconds(seconds)});
    Replay::start_playback(context,TimePoint::from_seconds(100));
    const auto sample=[&](double now) {
        return Replay::playback_progress_elapsed(context,TimePoint::from_seconds(now));
    };
    const auto apply=[&]() {
        Replay::mark_operation_consumed(context,replay.operations.front());
        replay.operations.pop();
    };

    // Smooth idle time before the first event must not keep racing ahead once
    // an expensive terminal frame leaves due events waiting behind it.
    const auto quietTime=sample(100.0002);
    assert(quietTime>Duration{});
    assert(sample(100.1)==quietTime);
    apply();apply();
    assert(sample(100.1)==Duration::from_seconds(1));
    assert(sample(100.2)==Duration::from_seconds(1));
    // A burst applied at the same wall-clock instant updates immediately;
    // there is no extrapolated UI clock left lagging behind the board.
    apply();
    assert(sample(100.2)==Duration::from_seconds(50));

    Replay::set_playback_speed(context,TimePoint::from_seconds(100.2),1);
    assert(Replay::playback_elapsed(context,TimePoint::from_seconds(100.2)).seconds()==50);
    assert(!Replay::event_due(replay.operations.front(),
        Replay::playback_elapsed(context,TimePoint::from_seconds(101.2))));
    Replay::set_playback_speed(context,TimePoint::from_seconds(100.2),1024);
    Replay::toggle_playback_pause(context,TimePoint::from_seconds(100.3));
    assert(Replay::playback_paused(context));
    assert(sample(110.3).seconds()==50);
    assert(!Replay::event_due(replay.operations.front(),
        Replay::playback_elapsed(context,TimePoint::from_seconds(110.3))));
    Replay::toggle_playback_pause(context,TimePoint::from_seconds(110.3));
    assert(std::fabs(sample(110.301).seconds()-51.024)<1e-9);

    Replay::prepare_playback_resume(context,Duration::from_seconds(75),2048,true,
        Duration::from_seconds(50));
    Replay::start_playback(context,TimePoint::from_seconds(200));
    assert(sample(200).seconds()==50);
    apply(); // The 60s event completes the reconstruction to the 75s target.
    Replay::complete_playback_resume(context,TimePoint::from_seconds(201));
    assert(Replay::playback_paused(context));
    assert(sample(210).seconds()==75);
    assert(Replay::playback_speed(context)==1024);
    apply(); // End of recording: the final frame fills the entire timeline.
    replay.playback.elapsed=Duration::from_seconds(400);
    assert(sample(210).seconds()==400);
}

void replay_speed_is_bounded_for_start_resume_and_controls() {
    Runtime::SessionState runtimeSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtimeSession,services);
    Replay::ReplaySessionState replay;
    context.attach_replay(replay);
    Replay::configure_playback(context,4096,Runtime::Duration::from_seconds(20));
    assert(Replay::playback_speed(context)==1024);
    Replay::configure_playback(context,1.0/4096.0,Runtime::Duration::from_seconds(20));
    assert(Replay::playback_speed(context)==1.0/1024.0);
    Replay::prepare_playback_resume(context,{},1.0/4096.0,true);
    assert(Replay::playback_speed(context)==1.0/1024.0);
    Replay::start_playback(context,Runtime::TimePoint::from_seconds(100));
    for (int i=0;i<24;++i)
        Replay::set_playback_speed(context,Runtime::TimePoint::from_seconds(100),
            Replay::playback_speed(context)*2);
    assert(Replay::playback_speed(context)==1024);
    assert(context.config.timeMultiplier==1024);
    for (int i=0;i<24;++i)
        Replay::set_playback_speed(context,Runtime::TimePoint::from_seconds(100),
            Replay::playback_speed(context)/2);
    assert(Replay::playback_speed(context)==1.0/1024.0);
    assert(context.config.timeMultiplier==1.0/1024.0);
    // Local-versus event streams have different consumed markers but share
    // one recorded wall timeline; changing speed must keep that timeline.
    replay.localVersusReplay=true;
    replay.playback.elapsed=Runtime::Duration::from_seconds(15);
    replay.lastConsumedTimestamp=Runtime::Duration::from_seconds(3);
    replay.operations.push({Replay::OperationKind::StartPlace,Runtime::Duration::from_seconds(4)});
    Replay::set_playback_speed(context,Runtime::TimePoint::from_seconds(100),2);
    assert(Replay::playback_elapsed(context,Runtime::TimePoint::from_seconds(100)).seconds()==15);
}

void replay_completion_fills_the_timeline_with_trailing_markers() {
    using Runtime::Duration;
    using Runtime::TimePoint;
    for (double duration : {0.0,20.0}) for (double speed : {1.0/1024.0,1.0,1024.0}) {
        Runtime::SessionState runtimeSession;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(runtimeSession,services);
        Replay::ReplaySessionState replay;
        context.attach_replay(replay);
        context.config.mode.replayMode=true;
        auto& player=runtimeSession.player;
        player.gameplay.playfield.width=10;
        player.gameplay.playfield.visibleHeight=20;
        player.presentation.margin=30;
        Replay::configure_playback(context,speed,Duration::from_seconds(duration));
        Replay::start_playback(context,TimePoint::from_seconds(100));
        replay.playback.elapsed=Duration::from_seconds(duration);
        replay.playback.paused=true;
        replay.lastConsumedTimestamp=Duration::from_seconds(duration/2);
        replay.operations.push({Replay::OperationKind::EndAll,Duration::from_seconds(duration)});
        assert(Replay::playback_progress_elapsed(context,TimePoint::from_seconds(100)).seconds()==duration/2);

        // Mode completion can leave a due EndAll marker queued. The final UI
        // must still reach the end, including zero-length recordings.
        Replay::finish_playback(context,TimePoint::from_seconds(100));
        std::ostringstream rendered;
        auto* original=std::cout.rdbuf(rendered.rdbuf());
        Engine::render_replay_playback_progress(context,player);
        std::cout.rdbuf(original);
        const std::string time=duration==0 ? "00:00.000" : "00:20.000";
        assert(rendered.str().find(time)!=std::string::npos);
        assert(rendered.str().find("["+std::string(19,'=')+"|]")!=std::string::npos);
        assert(Replay::playback_elapsed(context,TimePoint::from_seconds(1000)).seconds()==duration);
        assert(replay.operations.size()==1 && replay.lastConsumedTimestamp.seconds()==duration/2);

        if (duration>0) {
            Replay::prepare_playback_resume(context,Duration::from_seconds(10),speed,true);
            Replay::start_playback(context,TimePoint::from_seconds(200));
            Replay::complete_playback_resume(context,TimePoint::from_seconds(200));
            assert(!Replay::playback_finished(context));
            assert(Replay::playback_progress_elapsed(context,TimePoint::from_seconds(200)).seconds()==10);
        }
    }
}

void replay_undo_count_has_its_own_footer_row() {
    for (auto mode:{Runtime::GameMode::Zen,Runtime::GameMode::Racing,
        Runtime::GameMode::Blitz,Runtime::GameMode::QuickPlay})
        for (bool obscured:{false,true}) for (int layout=0;layout<3;++layout) {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        Replay::ReplaySessionState replay;
        context.attach_replay(replay);
        context.config.mode.mode=mode;
        context.config.mode.replayMode=true;
        context.config.disableUndo=true; // Recorded usage remains visible when Undo is disabled.
        context.config.display.obscuredColors=obscured;
        if (mode==Runtime::GameMode::QuickPlay) {
            context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
            context.config.quickPlay.snowball=Runtime::ModifierMode::Enabled;
            session.quickPlay.altitude=98765.4;
            session.quickPlay.speed=10;
            session.quickPlay.progress.windupUntil=1e20;
        }
        auto& player=session.player;
        auto& field=player.gameplay.playfield;
        field.width=layout==0 ? 4 : 10;
        field.visibleHeight=field.activeHeight=layout==0 ? 4 : layout==1 ? 20 : 32;
        field.nextCount=layout==0 ? 0 : layout==1 ? 5 : 9;
        field.field.initialize(field.width,field.visibleHeight);
        player.presentation.margin=30;
        player.presentation.blockMaxHeight=layout==0 ? 2 : 4;
        player.gameplay.quickPlay.blighted=true;
        Replay::configure_playback(context,1,Runtime::Duration::from_seconds(60));
        replay.undoCount=37;
        Test::TerminalScreen screen;
        std::ostringstream output;
        auto* original=std::cout.rdbuf(output.rdbuf());
        Render::redraw_player_board(context,player);
        screen.feed(output.str());
        const auto board=screen;
        output.str({});
        Engine::render_replay_playback_progress(context,player);
        screen.feed(output.str());
        const auto countAt=screen.position_of("UNDO: 37");
        const auto controlsAt=screen.position_of("Left/Right: -/+10s");
        const auto textLayout=Render::board_text_layout(context,player);
        const auto progressAt=screen.position_of("[");
        assert(countAt.first>field.visibleHeight+player.presentation.topMargin+1 && countAt.first==controlsAt.first+1);
        assert(countAt.second==textLayout.column+1);
        const Test::TerminalScreen::Color expected=obscured
            ? Test::TerminalScreen::Color{{54,57,62}} : Test::TerminalScreen::Color{{255,255,255}};
        assert(screen.at(countAt.first,countAt.second).foreground==expected);
        for (int row=1;row<=board.rows();++row) for (int column=1;column<=board.columns();++column) {
            const auto before=board.at(row,column), after=screen.at(row,column);
            if (row>=progressAt.first && column>textLayout.column && column<=textLayout.column+textLayout.columns) {
                if (before.glyph!=" ") std::cerr<<"Replay footer overlap: mode "<<static_cast<int>(mode)
                    <<", layout "<<layout<<", cell "<<row<<","<<column<<", glyph ["<<before.glyph
                    <<"], progress row "<<progressAt.first<<"\n";
                assert(before.glyph==" "); // Compact text uses the empty area beside long NEXT panels.
            } else assert(before.glyph==after.glyph && before.background==after.background);
        }
        output.str({});
        replay.undoCount=0;
        Engine::render_replay_playback_progress(context,player);
        screen.feed(output.str());
        assert(screen.position_of("UNDO: 0")==countAt);
        assert(screen.position_of("UNDO: 37")==std::make_pair(0,0));
        assert(screen.at(countAt.first,countAt.second+7).glyph==" ");
        // Repositioning the panel must erase the former count before rendering it below the grown board.
        output.str({});
        field.visibleHeight+=8; field.activeHeight+=8;
        Engine::render_replay_playback_progress(context,player);
        screen.feed(output.str());
        // Statistics can keep the footer at the same position on small fields.
        if (screen.position_of("UNDO: 0")!=countAt)
            assert(screen.at(countAt.first,countAt.second).glyph==" ");
        for (int battle=0;battle<3;++battle) {
            output.str({});
            context.config.mode.mode=battle==0 ? Runtime::GameMode::Multiplayer : mode;
            context.config.garbage.mode=battle==1 ? Runtime::GarbageMode::Multiplayer : Runtime::GarbageMode::None;
            replay.localVersusReplay=battle==2;
            Engine::render_replay_playback_progress(context,player);
            screen.feed(output.str());
            assert(output.str().find("UNDO:")==std::string::npos);
            assert(screen.position_of("UNDO: 0")==std::make_pair(0,0));
        }
        std::cout.rdbuf(original);
    }
}

void replay_interface_follows_language_and_cell_width() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    Replay::ReplaySessionState replay;
    UserConfig::Settings user;
    context.attach_replay(replay); context.attach_user_settings(user);
    context.config.mode.mode=Runtime::GameMode::Zen;
    context.config.mode.replayMode=true;
    auto& p=session.player;
    p.gameplay.playfield.width=10;
    p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
    p.gameplay.playfield.field.initialize(10,20);
    p.presentation.margin=2;
    p.presentation.blockMaxHeight=p.presentation.blockMaxWidth=4;
    Replay::configure_playback(context,1,Runtime::Duration::from_seconds(60));
    replay.undoCount=37; replay.pauseCount=5;
    std::ostringstream output;
    auto* original=std::cout.rdbuf(output.rdbuf());
    Test::TerminalScreen screen;
    user.uiLanguage=1;
    Engine::render_replay_playback_progress(context,p); screen.feed(output.str()); output.str("");
    assert(screen.position_of("Left/Right: -/+10s").first!=0);
    user.uiLanguage=0;
    Engine::render_replay_playback_progress(context,p); screen.feed(output.str()); output.str("");
    assert(screen.position_of("播放中").first!=0 && screen.position_of("倍速：").first!=0);
    assert(screen.position_of("暂停键/空格：暂停").first!=0);
    assert(screen.position_of("←/→：±10秒").first!=0);
    assert(screen.position_of("撤回：37").first!=0 && screen.position_of("暂停：5").first!=0);
    assert(screen.position_of("Left/Right: -/+10s").first==0 && screen.position_of("UNDO: 37").first==0);
    const auto required=Engine::replay_playback_terminal_size(context,p);
    const int textWidth=static_cast<int>(replay.playback.lastProgressWidth);
    assert(required.columns==Render::board_text_layout(context,p).column+textWidth && textWidth<=44);
    assert(p.presentation.undoCountDisplay.width==Util::TerminalText::width("撤回：37  暂停：5"));
    replay.playback.paused=true;
    Engine::render_replay_playback_progress(context,p); screen.feed(output.str()); output.str("");
    assert(screen.position_of("已暂停").first!=0 && screen.position_of("暂停键/空格：继续").first!=0);
    assert(screen.position_of("播放中").first==0);
    user.uiLanguage=1;
    Engine::render_replay_playback_progress(context,p); screen.feed(output.str());
    assert(screen.position_of("PAUSE").first!=0 && screen.position_of("Space: Play").first!=0);
    assert(screen.position_of("撤回：37").first==0 && screen.position_of("已暂停").first==0);
    std::cout.rdbuf(original);
}

void replay_rebuild_hides_intermediate_terminal_frames() {
    Runtime::SessionState runtimeSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtimeSession,services);
    Replay::ReplaySessionState replaySession;
    Input::Services input;
    App::ApplicationState app;
    context.attach_replay(replaySession);
    context.attach_input(input);
    context.attach_app(app);
    context.attach_output_mutex(app.terminalOutputMutex);
    app.primaryContext=&context;
    context.config.mode.replayMode=true;

    auto& player=context.session.player;
    player.gameplay.playfield.width=4;
    player.gameplay.playfield.visibleHeight=4;
    player.gameplay.playfield.activeHeight=4;
    player.gameplay.playfield.field.initialize(4,4);
    player.presentation.margin=10;

    Replay::configure_playback(
        context,1.0,Runtime::Duration::from_seconds(20.0));
    Replay::prepare_playback_resume(
        context,Runtime::Duration::from_seconds(10.0),1.0,false);
    Replay::start_playback(context,context.now());

    std::ostringstream rendered;
    std::streambuf* original=std::cout.rdbuf(rendered.rdbuf());
    Render::begin_output_suppression();
    std::cout<<"INTERMEDIATE_REPLAY_FRAME";
    Engine::update_replay_playback_controls(context,player,true);
    std::cout.rdbuf(original);

    assert(!Render::output_suppressed());
    assert(rendered.str().find("INTERMEDIATE_REPLAY_FRAME")==std::string::npos);
    assert(!Replay::playback_rebuilding(context));
    assert(!Replay::playback_paused(context));
    assert(Replay::playback_elapsed(context,context.now()).seconds()>=10.0);

    // A racing/blitz goal can terminate before a due EndAll is consumed. The
    // terminal-state hook must still end silent reconstruction instead of
    // leaving the final screen apparently frozen.
    Replay::configure_playback(
        context,1.0,Runtime::Duration::from_seconds(20.0));
    Replay::prepare_playback_resume(
        context,Runtime::Duration::from_seconds(20.0),1.0,false);
    Replay::start_playback(context,context.now());
    replaySession.operations.push({
        Replay::OperationKind::EndAll,
        Runtime::Duration::from_seconds(20.0),{}});
    Render::begin_output_suppression();
    Engine::update_replay_playback_controls(context,player,true);
    assert(Render::output_suppressed());
    assert(Replay::playback_rebuilding(context));
    Engine::complete_replay_rebuild_at_game_end(context);
    assert(!Render::output_suppressed());
    assert(!Replay::playback_rebuilding(context));
}

void replay_checkpoints_restore_state_and_operation_position() {
    Runtime::SessionState runtimeSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtimeSession,services);
    Replay::ReplaySessionState replaySession;
    context.attach_replay(replaySession);
    context.config.mode.replayMode=true;

    Replay::configure_playback_checkpoint_cache(context,"replay-a");
    Replay::configure_playback(
        context,1.0,Runtime::Duration::from_seconds(60.0));
    Replay::start_playback(context,context.now());
    replaySession.operations.push({
        Replay::OperationKind::StartPlace,
        Runtime::Duration::from_seconds(1.0),{}});
    replaySession.operations.push({
        Replay::OperationKind::EndMove,
        Runtime::Duration::from_seconds(11.0),{}});
    replaySession.operations.push({
        Replay::OperationKind::EndAll,
        Runtime::Duration::from_seconds(20.0),{}});
    Replay::capture_playback_checkpoint(context);

    replaySession.operations.pop();
    runtimeSession.player.gameplay.scoring.score=4242;
    // The high-speed scheduling clock may be much farther ahead than the
    // between-piece state. The checkpoint must retain the applied 12s time.
    replaySession.playback.elapsed=Runtime::Duration::from_seconds(30.0);
    replaySession.playback.paused=true;
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(12.0);
    Replay::capture_playback_checkpoint(context);
    assert(Replay::playback_checkpoint_count(context)==2);

    // Silent forward reconstruction also leaves interval checkpoints behind,
    // so repeated Right presses do not keep replaying the same prefix.
    replaySession.playback.rebuilding=true;
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(25.0);
    Replay::capture_playback_checkpoint(context);
    replaySession.playback.rebuilding=false;
    assert(Replay::playback_checkpoint_count(context)==3);

    const Runtime::Duration base=Replay::schedule_playback_checkpoint_restore(
        context,Runtime::Duration::from_seconds(18.0));
    assert(base.seconds()==12.0);
    runtimeSession.player.gameplay.scoring.score=0;
    replaySession.operations={};
    replaySession.operations.push({
        Replay::OperationKind::StartPlace,
        Runtime::Duration::from_seconds(1.0),{}});
    replaySession.operations.push({
        Replay::OperationKind::EndMove,
        Runtime::Duration::from_seconds(11.0),{}});
    replaySession.operations.push({
        Replay::OperationKind::EndAll,
        Runtime::Duration::from_seconds(20.0),{}});
    assert(Replay::restore_scheduled_playback_checkpoint(context));
    assert(runtimeSession.player.gameplay.scoring.score==4242);
    assert(replaySession.operations.size()==2);
    assert(replaySession.operations.front().kind==Replay::OperationKind::EndMove);

    Replay::configure_playback_checkpoint_cache(context,"replay-b");
    assert(Replay::playback_checkpoint_count(context)==0);
}

void replay_spike_uses_applied_operation_time_during_seek() {
    Runtime::SessionState runtimeSession;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(runtimeSession,services);
    Replay::ReplaySessionState replaySession;
    context.attach_replay(replaySession);
    context.config.mode.replayMode=true;
    auto& player=runtimeSession.player;
    player.presentation.margin=30;

    Replay::configure_playback(
        context,1.0,Runtime::Duration::from_seconds(20.0));
    replaySession.playback.elapsed=Runtime::Duration::from_seconds(10.0);
    replaySession.playback.paused=true;
    replaySession.playback.rebuilding=true;

    std::ostringstream rendered;
    std::streambuf* original=std::cout.rdbuf(rendered.rdbuf());
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(1.0);
    Engine::present_spike_attack(context,player,4);
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(1.5);
    Engine::present_spike_attack(context,player,4);
    assert(player.presentation.spike.attack==8);

    // Although the seek target remains 10s, this attack occurred at 3s and
    // must start a new spike instead of joining the 1.0-1.5s burst.
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(3.0);
    Engine::present_spike_attack(context,player,8);
    assert(player.presentation.spike.attack==8);
    assert(player.presentation.spike.lastAttackAt==
        Runtime::TimePoint::from_seconds(3.0));

    // While silent reconstruction still has due work, expiration also follows
    // applied time. Once caught up, it may use the normal scheduled timeline.
    replaySession.operations.push({
        Replay::OperationKind::EndMove,
        Runtime::Duration::from_seconds(4.0),{}});
    replaySession.lastConsumedTimestamp=Runtime::Duration::from_seconds(3.5);
    Engine::refresh_spike_display(context,player);
    assert(player.presentation.spike.attack==8);
    replaySession.operations={};
    replaySession.playback.rebuilding=false;
    Engine::refresh_spike_display(context,player);
    std::cout.rdbuf(original);
    assert(player.presentation.spike.attack==0);
    assert(!player.presentation.spike.lastAttackAt);
}

void network_attack_wire_order_is_explicit() {
    const Network::OutgoingAttackFrame outgoing{Runtime::MultiplayerSequenceId::from_wire(17), Runtime::MultiplayerSequenceId::from_wire(9), 6};
    const std::string wire = Network::encode_attack_frame(outgoing);
    assert(wire == "17;9;6");

    const auto incoming = Network::decode_frame_packet(wire, 10, 20);
    assert(incoming);
    assert(incoming->type == Network::FramePacketType::Attack);
    // First wire field is the sender's sequence, therefore it becomes the
    // receiver's receiveId. The second field acknowledges our sent sequence.
    assert(incoming->receiveId.value() == 17);
    assert(incoming->sendId.value() == 9);
    assert(incoming->attack == 6);
}

void garbage_packet_has_typed_api() {
    const auto packet = Rules::make_garbage_packet(
        4,
        1.75,
        Runtime::TimePoint::from_seconds(12.5),
        true,
        Runtime::Duration::from_seconds(5.0));
    assert(packet.line_count() == 7);
    assert(packet.arrival_time().seconds() == 6.5);
    assert(packet.arrival_time() == Runtime::TimePoint::from_seconds(6.5));

    std::deque<Rules::GarbagePacket> queue{packet};
    const auto notReady=Rules::consume_ready_garbage(
        queue,
        Runtime::TimePoint::from_seconds(11.49),
        Runtime::Duration::from_seconds(5.0));
    assert(notReady.chunks.empty());
    const auto ready=Rules::consume_ready_garbage(
        queue,
        Runtime::TimePoint::from_seconds(11.5),
        Runtime::Duration::from_seconds(5.0));
    assert(ready.chunks.size()==1&&ready.chunks.front().lines==7);
}

} // namespace

int run(int argc,char** argv) {
    replay_is_decoded_once();
    replay_gameplay_payloads_hide_argument_positions();
    local_versus_replay_envelope_preserves_both_streams();
    recording_metadata_preserves_gameplay_and_hashes();
    replay_paths_distinguish_filenames_and_project_references();
    replay_clock_pauses_and_changes_speed_without_jumping();
    replay_progress_renders_timeline_and_controls_below_the_board();
    high_speed_progress_and_controls_follow_the_applied_board();
    replay_speed_is_bounded_for_start_resume_and_controls();
    replay_completion_fills_the_timeline_with_trailing_markers();
    replay_undo_count_has_its_own_footer_row();
    replay_interface_follows_language_and_cell_width();
    replay_rebuild_hides_intermediate_terminal_frames();
    replay_checkpoints_restore_state_and_operation_position();
    replay_spike_uses_applied_operation_time_during_seek();
    network_attack_wire_order_is_explicit();
    garbage_packet_has_typed_api();
    std::cout << "serialization boundary tests passed\n";
    return 0;
}
} // namespace TetrTests::serialization_boundary_test
