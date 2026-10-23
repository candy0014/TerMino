#include "bot_adapter.h"

#include "ai_zzz.h"
#include "rule_tetr_srs_plus.h"
#include "search_aspin.h"
#include "search_cautious.h"
#include "search_path.h"
#include "search_simple.h"
#include "search_simulate.h"
#include "search_tspin.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace NativeBots {
namespace {

using Node = m_tetris::TetrisNode;
constexpr std::array<int, 10> kComboTable{{0, 0, 1, 1, 1, 2, 2, 3, 3, 4}};

class QqSearch {
public:
    enum class Mode { Simple, Simulate, Path };
    struct Config {
        Mode mode = Mode::Simulate;
    };

    void init(const m_tetris::TetrisContext* context, const Config* config) {
        simple_.init(context);
        simulate_.init(context);
        path_.init(context);
        config_ = config;
    }

    std::vector<char> make_path(const Node* node, const Node* target,
                                const m_tetris::TetrisMap& map) {
        // QQ's selected target still comes from its native search mode. The
        // simple path builder assumes rotations never need a wall kick; use
        // the upstream graph path builder to execute it under tetr SRS+.
        return path_.make_path(node, target, map);
    }

    const std::vector<const Node*>* search(const m_tetris::TetrisMap& map,
                                           const Node* node,
                                           std::size_t depth) {
        switch (config_->mode) {
            case Mode::Simple: return simple_.search(map, node, depth);
            case Mode::Simulate: return simulate_.search(map, node, depth);
            case Mode::Path: return path_.search(map, node, depth);
        }
        return nullptr;
    }

private:
    const Config* config_ = nullptr;
    search_simple::Search simple_;
    search_simulate::Search simulate_;
    search_path::Search path_;
};

const Node* target_node(const Node* target) {
    return target;
}

const Node* target_node(const search_aspin::Search::TetrisNodeWithASpinType& target) {
    return target.node;
}

const Node* target_node(const search_tspin::Search::TetrisNodeWithTSpinType& target) {
    return target.node;
}

void reported_spin(const Node*, Decision&) {}

void reported_spin(const search_aspin::Search::TetrisNodeWithASpinType& target,
                   Decision& decision) {
    decision.reportedSpinKnown = true;
    decision.reportedSpin = target.type != search_aspin::Search::None;
}

void reported_spin(const search_tspin::Search::TetrisNodeWithTSpinType& target,
                   Decision& decision) {
    decision.reportedSpinKnown = true;
    decision.reportedSpin = target.type != search_tspin::Search::None;
}

int proportional_budget(int strength, int searchUnitMs) {
    const long long budget = static_cast<long long>(std::max(1, strength)) *
        std::max(1, searchUnitMs);
    return static_cast<int>(std::min<long long>(10000, budget));
}

template<class Engine>
Decision run_engine(Engine& engine,
                    const m_tetris::TetrisMap& map,
                    char current,
                    char hold,
                    bool useHold,
                    const char* next,
                    std::size_t nextLength,
                    int budgetMs) {
    const Node* start = engine.context()->generate(current);
    const auto result = useHold
        ? engine.run_hold(map, start, hold, true, next, nextLength, budgetMs)
        : engine.run(map, start, next, nextLength, budgetMs);

    Decision decision;
    decision.target = target_node(result.target);
    decision.changeHold = useHold && result.change_hold;
    reported_spin(result.target, decision);
    if (decision.target != nullptr) {
        const Node* pathStart = decision.changeHold
            ? engine.context()->generate(decision.target->status.t)
            : start;
        decision.path = engine.make_path(pathStart, result.target, map);
    }
    return decision;
}

template<class EngineType>
class EngineAdapterBase : public Adapter {
public:
    EngineAdapterBase(const Info& botInfo, int width, int height)
        : botInfo_(botInfo) {
        if (!engine_.prepare(width, height)) {
            throw std::runtime_error(std::string(botInfo.id) + " engine initialization failed");
        }
    }

    const Info& bot_info() const noexcept override { return botInfo_; }

