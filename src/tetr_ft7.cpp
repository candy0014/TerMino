#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "tetr/app/application_state.h"
#include "tetr/app/game.h"
#include "tetr/app/init.h"
#include "tetr/app/first_to_seven.h"
#include "tetr/network/network_services.h"
#include "tetr/legacy/bot_strategy.h"
#include "tetr/input/services.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/file_path.h"
#include "tetr/replay/tetrio_recorder.h"
#include "tetr/runtime/game_context.h"
#include "tetr/config/user_config.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
struct Utf8Console {
#ifdef _WIN32
    UINT previous = GetConsoleOutputCP();
    Utf8Console() { SetConsoleOutputCP(CP_UTF8); }
    ~Utf8Console() { if (previous) SetConsoleOutputCP(previous); }
#endif
};
struct Player {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session, services};
    Network::NetworkServices network;
    Replay::ReplaySessionState replay;
    Legacy::BotStrategy bot;
    Input::Services input;
    UserConfig::Settings settings;
    Player(App::ApplicationState& app, int side) {
        settings.gameMode = 6;
        settings.autoplayMode = side ? 4 : 9;
        settings.autoplayPps = settings.simulationPps = 4;
        settings.turnPieces = 1;
        settings.autoplayWidth = 300;
        // Tournament controls are deliberately disabled. Ctrl+C remains an
        // ordinary process abort; completed rounds have already been saved.
        settings.keyCodes.fill(0);
        context.attach_app(app);
        context.attach_network(network);
        context.attach_replay(replay);
        context.attach_autoplay(bot);
        context.attach_input(input);
        context.attach_user_settings(settings);
        context.attach_output_mutex(app.terminalOutputMutex);
    }
};
void write_new(const std::filesystem::path& path, const std::string& data) {
    if (std::filesystem::exists(path)) throw std::runtime_error("Refusing to overwrite " + path.string());
    auto temporary = path;
    temporary += ".part";
    if (std::filesystem::exists(temporary)) throw std::runtime_error("Refusing to overwrite " + temporary.string());
    std::ofstream out(temporary, std::ios::binary);
    if (!out || !(out << data) || !(out.flush())) throw std::runtime_error("Cannot write " + temporary.string());
    out.close();
    if (!out) throw std::runtime_error("Cannot close " + temporary.string());
    // Publish only a complete JSON document; an interrupted write remains a
    // clearly labelled .part file, never a seemingly complete .ttrm.
    std::filesystem::rename(temporary, path);
}
std::string timestamp() {
    const auto now = std::time(nullptr);
    const auto utc = *std::gmtime(&now);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
struct Totals { long long pieces=0, attack=0, sent=0, received=0, cleared=0, frames=0; };
std::string match_json(const std::vector<std::string>& rounds, int firstWins, int secondWins,
                      const std::string& ts, const Totals (&totals)[2]) {
    std::ostringstream out;
    out << "{\"version\":1,\"id\":null,\"gamemode\":null,\"ts\":\"" << ts << "\",\"users\":["
        << Replay::TetrioRecorder::user_json(0) << ',' << Replay::TetrioRecorder::user_json(1)
        << "],\"replay\":{\"leaderboard\":[";
    for (int side = 0; side < 2; ++side) {
        const auto& t=totals[side];
        const double seconds=std::max(1.0/60, t.frames/60.0);
        out << (side ? ",{": "{") << Replay::TetrioRecorder::identity_json(side)
            << ",\"wins\":" << (side ? secondWins : firstWins) << ",\"stats\":{\"apm\":" << t.attack/seconds*60
            << ",\"pps\":" << t.pieces/seconds << ",\"vsscore\":" << (t.attack+t.cleared)/seconds*100
            << ",\"garbagesent\":" << t.sent << ",\"garbagereceived\":" << t.received << ",\"kills\":"
            << (side ? secondWins : firstWins) << "}}";
    }
    out << "],\"rounds\":[";
    for (std::size_t i = 0; i < rounds.size(); ++i) out << (i ? "," : "") << rounds[i];
    return out.str() + "]}}\n";
}
}
int main(int argc, char** argv) {
    try {
        [[maybe_unused]] Utf8Console utf8;
        if (argc == 2 && std::string(argv[1]) == "--rotation-profile") {
            std::cout << Replay::tetrio_native_rotation_profile() << '\n';
            return 0;
        }
        // A scaled wall clock would make a purported 4 PPS demonstration run
        // at a different physical speed. Unit fixtures may scale their clocks,
        // but the standalone match explicitly rejects this inherited override.
        std::filesystem::path output;
        std::uint32_t seed = std::random_device{}();
        bool headless = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "tetr_ft7 [--output NEW_DIRECTORY] [--seed UINT32] [--headless]\n"
                    << "P1 bot_chatgpt vs P2 zzztoj_botris; first to 7; both autoplay/simulation 4 PPS; turn 1.\n"
                    << "No round limits. Default: original single-window terminal UI.\n";
                std::cout << "--rotation-profile: print native kick tables as JSON without starting a match.\n";
                return 0;
            }
            if (arg == "--headless") headless = true;
            else if ((arg == "--output" || arg == "--seed") && i + 1 < argc) {
                const std::string value = argv[++i];
                if (arg == "--output") output = value;
                else {
                    std::size_t end = 0;
                    const auto n = std::stoull(value, &end);
                    if (end != value.size() || n > UINT32_MAX || value.front() == '-')
                        throw std::runtime_error("Seed must be a uint32");
                    seed = static_cast<std::uint32_t>(n);
                }
            } else throw std::runtime_error("Unknown/incomplete argument: " + arg);
        }
        if (const char* scale=std::getenv("TETR_TEST_CLOCK_SCALE")) {
            if (std::string(scale) != "1") throw std::runtime_error(
                "Unset TETR_TEST_CLOCK_SCALE before running the 4 PPS match");
        }
        if (output.empty()) output = Replay::replay_directory() /
            ("ft7_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(seed));
        if (!std::filesystem::create_directories(output)) throw std::runtime_error("Output directory must be new: " + output.string());
        const std::string ts = timestamp();
        write_new(output / "settings.txt", "P1=bot_chatgpt\nP2=zzztoj_botris\nautoplay_pps=4\nsimulation_pps=4\nturn_piece=1\nfirst_to=7\nround_limit=none\nmaster_seed=" + std::to_string(seed) + "\n");
        App::ApplicationState app;
        app.automaticLocalMatch = true;
        app.headlessLocalMatch = headless;
        Player first(app, 0), second(app, 1);
        app.primaryContext = &first.context;
        app.localPlayerTwoContext = &second.context;
        std::mt19937 seeds(seed);
        std::vector<std::string> rounds;
        int wins[2]{};
        App::FirstToSeven match;
        Totals totals[2];
        {
            Render::TerminalSession terminal;
            app.textInputSession = &terminal;
            if (headless) Render::begin_output_suppression();
            for (int round = 1; !match.finished(); ++round) {
                // Positive MINSTD seed, with independent piece/hole streams.
                const std::uint32_t roundSeed = 1 + seeds() % 2147483646u;
                first.replay.tetrioRecorder = std::make_shared<Replay::TetrioRecorder>(roundSeed, 0);
                second.replay.tetrioRecorder = std::make_shared<Replay::TetrioRecorder>(roundSeed, 1);
                App::initialize_game(first.context);
                App::run(first.context);
                const int next[2] = {first.session.player.gameplay.scoring.wins, second.session.player.gameplay.scoring.wins};
                match.accept_score(next[0], next[1]);
                const bool firstWon = next[0] > wins[0];
                wins[0] = next[0]; wins[1] = next[1];
                auto& a = *first.replay.tetrioRecorder;
                auto& b = *second.replay.tetrioRecorder;
                const auto end = std::max(a.last_frame(), b.last_frame()) + 1;
                for (int side = 0; side < 2; ++side) {
                    const auto& p=side ? second : first;
                    const auto& s=p.session.player.gameplay.scoring;
                    auto& t=totals[side];
                    t.pieces+=s.placedBlocks; t.attack+=s.totalAttack; t.sent+=s.totalOutgoingAttack;
                    t.cleared+=s.totalGarbageLines; t.frames+=end;
                    t.received+=p.replay.tetrioRecorder->received();
                }
                rounds.push_back('[' + a.round_json(first.context, firstWon, end) + ',' +
                    b.round_json(second.context, !firstWon, end) + ']');
                write_new(output / ("round_" + std::to_string(round) + ".audit.json"),
                    '[' + a.audit_json() + ',' + b.audit_json() + ']');
                write_new(output / ("through_round_" + std::to_string(round) + ".ttrm"), match_json(rounds, wins[0], wins[1], ts, totals));
                std::cerr << "FT7 round=" << round << " seed=" << roundSeed << " score=" << wins[0] << ':' << wins[1]
                    << " pieces=" << first.session.player.gameplay.scoring.placedBlocks << ':'
                    << second.session.player.gameplay.scoring.placedBlocks << '\n';
            }
            write_new(output / "match.ttrm", match_json(rounds, wins[0], wins[1], ts, totals));
            if (headless) Render::end_output_suppression();
        }
        std::cout << "FT7 finished: bot_chatgpt " << wins[0] << " : " << wins[1] << " zzztoj_botris\n"
            << "Replay: " << std::filesystem::absolute(output / "match.ttrm").string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tetr_ft7: " << error.what() << '\n';
        return 1;
    }
}
