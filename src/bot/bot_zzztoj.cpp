#include "tetr/bot/bot_zzztoj.h"
#include "tetr/bot/quick_play_search.h"

#include "bot_piece_access.h"
#include "bot_adapter.h"
#include "native_host.h"
#include "tetr/rules/spin.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Bot_zzztoj {
namespace {

constexpr int kBoardWidth = 10;
constexpr int kSearchHeight = 40;

struct EngineState {
    std::mutex mutex;
    std::array<std::unique_ptr<NativeBots::Adapter>, NativeBots::kBots.size()> adapters;
};

EngineState& engine_state() {
    static EngineState state;
    return state;
}

NativeBots::Kind selected_kind(const Bot::Environment& environment) noexcept {
    switch (environment.autoplayMode) {
        case 8: return NativeBots::Kind::Toj;
        case 9: return NativeBots::Kind::Dig;
        case 10: return NativeBots::Kind::Qq;
        case 11: return NativeBots::Kind::C2;
        default: return NativeBots::Kind::Botris;
    }
}

std::size_t kind_index(NativeBots::Kind kind) noexcept {
    return static_cast<std::size_t>(kind);
}

char piece_char(int piece) noexcept {
    constexpr char kPieces[] = {'I', 'J', 'L', 'O', 'S', 'T', 'Z'};
    return piece >= 0 && piece < 7 ? kPieces[piece] : '\0';
}

int piece_index(char piece) noexcept {
    switch (piece) {
        case 'I': return 0;
        case 'J': return 1;
        case 'L': return 2;
        case 'O': return 3;
        case 'S': return 4;
        case 'T': return 5;
        case 'Z': return 6;
        default: return -1;
    }
}

bool supports(const Bot::Environment& environment) {
    return bool(availability(environment));
}

m_tetris::TetrisMap make_upstream_map(const Runtime::Playfield& field) {
    m_tetris::TetrisMap map(kBoardWidth, kSearchHeight);
    for (int hostRow = 19; hostRow >= -20; --hostRow) {
        const int upstreamRow = 19 - hostRow;
        for (int col = 0; col < kBoardWidth; ++col) {
            if (field.contains(hostRow, col) && field.cell(hostRow, col).occupied()) {
                map.row[upstreamRow] |= std::uint32_t{1} << col;
                map.top[col] = upstreamRow + 1;
                map.roof = std::max(map.roof, upstreamRow + 1);
                ++map.count;
            }
        }
    }
    return map;
}

struct SearchState {
    int row = 0;
    int col = 0;
    int rotation = 0;
    int kick = 0;
};

std::uint64_t state_key(const SearchState& state) noexcept {
    return (static_cast<std::uint64_t>(state.row + 128) << 24) |
           (static_cast<std::uint64_t>(state.col + 16) << 16) |
           (static_cast<std::uint64_t>(state.rotation) << 8) |
           static_cast<std::uint64_t>(state.kick);
}

void apply_instant_gravity(SearchState& state, const Piece::Definition& piece,
                           const Runtime::Playfield& field, bool instantGravity) {
    if (!instantGravity) return;
    const int originalRow = state.row;
    while (BotPiece::can_place(piece, state.row + 1, state.col, state.rotation, field)) {
        ++state.row;
    }
    if (state.row != originalRow) state.kick = 0;
}

std::vector<Piece::Pose> matching_host_poses(const m_tetris::TetrisNode* target,
                                             const Piece::Definition& piece,
                                             const Runtime::Playfield& field) {
    std::array<std::pair<int, int>, 4> targetCells{};
    if (target == nullptr) return {};
    std::size_t cell = 0;
    for (int dy = 0; dy < target->height; ++dy) {
        const std::uint32_t bits = target->data[dy];
        for (int col = 0; col < kBoardWidth; ++col) {
            if ((bits & (std::uint32_t{1} << col)) == 0) continue;
            if (cell == targetCells.size()) return {};
            targetCells[cell++] = {19 - (target->row + dy), col};
        }
    }
    if (cell != targetCells.size()) return {};
    std::sort(targetCells.begin(), targetCells.end());

    std::vector<Piece::Pose> result;
    for (int rotation = 0; rotation < 4; ++rotation) {
        const auto& shape = piece.cells(Runtime::Rotation::from_index(rotation));
        for (const auto& targetCell : targetCells) {
            for (const auto& offset : shape) {
                const int row = targetCell.first - offset[0];
                const int col = targetCell.second - offset[1];
                Piece::Pose pose{{row, col}, Runtime::Rotation::from_index(rotation)};
                if (!piece.can_place(pose, field) ||
                    piece.can_place(pose.translated(1, 0), field)) {
                    continue;
                }
                std::array<std::pair<int, int>, 4> hostCells{};
                for (std::size_t cell = 0; cell < hostCells.size(); ++cell) {
                    hostCells[cell] = {row + shape[cell][0], col + shape[cell][1]};
                }
                std::sort(hostCells.begin(), hostCells.end());
                if (hostCells == targetCells &&
                    std::find(result.begin(), result.end(), pose) == result.end()) {
                    result.push_back(pose);
                }
            }
        }
    }
    return result;
}

bool is_target(const SearchState& state, const std::vector<Piece::Pose>& targets,
               bool requireLastRotation) {
    if (requireLastRotation && state.kick == 0) return false;
    const Piece::Pose pose{{state.row, state.col}, Runtime::Rotation::from_index(state.rotation)};
    return std::find(targets.begin(), targets.end(), pose) != targets.end();
}

bool find_reachable_pose(const Piece::Definition& piece, const Runtime::Playfield& field,
                         Piece::Pose start, const std::vector<Piece::Pose>& targets,
                         bool instantGravity, bool requireLastRotation,
                         Piece::Pose& answer, int& answerKick, int expectedMini = -1,
                         const Bot::Environment* environment = nullptr) {
    if (!piece.can_place(start, field)) return false;

    std::deque<SearchState> queue;
    std::unordered_set<std::uint64_t> visited;
    auto enqueue = [&](SearchState candidate) {
        apply_instant_gravity(candidate, piece, field, instantGravity);
        if (!BotPiece::can_place(piece, candidate.row, candidate.col,
                                 candidate.rotation, field)) {
            return;
        }
        if (visited.insert(state_key(candidate)).second) queue.push_back(candidate);
    };

    enqueue(SearchState{start.position.row, start.position.col, start.rotation.index(), 0});
    while (!queue.empty()) {
        const SearchState current = queue.front();
        queue.pop_front();
        if (is_target(current, targets, requireLastRotation)) {
            bool accepted = true;
            if (expectedMini >= 0) {
                Rules::SpinInput spin;
                spin.spinChecker = Piece::metadata(piece.type()).spinCheck;
                spin.canonicalGeometry=Piece::metadata(piece.type()).tetrioRotation;
                spin.rotationSystem=Piece::metadata(piece.type()).rotationSystem;
                if(environment)spin.bonuses=environment->spinBonuses;
                spin.pose = {{current.row, current.col}, Runtime::Rotation::from_index(current.rotation)};
                spin.lastRotation = current.kick
                    ? Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(current.kick - 1))
                    : Runtime::RotationRecord::none();
                spin.mapWidth = 10;
                spin.mapHeight = 20;
                spin.immobile = !piece.can_place(spin.pose.translated(-1, 0), field) &&
                    !piece.can_place(spin.pose.translated(0, -1), field) &&
                    !piece.can_place(spin.pose.translated(0, 1), field);
                const auto result = Rules::detect_spin(spin, [&](int row, int col) {
                    return field.contains(row, col) && field.cell(row, col).occupied();
                });
                accepted = result.spin && result.mini == (expectedMini != 0);
            }
            if (accepted) {
                answer = {{current.row, current.col}, Runtime::Rotation::from_index(current.rotation)};
                answerKick = current.kick;
                return true;
            }
        }

        for (int colDelta : {-1, 1}) {
            SearchState moved = current;
            moved.col += colDelta;
            moved.kick = 0;
            enqueue(moved);
        }
        if (!instantGravity) {
            if(!environment || environment->partial_descent_available()) {
                SearchState down = current;
                ++down.row;
                down.kick = 0;
                enqueue(down);
            }

            SearchState dropped = current;
            while (BotPiece::can_place(piece, dropped.row + 1, dropped.col,
                                       dropped.rotation, field)) {
                ++dropped.row;
            }
            dropped.kick = 0;
            enqueue(dropped);
        }
        for (int delta = 1; delta <= 3; ++delta) {
            int row = current.row;
            int col = current.col;
            const int targetRotation = (current.rotation + delta) % 4;
            int kick = 0;
            if (BotPiece::try_rotate(piece, row, col, current.rotation,
                                     targetRotation, field, kick)) {
                enqueue(SearchState{row, col, targetRotation, std::min(kick + 1, 6)});
            }
        }
    }
    return false;
}