    const std::shared_ptr<m_tetris::TetrisContext>& context() const noexcept override {
        return engine_.context();
    }

protected:
    EngineType engine_;

private:
    const Info& botInfo_;
};

using BotrisEngine = m_tetris::TetrisEngine<
    rule_tetr_srs_plus::TetrisRule, ai_zzz::Botris, search_aspin::Search>;

class BotrisAdapter final : public EngineAdapterBase<BotrisEngine> {
public:
    double terrain(const m_tetris::TetrisMap& map) override {
        return engine_.ai()->eval(search_aspin::Search::TetrisNodeWithASpinType(
            engine_.context()->generate('I')), map, map, 0).value;
    }
    BotrisAdapter(int width, int height)
        : EngineAdapterBase(info(Kind::Botris), width, height) {
        auto* search = engine_.search_config();
        search->allow_rotate_move = true;
        search->allow_180 = true;
        search->allow_d = true;
        search->allow_D = true;
        search->allow_LR = true;
        search->is_20g = false;
        engine_.ai_config()->table = kComboTable.data();
        engine_.ai_config()->table_max = static_cast<int>(kComboTable.size());
    }

    Decision choose(const m_tetris::TetrisMap& map, char current, char hold,
                    const char* next, std::size_t nextLength, bool allowHold,
                    ChainState chain,
                    int strength, int searchUnitMs) override {
        auto* status = engine_.status();
        status->max_combo = 0;
        status->max_attack = 0;
        status->death = 0;
        status->combo = chain.combo;
        status->attack = 0;
        status->clear = 0;
        status->under_attack = 0;
        status->map_rise = 0;
        status->b2b = chain.b2b > 0;
        status->like = 0;
        status->value = 0;
        return run_engine(engine_, map, current, hold, allowHold, next, nextLength,
                          proportional_budget(strength, searchUnitMs));
    }
};

using TojEngine = m_tetris::TetrisEngine<
    rule_tetr_srs_plus::TetrisRule, ai_zzz::TOJ_v08, search_tspin::Search>;

class TojAdapter final : public EngineAdapterBase<TojEngine> {
public:
    double terrain(const m_tetris::TetrisMap& map) override {
        return engine_.ai()->eval(search_tspin::Search::TetrisNodeWithTSpinType(
            engine_.context()->generate('I')), map, map, 0).value;
    }
    TojAdapter(int width, int height)
        : EngineAdapterBase(info(Kind::Toj), width, height) {
        auto* search = engine_.search_config();
        search->allow_rotate_move = true;
        search->allow_180 = true;
        search->allow_d = true;
        search->allow_D = true;
        search->allow_LR = true;
        search->is_20g = false;
        search->last_rotate = false;
        engine_.ai_config()->table = kComboTable.data();
        engine_.ai_config()->table_max = static_cast<int>(kComboTable.size());
    }

    Decision choose(const m_tetris::TetrisMap& map, char current, char hold,
                    const char* next, std::size_t nextLength, bool allowHold,
                    ChainState chain,
                    int strength, int searchUnitMs) override {
        auto* status = engine_.status();
        status->max_combo = 0;
        status->max_attack = 0;
        status->death = 0;
        status->combo = chain.combo;
        status->attack = 0;
        status->under_attack = 0;
        status->map_rise = 0;
        status->b2b = chain.b2b > 0;
        status->like = 0;
        status->value = 0;
        return run_engine(engine_, map, current, hold, allowHold, next, nextLength,
                          proportional_budget(strength, searchUnitMs));
    }
};

using DigEngine = m_tetris::TetrisEngine<
    rule_tetr_srs_plus::TetrisRule, ai_zzz::Dig, search_path::Search>;

class DigAdapter final : public EngineAdapterBase<DigEngine> {
public:
    double terrain(const m_tetris::TetrisMap& map) override {
        return engine_.ai()->eval(engine_.context()->generate('I'), map, map, 0);
    }
    DigAdapter(int width, int height)
        : EngineAdapterBase(info(Kind::Dig), width, height) {}

    Decision choose(const m_tetris::TetrisMap& map, char current, char hold,
                    const char* next, std::size_t nextLength, bool,
                    ChainState,
                    int, int) override {
        *engine_.status() = 0;
        return run_engine(engine_, map, current, hold, false, next, nextLength, 49);
    }
};

using QqEngine = m_tetris::TetrisEngine<
    rule_tetr_srs_plus::TetrisRule, ai_zzz::qq::Attack, QqSearch>;

class QqAdapter final : public EngineAdapterBase<QqEngine> {
public:
    double terrain(const m_tetris::TetrisMap& map) override {
        return engine_.ai()->eval(engine_.context()->generate('I'), map, map, 0).map;
    }
    QqAdapter(int width, int height)
        : EngineAdapterBase(info(Kind::Qq), width, height) {
        engine_.ai_config()->level = 1;
        engine_.ai_config()->mode = 1;
    }

