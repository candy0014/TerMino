#include "tetr/compat/legacy_encoding.h"
#include "tetr/replay/config_codec.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/runtime/game_context.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <string>

namespace {

void normalization_is_deterministic() {
    UserConfig::Settings user;
    user.gameMode = 3;
    user.fieldWidth = 999;
    user.fieldHeight = 999;
    user.nextCount = -2;
    user.holdEnabled = 9;
    user.rotationSystem = 99;
    user.garbageMode = 99;
    user.qpNoHold = 2;
    user.qpGravity = 2;
    user.qpVolatile = 2;
    user.qpMessy = 2;
    user.qpFool = 3;
    user.qpAllSpin = 3;

    const Runtime::RuntimeConfig config = Runtime::normalize_user_config(
        user, Runtime::selected_game_mode(user));
    assert(config.mode.mode == Runtime::GameMode::QuickPlay);
    assert(config.garbage.mode == Runtime::GarbageMode::QuickPlay);
    assert(config.field.width == 10);
    assert(config.field.height == 16);
    assert(config.field.nextCount == 1);
    assert(!config.field.holdEnabled);
    assert(!config.field.ghost);
    assert(config.randomPieces);
    assert(config.rotationSystem == Runtime::RotationSystem::SrsPlus);
    assert(config.quickPlay.allSpin == Runtime::AllSpinMode::Reverse);
    assert(config.handling.gravityInterval.is_instant());
    assert(config.handling.epld.seconds()==0.4);
    assert(config.field.lineClearDelay.seconds()==70.0/60.0);
}

void local_versus_uses_multiplayer_garbage_with_player_specific_settings() {
    UserConfig::Settings user;
    user.gameMode = 6;
    user.fieldWidth = 12;
    user.fieldHeight = 24;
    user.nextCount = 3;
    user.gravityInterval = 0.25;
    user.autoplayMode = 4;
    user.autoplayPps = 8.0;
    user.autoplayWidth = 17;
    user.squareStyle = 1;
    user.obscuredColors = 1;

    const Runtime::RuntimeConfig config = Runtime::normalize_user_config(
        user, Runtime::selected_game_mode(user));
    assert(config.mode.mode == Runtime::GameMode::LocalVersus);
    assert(config.garbage.mode == Runtime::GarbageMode::Multiplayer);
    assert(config.field.width == 12 && config.field.height == 24);
    assert(config.field.nextCount == 3);
    assert(config.handling.gravityInterval.interval().seconds() == 0.25);
    assert(config.autoplay.mode == Runtime::AutoplayMode::ZzztojBotris);
    assert(config.autoplay.pps == 8.0 && config.autoplay.searchWidth == 17);
    assert(config.display.squareStyle == 1 && config.display.obscuredColors);
}

void garbage_in_time_applies_outside_quick_play() {
    UserConfig::Settings user;
    for (int garbageMode = 0; garbageMode <= 5; ++garbageMode) {
        user.garbageMode = garbageMode;
        const auto defaultConfig = Runtime::normalize_user_config(user, Runtime::GameMode::Zen);
        assert(defaultConfig.garbage.incomingDelay.seconds() == 0.3 && defaultConfig.garbage.entryMode==Runtime::GarbageEntryMode::AfterNonClear);
        user.garbageInTime = 2.25;
        const auto customConfig = Runtime::normalize_user_config(user, Runtime::GameMode::Zen);
        assert(customConfig.garbage.incomingDelay.seconds() == 2.25);
        user.garbageInTime = 0.3;
    }

    user.garbageEntryMode=2;
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Zen).garbage.entryMode==Runtime::GarbageEntryMode::Immediate);
    user.garbageInTime = 2.25;
    const auto versusConfig = Runtime::normalize_user_config(user, Runtime::GameMode::LocalVersus);
    assert(versusConfig.garbage.incomingDelay.seconds() == 2.25);

    const auto quickPlayConfig = Runtime::normalize_user_config(user, Runtime::GameMode::QuickPlay);
    assert(quickPlayConfig.garbage.incomingDelay.seconds() == 5.0);
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);
    context.config = quickPlayConfig;
    Engine::initialize_quick_play(context, session.player);
    assert(context.config.garbage.incomingDelay.seconds() == 5.0);
    assert(session.quickPlay.garbageInTime.seconds() == 1.0);
}

void garbage_rise_config_and_legacy_replays() {
    UserConfig::Settings user;
    assert(user.garbageRiseInterval==0);
    user.garbageRiseInterval=.123456789012345;
    const auto custom=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
    assert(custom.garbage.riseInterval.seconds()==user.garbageRiseInterval);
    for (int expert=0;expert<3;++expert) {
        user.qpExpert=expert;
        const auto qp=Runtime::normalize_user_config(user,Runtime::GameMode::QuickPlay);
        assert(qp.garbage.riseInterval.seconds()==(expert ? 0 : 5.0/60));
    }
    Runtime::RuntimeConfig decoded=custom;
    Replay::ConfigPrefixResult result;
    assert(Replay::decode_config_prefix("{{GarbageModel,11}}",decoded,result));
    assert(decoded.garbage.riseInterval==Runtime::Duration{}); // Historical recordings remain instant.
    user.garbageRiseInterval=-1;
    assert(Runtime::normalize_user_config(user,Runtime::GameMode::Zen).garbage.riseInterval==Runtime::Duration{});
}

