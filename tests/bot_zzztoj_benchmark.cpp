#include "tetr/bot/bot_zzztoj.h"
#include "tetr/compat/piece_encoding.h"
#include "tetr/engine/field_mutation.h"
#include "tetr/engine/locked_piece_resolution.h"
#include "tetr/engine/placement_application.h"
#include "tetr/engine/placement_board_adapter.h"
#include "tetr/rules/line_clear.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace TetrTests::bot_zzztoj_benchmark {
namespace {

struct BoardMetrics {
    int height = 0;
    int holes = 0;
};

BoardMetrics board_metrics(const Runtime::Playfield& field) {
    BoardMetrics result;
    for (int col = 0; col < 10; ++col) {
        bool covered = false;
        for (int row = -20; row < 20; ++row) {
            if (field.cell(row, col).occupied()) {
                covered = true;
                result.height = std::max(result.height, 20 - row);
            } else if (covered) {
                ++result.holes;
            }
        }
    }
    return result;
}

Bot::Environment standard_environment(int searchWidth) {
    Bot::Environment environment;
    environment.garbageModel = 11;
    environment.incomingGarbageDelaySeconds = 0.3;
    environment.gravityIntervalSeconds = 0.6;
    environment.fieldWidth = 10;
    environment.fieldHeight = 20;
    environment.nextCount = 5;
    environment.holdEnabled = true;
    environment.rotationSystem = 0;
    environment.autoplayPps = 100.0;
    environment.autoplaySearchWidth = searchWidth;
    return environment;
}

int bot_mode(std::string_view name) {
    if (name == "botris") return 7;
    if (name == "toj") return 8;
    if (name == "dig") return 9;
    if (name == "qq") return 10;
    if (name == "c2") return 11;
    throw std::runtime_error("unknown zzztoj bot: " + std::string(name));
}

void append_bag(std::deque<int>& queue, std::mt19937& random) {
    std::array<int, 7> bag{{0, 1, 2, 3, 4, 5, 6}};
    // Preserve the original libstdc++ regression bags on every compiler.
    // std::shuffle and uniform_int_distribution are implementation-dependent.
    const auto bounded = [&random](std::uint32_t range) {
        std::uint64_t product = std::uint64_t{random()} * range;
        std::uint32_t low = static_cast<std::uint32_t>(product);
        const std::uint32_t threshold = (std::uint32_t{0} - range) % range;
        while (low < threshold) {
            product = std::uint64_t{random()} * range;
            low = static_cast<std::uint32_t>(product);
        }
        return static_cast<std::uint32_t>(product >> 32);
    };
    for (std::uint32_t index = 1; index < bag.size(); index += 2) {
        const std::uint32_t firstRange = index + 1;
        const std::uint32_t secondRange = index + 2;
        const auto pair = bounded(firstRange * secondRange);
        std::swap(bag[index], bag[pair / secondRange]);
        std::swap(bag[index + 1], bag[pair % secondRange]);
    }
    queue.insert(queue.end(), bag.begin(), bag.end());
}

void print_field(const Runtime::Playfield& field) {
    for (int row = -4; row < 20; ++row) {
        std::cerr << (row < 0 ? '!' : '|');
        for (int col = 0; col < 10; ++col) {
            std::cerr << (field.cell(row, col).occupied() ? '#' : '.');
        }
        std::cerr << (row < 0 ? '!' : '|') << '\n';
    }
}

} // namespace

