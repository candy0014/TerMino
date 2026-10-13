#include "bot_adapter.h"
#include "rule_tetr_srs_plus.h"
#include "tetr_attack.h"
#include "tetr_spin.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

using Node = m_tetris::TetrisNode;

constexpr int kBoardWidth = 10;
constexpr int kBoardHeight = 40;
constexpr int kVisibleHeight = 20;
constexpr double kAnimationThresholdPps = 5.0;

struct Options {
    std::uint64_t pieces = 0;
    std::uint32_t seed = std::random_device{}();
    NativeBots::Kind bot = NativeBots::Kind::Botris;
    int strength = 1;
    int searchMs = 50;
    double pps = 2.5;
    int renderEvery = 1;
    bool headless = false;
    bool color = true;
    bool settings = true;
};

[[noreturn]] void usage(const char* program, int exitCode) {
    std::ostream& out = exitCode == 0 ? std::cout : std::cerr;
    out << "Usage: " << program << " [options]\n"
        << "  --pieces N       stop after N pieces (0 = unlimited, default)\n"
        << "  --seed N         deterministic seven-bag seed\n"
        << "  --bot NAME       botris, toj, dig, qq, or c2 (default: botris)\n"
        << "  --strength N     bot search width/strength (default: 1)\n"
        << "  --search-ms N    milliseconds per strength unit (default: 50)\n"
        << "  --pps N          target display speed (default: 2.5)\n"
        << "                   speeds below 5 PPS animate the movement path\n"
        << "  --render-every N draw every Nth locked piece at >=5 PPS (default: 1)\n"
        << "  --headless       disable rendering and pacing for soak tests\n"
        << "  --no-settings    skip the interactive startup settings page\n"
        << "  --no-color       draw occupied cells without ANSI color\n"
        << "  --help           show this message\n";
    std::exit(exitCode);
}

std::uint64_t parse_unsigned(const char* text, const char* name) {
    try {
        std::size_t used = 0;
        const std::string value(text);
        const auto result = std::stoull(value, &used, 10);
        if (used != value.size()) throw std::invalid_argument("trailing data");
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
}

double parse_real(const char* text, const char* name) {
    try {
        std::size_t used = 0;
        const std::string value(text);
        const double result = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(result)) {
            throw std::invalid_argument("invalid real number");
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        auto value = [&](const char* name) {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };
        if (argument == "--pieces") {
            options.pieces = parse_unsigned(value("--pieces"), "piece count");
        } else if (argument == "--bot") {
            const std::string name(value("--bot"));
            if (!NativeBots::parse_kind(name, options.bot)) {
                throw std::runtime_error("unknown --bot value: " + name);
            }
        } else if (argument == "--strength") {
            const auto strength = parse_unsigned(value("--strength"), "bot strength");
            if (strength < 1 || strength > 20) {
                throw std::runtime_error("--strength must be in [1, 20]");
            }
            options.strength = static_cast<int>(strength);
        } else if (argument == "--seed") {
            const auto seed = parse_unsigned(value("--seed"), "seed");
            if (seed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("seed is outside uint32 range");
            }
            options.seed = static_cast<std::uint32_t>(seed);
        } else if (argument == "--search-ms") {
            const auto searchMs = parse_unsigned(value("--search-ms"), "search time");
            if (searchMs < 1 || searchMs > 10000) {
                throw std::runtime_error("--search-ms must be in [1, 10000]");
            }
            options.searchMs = static_cast<int>(searchMs);
        } else if (argument == "--pps") {
            options.pps = parse_real(value("--pps"), "PPS");
            if (options.pps < 0.1 || options.pps > 100.0) {
                throw std::runtime_error("--pps must be in [0.1, 100]");
            }
        } else if (argument == "--render-every") {
            const auto renderEvery = parse_unsigned(value("--render-every"), "render interval");
            if (renderEvery < 1 || renderEvery > std::numeric_limits<int>::max()) {
                throw std::runtime_error("--render-every must be in [1, INT_MAX]");
            }
            options.renderEvery = static_cast<int>(renderEvery);
        } else if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--no-settings") {
            options.settings = false;
        } else if (argument == "--no-color") {
            options.color = false;
        } else if (argument == "--help") {
            usage(argv[0], 0);
        } else {
            throw std::runtime_error("unknown option: " + argument);
        }
    }
    const auto& bot = NativeBots::info(options.bot);
    if (bot.supportsStrength && options.strength > bot.maxStrength) {
        throw std::runtime_error("--strength exceeds the selected bot's maximum of " +
                                 std::to_string(bot.maxStrength));
    }
    return options;
}