void replay_config_round_trips() {
    std::mt19937 rng(0xC0F16u);
    for (int iteration = 0; iteration < 10000; ++iteration) {
        Runtime::RuntimeConfig input;
        input.mode.mode = Compat::game_mode_from_legacy(rng() % 5);
        input.mode.fixedBlitzSpeed = (rng() & 1U) != 0;
        input.mode.racingDistance = 1 + static_cast<int>(rng() % 100000);
        input.mode.blitzTime = Runtime::Duration::from_seconds(static_cast<double>(rng() % 100000) / 8.0);
        input.garbage.mode = Compat::garbage_mode_from_legacy(
            (rng() & 1U) ? static_cast<int>(rng() % 6) : 10 + static_cast<int>(rng() % 2));
        input.garbage.cheeseModel = static_cast<int>(rng() % 7) - 3;
        input.garbage.holePattern = Compat::hole_pattern_from_legacy(
            static_cast<int>(rng() % 21) - 10);
        input.garbage.layerHeight = static_cast<int>(rng() % 100);
        input.garbage.generationInterval = Runtime::Duration::from_seconds(static_cast<double>(rng() % 10000) / 8.0);
        input.garbage.cheeseMessiness = static_cast<double>(rng() % 9) / 8.0;
        input.garbage.singleMessiness = static_cast<double>(rng() % 9) / 8.0;
        input.garbage.attackMultiplier = static_cast<double>(rng() % 1000) / 8.0;
        input.garbage.survivalApm = static_cast<double>(rng() % 10000) / 8.0;
        input.garbage.incomingDelay = Runtime::Duration::from_seconds(static_cast<double>(rng() % 100) / 8.0);
        input.garbage.entryMode=static_cast<Runtime::GarbageEntryMode>(rng()%3);
        input.garbage.riseInterval=Runtime::Duration::from_seconds(static_cast<double>(rng())/1000000007.0);
        input.quickPlay.expert = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.noHold = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.messy = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.gravity = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.volatileMode = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.doubleHole = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.invisible = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.allSpin = Compat::all_spin_mode_from_legacy(rng() % 4);
        input.quickPlay.backfire = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.snakeEyes = Compat::modifier_mode_from_legacy(rng() % 3);
        input.quickPlay.fool = Compat::fool_mode_from_legacy(rng() % 5);
        input.quickPlay.defaultFloor = 1 + static_cast<int>(rng() % 10);
        input.legacyFoolPieceSet = (rng() & 1U) != 0;
        input.quickPlay.continuousCancellation = (rng() & 1U) != 0;
        input.handling.gravityInterval = Compat::gravity_from_legacy_seconds(static_cast<double>(rng() % 1000) / 8.0);
        input.handling.softDropFactor = static_cast<double>(rng() % 1000) / 8.0;
        input.handling.das = Runtime::Duration::from_seconds(static_cast<double>(rng() % 1000) / 8.0);
        input.handling.arr = Runtime::Duration::from_seconds(static_cast<double>(rng() % 1000) / 8.0);
        input.handling.epld = Runtime::Duration::from_seconds(static_cast<double>(rng() % 1000) / 8.0);
        input.handling.lockResetLimit = static_cast<int>(rng() % 1000);
        input.field.width = 4 + static_cast<int>(rng() % 97);
        input.field.height = 2 + static_cast<int>(rng() % 99);
        input.field.nextCount = static_cast<int>(rng() % 101);
        input.field.holdEnabled = (rng() & 1U) != 0;
        input.field.invisible = (rng() & 1U) != 0;
        input.field.antiGravity = (rng() & 1U) != 0;
        input.field.ghost = (rng() & 1U) != 0;
        input.field.lineClearDelay = Runtime::Duration::from_seconds(static_cast<double>(rng() % 1000) / 8.0);
        input.rotationSystem = Compat::rotation_system_from_legacy(rng() % 7);
        input.randomPieces = (rng() & 1U) != 0;
        input.autoplay.mode = Runtime::autoplay_mode_from_index(static_cast<int>(rng() % 4));
        input.autoplay.pps = static_cast<double>(rng() % 800) / 8.0;
        input.autoplay.searchWidth = static_cast<int>(rng() % 1001);

        const unsigned int blockSeed = rng();
        const unsigned int garbageSeed = rng();
        const std::string encoded = Replay::encode_config_prefix(input) +
            "{block_seed," + std::to_string(blockSeed) + "}" +
            "{garbage_seed," + std::to_string(garbageSeed) + "}}";

        Runtime::RuntimeConfig output;
        Replay::ConfigPrefixResult result;
        assert(Replay::decode_config_prefix(encoded, output, result));
        assert(result.operationOffset == encoded.size());
        assert(result.blockSeed == blockSeed);
        assert(result.garbageSeed == garbageSeed);
        Runtime::redirect_legacy_mode(input);
        assert(output.mode.mode == input.mode.mode && output.mode.goal==input.mode.goal);
        assert(Compat::to_legacy(output.garbage.mode) == Compat::to_legacy(input.garbage.mode));
        assert(output.garbage.holePattern == input.garbage.holePattern);
        assert(Compat::to_legacy(output.rotationSystem) == Compat::to_legacy(input.rotationSystem));
        assert(Compat::to_legacy(output.quickPlay.allSpin) == Compat::to_legacy(input.quickPlay.allSpin));
        assert(output.quickPlay.continuousCancellation == input.quickPlay.continuousCancellation);
        assert(output.quickPlay.defaultFloor == input.quickPlay.defaultFloor);
        assert(output.legacyFoolPieceSet == input.legacyFoolPieceSet);
        assert(output.field.width == input.field.width);
        assert(output.field.height == input.field.height);
        assert(output.field.holdEnabled == input.field.holdEnabled);
        assert(output.handling.gravityInterval.enabled() == input.handling.gravityInterval.enabled());
        assert(std::abs(Compat::to_legacy_seconds(output.handling.gravityInterval) - Compat::to_legacy_seconds(input.handling.gravityInterval)) < 1e-5);
        assert(std::abs((output.garbage.incomingDelay - input.garbage.incomingDelay).seconds()) < 1e-9);
        assert(output.garbage.entryMode==input.garbage.entryMode);
        assert(output.garbage.riseInterval==input.garbage.riseInterval);
        assert(output.autoplay.searchWidth == input.autoplay.searchWidth);
    }
}

