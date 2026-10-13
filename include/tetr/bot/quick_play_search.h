#ifndef TETR_BOT_QUICK_PLAY_SEARCH_H
#define TETR_BOT_QUICK_PLAY_SEARCH_H

#include "tetr/bot/environment.h"
#include "tetr/autoplay/strategy.h"
#include "tetr/engine/placement_application.h"
#include <optional>
#include "tetr/runtime/player_session_state.h"
#include <functional>
#include <vector>

namespace Bot::QuickPlaySearch {
enum class Profile { While1, Botris, ChatGPT };
bool enabled(const Environment& environment) noexcept;
Autoplay::Availability availability(const Environment& environment);
// Plain QP retains each bot's complete free-play search and tactical policy.
// The host still settles actual QP attacks/XP; mods use the rule-aware search.
bool plain(const Environment& environment) noexcept;
Environment native_environment(const Environment& environment);
struct NativeBaseline {
    Environment environment;
    std::shared_ptr<Runtime::PlayerSessionState> player;
    std::shared_ptr<Runtime::Playfield> field;
    int rowOffset=0;
};
std::optional<NativeBaseline> native_baseline(Profile profile,const Environment& environment,
    const Runtime::PlayerSessionState& player,const Runtime::Playfield& field);
bool native_answer_valid(const Environment& environment,const Runtime::PlayerSessionState& player,
    const Runtime::Playfield& field,const std::vector<int>& next,Piece::Pose start,int hold,
    int selected,Piece::Pose answer,Runtime::RotationRecord rotation);
// Bottom-first occupied rows, with the complete host width and visible height.
using ShapeEvaluator=std::function<double(const std::vector<std::uint32_t>&,int,int,int,int)>;
struct Decision {
    int selected=-1;
    Piece::Pose pose{};
    Runtime::RotationRecord rotation{};
    double value=0, searchMilliseconds=0;
    int attack=0, outgoing=0, cancelled=0, risen=0, b2b=0, depth=0;
    bool dead=false;
    std::vector<Autoplay::PlacementCandidate> candidates;
};
Decision search(Profile profile,const Environment& environment,
    const Runtime::PlayerSessionState& player,const Runtime::Playfield& field,
    const std::vector<int>& next,const std::vector<int>& unseenBagPieces,
    Piece::Pose start,int hold,const ShapeEvaluator& shape={});

struct Forecast {
    std::shared_ptr<Runtime::Playfield> field;
    Rules::PlacementResult placement;
    Engine::PlacementApplicationResult application;
    Runtime::GarbageSessionState garbage;
    int activeHeight=0, visibleHeight=0, solidHeight=0, garbageHeight=0;
    int cancelled=0, risen=0;
    bool dead=false;
};
// Deterministic known-placement projection. Unknown holes use an independent
// representative sample; fractional attack rounds conservatively down.
Forecast forecast_placement(const Environment& environment,
    const Runtime::PlayerSessionState& player,const Runtime::Playfield& field,
    Piece::Type piece,Piece::Pose pose,Runtime::RotationRecord rotation,
    Runtime::Duration afterLock = {});
}
#endif