int safe_fallback(const Runtime::Playfield& field, const std::vector<int>& next,
                  int startRow, int startCol, int startRotation,
                  bool instantGravity,
                  int& answerRow, int& answerCol, int& answerRotation,
                  int& answerKick) {
    const int current = next.empty() ? -1 : next.front();
    if (piece_char(current) == '\0') return -1;
    const Piece::Definition piece = BotPiece::definition(current);
    if (!BotPiece::can_place(piece, startRow, startCol, startRotation, field)) return -1;

    auto placement_score = [&](const SearchState& state) {
        constexpr std::uint16_t kFullRow = (std::uint16_t{1} << kBoardWidth) - 1;
        std::array<std::uint16_t, kSearchHeight> rows{};
        for (int hostRow = 19; hostRow >= -20; --hostRow) {
            const int y = 19 - hostRow;
            for (int col = 0; col < kBoardWidth; ++col) {
                if (field.cell(hostRow, col).occupied()) {
                    rows[y] |= std::uint16_t{1} << col;
                }
            }
        }
        const auto& cells = piece.cells(Runtime::Rotation::from_index(state.rotation));
        for (const auto& offset : cells) {
            const int hostRow = state.row + offset[0];
            const int col = state.col + offset[1];
            rows[19 - hostRow] |= std::uint16_t{1} << col;
        }

        int destination = 0;
        int cleared = 0;
        for (int source = 0; source < kSearchHeight; ++source) {
            if (rows[source] == kFullRow) {
                ++cleared;
            } else {
                rows[destination++] = rows[source];
            }
        }
        std::fill(rows.begin() + destination, rows.end(), std::uint16_t{0});

        std::array<int, kBoardWidth> heights{};
        int holes = 0;
        int aggregateHeight = 0;
        int maxHeight = 0;
        for (int col = 0; col < kBoardWidth; ++col) {
            int top = -1;
            for (int y = kSearchHeight - 1; y >= 0; --y) {
                if ((rows[y] & (std::uint16_t{1} << col)) != 0) {
                    top = y;
                    break;
                }
            }
            heights[col] = top + 1;
            aggregateHeight += heights[col];
            maxHeight = std::max(maxHeight, heights[col]);
            for (int y = 0; y < top; ++y) {
                if ((rows[y] & (std::uint16_t{1} << col)) == 0) ++holes;
            }
        }
        int bumpiness = 0;
        for (int col = 1; col < kBoardWidth; ++col) {
            bumpiness += std::abs(heights[col] - heights[col - 1]);
        }
        return static_cast<long long>(holes) * 100000LL +
               static_cast<long long>(maxHeight) * 5000LL +
               static_cast<long long>(aggregateHeight) * 200LL +
               static_cast<long long>(bumpiness) * 100LL -
               static_cast<long long>(cleared) * 10000LL;
    };

    std::deque<SearchState> queue;
    std::unordered_set<std::uint64_t> visited;
    auto enqueue = [&](SearchState candidate) {
        apply_instant_gravity(candidate, piece, field, instantGravity);
        if (!BotPiece::can_place(piece, candidate.row, candidate.col,
                                 candidate.rotation, field)) {
            return;
        }
        if (visited.insert(state_key(candidate)).second) queue.push_back(candidate);
    };
    enqueue(SearchState{startRow, startCol, startRotation, 0});

    bool found = false;
    long long bestScore = 0;
    SearchState best{};
    while (!queue.empty()) {
        const SearchState state = queue.front();
        queue.pop_front();
        if (!BotPiece::can_place(piece, state.row + 1, state.col,
                                 state.rotation, field)) {
            const long long score = placement_score(state);
            if (!found || score < bestScore) {
                found = true;
                bestScore = score;
                best = state;
            }
        }
        for (int colDelta : {-1, 1}) {
            SearchState moved = state;
            moved.col += colDelta;
            moved.kick = 0;
            enqueue(moved);
        }
        if (!instantGravity) {
            SearchState down = state;
            ++down.row;
            down.kick = 0;
            enqueue(down);
        }
        for (int delta = 1; delta <= 3; ++delta) {
            int row = state.row;
            int col = state.col;
            const int rotation = (state.rotation + delta) % 4;
            int kick = 0;
            if (BotPiece::try_rotate(piece, row, col, state.rotation,
                                     rotation, field, kick)) {
                enqueue(SearchState{row, col, rotation, std::min(kick + 1, 6)});
            }
        }
    }
    if (!found) return -1;
    answerRow = best.row;
    answerCol = best.col;
    answerRotation = best.rotation;
    answerKick = best.kick;
    return current;
}

