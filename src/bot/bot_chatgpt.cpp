#include "tetr/bot/bot_chatgpt.h"
#include "tetr/bot/quick_play_search.h"

#include "chatgpt/evaluation.h"
#include "tetr/engine/autoplay_path.h"
#include <tuple>
#include "tetr/compat/config_encoding.h"
#include "tetr/compat/piece_encoding.h"
#ifdef TETR_CHATGPT_KEY_PIECE
#include "chatgpt/key_piece_evaluation.h"
#endif
#ifdef TETR_CHATGPT_KEY_QUAD
#include "chatgpt/key_quad_evaluation.h"
#endif
#include "native_host.h"
#include "rule_tetr_srs_plus.h"
#ifdef TETR_CHATGPT_BAG_TAIL
#include "chatgpt/bag_tail_engine.h"
#endif
#ifdef TETR_CHATGPT_MATERIAL_RESERVE
#include "chatgpt/material_reserve_evaluation.h"
#endif
#ifdef TETR_CHATGPT_OWN_MATERIAL
#include "chatgpt/own_material_evaluation.h"
#endif
#ifdef TETR_CHATGPT_RELATIVE_COVER
#include "chatgpt/relative_cover_evaluation.h"
#endif
#ifdef TETR_CHATGPT_VISIBLE_PATH_RISK
#include "chatgpt/visible_path_risk_evaluation.h"
#endif
#ifdef TETR_CHATGPT_RECOVERED_PATH_RISK
#include "chatgpt/recovered_path_risk_evaluation.h"
#endif
#ifdef TETR_CHATGPT_RELEASE_CHOICE
#include "chatgpt/release_choice_engine.h"
#endif
#ifdef TETR_CHATGPT_BURST_WINDOW
#include "chatgpt/burst_window_evaluation.h"
#endif
#ifdef TETR_CHATGPT_FLAT_ATTACK
#include "chatgpt/flat_attack_evaluation.h"
#endif
#ifdef TETR_CHATGPT_SPAWN_COLUMNS
#include "chatgpt/spawn_columns_evaluation.h"
#endif
#if defined(TETR_CHATGPT_CLEAR_RECOVERY) || defined(TETR_CHATGPT_SETUP_RECOVERY)
#include "chatgpt/clear_recovery_evaluation.h"
#endif
#if defined(TETR_CHATGPT_CHARGE_RESERVE) || defined(TETR_CHATGPT_CHARGE_BURST)
#include "chatgpt/charge_reserve_evaluation.h"
#endif
#ifdef TETR_CHATGPT_CLEAR_BURST
#include "chatgpt/clear_burst_evaluation.h"
#endif
#ifdef TETR_CHATGPT_CLEAR_STALL_RISK
#include "chatgpt/clear_stall_risk_evaluation.h"
#endif
#ifdef TETR_CHATGPT_FAST_TERRAIN
#include "chatgpt/fast_terrain_evaluation.h"
#endif
#ifdef TETR_CHATGPT_SETTLED_RISK
#include "chatgpt/settled_risk_evaluation.h"
#endif
#ifdef TETR_CHATGPT_ROOT_COVERAGE
#include "chatgpt/root_coverage_engine.h"
#endif
#if defined(TETR_CHATGPT_GARBAGE_ROLLOUT) || defined(TETR_CHATGPT_GARBAGE_BEAM)
#include "chatgpt/garbage_rollout_engine.h"
#endif
#ifdef TETR_CHATGPT_TRANSPOSITION
#include "chatgpt/transposition_engine.h"
#endif
#ifdef TETR_CHATGPT_RISE_HORIZON
#include "chatgpt/rise_horizon_evaluation.h"
#endif
#ifdef TETR_CHATGPT_MOCH_MULTI_STEP
#include "chatgpt/moch_multi_step_evaluation.h"
#endif
#ifdef TETR_CHATGPT_O_ROOF
#include "chatgpt/o_roof_search.h"
#endif
#if defined(TETR_CHATGPT_KNOWN_BAG) || defined(TETR_CHATGPT_KEY_QUAD) || defined(TETR_CHATGPT_KEY_PIECE)
#include "chatgpt/known_bag_preview.h"
#endif
#ifdef TETR_CHATGPT_TIME_B2B
#include "chatgpt/time_b2b_evaluation.h"
#endif
#ifdef TETR_CHATGPT_LOCAL_PATTERN
#include "chatgpt/local_pattern_evaluation.h"
#endif
#ifdef TETR_CHATGPT_LEARNED_TERRAIN
#include "chatgpt/learned_terrain_evaluation.h"
#endif
#ifdef TETR_CHATGPT_TERRAIN_V2
#include "chatgpt/terrain_bridge_evaluation.h"
#endif
#ifdef TETR_CHATGPT_TERRAIN_V1
#include "chatgpt/terrain_evaluation.h"
#endif
#ifdef TETR_CHATGPT_P5_A
#include "chatgpt/coverage_search.h"
#endif
#ifdef TETR_CHATGPT_P3_B
#include "chatgpt/rise_context_evaluation.h"
#endif
#ifdef TETR_CHATGPT_P3_A
#include "chatgpt/hold_context_evaluation.h"
#endif
#ifdef TETR_CHATGPT_P2_A
#include "chatgpt/spin_quota_engine.h"
#endif
#ifdef TETR_CHATGPT_P2_B
#include "chatgpt/spin_beam_quota_engine.h"
#endif