void cheese_probabilities_are_independent() {
    UserConfig::Settings user;
    user.garbageMode=3; user.cheeseMessiness=.13; user.cheeseDensity=.67;
    for (int pattern:{1,2,-1,-2}) {
        user.cheesePattern=pattern;
        const auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        assert(config.garbage.cheeseMessiness==(std::abs(pattern)==2 ? .67 : .13));
        Runtime::RuntimeConfig decoded;
        Replay::ConfigPrefixResult result;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,result));
        assert(std::abs(decoded.garbage.cheeseMessiness-config.garbage.cheeseMessiness)<1e-12);
    }
    // Density endpoints alter actual occupied cells, independent of messiness.
    user.cheesePattern=2; user.cheeseMessiness=1;
    for (double density:{0.0,1.0}) {
        user.cheeseDensity=density;
        const auto config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);
        Rules::GarbageGenerationConfig generation;
        generation.width=10; generation.garbageMode=config.garbage.mode;
        generation.cheeseModel=config.garbage.cheeseModel;
        generation.cheeseMessiness=config.garbage.cheeseMessiness;
        Rules::GarbageGenerationState state;
        std::mt19937 random(123);
        const auto row=Rules::generate_garbage_row(generation,state,random);
        const auto occupied=std::count_if(row.begin(),row.end(),[](auto cell) {return cell.occupied();});
        assert(occupied==(density==0 ? 0 : 9)); // Full rows keep one hole.
    }
    Runtime::RuntimeConfig legacy;
    Replay::ConfigPrefixResult result;
    assert(Replay::decode_config_prefix("{{GarbageModel,3}{CheeseModel,2}{CheeseMessiness,0.23}}",legacy,result));
    assert(std::abs(legacy.garbage.cheeseMessiness-.23)<1e-12); // Historical density replay unchanged.
}

void malformed_replays_are_rejected() {
    Runtime::RuntimeConfig config;
    Replay::ConfigPrefixResult result;
    assert(!Replay::decode_config_prefix("", config, result));
    assert(!Replay::decode_config_prefix("{{gameMode,3}", config, result));
    assert(!Replay::decode_config_prefix("{{gameMode}}", config, result));
    assert(!Replay::decode_config_prefix("{{block_seed,-1}}", config, result));
    assert(!Replay::decode_config_prefix("{{block_seed,12x}}", config, result));
    assert(!Replay::decode_config_prefix("{{garbage_seed,99999999999999999999}}", config, result));
}

} // namespace

int main() {
    normalization_is_deterministic();
    local_versus_uses_multiplayer_garbage_with_player_specific_settings();
    garbage_in_time_applies_outside_quick_play();
    garbage_rise_config_and_legacy_replays();
    replay_config_round_trips();
    malformed_replays_are_rejected();
    cheese_probabilities_are_independent();
}