int run(int argc,char** argv) {
    const int target = argc > 1 ? std::stoi(argv[1]) : 300;
    const int searchWidth = argc > 2 ? std::stoi(argv[2]) : 300;
    const unsigned seed = argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 20260918u;
    const int maxAllowedHoles = argc > 4 ? std::stoi(argv[4]) : -1;
    const std::string botName = argc > 5 ? argv[5] : "botris";
    const bool trace = std::getenv("BOT_ZZZTOJ_TRACE") != nullptr;

    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);
    context.config.mode.mode = Runtime::GameMode::Zen;
    context.config.garbage.mode = Runtime::GarbageMode::Multiplayer;
    context.config.field.width = 10;
    context.config.field.height = 20;
    context.config.field.nextCount = 5;
    context.config.field.holdEnabled = true;
    context.config.rotationSystem = Runtime::RotationSystem::SrsPlus;

    auto& player = session.player;
    player.gameplay.playfield.width = 10;
    player.gameplay.playfield.visibleHeight = 20;
    player.gameplay.playfield.activeHeight = 20;
    player.gameplay.playfield.nextCount = 5;
    player.gameplay.playfield.holdEnabled = true;
    player.gameplay.scoring = Runtime::ScoringState{};

    Runtime::Playfield field;
    field.initialize(10, 20);
    field.clear();

    Bot::Environment environment = standard_environment(searchWidth);
    environment.autoplayMode = bot_mode(botName);
    if (!Bot_zzztoj::init(environment)) {
        std::cerr << "bot_zzztoj init failed\n";
        return 2;
    }

    std::mt19937 random(seed);
    std::deque<int> queue;
    while (queue.size() < 14) append_bag(queue, random);
    int hold = -1;
    int placed = 0;
    int maxHeight = 0;
    int maxHoles = 0;
    int fallbackLike = 0;
    bool qualityFailed = false;

    while (placed < target) {
        while (queue.size() < 14) append_bag(queue, random);
        std::vector<int> next;
        for (int i = 0; i <= 5; ++i) next.push_back(queue[static_cast<std::size_t>(i)]);

        const int current = queue.front();
        int row = -2;
        int col = 4;
        int rotation = 0;
        int kick = 0;
        const int selected = Bot_zzztoj::getpos(
            environment, player, field, next, {}, -2, 4, 0, hold,
            row, col, rotation, kick);

        if (selected == current) {
            queue.pop_front();
        } else if (hold < 0 && queue.size() > 1 && selected == queue[1]) {
            hold = current;
            queue.pop_front();
            queue.pop_front();
        } else if (hold >= 0 && selected == hold) {
            hold = current;
            queue.pop_front();
        } else {
            std::cerr << "invalid selection at piece " << placed
                      << ": current=" << current << " hold=" << hold
                      << " selected=" << selected << '\n';
            print_field(field);
            break;
        }

        const Piece::Type type = Piece::Type::from_index(selected);
        Piece::Definition piece = Piece::get_definition(type);
        const Piece::Pose pose{{row, col}, Runtime::Rotation::from_index(rotation)};
        if (!piece.can_place(pose, field) || piece.can_place(pose.translated(1, 0), field)) {
            std::cerr << "illegal placement at piece " << placed
                      << ": type=" << selected << " pose=" << row << ',' << col
                      << ',' << rotation << " kick=" << kick << '\n';
            print_field(field);
            break;
        }
        if (col == 4 && rotation == 0) ++fallbackLike;

        const auto placement = Engine::resolve_locked_piece(
            context, field, piece, player, pose,
            Compat::rotation_record_from_legacy(kick));
        const auto applicationConfig =
            Engine::capture_placement_application_config(context, 1);
        const auto applicationBegin = Engine::begin_placement_application(
            Engine::capture_placement_application_state(player.gameplay, context),
            applicationConfig, placement, type);
        Engine::store_pre_offset_application_state(player.gameplay, applicationBegin.state);

        if (!placement.clear.clearedRows.empty()) {
            const auto plan = Rules::build_gravity_collapse_plan(
                -Runtime::kHiddenRowCapacity,
                player.gameplay.playfield.activeHeight,
                placement.clear.clearedRows);
            Engine::apply_gravity_collapse(
                10, plan,
                [&field](int source, int column) { return field.cell(source, column); },
                [&field](int destination, int source) { field.copy_row(destination, source); });
        }

        ++placed;
        const BoardMetrics metrics = board_metrics(field);
        maxHeight = std::max(maxHeight, metrics.height);
        maxHoles = std::max(maxHoles, metrics.holes);
        if (trace) {
            std::cerr << "[zzztoj] n=" << placed << " type=" << selected
                      << " hold=" << hold << " pose=" << row << ',' << col
                      << ',' << rotation << " kick=" << kick
                      << " clear=" << placement.clear.clearedLines
                      << " height=" << metrics.height << " holes=" << metrics.holes
                      << " b2b=" << player.gameplay.scoring.b2b << '\n';
        }
        if (metrics.height >= 22) {
            std::cerr << "top-out height reached after " << placed << " pieces\n";
            print_field(field);
            break;
        }
        if (maxAllowedHoles >= 0 && metrics.holes > maxAllowedHoles) {
            std::cerr << "hole limit exceeded after " << placed << " pieces: "
                      << metrics.holes << " > " << maxAllowedHoles << '\n';
            print_field(field);
            qualityFailed = true;
            break;
        }
    }

    const BoardMetrics finalMetrics = board_metrics(field);
    std::cout << "bot_zzztoj variant=" << botName
              << " n=" << placed
              << " target=" << target
              << " seed=" << seed
              << " width=" << searchWidth
              << " attack=" << player.gameplay.scoring.totalAttack
              << " lines=" << player.gameplay.scoring.totalLines
              << " b2b=" << player.gameplay.scoring.b2b
              << " maxHeight=" << maxHeight
              << " maxHoles=" << maxHoles
              << " finalHeight=" << finalMetrics.height
              << " finalHoles=" << finalMetrics.holes
              << " fallbackLike=" << fallbackLike
              << " dead=" << (placed < target && !qualityFailed)
              << " qualityFailed=" << qualityFailed << '\n';
    return placed == target && !qualityFailed ? 0 : 1;
}
} // namespace TetrTests::bot_zzztoj_benchmark
