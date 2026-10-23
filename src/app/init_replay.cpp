#include "tetr/util/file_path.h"
#include "init_replay.h"
#include "challenge_replay_analysis.h"
#include "tetr/app/init.h"
#include "tetr/app/application_state.h"
#include "tetr/replay/session_state.h"

#include "tetr/engine/autoplay_adapter.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/challenge_prefix.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/operation_codec.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/replay_hash.h"
#include "tetr/replay/local_versus_codec.h"
#include "tetr/replay/multiplayer_codec.h"
#include "tetr/replay/turn_timeline.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/map_store.h"
#include "tetr/network/network_services.h"
#include <stdexcept>
#include "tetr/runtime/constants.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <utility>
#include <sstream>
#include <iterator>

namespace App::InitDetail {
namespace {

std::optional<std::filesystem::path> session_replay_path(const UserConfig::Settings& user) {
    // Challenge records already contain project-relative references, including
    // files outside the default directory. Old absolute references still work.
    if(user.challengeReplay) {
        if(user.replayPath.empty())return std::nullopt;
        return std::filesystem::u8path(user.replayPath);
    }
    return Replay::replay_path_for_loading(user.replayPath);
}

bool replay_hash_valid(const std::string& replay) {
    if (replay.size() <= 17) return false;
    const std::size_t dataLength = replay.size() - 17;
    return Replay::replay_hash_suffix(replay.substr(0, dataLength)) == replay.substr(dataLength);
}

void reset_replay_state(Runtime::GameContext& context) {
    auto& state = Replay::session(context);
    state.data.clear();
    state.recordObserver={};state.recordingLimit=10000000;state.onlineRecording=false;state.multiplayerReplay.reset();
    state.onlineIdentity={};state.onlineProfile=0;
    state.loadedRecording.clear();state.sourcePath.clear();state.recentPath.clear();state.challengeRecords.clear();
    state.configPrefix.clear();
    state.undoCount = 0;
    state.pauseCount = 0;
    state.resultGameTime.reset();state.resultRealTime.reset();state.recordedTimeMultiplier=1;
    state.operations = {};
    state.lastConsumedTimestamp = {};
    state.lastGameTimestamp = {};
    state.separateGameTime = false;
    state.gameClockPaused = false;
    state.recordingStart.reset();
    state.turnPlayback.reset();
    state.localVersusReplay = false;
    state.challengePrefixActive = false;
    state.playback = {};
    state.pendingCheckpointRestore.reset();
}

bool load_replay_stream(
    Runtime::GameContext& context,
    std::string replay) {
    reset_replay_state(context);
    Replay::session(context).data = std::move(replay);
    Replay::configure_playback_checkpoint_cache(
        context,Replay::replay_hash_suffix(Replay::session(context).data));

    Runtime::RuntimeConfig replayConfig;
    replayConfig.mode.mode = Runtime::GameMode::Replay;
    replayConfig.mode.replayMode = true;
    replayConfig.timeMultiplier = 1.0;
    // Display preferences are local UI choices rather than replay gameplay
    // state, so keep the current user's settings while decoding the recorded
    // rules. They are intentionally not part of the replay config prefix.
    replayConfig.display = Runtime::normalize_user_config(context.user_settings(),Runtime::GameMode::Replay).display;

    Replay::ConfigPrefixResult prefix;
    if (!Replay::decode_config_prefix(Replay::session(context).data, replayConfig, prefix)) {
        reset_replay_state(context);
        return false;
    }
    replayConfig.mode.replayMode = true;
    // The viewer may disable Undo even when an older recording allowed it.
    replayConfig.disableUndo = replayConfig.disableUndo || UserConfig::clamped_int(
        context.user_settings(), UserConfig::SettingId::DisableUndo) != 0;
    replayConfig.disablePause = replayConfig.disablePause || UserConfig::clamped_int(
        context.user_settings(), UserConfig::SettingId::DisablePause) != 0;
    context.config = replayConfig;
    Replay::session(context).undoCount = prefix.undoCount;
    Replay::session(context).pauseCount = prefix.pauseCount;
    Replay::session(context).resultGameTime=prefix.resultGameTime;
    Replay::session(context).resultRealTime=prefix.resultRealTime;
    Replay::session(context).recordedTimeMultiplier=replayConfig.timeMultiplier;
    if (prefix.blockSeed) context.session.player.pieces.random.seed(*prefix.blockSeed);
    if (prefix.garbageSeed) {
        context.session.player.garbage.random.seed(*prefix.garbageSeed);
        context.session.player.attackRoundingRandom.seed(
            *prefix.garbageSeed ^ Runtime::kAttackRoundingSeedSalt);
    }
    Replay::session(context).operations = Replay::decode_operation_stream(
        Replay::session(context).data, prefix.operationOffset, 1.0);
    Runtime::Duration replayDuration{};
    auto remaining=Replay::session(context).operations;
    while (!remaining.empty()) {
        replayDuration=std::max(replayDuration,remaining.front().timestamp);
        if (remaining.front().gameTimestamp) Replay::session(context).separateGameTime = true;
        remaining.pop();
    }
    Replay::configure_playback(
        context,replayConfig.timeMultiplier,replayDuration);
    return true;
}

void apply_replay_resume_request(
    Runtime::GameContext& context,
    Runtime::GameContext* secondContext) {
    auto* app=context.app_component();
    if (!app || !app->replayResume.pending) return;

    const App::ReplayResumeRequest request=app->replayResume;
    app->replayResume.pending=false;
    const Runtime::Duration target=Runtime::Duration::from_seconds(
        std::max(0.0,request.targetSeconds));
    const Runtime::Duration firstBase=
        Replay::schedule_playback_checkpoint_restore(context,target);
    Replay::prepare_playback_resume(
        context,target,request.speed,request.paused,firstBase);
    if (secondContext) {
        const Runtime::Duration secondBase=
            Replay::schedule_playback_checkpoint_restore(*secondContext,target);
        Replay::prepare_playback_resume(
            *secondContext,target,request.speed,request.paused,secondBase);
    }
}

bool load_replay(
    Runtime::GameContext& context,
    Runtime::GameContext* secondContext,
    const UserConfig::Settings& user) {
    const auto previousSource=Replay::session(context).sourcePath;
    const auto previousRecords=Replay::session(context).challengeRecords;
    reset_replay_state(context);
    if (secondContext != nullptr) reset_replay_state(*secondContext);
    const auto replayPath=session_replay_path(user);
    if (!replayPath) return false;
    std::error_code sizeError;
    if(std::filesystem::file_size(*replayPath,sizeError)>Replay::kMaxMultiplayerReplayBytes+17 || sizeError)return false;
    std::ifstream input(*replayPath,std::ios::binary);
    std::string replay((std::istreambuf_iterator<char>(input)),{});
    if(!Replay::is_multiplayer_replay(replay)) {
        // Preserve the historical whitespace-tolerant single/local-VS reader.
        std::istringstream legacy(replay);std::string token;if(!(legacy>>token))return false;replay=std::move(token);
    }
    if (input.bad() || !replay_hash_valid(replay)) {
        return false;
    }
    replay.resize(replay.size() - 17);

    if(Replay::is_multiplayer_replay(replay)) {
        auto archive=Replay::decode_multiplayer_replay(replay);if(!archive)return false;
        std::string config;
        for(const auto& track:archive->tracks) {
            auto journal=std::string_view(track.journal);Replay::JournalRecord record;
            if(Replay::take_journal_record(journal,record) && record.kind=='C') {
                if(config.empty() || track.actor==archive->viewer)config=record.data;
                if(track.actor==archive->viewer)break;
            }
        }
        if(config.empty())config="{{Model,4}}";
        if(!load_replay_stream(context,config+"{end_all,0}"))return false;
        auto& state=Replay::session(context);state.multiplayerReplay=std::make_shared<Replay::MultiplayerReplay>(std::move(*archive));
        state.loadedRecording=std::move(replay);state.sourcePath=Util::FilePath::generic(*replayPath);
        return true;
    }

    if (!Replay::is_local_versus_replay(replay)) {
        const auto original=replay;
        bool prefixActive=false;
        if(user.challengeReplay && !user.challengeReplay->challengeId.empty()) {
            const auto definition=Challenges::lookup(user.challengeReplay->challengeId);
            if(!definition)return false;
            const auto prepared=Replay::prepare_challenge_replay(replay,definition->id,definition->ruleVersion,bool(definition->stopRule),
                [&](const std::string& source){return App::analyze_challenge_replay(source,*definition);});
            if(prepared.status!=Replay::PrefixPreparationStatus::Ready)return false;
            replay=prepared.recording;prefixActive=prepared.boundary.has_value();
        }
        const bool loaded=load_replay_stream(context,std::move(replay));
        if (loaded) {
            Replay::session(context).challengePrefixActive=prefixActive;
            Replay::session(context).loadedRecording=original;
            Replay::session(context).sourcePath=Util::FilePath::generic(*replayPath);
            if(Replay::session(context).sourcePath==previousSource)Replay::session(context).challengeRecords=previousRecords;
            apply_replay_resume_request(context,nullptr);
        }
        return loaded;
    }

    Replay::LocalVersusReplayStreams streams;
    if (secondContext == nullptr ||
        !Replay::decode_local_versus_replay(replay, streams) ||
        !load_replay_stream(context, std::move(streams.first)) ||
        !load_replay_stream(*secondContext, std::move(streams.second)) ||
        context.config.mode.mode != Runtime::GameMode::LocalVersus ||
        secondContext->config.mode.mode != Runtime::GameMode::LocalVersus) {
        reset_replay_state(context);
        if (secondContext != nullptr) reset_replay_state(*secondContext);
        return false;
    }
    Replay::session(context).localVersusReplay = true;
    Replay::session(context).loadedRecording=replay;
    Replay::session(context).sourcePath=Util::FilePath::generic(*replayPath);
    if(Replay::session(context).sourcePath==previousSource)Replay::session(context).challengeRecords=previousRecords;
    Replay::session(*secondContext).localVersusReplay = true;
    Replay::session(*secondContext).sourcePath=Replay::session(context).sourcePath;
    Replay::configure_turn_playback(context, *secondContext);
    apply_replay_resume_request(context,secondContext);
    return true;
}

void initialize_local_config(Runtime::GameContext& context, Runtime::GameMode selectedMode) {
    reset_replay_state(context);
    context.config = Runtime::normalize_user_config(context.user_settings(), selectedMode);
    const auto* network=context.network_component();
    const bool hostMapPending=network && !network->room.name.empty() && network->roomPolicyAvailable &&
        network->roomPolicy.forceRules && !Network::is_room_host(*network);
    if(context.user_settings().mapId>0 && !hostMapPending) {
        const auto map=UserConfig::Maps::find(context.user_settings().mapId);
        if(!map) throw App::MapUnavailable(context);
        Runtime::apply_map_config(context.config,*map);
    }
    if (const auto* app = context.app_component())
        context.config.quickPlay.continuousCancellation = app->qpContinuousCancellation;
    if (Runtime::autoplay_enabled(context.config.autoplay.mode)) {
        if (!Engine::initialize_autoplay(context)) {
            context.config.autoplay.mode = Runtime::AutoplayMode::Disabled;
        } else if (context.config.autoplay.mode != Runtime::AutoplayMode::ChatGPT &&
                   !context.config.autoplay.handlingSimulation &&
                   context.config.handling.gravityInterval.enabled() &&
                   !context.config.handling.gravityInterval.is_instant()) {
            context.config.handling.gravityInterval = Runtime::GravityInterval::disabled();
        }
    }
    Replay::session(context).configPrefix = Replay::encode_config_prefix(context.config);
}

} // namespace

bool configure_replay_or_local_game(
    Runtime::GameContext& context,
    Runtime::GameMode& selectedMode,
    Runtime::GameContext* localPlayerTwoContext) {
    auto playbackUser=context.user_settings();
    const auto* app=context.app_component();
    const bool preview=app && app->replayPreview.has_value();
    if(preview)playbackUser.replayPath=app->replayPreview->path;
    if (!preview && selectedMode == Runtime::GameMode::Replay && !context.user_settings().challengeReplay) {
        context.user_settings().replayPath=Replay::replay_setting_path(context.user_settings().replayPath);
    }
    const bool replayMode =
        selectedMode == Runtime::GameMode::Replay &&
        load_replay(context, localPlayerTwoContext, preview?playbackUser:context.user_settings());
    if (!replayMode) {
        if (auto* app=context.app_component()) app->replayResume.pending=false;
        if (!context.app_component() || !context.app_component()->headlessLocalMatch)
            Render::end_output_suppression();
        if (selectedMode == Runtime::GameMode::Replay) selectedMode = Runtime::GameMode::Zen;
        initialize_local_config(context, selectedMode);
    } else {
        Replay::session(context).configPrefix.clear();
    }
    return replayMode;
}

} // namespace App::InitDetail