int search_budget_ms(int configuredWidth) noexcept {
    // The host setting is a beam-width-style quality knob whose established
    // default is 300. Botris accepts wall-clock milliseconds instead; its
    // upstream match runner uses 50 ms. Preserve the host scale while mapping
    // the default setting to that upstream reference budget.
    const int normalizedWidth = std::clamp(configuredWidth, 1, 1000);
    return (normalizedWidth + 5) / 6;
}

int native_strength(NativeBots::Kind kind, int configuredWidth) noexcept {
    const int scaled = std::max(1, (std::max(1, configuredWidth) + 299) / 300);
    return std::min(scaled, NativeBots::info(kind).maxStrength);
}

} // namespace

bool init(const Bot::Environment& environment) {
    if (!supports(environment)) return false;
    if (Bot::QuickPlaySearch::enabled(environment)) {
        const auto baseline=Bot::QuickPlaySearch::native_environment(environment);
        if(Bot::QuickPlaySearch::native_policy_compatible(environment) && availability(baseline))return init(baseline);
        return true;
    }
    auto& state = engine_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    const NativeBots::Kind kind = selected_kind(environment);
    try {
        state.adapters[kind_index(kind)] =
            NativeBots::make_adapter(kind, kBoardWidth, kSearchHeight);
    } catch (const std::exception&) {
        state.adapters[kind_index(kind)].reset();
        return false;
    }
    return true;
}