    Decision choose(const m_tetris::TetrisMap& map, char current, char hold,
                    const char* next, std::size_t nextLength, bool,
                    ChainState,
                    int strength, int) override {
        const int level = std::clamp(strength, 1, 10);
        engine_.ai_config()->level = static_cast<std::size_t>(level);
        engine_.ai_config()->mode = 1;
        engine_.search_config()->mode = level == 10
            ? QqSearch::Mode::Path
            : (map.count <= map.width * 2 ? QqSearch::Mode::Simulate
                                           : QqSearch::Mode::Simple);
        auto* status = engine_.status();
        status->land_point = 0;
        status->attack = 0;
        status->rubbish = 0;
        status->value = 0;

        const std::size_t visibleNext = std::min<std::size_t>(level, nextLength);
        std::string qqNext(next, next + visibleNext);
        if (visibleNext <= 2) {
            std::string uncertain;
            uncertain.reserve(1 + visibleNext * 2);
            uncertain.push_back('?');
            for (char piece : qqNext) {
                uncertain.push_back(piece);
                uncertain.push_back('?');
            }
            qqNext.swap(uncertain);
        }
        return run_engine(engine_, map, current, hold, false,
                          qqNext.data(), qqNext.size(), 60);
    }
};

using C2Engine = m_tetris::TetrisEngine<
    rule_tetr_srs_plus::TetrisRule, ai_zzz::C2, search_cautious::Search>;

class C2Adapter final : public EngineAdapterBase<C2Engine> {
public:
    double terrain(const m_tetris::TetrisMap& map) override {
        return engine_.ai()->eval(engine_.context()->generate('I'), map, map, 0).map;
    }
    C2Adapter(int width, int height)
        : EngineAdapterBase(info(Kind::C2), width, height) {
        engine_.search_config()->fast_move_down = true;
        engine_.ai_config()->p = {
            2.87224, 0.372169, 0.102604, 0.723501, 3.08721, 0.802789,
            -0.786174, 107.713, -0.540719, 109.116, -3.84305, 116.58,
            -1.00066, 49.7899, -1.23986, 391.808, -4.30493, 91.0623,
            -1.60608, 67.7934, 2.36365, 46016.9, 34.2515, 0.285739,
        };
        engine_.ai_config()->p_rate = 1;
        engine_.ai_config()->safe = 0;
        engine_.ai_config()->mode = 0;
        engine_.ai_config()->danger = 0;
        engine_.ai_config()->soft_drop = 1;
    }

    Decision choose(const m_tetris::TetrisMap& map, char current, char hold,
                    const char* next, std::size_t nextLength, bool,
                    ChainState chain,
                    int strength, int searchUnitMs) override {
        auto* status = engine_.status();
        status->attack = 0;
        status->map = 0;
        status->combo = static_cast<std::size_t>(std::max(0, chain.combo));
        status->combo_limit = 0;
        status->value = 0;
        const std::size_t c2NextLength = std::min<std::size_t>(1, nextLength);
        return run_engine(engine_, map, current, hold, false, next, c2NextLength,
                          proportional_budget(strength, searchUnitMs));
    }
};

} // namespace

const Info& info(Kind kind) {
    for (const auto& entry : kBots) {
        if (entry.kind == kind) return entry;
    }
    return kBots.front();
}

bool parse_kind(std::string_view text, Kind& result) {
    for (const auto& entry : kBots) {
        if (entry.id == text) {
            result = entry.kind;
            return true;
        }
    }
    return false;
}

std::unique_ptr<Adapter> make_adapter(Kind kind, int width, int height) {
    switch (kind) {
        case Kind::Botris: return std::make_unique<BotrisAdapter>(width, height);
        case Kind::Toj: return std::make_unique<TojAdapter>(width, height);
        case Kind::Dig: return std::make_unique<DigAdapter>(width, height);
        case Kind::Qq: return std::make_unique<QqAdapter>(width, height);
        case Kind::C2: return std::make_unique<C2Adapter>(width, height);
    }
    throw std::runtime_error("unknown native bot kind");
}

} // namespace NativeBots
