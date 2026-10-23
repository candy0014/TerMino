#include "tetr/legacy/bot_adapter.h"
#include "../bot/bot_piece_access.h"
#include "tetr/compat/config_encoding.h"
#include "tetr/compat/gravity_encoding.h"
#include "tetr/compat/piece_encoding.h"
#include "tetr/runtime/session_state.h"
#include "tetr/runtime/dynamic_rules.h"
#include "tetr/runtime/game_context.h"
#include "tetr/app/application_state.h"
#include "tetr/engine/movement_presentation.h"
#include "tetr/engine/autoplay_path.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <mutex>



// BOT implementations now live in separate translation units.  This file owns
// their shared legacy compatibility environment and synchronizes it from the
// typed Runtime::GameContext before every BOT call.
#include "tetr/bot/environment.h"
#include "tetr/bot/bot_while1.h"
#include "tetr/bot/bot_candy0014.h"
#include "tetr/bot/bot_mech.h"
#include "tetr/bot/bot_zzztoj.h"
#include "tetr/bot/bot_chatgpt.h"
#include "tetr/bot/bot_coldclear.h"

namespace {
using BotGetPosition = int (*)(
    const Bot::Environment&, const Runtime::PlayerSessionState&, Runtime::Playfield, std::vector<int>,
    const std::vector<int>&, int, int, int, int, int&, int&, int&, int&);

struct BotImplementation {
    Runtime::AutoplayMode mode;
    bool (*initialize)(const Bot::Environment&);
	BotGetPosition choosePosition;
	Autoplay::Availability (*available)(const Bot::Environment&);
};

constexpr BotImplementation kBotImplementations[] = {
    {Runtime::AutoplayMode::While1, Bot_while1::init, Bot_while1::getpos, Bot_while1::availability},
    {Runtime::AutoplayMode::Candy0014, Bot_candy0014::init, Bot_candy0014::getpos, Bot_candy0014::availability},
    {Runtime::AutoplayMode::Mech, Bot_mech::init, Bot_mech::getpos, Bot_mech::availability},
    {Runtime::AutoplayMode::ZzztojBotris, Bot_zzztoj::init, Bot_zzztoj::getpos, Bot_zzztoj::availability},
    {Runtime::AutoplayMode::ZzztojToj, Bot_zzztoj::init, Bot_zzztoj::getpos, Bot_zzztoj::availability},
    {Runtime::AutoplayMode::ZzztojDig, Bot_zzztoj::init, Bot_zzztoj::getpos, Bot_zzztoj::availability},
    {Runtime::AutoplayMode::ZzztojQq, Bot_zzztoj::init, Bot_zzztoj::getpos, Bot_zzztoj::availability},
    {Runtime::AutoplayMode::ZzztojC2, Bot_zzztoj::init, Bot_zzztoj::getpos, Bot_zzztoj::availability},
    {Runtime::AutoplayMode::ChatGPT, Bot_chatgpt::init, Bot_chatgpt::getpos, Bot_chatgpt::availability},
    {Runtime::AutoplayMode::ColdClear, Bot_coldclear::init, Bot_coldclear::getpos, Bot_coldclear::availability},
};

const BotImplementation* implementation_for(Runtime::AutoplayMode mode) noexcept {
    for (const auto& implementation : kBotImplementations) {
        if (implementation.mode == mode) return &implementation;
    }
    return nullptr;
}
} // namespace

