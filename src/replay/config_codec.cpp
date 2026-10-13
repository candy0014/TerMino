#include "tetr/replay/config_codec.h"
#include "tetr/compat/config_encoding.h"
#include "tetr/compat/garbage_encoding.h"
#include "tetr/compat/gravity_encoding.h"
#include "tetr/compat/legacy_text.h"
#include "tetr/util/text_codec.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace Replay {
namespace {

using Config = Runtime::RuntimeConfig;
using EncodeField = void (*)(std::string&, const Config&);
using DecodeField = void (*)(Config&, std::string_view);

struct FieldCodec {
    std::string_view name;
    EncodeField encode;
    DecodeField decode;
};

void append(std::string& output, std::string_view name, int value) {
    output += "{";
    output += name;
    output += ",";
    output += std::to_string(value);
    output += "}";
}

void append(std::string& output, std::string_view name, double value) {
    output += "{";
    output += name;
    output += ",";
    output += std::to_string(value);
    output += "}";
}

void append_precise(std::string& output, std::string_view name, double value) {
    std::ostringstream field;
    field.imbue(std::locale::classic());
    field << '{' << name << ',' << std::setprecision(17) << value << '}';
    output += field.str();
}

int legacy_int(std::string_view value) {
    return static_cast<int>(Compat::LegacyText::parse_int(value));
}

double legacy_real(std::string_view value) {
    return Compat::LegacyText::parse_real(value);
}

#define INT_FIELD(NAME, ENCODE_EXPR, DECODE_STMT) \
    FieldCodec{NAME, \
        [](std::string& output, const Config& config) { append(output, NAME, (ENCODE_EXPR)); }, \
        [](Config& config, std::string_view value) { const int integer = legacy_int(value); DECODE_STMT; }}
#define REAL_FIELD(NAME, ENCODE_EXPR, DECODE_STMT) \
    FieldCodec{NAME, \
        [](std::string& output, const Config& config) { append(output, NAME, (ENCODE_EXPR)); }, \
        [](Config& config, std::string_view value) { const double real = legacy_real(value); DECODE_STMT; }}
#define ENCODE_ONLY_INT(NAME, ENCODE_EXPR) \
    FieldCodec{NAME, \
        [](std::string& output, const Config& config) { append(output, NAME, (ENCODE_EXPR)); }, \
        nullptr}

const std::vector<FieldCodec>& field_codecs() {
    static const std::vector<FieldCodec> codecs = {
        FieldCodec{"DisableUndo",
            [](std::string& output, const Config& config) {
                if (config.disableUndo) append(output, "DisableUndo", 1);
            },
            [](Config& config, std::string_view value) { config.disableUndo = legacy_int(value) != 0; }},
        FieldCodec{"DisablePause",
            [](std::string& output, const Config& config) {
                if (config.disablePause) append(output, "DisablePause", 1);
            },
            [](Config& config, std::string_view value) { config.disablePause = legacy_int(value) != 0; }},
        FieldCodec{"DisabledMoves",
            [](std::string& output, const Config& config) {
                if (config.movement.mask()) append(output, "DisabledMoves", static_cast<int>(config.movement.mask()));
            },
            [](Config& config, std::string_view value) {
                const auto mask = Util::Text::parse_integer<unsigned>(value);
                config.movement = Runtime::MovementRestrictions::from_mask(mask && *mask <= 127 ? *mask : 0);
            }},
        INT_FIELD("Model", Compat::to_legacy(config.mode.mode), config.mode.mode = Compat::game_mode_from_legacy(integer)),
        INT_FIELD("flagBlitz", config.mode.fixedBlitzSpeed ? 1 : 0, config.mode.fixedBlitzSpeed = integer != 0),
        INT_FIELD("RacingDistance", config.mode.racingDistance, config.mode.racingDistance = integer),
        REAL_FIELD("BlitzTime", config.mode.blitzTime.seconds(), config.mode.blitzTime = Runtime::Duration::from_seconds(real)),
        INT_FIELD("GarbageModel", Compat::to_legacy(config.garbage.mode), config.garbage.mode = Compat::garbage_mode_from_legacy(integer)),
        INT_FIELD("CheeseModel", config.garbage.cheeseModel, config.garbage.cheeseModel = integer),
        INT_FIELD("GarbageShapeVersion", config.garbage.legacyShapeRules ? 0 : 1, config.garbage.legacyShapeRules = integer < 1),
        INT_FIELD("HoleNum", Compat::to_legacy(config.garbage.holePattern), config.garbage.holePattern = Compat::hole_pattern_from_legacy(integer)),
        INT_FIELD("LayerHeight", config.garbage.layerHeight, config.garbage.layerHeight = integer),
        REAL_FIELD("TimeInterval", config.garbage.generationInterval.seconds(), config.garbage.generationInterval = Runtime::Duration::from_seconds(real)),
        REAL_FIELD("CheeseMessiness", config.garbage.cheeseMessiness, config.garbage.cheeseMessiness = real),
        REAL_FIELD("SingleMessiness", config.garbage.singleMessiness, config.garbage.singleMessiness = real),
        REAL_FIELD("GarbageMultiple", config.garbage.attackMultiplier, config.garbage.attackMultiplier = real),
        REAL_FIELD("SendGarbageMultiplier", config.garbage.sendMultiplier, config.garbage.sendMultiplier = std::clamp(real,0.0,1000.0)),
        FieldCodec{"HitMultiplier",
            [](std::string& output,const Config& config) { append_precise(output,"HitMultiplier",config.garbage.hitMultiplier); },
            [](Config& config,std::string_view value) {
                const auto real=Util::Text::parse_finite_real(value);
                config.garbage.hitMultiplier=real ? std::clamp(*real,0.0,1000.0) : 1.0;
            }},
        FieldCodec{"CancellationMultiplier",
            [](std::string& output,const Config& config) { append_precise(output,"CancellationMultiplier",config.garbage.cancelMultiplier); },
            [](Config& config,std::string_view value) {
                const auto real=Util::Text::parse_finite_real(value);
                config.garbage.cancelMultiplier=real ? std::clamp(*real,0.0,1000.0) : 1.0;
            }},
        FieldCodec{"LegacyIncomingRounding",
            [](std::string& output,const Config& config) { if(config.garbage.legacyIncomingRounding) append(output,"LegacyIncomingRounding",1); },
            [](Config& config,std::string_view value) { config.garbage.legacyIncomingRounding=legacy_int(value)!=0; }},
        INT_FIELD("AvoidSameColumn", config.garbage.avoidSameColumn ? 1 : 0, config.garbage.avoidSameColumn = integer != 0),
        INT_FIELD("GarbageCap", config.garbage.cap, config.garbage.cap = std::clamp(integer,0,1000000)),
        INT_FIELD("OpenerPhase", config.garbage.openerPhase, config.garbage.openerPhase = std::clamp(integer,0,1000000)),
        FieldCodec{"LegacyOpenerPhase",
            [](std::string& output,const Config& config) { if(config.garbage.legacyOpenerPhase) append(output,"LegacyOpenerPhase",1); },
            [](Config& config,std::string_view value) { config.garbage.legacyOpenerPhase = legacy_int(value)!=0; }},
        REAL_FIELD("SurvivalAPM", config.garbage.survivalApm, config.garbage.survivalApm = real),
        REAL_FIELD("GarbageInTime", config.garbage.incomingDelay.seconds(), config.garbage.incomingDelay = Runtime::Duration::from_seconds(real)),
        FieldCodec{"GarbageRiseInterval",
            [](std::string& output,const Config& config) {
                if (config.garbage.riseInterval!=Runtime::Duration{})
                    append_precise(output,"GarbageRiseInterval",config.garbage.riseInterval.seconds());
            },
            [](Config& config,std::string_view value) {
                const auto real=Util::Text::parse_finite_real(value);
                config.garbage.riseInterval=Runtime::Duration::from_seconds(real ? std::clamp(*real,0.0,1e18) : 0.0);
            }},
        FieldCodec{"GarbageEntryMode",
            [](std::string& output, const Config& config) {
                if (config.garbage.entryMode != Runtime::GarbageEntryMode::AfterNonClear)
                    append(output, "GarbageEntryMode", static_cast<int>(config.garbage.entryMode));
            },
            [](Config& config, std::string_view value) {
                config.garbage.entryMode = static_cast<Runtime::GarbageEntryMode>(std::clamp(legacy_int(value),0,2));
            }},
        INT_FIELD("QP_mod_expert", Compat::to_legacy(config.quickPlay.expert), config.quickPlay.expert = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_nohold", Compat::to_legacy(config.quickPlay.noHold), config.quickPlay.noHold = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_messy", Compat::to_legacy(config.quickPlay.messy), config.quickPlay.messy = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_gravity", Compat::to_legacy(config.quickPlay.gravity), config.quickPlay.gravity = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_volatile", Compat::to_legacy(config.quickPlay.volatileMode), config.quickPlay.volatileMode = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_doublehole", Compat::to_legacy(config.quickPlay.doubleHole), config.quickPlay.doubleHole = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_invisible", Compat::to_legacy(config.quickPlay.invisible), config.quickPlay.invisible = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_allspin", Compat::to_legacy(config.quickPlay.allSpin), config.quickPlay.allSpin = Compat::all_spin_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_backfire", Compat::to_legacy(config.quickPlay.backfire), config.quickPlay.backfire = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_snakeeyes", Compat::to_legacy(config.quickPlay.snakeEyes), config.quickPlay.snakeEyes = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_snowball", Compat::to_legacy(config.quickPlay.snowball), config.quickPlay.snowball = Compat::modifier_mode_from_legacy(integer)),
        INT_FIELD("QP_mod_fool", Compat::to_legacy(config.quickPlay.fool), config.quickPlay.fool = Compat::fool_mode_from_legacy(integer)),
        INT_FIELD("QP_default_floor", config.quickPlay.defaultFloor, config.quickPlay.defaultFloor = integer),
        INT_FIELD("QP_continuous_cancellation", config.quickPlay.continuousCancellation ? 1 : 0,
                  config.quickPlay.continuousCancellation = integer != 0),
        REAL_FIELD("Speed", Compat::to_legacy_seconds(config.handling.gravityInterval), config.handling.gravityInterval = Compat::gravity_from_legacy_seconds(real)),
        FieldCodec{"GravityProgression",
            [](std::string& output,const Config& config) {
                if(config.handling.gravityProgression!=Runtime::GravityProgression::Fixed)
                    append(output,"GravityProgression",static_cast<int>(config.handling.gravityProgression));
            },
            [](Config& config,std::string_view value) {
                config.handling.gravityProgression=static_cast<Runtime::GravityProgression>(std::clamp(legacy_int(value),0,2));
            }},
        FieldCodec{"GravityLevelLines",
            [](std::string& output,const Config& config) {
                if(config.handling.gravityLevelLines!=10) append(output,"GravityLevelLines",config.handling.gravityLevelLines);
            },
            [](Config& config,std::string_view value) { config.handling.gravityLevelLines=std::clamp(legacy_int(value),1,1000000); }},
        FieldCodec{"GravityAcceleration",
            [](std::string& output,const Config& config) {
                if(config.handling.gravityAcceleration!=1) append_precise(output,"GravityAcceleration",config.handling.gravityAcceleration);
            },
            [](Config& config,std::string_view value) {
                const auto factor=Util::Text::parse_finite_real(value);
                config.handling.gravityAcceleration=factor ? std::clamp(*factor,0.01,100.0) : 1.0;
            }},
        REAL_FIELD("SDF", config.handling.softDropFactor, config.handling.softDropFactor = real),
        INT_FIELD("SDFFixed", config.handling.softDropFixed ? 1 : 0, config.handling.softDropFixed = integer != 0),
        REAL_FIELD("DAS", config.handling.das.seconds(), config.handling.das = Runtime::Duration::from_seconds(real)),
        REAL_FIELD("ARR", config.handling.arr.seconds(), config.handling.arr = Runtime::Duration::from_seconds(real)),
        REAL_FIELD("DCD", config.handling.dcd.seconds(), config.handling.dcd = Runtime::Duration::from_seconds(std::max(0.0,real))),
        FieldCodec{"PreMove",
            [](std::string& output, const Config& config) { if (!config.handling.preMove) append(output,"PreMove",0); },
            [](Config& config, std::string_view value) { config.handling.preMove=legacy_int(value)!=0; }},
        FieldCodec{"DASPrecharge",
            [](std::string& output, const Config& config) { if (!config.handling.dasPrecharge) append(output,"DASPrecharge",0); },
            [](Config& config, std::string_view value) { config.handling.dasPrecharge=legacy_int(value)!=0; }},
        INT_FIELD("mapWidth", config.field.width, config.field.width = integer),
        INT_FIELD("mapHeight", config.field.height, config.field.height = integer),
        INT_FIELD("NextNum", config.field.nextCount, config.field.nextCount = integer),
        INT_FIELD("OpenHold", config.field.holdEnabled ? 1 : 0, config.field.holdEnabled = integer != 0),
        FieldCodec{"InfiniteHold",
            [](std::string& output, const Config& config) {
                if (config.field.infiniteHold) append(output,"InfiniteHold",1);
            },
            [](Config& config, std::string_view value) { config.field.infiniteHold=legacy_int(value)!=0; }},
        FieldCodec{"HoldCount",
            [](std::string& output, const Config& config) {
                if (config.field.holdCount!=1) append(output,"HoldCount",config.field.holdCount);
            },
            [](Config& config, std::string_view value) { config.field.holdCount=std::clamp(legacy_int(value),0,1000000000); }},
        INT_FIELD("Invisible", config.field.invisible ? 1 : 0, config.field.invisible = integer != 0),
        INT_FIELD("AntiGravity", config.field.antiGravity ? 1 : 0, config.field.antiGravity = integer != 0),
        INT_FIELD("Ghost", config.field.ghost ? 1 : 0, config.field.ghost = integer != 0),
        REAL_FIELD("EPLD", config.handling.epld.seconds(), config.handling.epld = Runtime::Duration::from_seconds(real)),
        INT_FIELD("EPLDLim", config.handling.lockResetLimit, config.handling.lockResetLimit = integer),
        INT_FIELD("InfiniteMovement", config.handling.infiniteMovement ? 1 : 0, config.handling.infiniteMovement = integer != 0),
        INT_FIELD("RotationSystem", Compat::to_legacy(config.rotationSystem), config.rotationSystem = Compat::rotation_system_from_legacy(integer)),
        INT_FIELD("AttackRulesVersion", config.attackRules.legacy ? 0 : 1, config.attackRules.legacy = integer < 1),
        INT_FIELD("B2BReward", static_cast<int>(config.attackRules.b2bReward), config.attackRules.b2bReward = static_cast<Runtime::B2BReward>(std::clamp(integer,0,2))),
        INT_FIELD("B2BRewardAmount", config.attackRules.b2bRewardAmount, config.attackRules.b2bRewardAmount = std::clamp(integer,0,1000000)),
        INT_FIELD("B2BCharging", config.attackRules.b2bCharging ? 1 : 0, config.attackRules.b2bCharging = integer != 0),
        INT_FIELD("B2BChargeThreshold", config.attackRules.b2bChargeThreshold, config.attackRules.b2bChargeThreshold = std::clamp(integer,0,1000000)),
        INT_FIELD("B2BChargeReduction", config.attackRules.b2bChargeReduction, config.attackRules.b2bChargeReduction = std::clamp(integer,-1000000,1000000)),
        INT_FIELD("PerfectClearEnabled", config.attackRules.perfectClearEnabled ? 1 : 0, config.attackRules.perfectClearEnabled = integer != 0),
        INT_FIELD("PerfectClearAttack", config.attackRules.perfectClearAttack, config.attackRules.perfectClearAttack = std::clamp(integer,0,1000000)),
        INT_FIELD("PerfectClearCharge", config.attackRules.perfectClearCharge, config.attackRules.perfectClearCharge = std::clamp(integer,0,1000000)),
        INT_FIELD("HalfClearEnabled", config.attackRules.halfClearEnabled ? 1 : 0, config.attackRules.halfClearEnabled = integer != 0),
        INT_FIELD("HalfClearAttack", config.attackRules.halfClearAttack, config.attackRules.halfClearAttack = std::clamp(integer,0,1000000)),
        INT_FIELD("HalfClearCharge", config.attackRules.halfClearCharge, config.attackRules.halfClearCharge = std::clamp(integer,0,1000000)),
        INT_FIELD("GarbageSpecialAttack", config.attackRules.garbageSpecialAttack, config.attackRules.garbageSpecialAttack = std::clamp(integer,0,1000000)),
        INT_FIELD("SpinBonuses", static_cast<int>(config.spinBonuses), config.spinBonuses = Runtime::spin_bonuses_from_index(integer)),
        INT_FIELD("RotationRulesVersion", config.legacyRotationRules ? 0 : 1, config.legacyRotationRules = integer < 1),
        INT_FIELD("FoolPieceSetVersion", config.legacyFoolPieceSet ? 0 : 1, config.legacyFoolPieceSet = integer < 1),
        FieldCodec{"LegacySpinScoring",
            [](std::string& output, const Config& config) {
                if (config.legacySpinScoring) append(output, "LegacySpinScoring", 1);
            },
            [](Config& config, std::string_view value) { config.legacySpinScoring = legacy_int(value) != 0; }},
        INT_FIELD("Random", config.randomPieces ? 1 : 0, config.randomPieces = integer != 0),
        INT_FIELD("RandomRules", static_cast<int>(Runtime::effective_bag_randomizer(config)), config.bagRandomizer = Runtime::bag_randomizer_from_index(integer)),
        INT_FIELD("RandomizerVersion", config.legacyRandomizerRules ? 0 : 1, config.legacyRandomizerRules = integer < 1),
        ENCODE_ONLY_INT("G20", config.handling.gravityInterval.is_instant() ? 1 : 0),
        REAL_FIELD("LineClearDelay", config.field.lineClearDelay.seconds(), config.field.lineClearDelay = Runtime::Duration::from_seconds(real)),
        REAL_FIELD("EntryDelay", config.field.entryDelay.seconds(), config.field.entryDelay = Runtime::Duration::from_seconds(std::max(0.0,real))),
        INT_FIELD("Autoplay", Compat::to_legacy(config.autoplay.mode), config.autoplay.mode = Compat::autoplay_mode_from_legacy(integer)),
        FieldCodec{"AutoplayHandlingSimulation",
            [](std::string& output,const Config& config) { if(config.autoplay.handlingSimulation) append(output,"AutoplayHandlingSimulation",1); },
            [](Config& config,std::string_view value) { config.autoplay.handlingSimulation=legacy_int(value)!=0; }},
        REAL_FIELD("Autoplay_PPS", config.autoplay.pps, config.autoplay.pps = real),
        INT_FIELD("Autoplay_Width", config.autoplay.searchWidth, config.autoplay.searchWidth = integer),
        FieldCodec{"TurnPieces",
            [](std::string& output, const Config& config) {
                std::ostringstream value;
                value.imbue(std::locale::classic());
                value << "{TurnPieces," << std::setprecision(17) << config.turnPieces << '}';
                output += value.str();
            },
            [](Config& config, std::string_view value) {
                const auto real = Util::Text::parse_finite_real(value);
                config.turnPieces = real && *real > 0.0 ? std::clamp(*real, 1e-9, 1000000.0) : 0.0;
            }},
        FieldCodec{"SimulationPPS",
            [](std::string& output, const Config& config) {
                if (config.simulationPps > 0.0) {
                    std::ostringstream value;
                    value.imbue(std::locale::classic());
                    value << "{SimulationPPS," << std::setprecision(17) << config.simulationPps << '}';
                    output += value.str();
                }
            },
            [](Config& config, std::string_view value) {
                const auto real = Util::Text::parse_finite_real(value);
                config.simulationPps = real ? std::clamp(*real, 0.0, 1000000.0) : 0.0;
            }},
        FieldCodec{"MapRotationSystem",[](std::string& output,const Config& config) {
            if(config.initialMap && config.initialMap->generator==Runtime::MapGenerator::CustomSequence && config.mapRotationSystem>=0)
                append(output,"MapRotationSystem",config.mapRotationSystem);
        },[](Config& config,std::string_view value) {config.mapRotationSystem=std::clamp(legacy_int(value),-1,11);}},
        FieldCodec{"MapGoal",[](std::string& output,const Config& config) {
            if(config.initialMap && config.mode.mapGoal!=Runtime::MapGoal::Disabled) append(output,"MapGoal",static_cast<int>(config.mode.mapGoal));
        },[](Config& config,std::string_view value) {config.mode.mapGoal=static_cast<Runtime::MapGoal>(std::clamp(legacy_int(value),0,2));}},
        FieldCodec{"Map",[](std::string& output,const Config& config) {
            if(config.initialMap) output+="{Map,"+Runtime::encode_map_hex(*config.initialMap)+"}";
        },nullptr}
    };
    return codecs;
}

#undef INT_FIELD
#undef REAL_FIELD
#undef ENCODE_ONLY_INT

const FieldCodec* find_codec(std::string_view name) {
    const auto& codecs = field_codecs();
    const auto found = std::find_if(codecs.begin(), codecs.end(), [name](const FieldCodec& codec) {
        return codec.name == name;
    });
    return found == codecs.end() ? nullptr : &*found;
}

} // namespace

