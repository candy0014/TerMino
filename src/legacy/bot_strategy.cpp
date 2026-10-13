#include "tetr/legacy/bot_strategy.h"
#include "tetr/bot/bot_chatgpt.h"
#include "tetr/legacy/bot_adapter.h"
#include "tetr/runtime/game_context.h"

#include <array>
#include <mutex>
#include <random>
#include <utility>

namespace Bot_while1 {
extern std::mt19937 rnd;
}

namespace Bot_mech {
extern int node;
extern int i;
extern int hld;
extern int l;
extern int r;
extern bool combo;
extern bool vis[8];
}

namespace {
std::mutex localVersusBotMutex;
}

namespace Legacy {

struct BotStrategy::InstanceState {
    Runtime::AutoplayMode mode = Runtime::AutoplayMode::Disabled;
    bool initialized = false;

    std::mt19937 while1Random;

    int mechNode = -1;
    int mechPieceIndex = 0;
    int mechHold = 7;
    int mechLeft = 0;
    int mechRight = 0;
    bool mechCombo = false;
    std::array<bool, 8> mechVisited{};
};

namespace {

void capture_bot_state(BotStrategy::InstanceState& state) {
    switch (state.mode) {
        case Runtime::AutoplayMode::While1:
            state.while1Random = Bot_while1::rnd;
            break;
        case Runtime::AutoplayMode::Mech:
            state.mechNode = Bot_mech::node;
            state.mechPieceIndex = Bot_mech::i;
            state.mechHold = Bot_mech::hld;
            state.mechLeft = Bot_mech::l;
            state.mechRight = Bot_mech::r;
            state.mechCombo = Bot_mech::combo;
            for (std::size_t index = 0; index < state.mechVisited.size(); ++index) {
                state.mechVisited[index] = Bot_mech::vis[index];
            }
            break;
        default:
            break;
    }
}

void swap_bot_state(BotStrategy::InstanceState& state) {
    using std::swap;
    switch (state.mode) {
        case Runtime::AutoplayMode::While1:
            swap(state.while1Random, Bot_while1::rnd);
            break;
        case Runtime::AutoplayMode::Mech:
            swap(state.mechNode, Bot_mech::node);
            swap(state.mechPieceIndex, Bot_mech::i);
            swap(state.mechHold, Bot_mech::hld);
            swap(state.mechLeft, Bot_mech::l);
            swap(state.mechRight, Bot_mech::r);
            swap(state.mechCombo, Bot_mech::combo);
            for (std::size_t index = 0; index < state.mechVisited.size(); ++index) {
                swap(state.mechVisited[index], Bot_mech::vis[index]);
            }
            break;
        default:
            break;
    }
}

class ScopedBotInstanceState {
public:
    explicit ScopedBotInstanceState(BotStrategy::InstanceState& state) : state_(state) {
        if (state_.initialized) swap_bot_state(state_);
    }

    ~ScopedBotInstanceState() {
        if (state_.initialized) swap_bot_state(state_);
    }

private:
    BotStrategy::InstanceState& state_;
};

} // namespace

BotStrategy::BotStrategy() : state_(std::make_unique<InstanceState>()) {}
BotStrategy::~BotStrategy() = default;

std::any BotStrategy::capture_state() const {
    std::lock_guard<std::mutex> lock(localVersusBotMutex);
    return *state_;
}

void BotStrategy::restore_state(const std::any& state) {
    std::lock_guard<std::mutex> lock(localVersusBotMutex);
    if (const auto* saved=std::any_cast<InstanceState>(&state)) *state_=*saved;
}

bool BotStrategy::initialize(Runtime::GameContext& context) {
    std::lock_guard<std::mutex> lock(localVersusBotMutex);
    const bool initialized = Bot::init(context);
    state_->mode = context.config.autoplay.mode;
    state_->initialized = initialized;
    if (initialized) capture_bot_state(*state_);
    return initialized;
}

Piece::Type BotStrategy::choose_position(
    Runtime::GameContext& context,
    Runtime::PlayerSessionState& player,
    Runtime::Playfield field,
    const std::vector<Piece::Type>& next,
    const std::vector<Piece::Type>& unseenBagPieces,
    Piece::Pose start,
    Autoplay::HoldInput hold,
    Piece::Pose& answer,
    Runtime::RotationRecord& answerRotation) {
    std::lock_guard<std::mutex> lock(localVersusBotMutex);
    ScopedBotInstanceState instance(*state_);
    const auto type=Bot::getpos(context, player, std::move(field), next, unseenBagPieces, start, hold, answer, answerRotation);
    candidates_=context.config.autoplay.mode==Runtime::AutoplayMode::ChatGPT ? Bot_chatgpt::last_candidates() : std::vector<Autoplay::PlacementCandidate>{};
    return type;
}

std::unique_ptr<Autoplay::Strategy> BotStrategy::clone_for_search() const {
    return std::make_unique<BotStrategy>();
}
std::vector<Autoplay::PlacementCandidate> BotStrategy::placement_candidates() const { return candidates_; }
std::string BotStrategy::movement_script(
    Runtime::GameContext& context,
    Runtime::Playfield field,
    Piece::Definition piece,
    Piece::Pose start,
    Piece::Pose answer,
    Runtime::RotationRecord answerRotation) {
    std::lock_guard<std::mutex> lock(localVersusBotMutex);
    return Bot::getop(context, std::move(field), std::move(piece), start, answer, answerRotation);
}

} // namespace Legacy