class Terminal {
public:
    explicit Terminal(bool enabled) : enabled_(enabled) {
#ifdef _WIN32
        output_ = GetStdHandle(STD_OUTPUT_HANDLE);
        enabled_ = enabled_ && GetConsoleMode(output_, &savedOutputMode_);
        if (!enabled_) return;
        if (!SetConsoleMode(output_, savedOutputMode_ | ENABLE_PROCESSED_OUTPUT |
            ENABLE_VIRTUAL_TERMINAL_PROCESSING)) { enabled_ = false; return; }
        savedOutputCodePage_ = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        inputEnabled_ = GetConsoleMode(input_, &savedInputMode_) != 0;
        if (inputEnabled_) SetConsoleMode(input_, (savedInputMode_ | ENABLE_EXTENDED_FLAGS) &
            ~(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
              ENABLE_VIRTUAL_TERMINAL_INPUT));
#else
        enabled_ = enabled_ && ::isatty(STDOUT_FILENO);
        if (!enabled_) return;
        inputEnabled_ = ::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &saved_) == 0;
        if (inputEnabled_) {
            termios raw = saved_;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            savedFlags_ = ::fcntl(STDIN_FILENO, F_GETFL, 0);
            ::fcntl(STDIN_FILENO, F_SETFL, savedFlags_ | O_NONBLOCK);
        }
#endif
        std::cout << "\x1b[?1049h\x1b[48;2;12;12;12m\x1b[2J\x1b[H\x1b[?25l"
                  << std::flush;
    }

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    ~Terminal() {
        if (!enabled_) return;
#ifndef _WIN32
        if (inputEnabled_) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
            ::fcntl(STDIN_FILENO, F_SETFL, savedFlags_);
        }
#endif
        std::cout << "\x1b[0m\x1b[?25h\x1b[?1049l" << std::flush;
#ifdef _WIN32
        if (inputEnabled_) SetConsoleMode(input_, savedInputMode_);
        SetConsoleMode(output_, savedOutputMode_);
        if (savedOutputCodePage_) SetConsoleOutputCP(savedOutputCodePage_);
#endif
    }

    bool enabled() const noexcept { return enabled_; }

    char read_key() const noexcept {
        if (!inputEnabled_) return '\0';
#ifdef _WIN32
        if (!_kbhit()) return '\0';
        const int key = _getch();
        if (key == 0 || key == 0xe0) { (void)_getch(); return '\0'; }
        return static_cast<char>(key);
#else
        char key = '\0';
        return ::read(STDIN_FILENO, &key, 1) == 1 ? key : '\0';
#endif
    }

private:
    bool enabled_ = false;
    bool inputEnabled_ = false;
#ifdef _WIN32
    HANDLE input_ = INVALID_HANDLE_VALUE, output_ = INVALID_HANDLE_VALUE;
    DWORD savedInputMode_ = 0, savedOutputMode_ = 0;
    UINT savedOutputCodePage_ = 0;
#else
    termios saved_{};
    int savedFlags_ = 0;
#endif
};

std::size_t bot_index(NativeBots::Kind kind) {
    for (std::size_t i = 0; i < NativeBots::kBots.size(); ++i) {
        if (NativeBots::kBots[i].kind == kind) return i;
    }
    return 0;
}

