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
enum class Profile { While1, Botris, ChatGPT, Candy, Mech, Toj, Dig, Qq, C2, ColdClear };
bool enabled(const Environment& environment) noexcept;
// Dimension-independent terrain terms for the native policies.
double terrain(Profile profile,const std::vector<std::uint32_t>& rows,int width);
Autoplay::Availability availability(const Environment& environment);
// Keep the complete native policy whenever placement settlement is compatible.
// Visibility, pressure, hole generation and disabling HOLD do not replace AI.
// Board transformations and special scoring still need the shared rule search.
bool native_policy_compatible(const Environment& environment) noexcept;
bool native_policy_compatible(Profile profile,const Environment& environment) noexcept;
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
// Optional exact geometry accelerator. Settlement/scoring remain in the shared
// simulator; nullopt asks it to enumerate with the general movement model.
using GeometryEnumerator=std::function<std::optional<std::vector<Autoplay::PlacementCandidate>>(
    const Runtime::Playfield&,int,Piece::Type,Piece::Pose,Runtime::GravityInterval)>;
struct Decision {
    int selected=-1;
    Piece::Pose pose{};
    Runtime::RotationRecord rotation{};
    double value=0, searchMilliseconds=0;
    int attack=0, outgoing=0, cancelled=0, risen=0, b2b=0, depth=0;
    int searchWidth=0, peakFrontier=0, evaluated=0;
    bool dead=false;
    bool usedNative=false;
    std::vector<Autoplay::PlacementCandidate> candidates;
};
Decision search(Profile profile,const Environment& environment,
    const Runtime::PlayerSessionState& player,const Runtime::Playfield& field,
    const std::vector<int>& next,const std::vector<int>& unseenBagPieces,
    Piece::Pose start,int hold,const ShapeEvaluator& shape={},const GeometryEnumerator& geometry={});
using NativePlanner=std::function<Decision(const NativeBaseline&)>;
// Shared entry for additional BOTs: retain their native planner when compatible,
// otherwise use real QP settlement with that BOT's terrain and reward policy.
Decision adapt(Profile profile,const Environment& environment,
    const Runtime::PlayerSessionState& player,const Runtime::Playfield& field,
    const std::vector<int>& next,const std::vector<int>& unseen,Piece::Pose start,int hold,
    const NativePlanner& native={},const ShapeEvaluator& shape={});

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
