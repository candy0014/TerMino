#include "tetr/replay/tetrio_recorder.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/rotation_systems.h"
#include "tetr/rules/placement_resolution.h"
#include "tetr/rules/garbage_queue.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Replay {
namespace {
std::string piece(Piece::Type type) {
    return type.valid() ? std::string("\"") + "ijlostz"[Piece::metadata(type).familyIndex] + "\"" : "null";
}
std::string board(const Runtime::Playfield& field) {
    std::ostringstream out;
    out << '[';
    for (int r = -100; r < 20; ++r) {
        if (r != -100) out << ',';
        out << '[';
        for (int c = 0; c < 10; ++c) {
            if (c) out << ',';
            const auto cell = field.cell(r, c);
            if (cell.empty_cell()) out << "null";
            else if (cell.is_normal_garbage()) out << "\"gb\"";
            else if (cell.is_piece() && cell.color_id() >= 0 && cell.color_id() < 7)
                out << '"' << "ijlostz"[cell.color_id()] << '"';
            else throw std::logic_error("Unsupported cell in TETR.IO match");
        }
        out << ']';
    }
    return out.str() + ']';
}
const char* username(int side) { return side ? "zzztoj_botris" : "bot_chatgpt"; }
const char* id(int side) { return side ? "000000000000000000000002" : "000000000000000000000001"; }
}