std::string encode_config_prefix(const Runtime::RuntimeConfig& config) {
    std::string result = "{";
    for (const FieldCodec& codec : field_codecs()) codec.encode(result, config);
    return result;
}

bool decode_config_prefix(std::string_view replay, Runtime::RuntimeConfig& config, ConfigPrefixResult& result) {
    const std::size_t outerEnd = Util::Text::matching_brace(replay, 0);
    if (outerEnd == std::string_view::npos) return false;
    result = ConfigPrefixResult{};
    config.initialMap.reset();
    config.mapRotationSystem=-1;
    config.mode.mapGoal=Runtime::MapGoal::Disabled;
    config.simulationPps = 0.0;
    config.turnPieces = 0.0;
    config.field.infiniteHold = false;
    config.field.holdCount = 1;
    config.movement = {}; // Old recordings permit all movement actions.
    config.disableUndo = false;
    config.disablePause = false;
    config.attackRules = {};
    config.attackRules.legacy = true;
    config.spinBonuses = Runtime::SpinBonuses::AllMiniPlus;
    config.legacySpinScoring = false;
    config.legacyRotationRules = true;
    config.legacyFoolPieceSet = true;
    config.bagRandomizer = Runtime::BagRandomizer::SevenBag;
    config.legacyRandomizerRules = true;
    config.handling.infiniteMovement = false;
    config.handling.softDropFixed = false;
    config.autoplay.handlingSimulation = false;
    config.handling.dcd = {};
    config.handling.preMove = true;
    config.handling.dasPrecharge = true;
    config.handling.gravityProgression = Runtime::GravityProgression::Fixed;
    config.handling.gravityLevelLines = 10;
    config.handling.gravityAcceleration = 1.0;
    config.field.entryDelay = {};
    config.garbage.sendMultiplier = 1.0;
    config.garbage.attackMultiplier = 1.0;
    config.garbage.hitMultiplier = 1.0;
    config.garbage.cancelMultiplier = 1.0;
    config.garbage.legacyIncomingRounding = false;
    config.garbage.singleMessiness = 0.0;
    config.garbage.avoidSameColumn = false;
    config.garbage.cap = 8;
    config.garbage.entryMode = Runtime::GarbageEntryMode::AfterNonClear;
    config.garbage.riseInterval={}; // Older recordings always raised whole batches.
    config.garbage.openerPhase = 14;
    config.garbage.legacyOpenerPhase = true;
    config.garbage.legacyShapeRules = true;
    // Recordings predating the startup switch used continuous cancellation unconditionally.
    config.quickPlay.continuousCancellation = true;
    config.quickPlay.snowball = Runtime::ModifierMode::Disabled;
    result.operationOffset = outerEnd + 1;
    bool hasTurnPieces = false;
    bool hasUndoCount = false;
    bool hasSpinBonuses = false;
    bool hasRandomRules = false;
    bool hasRandomizerVersion = false;
    bool hasOpenerPhase = false;
    bool hasLegacyOpenerPhase = false;
    bool hasHitMultiplier = false;

    std::size_t position = 1;
    while (position < outerEnd) {
        if (replay[position] != '{') return false;
        const std::size_t itemEnd = Util::Text::matching_brace(replay, position);
        if (itemEnd == std::string_view::npos || itemEnd > outerEnd) return false;
        const std::string_view item = replay.substr(position + 1, itemEnd - position - 1);
        const std::size_t separator = item.find(',');
        if (separator == std::string_view::npos) return false;
        const std::string_view name = item.substr(0, separator);
        const std::string_view value = item.substr(separator + 1);
        if (name == "TurnPieces") hasTurnPieces = true;
        if (name == "SpinBonuses") hasSpinBonuses = true;
        if (name == "RandomRules") hasRandomRules = true;
        if (name == "RandomizerVersion") hasRandomizerVersion = true;
        if (name == "OpenerPhase") hasOpenerPhase = true;
        if (name == "LegacyOpenerPhase") hasLegacyOpenerPhase = true;
        if (name == "HitMultiplier") hasHitMultiplier = true;
        if (name == "block_seed") {
            const auto seed = Util::Text::parse_integer<unsigned int>(value);
            if (!seed) return false;
            result.blockSeed = *seed;
        } else if (name == "garbage_seed") {
            const auto seed = Util::Text::parse_integer<unsigned int>(value);
            if (!seed) return false;
            result.garbageSeed = *seed;
        } else if (name == "undo_count") {
            const auto count = Util::Text::parse_integer<std::uint64_t>(value);
            if (!count) return false;
            result.undoCount = *count;
            hasUndoCount = true;
        } else if (name == "pause_count") {
            const auto count = Util::Text::parse_integer<std::uint64_t>(value);
            if (!count) return false;
            result.pauseCount = *count;
        } else if (name == "result_game_time" || name == "result_real_time") {
            const auto seconds=Util::Text::parse_finite_real(value);
            if (!seconds || *seconds<0) return false;
            const auto time=Runtime::Duration::from_seconds(*seconds);
            if (name=="result_game_time") result.resultGameTime=time;
            else result.resultRealTime=time;
        } else if (name == "undo_used" && !hasUndoCount) {
            // Old recordings only knew whether undo had been used.
            result.undoCount = value == "1" ? 1 : 0;
        } else if(name=="Map") {
            Runtime::MapDefinition map;
            if(!Runtime::decode_map_hex(value,map)) return false;
            config.initialMap=std::make_shared<const Runtime::MapDefinition>(std::move(map));
        } else if (const FieldCodec* codec = find_codec(name); codec && codec->decode) {
            codec->decode(config, value);
        }
        position = itemEnd + 1;
    }
    // The previous recorder omitted TurnPieces when its default was 1.
    // New recordings always write this field, including the new default 0.
    if (!hasTurnPieces && config.simulationPps > 0.0) config.turnPieces = 1.0;
    if (!hasSpinBonuses) config.legacySpinScoring = true;
    if(config.initialMap) Runtime::apply_map_config(config,*config.initialMap);
    if (!hasRandomRules) config.bagRandomizer = config.randomPieces ? Runtime::BagRandomizer::TotalMayhem : Runtime::BagRandomizer::SevenBag;
    else {
        if (!hasRandomizerVersion) config.legacyRandomizerRules = false;
        if (!config.legacyRandomizerRules) config.randomPieces = config.bagRandomizer != Runtime::BagRandomizer::SevenBag;
    }
    if (hasOpenerPhase && !hasLegacyOpenerPhase) config.garbage.legacyOpenerPhase = false;
    if (!hasHitMultiplier) {
        config.garbage.hitMultiplier=config.garbage.attackMultiplier;
        config.garbage.legacyIncomingRounding=true;
    }
    return position == outerEnd;
}

} // namespace Replay