bool edit_settings(const Terminal& terminal, Options& options) {
    if (!terminal.enabled()) return true;
    int selected = 0;
    int escapeState = 0;
    bool redraw = true;

    auto adjust = [&](int direction) {
        if (selected == 0) {
            const int count = static_cast<int>(NativeBots::kBots.size());
            int index = static_cast<int>(bot_index(options.bot));
            index = (index + direction + count) % count;
            options.bot = NativeBots::kBots[static_cast<std::size_t>(index)].kind;
            options.strength = std::min(
                options.strength, NativeBots::info(options.bot).maxStrength);
        } else if (selected == 1) {
            options.pps = std::clamp(options.pps + direction * 0.5, 0.5, 100.0);
        } else if (selected == 2 && NativeBots::info(options.bot).supportsStrength) {
            options.strength = std::clamp(
                options.strength + direction, 1,
                NativeBots::info(options.bot).maxStrength);
        }
    };

    while (true) {
        if (redraw) {
            const auto& bot = NativeBots::info(options.bot);
            std::cout << "\x1b[H\x1b[2J"
                      << "            TETRIS AI RUNNER SETTINGS\n"
                      << "  +------------------------------------------+\n";
            const auto row = [&](int index, const std::string& label,
                                 const std::string& value) {
                std::cout << (selected == index ? "  > " : "    ")
                          << std::left << std::setw(13) << label
                          << "[ " << std::setw(20) << value << " ]\n";
            };
            row(0, "Bot", std::string(bot.displayName));
            row(1, "PPS", std::to_string(options.pps));
            row(2, "Strength", bot.supportsStrength
                ? std::to_string(options.strength)
                : std::string("N/A (native fixed)"));
            std::cout << "  +------------------------------------------+\n"
                      << "  W/S or arrows: select    A/D or arrows: change\n"
                      << "  Enter: start             Q: quit\n"
                      << "\n  " << bot.id
                      << (bot.supportsHold ? "  HOLD" : "  NO HOLD")
                      << (bot.supportsStrength
                              ? "  scalable strength"
                              : "  fixed native strength")
                      << "\x1b[J" << std::flush;
            redraw = false;
        }

        const char key = terminal.read_key();
        if (key == '\0') {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (escapeState == 1) {
            escapeState = key == '[' ? 2 : 0;
            continue;
        }
        if (escapeState == 2) {
            if (key == 'A') selected = (selected + 2) % 3;
            else if (key == 'B') selected = (selected + 1) % 3;
            else if (key == 'C') adjust(1);
            else if (key == 'D') adjust(-1);
            escapeState = 0;
            redraw = true;
            continue;
        }
        if (key == '\x1b') escapeState = 1;
        else if (key == 'w' || key == 'W') {
            selected = (selected + 2) % 3;
            redraw = true;
        } else if (key == 's' || key == 'S') {
            selected = (selected + 1) % 3;
            redraw = true;
        } else if (key == 'a' || key == 'A' || key == '-') {
            adjust(-1);
            redraw = true;
        } else if (key == 'd' || key == 'D' || key == '+' || key == '=') {
            adjust(1);
            redraw = true;
        }
        else if (key == '\r' || key == '\n') return true;
        else if (key == 'q' || key == 'Q') return false;
    }
}

struct Metrics {
    int height = 0;
    int holes = 0;
};

Metrics metrics(const m_tetris::TetrisMap& map) {
    Metrics result;
    for (int x = 0; x < map.width; ++x) {
        bool covered = false;
        for (int y = map.height - 1; y >= 0; --y) {
            if (map.full(x, y)) {
                if (!covered) result.height = std::max(result.height, y + 1);
                covered = true;
            } else if (covered) {
                ++result.holes;
            }
        }
    }
    return result;
}

struct PathExecution {
    const Node* finalNode = nullptr;
    std::vector<const Node*> frames;
    bool lastRotated = false;
    int lastKickIndex = 0;
};

void append_frame(PathExecution& execution, const Node* node) {
    if (execution.frames.empty() || execution.frames.back() != node) {
        execution.frames.push_back(node);
    }
}

PathExecution execute_path(const Node* node, const m_tetris::TetrisMap& map,
                           const std::vector<char>& path) {
    PathExecution execution;
    append_frame(execution, node);

    auto translate = [&](const Node* candidate) {
        node = candidate;
        execution.lastRotated = false;
        execution.lastKickIndex = 0;
        append_frame(execution, node);
    };
    auto rotate = [&](const Node* const* kicks) {
        for (int kick = 0; kick < m_tetris::max_wall_kick; ++kick) {
            const Node* candidate = kicks[kick];
            if (candidate == nullptr) break;
            if (candidate->check(map)) {
                node = candidate;
                execution.lastRotated = true;
                execution.lastKickIndex = kick;
                append_frame(execution, node);
                break;
            }
        }
    };
    auto drop_one_cell_at_a_time = [&]() {
        while (node->move_down != nullptr && node->move_down->check(map)) {
            translate(node->move_down);
        }
    };

    for (const char operation : path) {
        switch (operation) {
            case 'L':
                while (node->move_left != nullptr && node->move_left->check(map)) {
                    translate(node->move_left);
                }
                break;
            case 'R':
                while (node->move_right != nullptr && node->move_right->check(map)) {
                    translate(node->move_right);
                }
                break;
            case 'd':
                if (node->move_down != nullptr && node->move_down->check(map)) {
                    translate(node->move_down);
                }
                break;
            case 'D':
                drop_one_cell_at_a_time();
                break;
            case 'l':
                if (node->move_left != nullptr && node->move_left->check(map)) {
                    translate(node->move_left);
                }
                break;
            case 'r':
                if (node->move_right != nullptr && node->move_right->check(map)) {
                    translate(node->move_right);
                }
                break;
            case 'z': case 'Z':
                rotate(node->wall_kick_counterclockwise);
                break;
            case 'x': case 'X':
                rotate(node->wall_kick_opposite);
                break;
            case 'c': case 'C':
                rotate(node->wall_kick_clockwise);
                break;
            default:
                break;
        }
    }
    drop_one_cell_at_a_time();
    execution.finalNode = node;
    return execution;
}

class NativeBotGame {
public:
    using FrameCallback = std::function<void(const Node*, std::size_t, std::size_t)>;

    explicit NativeBotGame(const Options& options)
        : options_(options), bot_(NativeBots::make_adapter(
              options.bot, kBoardWidth, kBoardHeight)),
          map_(kBoardWidth, kBoardHeight), random_(options.seed) {
        for (auto& row : colors_) row.fill(' ');
    }

    bool step(const FrameCallback& showFrame = {}) {
        prepare_next();
        const char current = next_.front();
        const auto begin = std::chrono::steady_clock::now();
        const NativeBots::Decision decision = bot_->choose(
            map_, current, hold_, next_.data() + 1, kPreviewLength,
            true,
            {tetrAttackState_.combo, tetrAttackState_.b2b},
            options_.strength, options_.searchMs);
        lastSearchMs_ = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        if (decision.target == nullptr || decision.target->low >= kVisibleHeight) {
            dead_ = true;
            return false;
        }

        const Node* node = bot_->context()->generate(current);
        if (decision.changeHold) {
            node = bot_->context()->generate(decision.target->status.t);
            if (hold_ == ' ') next_.erase(next_.begin());
            hold_ = current;
        }
        lastPiece_ = decision.target->status.t;

        const PathExecution execution = execute_path(node, map_, decision.path);
        node = execution.finalNode;
        if (node == nullptr || node->index_filtered != decision.target->index_filtered) {
            throw std::runtime_error("upstream make_path did not reach its selected target");
        }

        if (showFrame) {
            for (std::size_t frame = 0; frame < execution.frames.size(); ++frame) {
                showFrame(execution.frames[frame], frame, execution.frames.size());
            }
        }

        const TetrSpin::Result spin = classify_tetr_spin(node, execution);
        if (decision.reportedSpinKnown) ++spinReports_;
        if (decision.reportedSpinKnown && decision.reportedSpin) ++nativeSpinTargets_;
        if (spin.spin) ++tetrSpinPlacements_;
        if (spin.mini) ++tetrMiniSpinPlacements_;
        if (decision.reportedSpinKnown && decision.reportedSpin != spin.spin) {
            ++spinClassificationMismatches_;
        }
        const int colorClears = lock_color_board(node);
        const int cleared = static_cast<int>(node->attach(bot_->context().get(), map_));
        if (colorClears != cleared) {
            throw std::runtime_error("color board and native map disagree on cleared rows");
        }
        verify_color_board();

        const bool perfectClear = map_.count == 0;
        const auto tetrResult = TetrAttack::calculate(
            tetrAttackState_, TetrAttack::Input{cleared, spin.spin, spin.mini, perfectClear});
        tetrAttackState_ = tetrResult.state;
        lastClear_ = cleared;
        lastAttack_ = tetrResult.attack;
        lastSpin_ = spin.spin;
        lastMini_ = spin.mini;
        lastPerfectClear_ = perfectClear;
        ++blocks_;
        lines_ += cleared;
        attack_ += lastAttack_;
        const Metrics board = metrics(map_);
        maxHeight_ = std::max(maxHeight_, board.height);
        maxHoles_ = std::max(maxHoles_, board.holes);
        return true;
    }

    void render(const Terminal& terminal, double targetPps, bool paused,
                std::chrono::steady_clock::time_point started,
                const Node* active = nullptr) const {
        if (!terminal.enabled()) return;
        const Metrics boardMetrics = metrics(map_);
        const double elapsed = std::max(0.001, std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count());
        const auto left = left_panel(boardMetrics, targetPps, paused, elapsed);
        const auto right = right_panel();

        std::cout << "\x1b[H";
        for (int screenRow = 0; screenRow < kVisibleHeight + 2; ++screenRow) {
            std::cout << left[screenRow];
            if (screenRow == 0 || screenRow == kVisibleHeight + 1) {
                std::cout << "+--------------------+";
            } else {
                const int y = kVisibleHeight - screenRow;
                std::cout << '|';
                const Node* ghost = active == nullptr ? nullptr : active->drop(map_);
                for (int x = 0; x < kBoardWidth; ++x) {
                    if (active != nullptr && node_has_cell(active, x, y)) {
                        std::cout << painted_cell(active->status.t);
                    } else if (ghost != nullptr && ghost != active &&
                               !map_.full(x, y) && node_has_cell(ghost, x, y)) {
                        std::cout << ghost_cell();
                    } else if (colors_[y][x] != ' ') {
                        std::cout << painted_cell(colors_[y][x]);
                    } else {
                        std::cout << "  ";
                    }
                }
                std::cout << '|';
            }
            std::cout << right[screenRow] << "\x1b[K\n";
        }
        std::cout << "  Space pause   +/- speed   F fastest   Q quit"
                  << "\x1b[K\n\x1b[J" << std::flush;
    }

    std::string summary(double targetPps) const {
        const Metrics board = metrics(map_);
        std::ostringstream out;
        out << "tetris_ai_runner bot=" << bot_->bot_info().id
            << " blocks=" << blocks_
            << " target=" << options_.pieces
            << " seed=" << options_.seed
            << " strength=" << options_.strength
            << " search_unit_ms=" << options_.searchMs
            << " rotation=srs_plus"
            << " spin_rule=all_mini_plus"
            << " target_pps=" << std::fixed << std::setprecision(2) << targetPps
            << " lines=" << lines_
            << " attack=" << attack_
            << " app=" << std::setprecision(6)
            << attack_ / std::max(1.0, static_cast<double>(blocks_))
            << " height=" << board.height
            << " holes=" << board.holes
            << " max_height=" << maxHeight_
            << " max_holes=" << maxHoles_
            << " spin_reports=" << spinReports_
            << " reported_spin_targets=" << nativeSpinTargets_
            << " tetr_spins=" << tetrSpinPlacements_
            << " tetr_mini_spins=" << tetrMiniSpinPlacements_
            << " spin_mismatches=" << spinClassificationMismatches_
            << " dead=" << dead_ << '\n';
        return out.str();
    }

    std::uint64_t blocks() const noexcept { return blocks_; }
    bool dead() const noexcept { return dead_; }

private:
    static constexpr std::size_t kPreviewLength = 6;
    static constexpr std::size_t kDisplayedNext = 5;

    static std::string fixed(double value, int precision) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }

    static std::string fit(std::string value, std::size_t width) {
        if (value.size() > width) value.resize(width);
        value.append(width - value.size(), ' ');
        return value;
    }

    static std::string panel_line(const std::string& text) {
        return '|' + fit(text, 10) + '|';
    }

    static std::string value_line(const std::string& label, const std::string& value) {
        const std::size_t valueWidth = label.size() < 9 ? 9 - label.size() : 0;
        std::string clipped = value;
        if (clipped.size() > valueWidth) clipped.resize(valueWidth);
        return panel_line(' ' + label + std::string(valueWidth - clipped.size(), ' ') + clipped);
    }

    static std::array<std::string, 2> preview_shape(char piece) {
        switch (piece) {
            case 'I': return {{"IIII", "...."}};
            case 'J': return {{"J...", "JJJ."}};
            case 'L': return {{"..L.", "LLL."}};
            case 'O': return {{".OO.", ".OO."}};
            case 'S': return {{".SS.", "SS.."}};
            case 'T': return {{".T..", "TTT."}};
            case 'Z': return {{"ZZ..", ".ZZ."}};
            default: return {{"....", "...."}};
        }
    }

    std::string painted_cell(char piece) const {
        if (!options_.color) return "##";
        switch (piece) {
            case 'I': return "\x1b[38;2;54;235;168m██\x1b[39m";
            case 'J': return "\x1b[38;2;95;78;179m██\x1b[39m";
            case 'L': return "\x1b[38;2;197;117;68m██\x1b[39m";
            case 'O': return "\x1b[38;2;193;168;65m██\x1b[39m";
            case 'S': return "\x1b[38;2;139;185;61m██\x1b[39m";
            case 'T': return "\x1b[38;2;176;78;166m██\x1b[39m";
            case 'Z': return "\x1b[38;2;202;70;77m██\x1b[39m";
            default: return "██";
        }
    }

    std::string ghost_cell() const {
        return options_.color ? "\x1b[38;2;54;57;62m██\x1b[39m" : "..";
    }

    std::string shape_line(char piece, int row) const {
        std::string result = "| ";
        const auto shape = preview_shape(piece);
        for (const char cell : shape[row]) {
            result += cell == '.' ? "  " : painted_cell(piece);
        }
        result += " |";
        return result;
    }

    std::array<std::string, kVisibleHeight + 2> left_panel(
        const Metrics& board, double targetPps, bool paused, double elapsed) const {
        std::array<std::string, kVisibleHeight + 2> panel{};
        panel.fill("            ");
        panel[0] = "+---HOLD---+";
        panel[1] = shape_line(hold_, 0);
        panel[2] = shape_line(hold_, 1);
        panel[3] = "+----------+";
        panel[5] = "+---DATA---+";
        panel[6] = value_line("P", std::to_string(blocks_));
        panel[7] = value_line("A", std::to_string(attack_));
        panel[8] = value_line("S", fixed(elapsed, 2));
        panel[9] = value_line("PPS", fixed(blocks_ / elapsed, 2));
        panel[10] = value_line("APM", fixed(attack_ * 60.0 / elapsed, 1));
        panel[11] = value_line("APP", fixed(attack_ / std::max(1.0, static_cast<double>(blocks_)), 2));
        panel[12] = value_line("LINE", std::to_string(lines_));
        panel[13] = value_line("H", std::to_string(board.height));
        panel[14] = value_line("HOLE", std::to_string(board.holes));
        panel[15] = value_line("B2B", std::to_string(tetrAttackState_.b2b));
        panel[16] = value_line("COM", std::to_string(tetrAttackState_.combo));
        panel[17] = value_line("AI", fixed(lastSearchMs_, 1));
        panel[18] = value_line("TGT", fixed(targetPps, 1));
        std::string state = paused ? " PAUSED" : " ";
        if (lastSpin_) state += lastMini_ ? "MINI" : "TSPIN";
        else if (lastPerfectClear_) state += "PC";
        else state += lastPiece_ == ' ' ? "READY" : std::string(1, lastPiece_);
        panel[19] = panel_line(state);
        panel[20] = "+----------+";
        return panel;
    }

    std::array<std::string, kVisibleHeight + 2> right_panel() const {
        std::array<std::string, kVisibleHeight + 2> panel{};
        panel.fill("            ");
        panel[0] = "+---NEXT---+";
        for (std::size_t i = 0; i < kDisplayedNext; ++i) {
            const char piece = i + 1 < next_.size() ? next_[i + 1] : ' ';
            panel[1 + i * 3] = shape_line(piece, 0);
            panel[2 + i * 3] = shape_line(piece, 1);
            panel[3 + i * 3] = panel_line("");
        }
        panel[16] = "+----------+";
        panel[17] = panel_line("AI RUNNER");
        panel[18] = panel_line(std::string(bot_->bot_info().id));
        panel[19] = panel_line("SRS+");
        panel[20] = panel_line("ALL-MINI+");
        return panel;
    }

    static bool node_has_cell(const Node* node, int x, int y) {
        if (node == nullptr || y < node->row || y >= node->row + node->height ||
            x < 0 || x >= kBoardWidth) {
            return false;
        }
        return (node->data[y - node->row] & (std::uint32_t{1} << x)) != 0;
    }

    bool occupied_for_tetr_spin(int x, int y) const {
        if (x < 0 || x >= kBoardWidth || y < 0) return true;
        if (y >= kBoardHeight) return false;
        return map_.full(x, y);
    }

    TetrSpin::Result classify_tetr_spin(const Node* node,
                                        const PathExecution& execution) const {
        const bool immobile =
            (node->move_down == nullptr || !node->move_down->check(map_)) &&
            (node->move_up == nullptr || !node->move_up->check(map_)) &&
            (node->move_left == nullptr || !node->move_left->check(map_)) &&
            (node->move_right == nullptr || !node->move_right->check(map_));

        // Host tetr T-pose center is (row = 20-status.y, col = status.x+1).
        // In the upstream bottom-origin map this is (x+1, y-1). The corner
        // order below is exactly src/rules/spin.cpp transformed to that map.
        static constexpr int corners[4][2] = {
            {-1, +1}, {+1, +1}, {+1, -1}, {-1, -1}
        };
        const int centerX = node->status.x + 1;
        const int centerY = node->status.y - 1;
        std::array<bool, 4> occupiedCorners{};
        for (int corner = 0; corner < 4; ++corner) {
            occupiedCorners[corner] = occupied_for_tetr_spin(
                centerX + corners[corner][0], centerY + corners[corner][1]);
        }
        return TetrSpin::classify({
            node->status.t,
            node->status.r,
            execution.lastRotated,
            execution.lastKickIndex,
            immobile,
            occupiedCorners
        });
    }

    int lock_color_board(const Node* node) {
        for (int dy = 0; dy < node->height; ++dy) {
            const int y = node->row + dy;
            for (int x = 0; x < kBoardWidth; ++x) {
                if ((node->data[dy] & (std::uint32_t{1} << x)) != 0) {
                    colors_[y][x] = node->status.t;
                }
            }
        }

        int destination = 0;
        int cleared = 0;
        for (int source = 0; source < kBoardHeight; ++source) {
            const bool full = std::all_of(colors_[source].begin(), colors_[source].end(),
                                          [](char cell) { return cell != ' '; });
            if (full) {
                ++cleared;
            } else {
                if (destination != source) colors_[destination] = colors_[source];
                ++destination;
            }
        }
        for (; destination < kBoardHeight; ++destination) colors_[destination].fill(' ');
        return cleared;
    }

    void verify_color_board() const {
        for (int y = 0; y < kBoardHeight; ++y) {
            for (int x = 0; x < kBoardWidth; ++x) {
                if ((colors_[y][x] != ' ') != map_.full(x, y)) {
                    throw std::runtime_error("color board diverged from native map");
                }
            }
        }
    }

    void prepare_next() {
        if (!next_.empty()) next_.erase(next_.begin());
        while (next_.size() <= kPreviewLength) {
            const std::size_t bagBegin = next_.size();
            for (std::size_t i = 0; i < bot_->context()->type_max(); ++i) {
                next_.push_back(bot_->context()->convert(i));
            }
            std::shuffle(next_.begin() + static_cast<std::ptrdiff_t>(bagBegin),
                         next_.end(), random_);
        }
    }

    Options options_;
    std::unique_ptr<NativeBots::Adapter> bot_;
    m_tetris::TetrisMap map_;
    std::array<std::array<char, kBoardWidth>, kBoardHeight> colors_{};
    std::mt19937 random_;
    std::vector<char> next_;
    char hold_ = ' ';
    char lastPiece_ = ' ';
    TetrAttack::State tetrAttackState_{};
    bool dead_ = false;
    bool lastSpin_ = false;
    bool lastMini_ = false;
    bool lastPerfectClear_ = false;
    std::uint64_t blocks_ = 0;
    std::uint64_t lines_ = 0;
    std::uint64_t attack_ = 0;
    std::uint64_t spinReports_ = 0;
    std::uint64_t nativeSpinTargets_ = 0;
    std::uint64_t tetrSpinPlacements_ = 0;
    std::uint64_t tetrMiniSpinPlacements_ = 0;
    std::uint64_t spinClassificationMismatches_ = 0;
    int lastClear_ = 0;
    int lastAttack_ = 0;
    int maxHeight_ = 0;
    int maxHoles_ = 0;
    double lastSearchMs_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_options(argc, argv);
        std::string finalSummary;
        bool finalDead = false;
        {
            Terminal terminal(!options.headless);
            if (options.settings && !options.headless && !edit_settings(terminal, options)) {
                return 0;
            }
            NativeBotGame game(options);
            const auto started = std::chrono::steady_clock::now();
            bool paused = false;
            bool quit = false;
            double targetPps = options.pps;

            auto process_keys = [&]() {
                for (char key = terminal.read_key(); key != '\0'; key = terminal.read_key()) {
                    if (key == 'q' || key == 'Q') quit = true;
                    else if (key == ' ') paused = !paused;
                    else if (key == '+' || key == '=') targetPps = std::min(100.0, targetPps + 0.5);
                    else if (key == '-') targetPps = std::max(0.5, targetPps - 0.5);
                    else if (key == 'f' || key == 'F') targetPps = 100.0;
                }
            };

            while (!quit && !game.dead() &&
                   (options.pieces == 0 || game.blocks() < options.pieces)) {
                process_keys();
                if (quit) break;
                if (paused) {
                    game.render(terminal, targetPps, true, started);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }

                const auto pieceStarted = std::chrono::steady_clock::now();
                const auto pieceDuration = std::chrono::duration<double>(1.0 / targetPps);
                const auto deadline = pieceStarted +
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(pieceDuration);
                const bool animate = terminal.enabled() && targetPps < kAnimationThresholdPps;
                const auto showFrame = animate ? NativeBotGame::FrameCallback{
                    [&](const Node* active, std::size_t frame, std::size_t frameCount) {
                        game.render(terminal, targetPps, false, started, active);
                        process_keys();
                        const auto now = std::chrono::steady_clock::now();
                        const auto framesLeft = std::max<std::size_t>(1, frameCount - frame);
                        if (now < deadline) {
                            std::this_thread::sleep_for((deadline - now) / framesLeft);
                        }
                    }} : NativeBotGame::FrameCallback{};

                if (!game.step(showFrame)) break;
                if (game.blocks() % static_cast<std::uint64_t>(options.renderEvery) == 0) {
                    game.render(terminal, targetPps, false, started);
                }
                if (terminal.enabled() && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_until(deadline);
                }
            }

            game.render(terminal, targetPps, false, started);
            finalSummary = game.summary(targetPps);
            finalDead = game.dead();
        }
        std::cout << finalSummary;
        return finalDead ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "zzztoj_visualizer: " << error.what() << '\n';
        return 2;
    }
}