#include <algorithm>
#include <chrono>
#include <memory>
#include <limits>
#include <stdexcept>

namespace Bot_chatgpt {
namespace {
#if defined(TETR_CHATGPT_KEY_PIECE)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, KeyPieceEvaluation, Search>;
#elif defined(TETR_CHATGPT_KEY_QUAD)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, KeyQuadEvaluation, Search>;
#elif defined(TETR_CHATGPT_OWN_MATERIAL)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, OwnMaterialEvaluation, Search>;
#elif defined(TETR_CHATGPT_RELATIVE_COVER)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, RelativeCoverEvaluation, Search>;
#elif defined(TETR_CHATGPT_MATERIAL_RESERVE)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, MaterialReserveEvaluation, Search>;
#elif defined(TETR_CHATGPT_BAG_TAIL)
using Engine = BagTailEngine<>;
#elif defined(TETR_CHATGPT_RELEASE_CHOICE)
using Engine = ReleaseChoiceEngine;
#elif defined(TETR_CHATGPT_RECOVERED_PATH_RISK)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, RecoveredPathRiskEvaluation, Search>;
#elif defined(TETR_CHATGPT_VISIBLE_PATH_RISK)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, VisiblePathRiskEvaluation, Search>;
#elif defined(TETR_CHATGPT_BURST_WINDOW)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, BurstWindowEvaluation, Search>;
#elif defined(TETR_CHATGPT_FLAT_ATTACK)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, FlatAttackEvaluation, Search>;
#elif defined(TETR_CHATGPT_SPAWN_COLUMNS)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, SpawnColumnsEvaluation, Search>;
#elif defined(TETR_CHATGPT_CHARGE_BURST)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, ChargeBurstEvaluation, Search>;
#elif defined(TETR_CHATGPT_CHARGE_RESERVE)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, ChargeReserveEvaluation, Search>;
#elif defined(TETR_CHATGPT_SETUP_RECOVERY)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, SetupRecoveryEvaluation, Search>;
#elif defined(TETR_CHATGPT_CLEAR_RECOVERY)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, ClearRecoveryEvaluation, Search>;
#elif defined(TETR_CHATGPT_CLEAR_BURST)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, ClearBurstEvaluation, Search>;
#elif defined(TETR_CHATGPT_CLEAR_STALL_RISK)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, ClearStallRiskEvaluation, Search>;
#elif defined(TETR_CHATGPT_FAST_TERRAIN)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, FastTerrainEvaluation, Search>;
#elif defined(TETR_CHATGPT_SETTLED_RISK)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, SettledRiskEvaluation, Search>;
#elif defined(TETR_CHATGPT_GARBAGE_BEAM)
using Engine = GarbageBeamEngine;
#elif defined(TETR_CHATGPT_ROOT_COVERAGE)
using Engine = RootCoverageEngine;
#elif defined(TETR_CHATGPT_GARBAGE_ROLLOUT)
using Engine = GarbageRolloutEngine;
#elif defined(TETR_CHATGPT_TRANSPOSITION)
using Engine = TranspositionEngine;
#elif defined(TETR_CHATGPT_RISE_HORIZON)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, RiseHorizonEvaluation, Search>;
#elif defined(TETR_CHATGPT_MOCH_MULTI_STEP)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, MochMultiStepEvaluation, Search>;
#elif defined(TETR_CHATGPT_O_ROOF)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Evaluation, ORoofSearch>;
#elif defined(TETR_CHATGPT_TIME_B2B)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, TimeB2BEvaluation, Search>;
#elif defined(TETR_CHATGPT_LOCAL_PATTERN)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, LocalPatternEvaluation, Search>;
#elif defined(TETR_CHATGPT_LEARNED_TERRAIN)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, LearnedTerrainEvaluation, Search>;
#elif defined(TETR_CHATGPT_TERRAIN_V2)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, TerrainBridgeEvaluation, Search>;
#elif defined(TETR_CHATGPT_TERRAIN_V1)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, TerrainEvaluation, Search>;
#elif defined(TETR_CHATGPT_P5_A)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Evaluation, CoverageSearch>;
#elif defined(TETR_CHATGPT_P2_B)
using Engine = SpinBeamQuotaEngine;
#elif defined(TETR_CHATGPT_P2_A)
using Engine = SpinQuotaEngine;
#elif defined(TETR_CHATGPT_P3_A)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, HoldContextEvaluation, Search>;
#elif defined(TETR_CHATGPT_P3_B)
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, RiseContextEvaluation, Search>;
#else
using Engine = m_tetris::TetrisEngine<rule_tetr_srs_plus::TetrisRule, Evaluation, Search>;
#endif

// Scratch search storage is private to the calling thread. Root chain/garbage
// state is supplied afresh on every decision; no test or player globals exist.
struct Workspace {
    using Geometry=std::tuple<bool,bool,bool,Runtime::SpinBonuses,std::array<Piece::Type,7>,bool,bool,bool>;
    std::optional<Geometry> geometry;
    Engine engine;
    Search quickGeometry;
    Search::Config quickGeometryConfig;
    bool started = false;
    Workspace() {
        if (!engine.prepare(10, 40)) throw std::runtime_error("bot_chatgpt initialization failed");
        engine.memory_limit(64ull << 20);
        quickGeometry.init(engine.context().get(),&quickGeometryConfig);
    }
};
thread_local std::unique_ptr<Workspace> workspace;
thread_local Diagnostics diagnostics;
thread_local std::vector<Autoplay::PlacementCandidate> candidates;

Workspace& current_workspace() {
    if (!workspace) workspace = std::make_unique<Workspace>();
    return *workspace;
}

std::optional<std::vector<Autoplay::PlacementCandidate>> quick_geometry(const Bot::Environment& env,
    const Runtime::Playfield& field,int active,Piece::Type type,Piece::Pose start,Runtime::GravityInterval gravity) {
    const auto metadata=Piece::metadata(type);
    if(field.width()!=10 || env.rotationSystem!=0 || gravity.is_instant() || env.wall_only_horizontal() ||
        env.movement.mask()!=0 || !env.partial_descent_available() || metadata.familyIndex<0 || metadata.familyIndex>=7 ||
        metadata.rotationSystem!=Runtime::RotationSystem::SrsPlus || Piece::catalog_entry(type).shapes[0].size()!=4)return {};
    // The graph represents forty rows. Never discard occupied hidden cells.
    for(int y=-Runtime::kHiddenRowCapacity;y<active-40;++y)
        for(int x=0;x<10;++x)if(field.cell(y,x).occupied())return {};
    m_tetris::TetrisMap map(10,40);
    for(int y=0;y<40;++y)for(int x=0;x<10;++x)if(field.cell(active-1-y,x).occupied()) {
        map.row[y]|=1u<<x;++map.count;map.top[x]=y+1;map.roof=std::max(map.roof,y+1);
    }
    auto& work=current_workspace();auto& config=work.quickGeometryConfig;
    config.retainSpinVariants=true;config.spinBonuses=env.spinBonuses;config.pieceTypes=env.pieceTypes;
    const auto spin=env.quickPlayRules?::Engine::autoplay_spin_rules(*env.quickPlayRules):
        ::Engine::AutoplaySpinRules{env.spinBonuses};
    config.allSpinEnabled=spin.allSpinEnabled;config.noHoldReverse=spin.noHoldReverse;
    config.suppressNonTSpins=spin.suppressNonTSpins;
    const int offset=20-active;
    const auto* node=native_pose(work.engine.context().get(),"IJLOSTZ"[metadata.familyIndex],start.translated(offset,0));
    if(!node || !node->check(map))return {};
    std::vector<Autoplay::PlacementCandidate> result;
    for(const auto& landing:*work.quickGeometry.search(map,node,0)) {
        const auto record=landing.kick?Runtime::RotationRecord::from_kick(Piece::KickIndex::from_index(landing.kick-1)):
            Runtime::RotationRecord{};
        result.push_back({type,host_pose(*landing.node).translated(-offset,0),record,0,0});
    }
    return result;
}
}

Diagnostics last_diagnostics() { return diagnostics; }
std::vector<Autoplay::PlacementCandidate> last_candidates() { return candidates; }
std::optional<std::vector<Autoplay::PlacementCandidate>> enumerate_placements(const Bot::Environment& env,
    const Runtime::Playfield& field,int active,Piece::Type type,Piece::Pose start,Runtime::GravityInterval gravity) {
    return quick_geometry(env,field,active,type,start,gravity);
}

bool init(const Bot::Environment& environment) {
    if (!availability(environment)) return false;
    workspace.reset();
    if(Bot::QuickPlaySearch::enabled(environment)) {
        const auto baseline=Bot::QuickPlaySearch::native_environment(environment);
        if(!Bot::QuickPlaySearch::native_policy_compatible(Bot::QuickPlaySearch::Profile::ChatGPT,environment) || !availability(baseline))return true;
    }
    (void)current_workspace();
    return true;
}

int getpos(const Bot::Environment& environment, const Runtime::PlayerSessionState& player,
           Runtime::Playfield field, std::vector<int> next, const std::vector<int>& unseenBagPieces,
           int startRow, int startCol, int startRotation, int hold,
           int& answerRow, int& answerCol, int& answerRotation, int& answerKick) {
    diagnostics = {};
    candidates.clear();
    if (!availability(environment) || next.empty()) return -1;
    if(Bot::QuickPlaySearch::enabled(environment)) {
        if(Bot::QuickPlaySearch::native_policy_compatible(Bot::QuickPlaySearch::Profile::ChatGPT,environment) &&
            availability(Bot::QuickPlaySearch::native_environment(environment))) {
            if(auto baseline=Bot::QuickPlaySearch::native_baseline(Bot::QuickPlaySearch::Profile::ChatGPT,
                environment,player,field)) {
                const int selected=getpos(baseline->environment,*baseline->player,*baseline->field,next,unseenBagPieces,
                    startRow+baseline->rowOffset,startCol,startRotation,hold,answerRow,answerCol,answerRotation,answerKick);
                answerRow-=baseline->rowOffset;diagnostics.rootRow=startRow;
                for(auto& candidate:candidates)candidate.pose.position.row-=baseline->rowOffset;
                diagnostics.rows={};diagnostics.roof=0;
                for(int y=field.height()-1;y>=field.height()-40;--y)for(int x=0;x<10;++x)
                    if(field.cell(y,x).occupied()) {
                        diagnostics.rows[field.height()-1-y]|=1u<<x;
                        diagnostics.roof=std::max(diagnostics.roof,field.height()-y);
                    }
                const auto record=answerKick>0?Runtime::RotationRecord::from_kick(
                    Piece::KickIndex::from_index(answerKick-1)):Runtime::RotationRecord{};
                if(Bot::QuickPlaySearch::native_answer_valid(environment,player,field,next,
                    {{startRow,startCol},Runtime::Rotation::from_index(startRotation)},hold,selected,
                    {{answerRow,answerCol},Runtime::Rotation::from_index(answerRotation)},record))return selected;
                diagnostics={};candidates.clear();
            }
        }
        const auto result=Bot::QuickPlaySearch::search(Bot::QuickPlaySearch::Profile::ChatGPT,
            environment,player,field,next,unseenBagPieces,
            {{startRow,startCol},Runtime::Rotation::from_index(startRotation)},hold,{},
            [&](const auto& image,int active,auto type,auto origin,auto gravity) {
                return quick_geometry(environment,image,active,type,origin,gravity);
            });
        candidates=result.candidates;
        diagnostics.rootNext=next;diagnostics.rootUnseen=unseenBagPieces;diagnostics.rootHold=hold;
        diagnostics.rootRow=startRow;diagnostics.rootCol=startCol;diagnostics.rootRotation=startRotation;
        diagnostics.rootB2b=player.gameplay.scoring.b2b;diagnostics.rootCombo=player.gameplay.scoring.combo;
        diagnostics.rootGarbageHeight=player.gameplay.playfield.garbageHeight;
        diagnostics.rootPreviousCleared=player.clutch.previousPieceCleared;
        diagnostics.incoming=Rules::pending_garbage(player.garbage.buffer);
        for(const auto& packet:player.garbage.buffer) {
            const double due=packet.arrival_time().seconds()+environment.incomingGarbageDelaySeconds-environment.nowSeconds;
            diagnostics.rootPackets.emplace_back(packet.line_count(),due);
            if(due<=0)diagnostics.readyIncoming+=packet.line_count();
        }
        for(int y=field.height()-1;y>=std::max(-Runtime::kHiddenRowCapacity,field.height()-40);--y)
            for(int x=0;x<field.width() && x<32;++x)if(field.cell(y,x).occupied()) {
                diagnostics.rows[field.height()-1-y]|=1u<<x;
                diagnostics.roof=std::max(diagnostics.roof,field.height()-y);
            }
        diagnostics.forecastStepSeconds=environment.secondsPerPiece;
        diagnostics.projectedAttack=result.attack;diagnostics.projectedB2b=result.b2b;
        diagnostics.projectedRisen=result.risen;diagnostics.projectedDepth=result.depth;
        diagnostics.projectedDead=result.dead;diagnostics.value=result.value;diagnostics.searchMilliseconds=result.searchMilliseconds;
        diagnostics.searchWidth=result.searchWidth;diagnostics.peakFrontier=result.peakFrontier;diagnostics.evaluated=result.evaluated;
        diagnostics.fallbackReason=result.selected>=0?0:1;
        if(result.selected>=0) {
            answerRow=result.pose.position.row;answerCol=result.pose.position.col;answerRotation=result.pose.rotation.index();
            answerKick=result.rotation.rotated()?result.rotation.kick().index()+1:0;
            return result.selected;
        }
        // The host has a geometry-aware grounded fallback. The native helper
        // is fixed to a 10x20 tetromino field and cannot handle QP dimensions.
        return -1;
    }
    for (int type : next) if (type < 0 || type >= 7) return -1;
    const auto spinRules=environment.quickPlayRules?::Engine::autoplay_spin_rules(*environment.quickPlayRules):
        ::Engine::AutoplaySpinRules{environment.spinBonuses};
    const Workspace::Geometry geometry{environment.partial_descent_available(),
        environment.naturalGravityEnabled && environment.gravityIntervalSeconds<=1e-7,
        environment.wall_only_horizontal(),environment.spinBonuses,environment.pieceTypes,
        spinRules.allSpinEnabled,spinRules.noHoldReverse,spinRules.suppressNonTSpins};
    if(workspace && workspace->geometry && *workspace->geometry!=geometry) workspace.reset();
    auto& work = current_workspace();work.geometry=geometry;
    const auto map = BotNativeHost::make_map(field);
    constexpr char pieces[] = "IJLOSTZ";
    std::string preview;
    for (std::size_t i = 1; i < next.size(); ++i) preview.push_back(pieces[next[i]]);
#if defined(TETR_CHATGPT_KNOWN_BAG) || defined(TETR_CHATGPT_KEY_QUAD) || defined(TETR_CHATGPT_KEY_PIECE)
    // A new candidate, not a default policy upgrade. Only the final singleton
    // in the supplied visible bag complement extends this fresh search tail.
    // Keep visible next intact for host HOLD validation and root diagnostics.
    diagnostics.inferredNextPiece = append_known_bag_preview(
        preview, unseenBagPieces, !environment.randomPieces);
#else
    (void)unseenBagPieces;
#endif
#if defined(TETR_CHATGPT_KEY_PIECE)
    KeyPieceEvaluation::Status status;
    status.holdEmpty = environment.holdEnabled && hold == -1;
#elif defined(TETR_CHATGPT_P3_A)
    HoldContextEvaluation::Status status;
    status.holdEmpty = environment.holdEnabled && hold == -1;
#else
    Evaluation::Status status;
#endif
    status.combo = player.gameplay.scoring.combo;
    status.b2b = player.gameplay.scoring.b2b;
    status.garbageHeight = player.gameplay.playfield.garbageHeight;
    status.previousCleared = player.clutch.previousPieceCleared;
    status.placedPieces = player.gameplay.scoring.placedBlocks;
    status.openerDoubleCancelEnabled = environment.openerDoubleCancelEnabled;
    status.openerPhase = environment.openerPhase;
    status.legacyOpenerPhase = environment.legacyOpenerPhase;
    status.sentAttack = environment.sentAttack;
    status.garbageCap = environment.garbageCap;
    status.sendMultiplier = environment.sendMultiplier;
    status.cancellationMultiplier = environment.cancellationMultiplier;
    status.cancellation = player.garbage.cancellation;
    status.spinBonuses = environment.spinBonuses;
    status.attackRules = environment.attackRules;
    status.pieceTypes=environment.pieceTypes;
    if(environment.quickPlayRules) {
        const auto& q=environment.quickPlayRules->quickPlay;
        status.qpAllSpin=q.allSpin;
        status.qpModifiers={q.expert,q.noHold,q.messy,q.gravity,q.volatileMode,q.doubleHole,
            q.invisible,q.allSpin,q.backfire,q.snakeEyes,q.snowball};
        status.qpLastOperation=player.gameplay.quickPlay.lastOperation;
    }
    if (environment.attackRules.halfClearEnabled)
        for (int row=-20; row<20; ++row)
            for (int col=0; col<10; ++col)
                if (field.cell(row,col).is_piece()) status.pieceRows[19-row] |= 1u<<col;
    status.entryDelaySeconds = environment.entryDelaySeconds;
    status.lineClearDelaySeconds = environment.lineClearDelaySeconds;
    status.pieceTime = environment.pieceTime;
    // Handling is a presentation option. Placement uses the ordinary BOT
    // candidates and evaluation, regardless of the animation settings.
    status.calculationSeconds = search_budget_ms(environment.autoplaySearchWidth)/1000.0;
    status.rootWaitSeconds = environment.rootPaceRemainingSeconds;
    std::vector<VisiblePacket> packets;
    bool readyPrefix = true;
    for (const auto& packet : player.garbage.buffer) {
        const int lines = std::max(0, packet.line_count());
        status.incoming += std::min(lines, std::numeric_limits<int>::max() - status.incoming);
        packets.push_back({packet.line_count(), packet.arrival_time().seconds() +
            environment.incomingGarbageDelaySeconds - environment.nowSeconds});
        readyPrefix = readyPrefix && environment.nowSeconds - packet.arrival_time().seconds() >=
            environment.incomingGarbageDelaySeconds;
        if (readyPrefix)
            status.readyIncoming += std::min(lines, std::numeric_limits<int>::max() - status.readyIncoming);
    }
    const auto bonuses=environment.spinBonuses;
    const auto policy=capability_policy({map.roof,status.garbageHeight,status.incoming,status.readyIncoming},
        player.gameplay.playfield.activeHeight,environment.holdEnabled,
        !spinRules.suppressNonTSpins && full_non_t_spins(spinRules.allSpinEnabled?Runtime::SpinBonuses::AllSpinPlus:bonuses));
    diagnostics.digPressure = policy.digPressure;
    diagnostics.roof = map.roof;
    diagnostics.incoming = status.incoming;
    diagnostics.readyIncoming = status.readyIncoming;
    std::copy_n(map.row, 40, diagnostics.rows.begin());
    const int budgetMs = search_budget_ms(environment.autoplaySearchWidth);
    const bool canHold = environment.holdEnabled && hold != -2;
    auto& search = work.engine;
    *search.status() = status;
    search.search_config()->rootPreviousCleared = status.previousCleared;
    search.search_config()->allowPartialDescent=environment.partial_descent_available();
    search.search_config()->instantGravity=environment.naturalGravityEnabled && environment.gravityIntervalSeconds<=1e-7;
    search.search_config()->wallOnlyHorizontal=environment.wall_only_horizontal();
    search.search_config()->spinBonuses=environment.spinBonuses;
    search.search_config()->pieceTypes=environment.pieceTypes;
    search.search_config()->allSpinEnabled=spinRules.allSpinEnabled;
    search.search_config()->noHoldReverse=spinRules.noHoldReverse;
    search.search_config()->suppressNonTSpins=spinRules.suppressNonTSpins;
    search.ai()->clear_candidates();
    search.ai()->set_policy(policy);
    search.ai()->set_terrain_capabilities({environment.holdEnabled,
        environment.wall_only_horizontal() || !environment.partial_descent_available() ||
            (environment.naturalGravityEnabled && environment.gravityIntervalSeconds<=1e-7),true,4});
    search.ai()->set_fast_terrain(bool(environment.quickPlayRules));
    diagnostics.forecastStepSeconds = environment.secondsPerPiece > 0
        ? environment.secondsPerPiece : budgetMs / 1000.0;
    diagnostics.openerDoubleCancelEnabled = environment.openerDoubleCancelEnabled;
    search.ai()->set_garbage_forecast(packets, diagnostics.forecastStepSeconds);
#ifdef TETR_CHATGPT_KEY_PIECE
    search.ai()->set_key_state(canHold);
#endif
#ifdef TETR_CHATGPT_KEY_QUAD
    // A deduced singleton is now part of the searched queue. The following
    // bag starts afresh; the same inferred I must never be reserved twice.
    search.ai()->set_key_state(preview.size()+1, hold,
        diagnostics.inferredNextPiece>=0?std::vector<int>{}:unseenBagPieces, environment.randomPieces);
#endif
#ifdef TETR_CHATGPT_BAG_TAIL
    search.set_bag_state(unseenBagPieces, environment.randomPieces);
#endif
    if(environment.searchCancelled && environment.searchCancelled->load()) return -1;
    // Force rescoring after visible garbage/chain/policy changes. Only
    // allocation is reused, never another player's root information.
    const Piece::Pose currentPose{{startRow, startCol}, Runtime::Rotation::from_index(startRotation)};
    const auto* start = native_pose(search.context().get(), pieces[next.front()], currentPose);
    // Upstream update() reads max_length, initialized only by the first
    // valid run. A fresh context must not call update() prematurely.
    if (work.started) search.update();
    if (start != nullptr && start->check(map)) work.started = true;
    const auto searchStarted = std::chrono::steady_clock::now();
#ifdef TETR_CHATGPT_KEY_PIECE
    search.ai()->begin_search(map, std::string(1, pieces[next.front()]) + preview,
        canHold && hold >= 0 ? pieces[hold] : ' ', status.previousCleared);
    const int remainingBudget = std::max(1, budgetMs - int(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - searchStarted).count()));
#else
    const int remainingBudget = budgetMs;
#endif
    const auto result = canHold
        ? search.run_hold(map, start, hold >= 0 && hold < 7 ? pieces[hold] : ' ',
                          true, preview.data(), preview.size(), remainingBudget)
        : search.run(map, start, preview.data(), preview.size(), remainingBudget);
    for(const auto& candidate:search.ai()->candidates())
        candidates.push_back({environment.pieceTypes[candidate.type],candidate.pose,candidate.rotation,candidate.value,candidate.movementSeconds});
    diagnostics.searchMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - searchStarted).count();
    diagnostics.searchWidth=environment.autoplaySearchWidth;
    diagnostics.projectedAttack = result.status.attack;
    diagnostics.value = result.status.value;
    diagnostics.rootNext = next;
    diagnostics.rootUnseen = unseenBagPieces;
    diagnostics.rootHold = canHold ? hold : -2;
    diagnostics.rootRow = startRow; diagnostics.rootCol = startCol;
    diagnostics.rootRotation = startRotation;
    diagnostics.rootB2b = status.b2b; diagnostics.rootCombo = status.combo;
    diagnostics.rootGarbageHeight = status.garbageHeight;
    diagnostics.rootPreviousCleared = status.previousCleared;
    for (const auto& packet : packets)
        diagnostics.rootPackets.emplace_back(packet.lines, packet.secondsUntilReady);
    diagnostics.projectedB2b = result.status.b2b;
    diagnostics.projectedRisen = result.status.risen;
    diagnostics.projectedDepth = result.status.placedPieces - status.placedPieces;
    diagnostics.projectedDead = result.status.dead;
    diagnostics.projectedReward = result.status.reward;
    diagnostics.projectedPathRisk = result.status.pathRisk;
    diagnostics.fallbackReason = 1;
    if (result.target != nullptr) {
        diagnostics.fallbackReason = 2;
        const int selected = piece_index(result.target->status.t);
        const int expected = result.change_hold ? (hold >= 0 ? hold : (next.size() > 1 ? next[1] : -1)) : next[0];
        if (selected == expected) {
            diagnostics.fallbackReason = 3;
            const auto piece = Piece::get_definition(environment.pieceTypes[selected]);
            Piece::Pose answer;
            int kick = 0;
            // The host encodes HOLD by a change of piece family. Swapping a
            // same-family HOLD cannot reset the actual movement start there.
            const bool appliesHold = result.change_hold && selected != next.front();
            const auto* heldSpawn = appliesHold
                ? spawn_node(search.context().get(), map, pieces[selected], status.previousCleared) : nullptr;
            if (appliesHold && !heldSpawn)
                return BotNativeHost::fallback(field, next, startRow, startCol, startRotation,
                    answerRow, answerCol, answerRotation, answerKick);
            const Piece::Pose hostStart = appliesHold ? host_pose(*heldSpawn) : currentPose;
            if(search.search_config()->instantGravity) {
                // 20G changes geometry, not input timers. Validate the chosen
                // surface landing through the same route search used to show it.
                const auto target=host_pose(*result.target.node);
                const auto record=result.target.kick ? Runtime::RotationRecord::from_kick(
                    Piece::KickIndex::from_index(result.target.kick-1)) : Runtime::RotationRecord::none();
                Runtime::HandlingConfig handling;handling.das=Runtime::Duration::from_seconds(environment.dasSeconds);
                handling.arr=Runtime::Duration::from_seconds(environment.arrSeconds);
                ::Engine::MovementState origin;origin.pose=hostStart;
                const ::Engine::AutoplaySpinRules spinRules{environment.spinBonuses};
                if(::Engine::search_autoplay_path(field,piece,origin,target,record,handling,environment.movement,
                    Runtime::GravityInterval::instant(),false,{},spinRules)) {
                    answerRow=target.position.row;answerCol=target.position.col;answerRotation=target.rotation.index();
                    answerKick=result.target.kick;diagnostics.fallbackReason=0;return selected;
                }
            }
            if (BotNativeHost::reachable(result.target.node, piece, field, hostStart,
                                         result.target.spin, result.target.mini, answer, kick,&environment)) {
                answerRow = answer.position.row;
                answerCol = answer.position.col;
                answerRotation = answer.rotation.index();
                answerKick = kick;
                diagnostics.fallbackReason = 0;
                return selected;
            }
        }
    }
    return BotNativeHost::fallback(field, next, startRow, startCol, startRotation,
        answerRow, answerCol, answerRotation, answerKick);
}
}