namespace Bot{
	static Environment make_environment(const Runtime::GameContext& context){
		Environment environment=Bot::make_environment(context.config);
        environment.searchCancelled=context.autoplaySearchCancelled;
		environment.quickPlayFloor=context.session.quickPlay.floor;
        environment.quickPlayProgress=context.session.quickPlay.progress;
        environment.quickPlayAltitude=context.session.quickPlay.altitude;
        environment.quickPlayBooster=context.session.quickPlay.speed;
        environment.elapsedGameSeconds=Runtime::elapsed_game_time(context.session,context.now()).seconds();
		environment.incomingGarbageDelaySeconds=Runtime::current_incoming_garbage_delay(context).seconds();
		environment.gravityIntervalSeconds=Compat::to_legacy_seconds(Runtime::current_gravity_interval(context));
		environment.naturalGravityEnabled=Runtime::current_gravity_interval(context).enabled();
        environment.lockDelaySeconds=Runtime::current_epld(context).seconds();
		environment.nowSeconds=context.now().seconds();
		environment.secondsPerPiece=context.uses_piece_time() ? 1.0/context.config.simulationPps :
			(context.config.autoplay.pps > 0 ? 1.0/context.config.autoplay.pps : 0.0);
		environment.openerDoubleCancelEnabled=context.config.garbage.mode==Runtime::GarbageMode::Multiplayer;
		environment.sentAttack=context.session.player.gameplay.scoring.totalOutgoingAttack;
		return environment;
	}
	bool init(Runtime::GameContext& context){
		const Environment environment=make_environment(context);
		if(context.config.autoplay.mode==Runtime::AutoplayMode::Disabled) return true;
		const BotImplementation* implementation=implementation_for(context.config.autoplay.mode);
		assert(implementation);
		return implementation ? implementation->initialize(environment) : false;
	}
	Piece::Type getpos(Runtime::GameContext& context,Runtime::PlayerSessionState& player,Runtime::Playfield field,
		const std::vector<Piece::Type>& next,const std::vector<Piece::Type>& unseenBagPieces,
		Piece::Pose start,HoldInput hold,
		Piece::Pose& answer,Runtime::RotationRecord& answerRotation){
		Environment environment=make_environment(context);
        environment.rootPaceRemainingSeconds=std::max(0.0,context.config.autoplay.pps>0 ?
            1.0/context.config.autoplay.pps-(context.real_now()-player.timing.autoplay).seconds() : 0.0);
		if(next.empty())return Piece::Type::none();
		environment.fieldWidth=field.width();environment.fieldHeight=field.height();
		environment.sentAttack=player.gameplay.scoring.totalOutgoingAttack;
		std::vector<int> legacyNext;
		legacyNext.reserve(next.size());
		const auto logicalType=[&](Piece::Type type) {
			if (!type.valid()) return -1;
			// Event bags can contain several geometries of the same family,
			// including an ordinary piece followed by its event counterpart.
			// A per-family alias would let a later NEXT/HOLD overwrite the
			// current piece. The QP rule search accepts their actual catalog IDs.
			if(context.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
				(context.config.quickPlay.fool!=Runtime::FoolMode::Disabled || context.config.quickPlay.continuousCancellation))
				return type.index();
			const auto metadata=Piece::metadata(type);
			const bool coldclearStandard = context.config.autoplay.mode==Runtime::AutoplayMode::ColdClear &&
				Runtime::standard_rotation_system(metadata.rotationSystem);
			if ((type.index()<21 || type.index()>=173 || coldclearStandard) && metadata.familyIndex>=0 && metadata.familyIndex<7) {
				environment.pieceTypes[metadata.familyIndex]=type;
				return metadata.familyIndex;
			}
			return type.index();
		};
		for(Piece::Type type:next) legacyNext.push_back(logicalType(type));
		std::vector<int> legacyUnseenBagPieces;
		legacyUnseenBagPieces.reserve(unseenBagPieces.size());
		for(Piece::Type type:unseenBagPieces) legacyUnseenBagPieces.push_back(logicalType(type));
		int ans_x=answer.position.row,ans_y=answer.position.col;
		int ans_type=Compat::to_legacy(answer.rotation),ans_rot=Compat::to_legacy(answerRotation);
		const int sx=start.position.row,sy=start.position.col,stype=Compat::to_legacy(start.rotation);
		const int legacyHold=hold.enabled ? logicalType(hold.piece) : -2;
		if(context.config.autoplay.mode==Runtime::AutoplayMode::Disabled) return Piece::Type::none();
		const BotImplementation* implementation=implementation_for(context.config.autoplay.mode);
		assert(implementation);
		if(!implementation) return Piece::Type::none();
		if (!implementation->available(environment)) {
			answer=start;answerRotation=Runtime::RotationRecord::none();
			const auto definition=Piece::get_definition(next.front());
			while(definition.can_place(answer.translated(1,0),field))++answer.position.row;
			return next.front();
		}
		const int selected=implementation->choosePosition(
			environment,player,field,std::move(legacyNext),legacyUnseenBagPieces,sx,sy,stype,legacyHold,
			ans_x,ans_y,ans_type,ans_rot);
		if (context.config.autoplay.mode == Runtime::AutoplayMode::ChatGPT &&
			std::getenv("TETR_TEST_METRICS")) {
			const auto diagnostic = Bot_chatgpt::last_diagnostics();
			const bool second = context.app_component() && context.app_component()->primaryContext != &context;
			std::ostringstream line;
			line.precision(17); // Round-trip forecast timing and all diagnostic doubles.
			line << "TETR_BOT_DIAGNOSTIC side=" << (second ? "second" : "first")
				 << " pieces=" << player.gameplay.scoring.placedBlocks
				 << " roof=" << diagnostic.roof << " incoming=" << diagnostic.incoming
				 << " ready=" << diagnostic.readyIncoming
				 << " fallback=" << diagnostic.fallbackReason
				 << " projected_attack=" << diagnostic.projectedAttack << " value=" << diagnostic.value
				 << " search_ms=" << diagnostic.searchMilliseconds
				 << " dig_pressure=" << diagnostic.digPressure
				 << " forecast_step=" << diagnostic.forecastStepSeconds
				 << " opener_double=" << int(diagnostic.openerDoubleCancelEnabled)
				 << " selected=" << selected << " row=" << ans_x << " col=" << ans_y
				 << " rotation=" << ans_type << " kick=" << ans_rot << " board=";
			for (std::size_t i = 0; i < diagnostic.rows.size(); ++i)
				line << (i ? "," : "") << diagnostic.rows[i];
			line << " trace_version=1 root_hold=" << diagnostic.rootHold
				 << " root_row=" << diagnostic.rootRow << " root_col=" << diagnostic.rootCol
				 << " root_rotation=" << diagnostic.rootRotation
				 << " root_b2b=" << diagnostic.rootB2b << " root_combo=" << diagnostic.rootCombo
				 << " root_garbage_height=" << diagnostic.rootGarbageHeight
				 << " root_cleared=" << int(diagnostic.rootPreviousCleared)
				 << " projected_b2b=" << diagnostic.projectedB2b
				 << " projected_risen=" << diagnostic.projectedRisen
				 << " projected_depth=" << diagnostic.projectedDepth
				 << " projected_dead=" << int(diagnostic.projectedDead)
				 << " projected_reward=" << diagnostic.projectedReward
				 << " projected_risk=" << diagnostic.projectedPathRisk << " root_next=";
			for (std::size_t i = 0; i < diagnostic.rootNext.size(); ++i)
				line << (i ? "," : "") << diagnostic.rootNext[i];
			line << " root_unseen=";
			if (diagnostic.rootUnseen.empty()) line << '-';
			for (std::size_t i = 0; i < diagnostic.rootUnseen.size(); ++i)
				line << (i ? "," : "") << diagnostic.rootUnseen[i];
			line << " inferred_next=" << diagnostic.inferredNextPiece;
			line << " root_packets=";
			if (diagnostic.rootPackets.empty()) line << '-';
			for (std::size_t i = 0; i < diagnostic.rootPackets.size(); ++i)
				line << (i ? "," : "") << diagnostic.rootPackets[i].first << ':' << diagnostic.rootPackets[i].second;
			line << '\n';
			std::unique_lock<std::recursive_mutex> outputLock;
			if (context.output_mutex()) outputLock = std::unique_lock<std::recursive_mutex>(*context.output_mutex());
			std::cerr << line.str();
		}
		answer={{ans_x,ans_y},Compat::rotation_from_legacy(ans_type)};
		answerRotation=Compat::rotation_record_from_legacy(ans_rot);
		const auto selectedType=selected>=0 && selected<7 ? environment.pieceTypes[selected] : Compat::piece_type_from_legacy(selected);
		const bool allowedSelection=selectedType==next.front() || (hold.enabled &&
			(hold.piece.valid() ? selectedType==hold.piece : next.size()>1 && selectedType==next[1]));
		if (selectedType.valid() && allowedSelection) {
			auto plannedStart=start;
			if (selectedType!=next.front()) {
				plannedStart=Piece::spawn_pose(field.width(),selectedType);
				const auto definition=Piece::get_definition(selectedType);
                if(Runtime::enabled(context.config.quickPlay.snowball))
                    plannedStart=Piece::fit_spawn_inside_walls(plannedStart,definition.cells(plannedStart.rotation),field.width());
                int minimumOffset=definition.cells(plannedStart.rotation).front()[0];
                for (const auto& cell:definition.cells(plannedStart.rotation))minimumOffset=std::min(minimumOffset,cell[0]);
                const int minimumOrigin=-Runtime::kHiddenRowCapacity-minimumOffset;
				while (!definition.can_place(plannedStart,field) && player.clutch.previousPieceCleared && plannedStart.position.row>minimumOrigin)
					--plannedStart.position.row;
			}
			Engine::MovementState origin;origin.pose=plannedStart;
            const auto plannedPiece=Piece::get_definition(selectedType);
            if(Runtime::current_gravity_interval(context).is_instant() && plannedPiece.can_place(origin.pose,field))
                Engine::apply_presentation_instant_gravity(origin,
                    [&](Piece::Pose pose){return plannedPiece.can_place(pose,field);});
            if (Engine::search_autoplay_path(field,plannedPiece,origin,answer,answerRotation,
                context.config.handling,context.config.movement,Runtime::current_gravity_interval(context),false,{},
                Engine::autoplay_spin_rules(context.config))) return selectedType;
		}
		// A strategy's optimistic target must never become an unreachable move,
		// including high-PPS playback that skips the visible movement script.
		answer=start;answerRotation=Runtime::RotationRecord::none();
		const auto currentPiece=Piece::get_definition(next.front());
		while(currentPiece.can_place(answer.translated(1,0),field))++answer.position.row;
		return next.front();
	}
	std::string getop(Runtime::GameContext& context,Runtime::Playfield field,Piece::Definition piece,
        Piece::Pose start,Piece::Pose answer,Runtime::RotationRecord answerRotation) {
        Engine::MovementState origin;origin.pose=start;
        const auto path=Engine::search_autoplay_path(field,piece,origin,answer,answerRotation,
            context.config.handling,context.config.movement,Runtime::current_gravity_interval(context),
            context.config.autoplay.handlingSimulation,
            [&] {return context.autoplaySearchCancelled && context.autoplaySearchCancelled->load();},
            Engine::autoplay_spin_rules(context.config));
        return path ? path->script : std::string{};
    }
}