TetrioRecorder* tetrio_recorder(Runtime::GameContext& context) {
    return context.replay_component() ? session(context).tetrioRecorder.get() : nullptr;
}
TetrioRecorder::TetrioRecorder(std::uint32_t seed, int side) : seed_(seed), side_(side) {
    events_.push_back({0, -1, "{\"frame\":0,\"type\":\"start\",\"data\":{}}"});
    ige(0, "target", "{\"targets\":[" + std::to_string(2 - side) + "]}");
    ige(0, "allow_targeting", "{\"value\":false}");
    for (const char* name : {"singles", "doubles", "triples", "quads", "pentas", "realtspins",
         "minitspins", "minitspinsingles", "tspinsingles", "minitspindoubles", "tspindoubles",
         "minitspintriples", "tspintriples", "minitspinquads", "tspinquads", "tspinpentas", "allclear"})
        clears_[name] = 0;
}
void TetrioRecorder::flush_inputs() {
    const auto& events = inputs_.events();
    for (; inputCount_ < events.size(); ++inputCount_) {
        const auto& event = events[inputCount_];
        std::ostringstream out;
        out << std::setprecision(12) << "{\"frame\":" << event.frame << ",\"type\":\""
            << (event.down ? "keydown" : "keyup") << "\",\"data\":{\"key\":\"" << event.key
            << "\",\"subframe\":" << event.subframe << "}}";
        events_.push_back({event.frame, 1, out.str()});
    }
}
void TetrioRecorder::hold() {
    inputs_.advance_to(pieceStartFrame_ + (side_ ? 8 : 0));
    inputs_.hold();
    ++holds_;
    flush_inputs();
}
void TetrioRecorder::begin_script(std::size_t length) {
    scriptLength_ = std::max<std::size_t>(1, length);
    commandIndex_ = 0;
}
void TetrioRecorder::movement(char command, int rows, int columns) {
    if (command == 'H') return; // the placement hook records exactly one lock
    const double position = (side_ ? 8.0 : 1.0) + 5.0 * double(commandIndex_++) / double(scriptLength_);
    inputs_.advance_to(pieceStartFrame_ + std::int64_t(position), position - std::floor(position));
    inputs_.movement(command, rows, columns);
    flush_inputs();
}
void TetrioRecorder::ige(std::int64_t frame, const std::string& type, const std::string& data) {
    const auto f = std::to_string(frame);
    events_.push_back({frame, 0, "{\"frame\":" + f + ",\"type\":\"ige\",\"data\":{\"id\":" +
        std::to_string(igeId_++) + ",\"frame\":" + f + ",\"type\":\"" + type + "\",\"data\":" + data + "}}"});
}
void TetrioRecorder::incoming(const Runtime::GameContext& context, int lines) {
    if (lines <= 0) return;
    received_ += lines;
    const auto frame = std::int64_t(std::llround(context.now().seconds() * 60)) + lock_phase()
        + pieceStartFrame_ - 15LL * locks_;
    const auto packet = std::to_string(++packetId_);
    const std::string data = "{\"type\":\"garbage\",\"amt\":" + std::to_string(lines) +
        ",\"gameid\":" + std::to_string(2 - side_) + ",\"frame\":" + std::to_string(frame) +
        ",\"cid\":" + packet + ",\"iid\":" + packet + ",\"ackiid\":0,\"x\":4,\"y\":119,\"size\":1}";
    ige(frame, "interaction", data);
    ige(frame, "interaction_confirm", data);
}
void TetrioRecorder::cancellation(int placedPieces, bool doubled) {
    if (doubled_ == doubled) return;
    doubled_ = doubled;
    // Native Opener Phase differs from v19. Reproduce its cancellation budget
    // with a supported custom option event; never change the live game's rules.
    (void)placedPieces;
    ige(pieceStartFrame_ + lock_phase(), "custom",
        "{\"type\":\"setoptions\",\"data\":{\"options\":{\"cancelmultiplier\":" +
        std::to_string(doubled ? 2 : 1) + "}}}");
}
void TetrioRecorder::lock(const Runtime::GameContext& context, const Rules::PlacementResult& placement) {
    const auto frame = pieceStartFrame_ + lock_phase();
    inputs_.advance_to(frame, 0.5);
    inputs_.movement('H', 0, 0);
    flush_inputs();
    ++locks_;
    const int lines = placement.clear.clearedLines;
    const double delay = context.config.field.entryDelay.seconds() +
        (lines ? context.config.field.lineClearDelay.seconds() : 0.0);
    // Keep the 4 PPS schedule unless ARE requires more time. Even a Hold
    // command must occur after the next piece has spawned in the target client.
    pieceStartFrame_ = std::max(pieceStartFrame_ + 15,
        frame + static_cast<std::int64_t>(std::ceil(delay * 60)) + 1 - (side_ ? 8 : 0));
    const bool spin = placement.spin.spin, mini = placement.attack.effectiveMini;
    const char* counts[] = {"", "singles", "doubles", "triples", "quads", "pentas"};
    if (spin) {
        ++spins_;
        // These two v19 fields count zero-line spins, not all spins.
        if (!lines) ++clears_[mini ? "minitspins" : "realtspins"];
    }
    if (lines > 0 && lines <= 5) ++clears_[(spin ? (mini ? "minitspin" : "tspin") : "") + std::string(counts[lines])];
    if (placement.clear.perfectClear && lines) ++clears_["allclear"];
    const auto& player = context.session.player;
    const auto& score = player.gameplay.scoring;
    const int sent = score.totalOutgoingAttack - previousSent_;
    previousSent_ = score.totalOutgoingAttack;
    if (sent > 0) {
        if (frame - lastAttackFrame_ >= 60) spike_ = spikeNomult_ = 0;
        spike_ += sent;
        spikeNomult_ += std::max(0, sent - placement.chain.b2bCharging);
        maxSpike_ = std::max(maxSpike_, spike_);
        maxSpikeNomult_ = std::max(maxSpikeNomult_, spikeNomult_);
        lastAttackFrame_ = frame;
    }
    std::ostringstream out;
    out << "{\"frame\":" << frame << ",\"pieces\":" << locks_ << ",\"lines\":" << score.totalLines
        << ",\"attack\":" << score.totalAttack << ",\"sent\":" << score.totalOutgoingAttack
        << ",\"b2b\":" << score.b2b << ",\"combo\":" << score.combo
        << ",\"pending\":" << Rules::pending_garbage(player.garbage.buffer)
        << ",\"hold\":" << piece(player.gameplay.hold.piece)
        << ",\"spin\":" << int(spin) << ",\"mini\":" << int(mini)
        << ",\"cleared_lines\":" << lines << ",\"native_score\":" << score.score
        << ",\"hole_rng\":" << player.garbage.tetrioRandom->random.state()
        << ",\"hole_column\":" << player.garbage.tetrioRandom->column
        << ",\"next\":[";
    for (std::size_t i = 0; i < player.pieces.nextPreview.size(); ++i)
        out << (i ? "," : "") << piece(player.pieces.nextPreview[i]);
    out << "],\"board\":" << board(player.gameplay.playfield.field) << '}';
    snapshots_.push_back(out.str());
}
std::int64_t TetrioRecorder::last_frame() const {
    std::int64_t result = 0;
    for (const auto& event : events_) result = std::max(result, event.frame);
    return result;
}
std::string TetrioRecorder::stats_json(const Runtime::GameContext& context, std::int64_t frames) const {
    const auto& s = context.session.player.gameplay.scoring;
    std::ostringstream out;
    out << "{\"lines\":" << s.totalLines << ",\"level_lines\":0,\"level_lines_needed\":1,\"inputs\":"
        << inputs_.events().size() / 2 << ",\"holds\":" << holds_ << ",\"score\":" << s.score
        << ",\"zenlevel\":1,\"zenprogress\":0,\"level\":1,\"combo\":" << s.combo
        << ",\"topcombo\":" << s.maxCombo << ",\"combopower\":0,\"btb\":" << s.b2b
        << ",\"topbtb\":" << s.maxB2B << ",\"btbpower\":0,\"tspins\":"
        << spins_ << ",\"piecesplaced\":" << locks_ << ",\"clears\":{";
    bool first = true;
    for (const auto& [key, count] : clears_) { out << (first ? "" : ",") << '"' << key << "\":" << count; first = false; }
    out << "},\"garbage\":{\"sent\":" << s.totalOutgoingAttack << ",\"sent_nomult\":" << s.totalOutgoingAttack
        << ",\"received\":" << received_ << ",\"attack\":" << s.totalAttack
        << ",\"cleared\":" << s.totalGarbageLines << ",\"maxspike\":" << maxSpike_
        << ",\"maxspike_nomult\":" << maxSpikeNomult_ << "},"
        << "\"kills\":0,\"finesse\":{\"combo\":0,\"faults\":0,\"perfectpieces\":0},\"finaltime\":" << frames * 1000.0 / 60 << '}';
    return out.str();
}
std::string TetrioRecorder::identity_json(int side) {
    return "\"id\":\"" + std::string(id(side)) + "\",\"username\":\"" + username(side) +
        "\",\"active\":true,\"naturalorder\":" + std::to_string(side + 1) + ",\"shadows\":[],\"shadowedBy\":[null,null]";
}
std::string TetrioRecorder::user_json(int side) {
    return "{\"id\":\"" + std::string(id(side)) + "\",\"username\":\"" + username(side) +
        "\",\"avatar_revision\":0,\"banner_revision\":0,\"flags\":0,\"country\":null}";
}
std::string TetrioRecorder::round_json(const Runtime::GameContext& context, bool won, std::int64_t endFrame) {
    const auto* rotationInfo = Runtime::rotation_system_info(context.config.rotationSystem);
    const auto& randomizerInfo = Runtime::bag_randomizer_info(Runtime::effective_bag_randomizer(context.config));
    std::stable_sort(events_.begin(), events_.end(), [](const Event& a, const Event& b) {
        return a.frame != b.frame ? a.frame < b.frame : a.priority < b.priority;
    });
    const auto& s = context.session.player.gameplay.scoring;
    const double seconds = std::max(1.0 / 60, endFrame / 60.0);
    std::ostringstream aggregate;
    aggregate << "\"apm\":" << s.totalAttack / seconds * 60 << ",\"pps\":" << locks_ / seconds
        << ",\"vsscore\":" << (s.totalAttack + s.totalGarbageLines) / seconds * 100;
    std::ostringstream out;
    out << '{' << identity_json(side_) << ",\"alive\":" << (won ? "true" : "false")
        << ",\"lifetime\":" << endFrame * 1000.0 / 60 << ",\"stats\":{" << aggregate.str()
        << ",\"garbagesent\":" << s.totalOutgoingAttack << ",\"garbagereceived\":" << received_
        << ",\"kills\":" << int(won) << ",\"rank\":" << (won ? 1 : 2) << ",\"altitude\":0,\"btb\":" << s.b2b
        << "},\"replay\":{\"frames\":" << endFrame << ",\"events\":[";
    bool first = true;
    for (const auto& event : events_) { out << (first ? "" : ",") << event.json; first = false; }
    out << ",{\"frame\":" << endFrame << ",\"type\":\"end\",\"data\":{\"reason\":\"" << (won ? "winner" : "topout") << "\"}}],"
        << "\"options\":{\"version\":19,\"seed\":" << seed_ << ",\"seed_random\":false,\"gameid\":" << side_ + 1
        << ",\"username\":\"" << username(side_) << "\",\"g\":0,\"gincrease\":0,\"boardbuffer\":100,"
        << "\"boardwidth\":10,\"boardheight\":20,\"bagtype\":\"" << randomizerInfo.tetrioName << "\",\"kickset\":\""
        << (rotationInfo ? rotationInfo->tetrioName : "SRS+") << "\",\"nextcount\":5,"
        << "\"are\":" << context.config.field.entryDelay.seconds()*60
        << ",\"lineclear_are\":" << (context.config.field.lineClearDelay + context.config.field.entryDelay).seconds()*60
        << ",\"infinite_movement\":" << (context.config.handling.infiniteMovement ? "true" : "false")
        << ",\"allclears\":true,\"clutch\":true,\"allow_harddrop\":true,"
        << "\"hasgarbage\":true,\"garbagespeed\":18,\"garbagephase\":0,\"garbageincrease\":0,\"garbagecap\":" << context.config.garbage.cap
        << ",\"garbagecapmax\":" << context.config.garbage.cap
        << ",\"garbagemultiplier\":" << context.config.garbage.sendMultiplier
        << ",\"receivemultiplier\":1,\"cancelmultiplier\":1,\"garbageholesize\":1,"
        << "\"garbageentry\":\"instant\",\"garbageblocking\":\"combo blocking\",\"garbagecapincrease\":0,"
        << "\"messiness_change\":1,\"messiness_inner\":" << context.config.garbage.singleMessiness
        << ",\"messiness_nosame\":" << (context.config.garbage.avoidSameColumn ? "true" : "false")
        << ",\"messiness_center\":false,\"messiness_timeout\":0,"
        << "\"passthrough\":\"full\",\"openerphase\":" << (context.config.garbage.legacyOpenerPhase ? 0 : context.config.garbage.openerPhase)
        << ",\"spinbonuses\":\""
        << Runtime::spin_bonuses_info(context.config.spinBonuses).tetrioName << "\",\"allow180\":true,"
        << "\"combotable\":\"multiplier\",\"roundmode\":\"down\",\"b2bchaining\":false,\"b2bextras\":false,"
        << "\"b2bcharging\":true,\"b2bcharge_at\":4,\"b2bcharge_base\":3,\"allclear_garbage\":5,\"allclear_b2b\":1,\"allclear_charges\":false,"
        << "\"allclear_b2b_sends\":true,\"allclear_b2b_dupes\":false,\"nolockout\":true,\"garbagespecialbonus\":true,"
        << "\"slot_counter1\":\"stopwatch\",\"slot_counter2\":\"attack\",\"slot_counter3\":\"pieces\",\"slot_bar1\":\"impending\","
        << "\"mission_type\":\"mission_versus\",\"mission\":\"Native FT7 - 4 PPS\",\"display_username\":true,\"song\":\"none\","
        << "\"handling\":{\"arr\":0,\"das\":4,\"dcd\":0,\"sdf\":20,\"safelock\":false,\"cancel\":false,\"may20g\":false,\"irs\":\"off\",\"ihs\":\"off\"}},"
        << "\"results\":{\"aggregatestats\":{" << aggregate.str() << "},\"stats\":" << stats_json(context, endFrame)
        << ",\"gameoverreason\":\"" << (won ? "winner" : "topout") << "\"}}}";
    return out.str();
}
std::string TetrioRecorder::audit_json() const {
    std::string out = "[";
    for (const auto& snapshot : snapshots_) { if (out.size() > 1) out += ','; out += snapshot; }
    return out + ']';
}
} // namespace Replay