int getpos(const Bot::Environment& environment, const Runtime::PlayerSessionState& player,
           Runtime::Playfield field, std::vector<int> next,
           const std::vector<int>& unseenBagPieces,
           int startRow, int startCol, int startRotation, int hold,
           int& answerRow, int& answerCol, int& answerRotation, int& answerKick) {
    if(Bot::QuickPlaySearch::enabled(environment)) {
        using namespace Bot::QuickPlaySearch;
        const auto kind=selected_kind(environment);
        const auto profile=kind==NativeBots::Kind::Toj?Profile::Toj:
            kind==NativeBots::Kind::Dig?Profile::Dig:kind==NativeBots::Kind::Qq?Profile::Qq:
            kind==NativeBots::Kind::C2?Profile::C2:Profile::Botris;
        const NativePlanner native=[&](const NativeBaseline& baseline) {
            Decision result;
            if(!availability(baseline.environment))return result;
            int row=0,col=0,rotation=0,kick=0;
            result.selected=getpos(baseline.environment,*baseline.player,*baseline.field,next,unseenBagPieces,
                startRow+baseline.rowOffset,startCol,startRotation,hold,row,col,rotation,kick);
            result.pose={{row-baseline.rowOffset,col},Runtime::Rotation::from_index(rotation)};
            if(kick>0)result.rotation=Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(kick-1));
            return result;
        };
        const ShapeEvaluator terrain=[kind,profile](const std::vector<std::uint32_t>& rows,int width,int,int,int) {
            // Cache separate native evaluators: no upstream fixed-piece search
            // assumptions enter the shared variable-geometry rule simulator.
            if(width!=10)return Bot::QuickPlaySearch::terrain(profile,rows,width);
            struct Cache {int width=0;std::unique_ptr<NativeBots::Adapter> adapter;double empty=0;};
            thread_local std::array<Cache,5> caches;
            auto& cache=caches[kind_index(kind)];
            if(cache.width!=width || !cache.adapter) {
                cache.adapter=NativeBots::make_adapter(kind,width,kSearchHeight);
                cache.width=width;
                cache.empty=cache.adapter->terrain(m_tetris::TetrisMap(width,kSearchHeight));
            }
            m_tetris::TetrisMap map(width,kSearchHeight);
            for(std::size_t y=0;y<rows.size();++y) {
                if(y>=static_cast<std::size_t>(kSearchHeight)) {if(rows[y])return -1e12;continue;}
                map.row[y]=rows[y];
                if(rows[y])map.roof=static_cast<int>(y)+1;
                for(int x=0;x<width;++x)if(rows[y]&(std::uint32_t{1}<<x)) {
                    map.top[x]=static_cast<int>(y)+1;++map.count;
                }
            }
            return cache.adapter->terrain(map)-cache.empty;
        };
        const auto result=adapt(profile,environment,player,field,next,unseenBagPieces,
            {{startRow,startCol},Runtime::Rotation::from_index(startRotation)},hold,native,terrain);
        if(result.selected>=0) {
            answerRow=result.pose.position.row;answerCol=result.pose.position.col;answerRotation=result.pose.rotation.index();
            answerKick=result.rotation.rotated()?result.rotation.kick().index()+1:0;
            return result.selected;
        }
        return -1; // Shared host fallback uses the actual QP piece and field.
    }
    (void)unseenBagPieces;
    const bool trace = std::getenv("BOT_ZZZTOJ_ADAPTER_TRACE") != nullptr;
    const bool instantGravity = environment.gravityIntervalSeconds <= 1e-7;
    auto fallback = [&](const char* reason) {
        if (trace) std::cerr << "[bot_zzztoj adapter] fallback: " << reason << '\n';
        return safe_fallback(field, next, startRow, startCol, startRotation,
                             instantGravity,
                             answerRow, answerCol, answerRotation, answerKick);
    };
    if (!supports(environment) || next.empty() || piece_char(next.front()) == '\0') {
        return fallback("unsupported environment or invalid current piece");
    }

    auto& state = engine_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    const NativeBots::Kind kind = selected_kind(environment);
    NativeBots::Adapter* adapter = state.adapters[kind_index(kind)].get();
    if (adapter == nullptr) {
        return fallback("engine is not initialized");
    }

    const char* stage = "map conversion";
    try {
        const auto map = make_upstream_map(field);
        const char active = piece_char(next.front());
        const m_tetris::TetrisNode* node = adapter->context()->generate(active);
        if (node == nullptr || !node->check(map)) return fallback("spawn is blocked");

        std::vector<char> preview;
        preview.reserve(next.size() > 1 ? next.size() - 1 : 0);
        for (std::size_t i = 1; i < next.size(); ++i) {
            const char type = piece_char(next[i]);
            if (type != '\0') preview.push_back(type);
        }

        const bool canHold = environment.holdEnabled && hold != -2 &&
            adapter->bot_info().supportsHold;
        const char heldPiece = hold >= 0 ? piece_char(hold) : ' ';
        stage = "native bot search";
        const int strength = kind == NativeBots::Kind::Qq
            ? native_strength(kind, environment.autoplaySearchWidth)
            : 1;
        const int searchUnitMs = kind == NativeBots::Kind::Qq
            ? 50
            : search_budget_ms(environment.autoplaySearchWidth);
        const NativeBots::Decision decision = adapter->choose(
            map, active, heldPiece, preview.data(), preview.size(), canHold,
            {player.gameplay.scoring.combo, player.gameplay.scoring.b2b},
            strength, searchUnitMs);
        if (decision.target == nullptr) return fallback("upstream returned no target");

        int selected = next.front();
        if (decision.changeHold) {
            if (!canHold || (hold < 0 && next.size() < 2)) {
                return fallback("upstream requested unavailable hold");
            }
            selected = hold >= 0 ? hold : next[1];
        }
        if (piece_index(decision.target->status.t) != selected) {
            return fallback("upstream selected a piece unavailable to host hold");
        }
        const Piece::Definition piece = BotPiece::definition(selected,environment);
        const auto targets = matching_host_poses(decision.target, piece, field);
        if (targets.empty()) {
            if (trace) {
                std::cerr << "[bot_zzztoj adapter] target type=" << decision.target->status.t
                          << " row=" << decision.target->row
                          << " col=" << decision.target->col
                          << " height=" << decision.target->height
                          << " width=" << decision.target->width << " data=";
                for (int i = 0; i < decision.target->height; ++i) {
                    std::cerr << ' ' << decision.target->data[i];
                }
                std::cerr << '\n';
            }
            return fallback("upstream target geometry does not match host piece");
        }

        Piece::Pose answer;
        int kick = 0;
        const Piece::Pose start{{startRow, startCol},
                                Runtime::Rotation::from_index(startRotation)};
        const bool spinTarget = decision.reportedSpinKnown && decision.reportedSpin;
        if (!find_reachable_pose(piece, field, start, targets, instantGravity,
                                 spinTarget, answer, kick,-1,&environment) &&
            !find_reachable_pose(piece, field, start, targets, instantGravity,
                                 false, answer, kick,-1,&environment)) {
            if (trace) {
                std::cerr << "[bot_zzztoj adapter] unreachable type=" << piece_char(selected)
                          << " start=" << startRow << ',' << startCol << ',' << startRotation
                          << " spin=" << spinTarget << " targets=";
                for (const auto& target : targets) {
                    std::cerr << ' ' << target.position.row << ',' << target.position.col
                              << ',' << target.rotation.index();
                }
                std::cerr << '\n';
            }
            return fallback("upstream target is unreachable under host SRS+");
        }

        answerRow = answer.position.row;
        answerCol = answer.position.col;
        answerRotation = answer.rotation.index();
        answerKick = kick;
        return selected;
    } catch (const std::exception& error) {
        if (trace) {
            std::cerr << "[bot_zzztoj adapter] exception during " << stage
                      << ": " << error.what() << '\n';
        }
        return fallback("exception");
    }
}

} // namespace Bot_zzztoj

namespace BotNativeHost {
bool supported(const Bot::Environment& environment) { return Bot_zzztoj::supports(environment); }
m_tetris::TetrisMap make_map(const Runtime::Playfield& field) {
    return Bot_zzztoj::make_upstream_map(field);
}
bool reachable(const m_tetris::TetrisNode* target, const Piece::Definition& piece,
               const Runtime::Playfield& field, Piece::Pose start,
               bool spin, bool mini, Piece::Pose& answer, int& kick,
               const Bot::Environment* environment) {
    return Bot_zzztoj::find_reachable_pose(piece, field, start,
        Bot_zzztoj::matching_host_poses(target, piece, field), false, spin,
        answer, kick, spin ? int(mini) : -1,environment);
}
int fallback(const Runtime::Playfield& field, const std::vector<int>& next,
             int row, int col, int rotation, int& answerRow, int& answerCol,
             int& answerRotation, int& answerKick) {
    return Bot_zzztoj::safe_fallback(field, next, row, col, rotation, false,
        answerRow, answerCol, answerRotation, answerKick);
}
}
