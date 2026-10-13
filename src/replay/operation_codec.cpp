#include "tetr/replay/operation_codec.h"
#include "tetr/compat/board_encoding.h"
#include "tetr/compat/piece_encoding.h"

#include "tetr/compat/legacy_text.h"
#include "tetr/util/text_codec.h"

#include <algorithm>
#include <utility>

namespace Replay {

namespace {

struct OperationName {
    OperationKind kind;
    std::string_view name;
};

constexpr OperationName kOperationNames[] = {
    {OperationKind::PutErase, "put0"},
    {OperationKind::PutDraw, "put1"},
    {OperationKind::Hold, "hold"},
    {OperationKind::AddGarbage, "add_garbage"},
    {OperationKind::AddSolidGarbage, "add_solid_garbage"},
    {OperationKind::AddBuffer, "add_buffer"},
    {OperationKind::OffsetBuffer, "offset_buffer"},
    {OperationKind::RaiseBuffer, "raise_buffer"},
    {OperationKind::QuickPlayWindup, "qp_windup"},
    {OperationKind::StartPlace, "start_place"},
    {OperationKind::Move, "move"},
    {OperationKind::EndMove, "end_move"},
    {OperationKind::EndMoveData, "end_move_data"},
    {OperationKind::OffsetData, "offset_data"},
    {OperationKind::ClearLine, "clear_line"},
    {OperationKind::EndClearLine, "end_clear_line"},
    {OperationKind::EndPlaceData, "end_place_data"},
    {OperationKind::EndPlace, "end_place"},
    {OperationKind::EndAll, "end_all"},
    {OperationKind::PauseGameTime, "pause_game_time"},
    {OperationKind::ResumeGameTime, "resume_game_time"}
};

} // namespace

OperationKind operation_kind_from_name(std::string_view name) {
    for (const auto& entry : kOperationNames) {
        if (entry.name == name) return entry.kind;
    }
    return OperationKind::Unknown;
}

std::string_view operation_name(OperationKind kind) {
    for (const auto& entry : kOperationNames) {
        if (entry.kind == kind) return entry.name;
    }
    return {};
}

namespace {


using FieldViews = std::vector<std::string_view>;

FieldViews split_record_fields(
    const std::string& replay,
    std::size_t open,
    std::size_t close) {
    const std::string_view record(replay.data() + open + 1, close - open - 1);
    return Util::Text::split(record, ',');
}

int int_field(const FieldViews& fields, std::size_t index, int fallback = 0) {
    // index is relative to payload arguments. fields[0] is the operation name
    // and fields[1] is the timestamp.
    const std::size_t fieldIndex = index + 2;
    return fieldIndex < fields.size() ? Compat::LegacyText::parse_int(fields[fieldIndex]) : fallback;
}

double double_field(const FieldViews& fields, std::size_t index, double fallback = 0.0) {
    const std::size_t fieldIndex = index + 2;
    return fieldIndex < fields.size() ? Compat::LegacyText::parse_real(fields[fieldIndex]) : fallback;
}

long long score_field(const FieldViews& fields, std::size_t index) {
    const std::size_t fieldIndex = index + 2;
    return fieldIndex < fields.size() ? Compat::LegacyText::parse_int(fields[fieldIndex]) : 0;
}

double quick_play_real_field(const FieldViews& fields, std::size_t index, double fallback = 0.0) {
    const std::size_t fieldIndex = index + 2;
    if (fieldIndex < fields.size()) {
        // Correct rounding (and exponent support) preserves XP just below an
        // upgrade boundary. Keep permissive parsing for historical malformed tokens.
        if (const auto value = Util::Text::parse_finite_real(fields[fieldIndex])) return *value;
    }
    return double_field(fields,index,fallback);
}

Piece::Pose pose_fields(const FieldViews& fields, std::size_t offset = 0) {
    return {{int_field(fields, offset), int_field(fields, offset + 1)},
            Compat::rotation_from_legacy(int_field(fields, offset + 2))};
}

OperationPayload decode_known_payload(OperationKind kind, const FieldViews& fields) {
    switch (kind) {
        case OperationKind::PutErase:
        case OperationKind::PutDraw:
        case OperationKind::Move:
            return PosePayload{pose_fields(fields)};
        case OperationKind::EndMoveData:
            return EndMoveDataPayload{
                Compat::rotation_record_from_legacy(int_field(fields, 0)),
                score_field(fields, 1),
                pose_fields(fields, 2)};
        case OperationKind::AddGarbage: {
            AddGarbagePayload payload{
                int_field(fields, 0),
                int_field(fields, 1) == 2,
                Compat::board_cell_from_legacy(int_field(fields, 2, 7))};
            if (fields.size()>=8) payload.quickPlayPolicy=AddGarbagePayload::QuickPlayPolicy{
                std::clamp(int_field(fields,3),1,10),std::clamp(int_field(fields,4),0,18),quick_play_real_field(fields,5)};
            return payload;
        }
        case OperationKind::AddSolidGarbage:
            return AddSolidGarbagePayload{
                int_field(fields, 0),
                Compat::board_cell_from_legacy(int_field(fields, 1, 8))};
        case OperationKind::AddBuffer: {
            AddBufferPayload payload{int_field(fields,0),int_field(fields,1)!=0,int_field(fields,2,-1),{}};
            if (fields.size()>5) {
                if (const auto fraction=Util::Text::parse_finite_real(fields[5]); fraction && *fraction>=0.0 && *fraction<1.0)
                    payload.incomingFraction=*fraction;
            }
            payload.deferDisplay=int_field(fields,4)!=0;
            if (fields.size()>7) payload.receivedAttack=std::max(0,int_field(fields,5));
            return payload;
        }
        case OperationKind::OffsetBuffer:
            return OffsetBufferPayload{int_field(fields, 0), int_field(fields, 1), int_field(fields, 2)};
        case OperationKind::RaiseBuffer:
            return RaiseBufferPayload{std::max(0,int_field(fields,0))};
        case OperationKind::QuickPlayWindup: {
            const double start=std::max(0.0,quick_play_real_field(fields,0));
            return QuickPlayWindupPayload{{start,std::max(start,quick_play_real_field(fields,1)),
                std::clamp(int_field(fields,2),1,4)}};
        }
        case OperationKind::OffsetData:
            return OffsetDataPayload{int_field(fields, 0)};
        case OperationKind::EndPlaceData: {
            QuickPlayPlacementPayload payload{
                quick_play_real_field(fields, 0), quick_play_real_field(fields, 1), int_field(fields, 2, 1),
                quick_play_real_field(fields, 3, 1.0), Runtime::Duration::from_seconds(quick_play_real_field(fields, 4, 1.0)),
                int_field(fields, 5) != 0};
            if (fields.size() > 8) {
                Runtime::QuickPlayProgress p;
                p.experience = quick_play_real_field(fields,6);
                p.pendingAltitude = quick_play_real_field(fields,7);
                p.decayProtection = quick_play_real_field(fields,8);
                p.upgradeFatigue = int_field(fields,9);
                p.upgraded = int_field(fields,10) != 0;
                p.cancellation = quick_play_real_field(fields,11);
                p.sinceRelief = quick_play_real_field(fields,12);
                p.piecesSinceRelief = int_field(fields,13);
                p.receiveProtection = int_field(fields,14);
                p.sinceReceived = quick_play_real_field(fields,15);
                p.floorTime = quick_play_real_field(fields,16);
                p.stallMultiplier = quick_play_real_field(fields,17,1.0);
                p.fatigueStage = int_field(fields,18);
                p.windupUntil = quick_play_real_field(fields,19);
                p.fatigueGarbageLines = int_field(fields,20);
                payload.progress = p;
            }
            return payload;
        }
        case OperationKind::Unknown:
            break;
        default:
            return std::monostate{};
    }
    return std::monostate{};
}

} // namespace

namespace {

Operation decode_operation_views(
    const FieldViews& fields,
    double replayTimeMultiplier) {
    Operation result;
    if (fields.empty()) return result;

    const std::string_view name = fields[0];
    result.kind = operation_kind_from_name(name);
    if (fields.size() > 1) {
        const double divisor = replayTimeMultiplier == 0.0 ? 1.0 : replayTimeMultiplier;
        result.timestamp = Runtime::Duration::from_seconds(
            Compat::LegacyText::parse_real(fields[1]) / divisor);
    }

    if (result.kind == OperationKind::Unknown) {
        UnknownPayload unknown;
        unknown.name.assign(name);
        if (fields.size() > 2) {
            unknown.arguments.reserve(fields.size() - 2);
            for (std::size_t index = 2; index < fields.size(); ++index)
                unknown.arguments.emplace_back(fields[index]);
        }
        result.payload = std::move(unknown);
    } else {
        result.payload = decode_known_payload(result.kind, fields);
    }
    return result;
}

} // namespace

Operation decode_operation_fields(
    const std::vector<std::string>& fields,
    double replayTimeMultiplier) {
    FieldViews views;
    views.reserve(fields.size());
    for (const std::string& field : fields) views.emplace_back(field);
    return decode_operation_views(views, replayTimeMultiplier);
}

std::queue<Operation> decode_operation_stream(
    const std::string& replay,
    std::size_t beginOffset,
    double replayTimeMultiplier) {
    std::queue<Operation> result;
    std::optional<Runtime::Duration> wallTime;
    std::size_t i = beginOffset;
    while (i < replay.size()) {
        const std::size_t open = replay.find('{', i);
        if (open == std::string::npos) break;
        const std::size_t close = Util::Text::matching_brace(replay, open);
        if (close == std::string::npos) break;
        const auto fields = split_record_fields(replay, open, close);
        if (fields.size() == 2 && fields[0] == "wall_time") {
            const auto time = Util::Text::parse_finite_real(fields[1]);
            if (time && *time >= 0.0)
                wallTime = Runtime::Duration::from_seconds(*time);
        } else {
            auto operation = decode_operation_views(fields, replayTimeMultiplier);
            if (wallTime) {
                operation.gameTimestamp = operation.timestamp;
                operation.timestamp = *wallTime;
                wallTime.reset();
            }
            result.push(std::move(operation));
        }
        i = close + 1;
    }
    return result;
}

std::string encode_operation_base(
    OperationKind operation,
    Runtime::Duration elapsed) {
    return encode_operation_base(std::string(operation_name(operation)), elapsed);
}

std::string encode_operation_base(
    const std::string& operation,
    Runtime::Duration elapsed) {
    return operation + "," + std::to_string(elapsed.seconds());
}

} // namespace Replay
