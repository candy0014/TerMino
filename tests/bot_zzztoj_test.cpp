#include "tetr/legacy/bot_adapter.h"
#include "tetr/runtime/game_context.h"

#include <stdexcept>
#include <vector>

namespace TetrTests::bot_zzztoj_test {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void configure(Runtime::GameContext& context) {
    context.config.autoplay.mode = Runtime::AutoplayMode::Zzztoj;
    context.config.autoplay.searchWidth = 10;
    context.config.handling.gravityInterval =
        Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(0.6));
    context.config.field.width = 10;
    context.config.field.height = 20;
    context.config.field.nextCount = 5;
    context.config.field.holdEnabled = true;
    context.config.field.antiGravity = false;
    context.config.rotationSystem = Runtime::RotationSystem::SrsPlus;
    context.config.randomPieces = false;
    context.config.quickPlay = Runtime::QuickPlayConfig{};
}

int place_and_clear(Runtime::Playfield& field, const Piece::Definition& piece,
                    Piece::Pose pose) {
    for (const auto& offset : piece.cells(pose.rotation)) {
        field.set_cell(pose.position.row + offset[0], pose.position.col + offset[1],
                       Runtime::BoardCell::piece(piece.color_id()));
    }

    int destination = 19;
    int cleared = 0;
    for (int source = 19; source >= -20; --source) {
        bool full = true;
        for (int col = 0; col < 10; ++col) {
            full = full && field.cell(source, col).occupied();
        }
        if (full) {
            ++cleared;
            continue;
        }
        if (destination != source) field.copy_row(destination, source);
        --destination;
    }
    for (; destination >= -20; --destination) {
        for (int col = 0; col < 10; ++col) {
            field.set_cell(destination, col, Runtime::BoardCell::empty());
        }
    }
    return cleared;
}

} // namespace

int run(int argc,char** argv) {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session, services);
    configure(context);

    Runtime::PlayerSessionState player;
    player.gameplay.playfield.width = 10;
    player.gameplay.playfield.visibleHeight = 20;
    player.gameplay.playfield.activeHeight = 20;
    player.gameplay.playfield.nextCount = 5;
    player.gameplay.playfield.holdEnabled = true;
    player.gameplay.playfield.field.initialize(10, 20);
    player.gameplay.playfield.field.clear();

    require(Bot::init(context), "bot_zzztoj must initialize for standard rules");
    const std::vector<Piece::Type> sequence{
        Piece::Type::from_index(5), // T
        Piece::Type::from_index(0), // I
        Piece::Type::from_index(3), // O
        Piece::Type::from_index(2), // L
        Piece::Type::from_index(4), // S
        Piece::Type::from_index(6)  // Z
    };
    const Piece::Pose start{{-2, 4}, Runtime::Rotation::from_index(0)};
    Piece::Pose answer = start;
    Runtime::RotationRecord answerRotation = Runtime::RotationRecord::none();
    const Piece::Type selected = Bot::getpos(
        context, player, player.gameplay.playfield.field, sequence, {}, start,
        Autoplay::HoldInput{true, Piece::Type::none()}, answer, answerRotation);

    require(selected.valid(), "empty-hold search must select a valid piece");
    require(selected == sequence.front() || selected == sequence[1],
            "empty-hold search selected a piece outside current/NEXT");
    const Piece::Definition piece = Piece::get_definition(selected);
    require(piece.can_place(answer, player.gameplay.playfield.field),
            "empty-hold answer is illegal");
    require(!piece.can_place(answer.translated(1, 0), player.gameplay.playfield.field),
            "empty-hold answer is not landed");

    const std::string script = Bot::getop(
        context, player.gameplay.playfield.field, piece, start, answer, answerRotation);
    require(!script.empty(), "movement script must not be empty");
    require(script.back() == 'H', "movement script must end in hard drop");

    context.config.field.holdEnabled = false;
    require(Bot::init(context), "no-hold rules must initialize");
    answer = start;
    answerRotation = Runtime::RotationRecord::none();
    const Piece::Type withoutHold = Bot::getpos(
        context, player, player.gameplay.playfield.field, sequence, {}, start,
        Autoplay::HoldInput{false, Piece::Type::none()}, answer, answerRotation);
    require(withoutHold == sequence.front(), "no-hold search changed the active piece");
    const Piece::Definition currentPiece = Piece::get_definition(withoutHold);
    require(currentPiece.can_place(answer, player.gameplay.playfield.field),
            "no-hold answer is illegal");
    require(!currentPiece.can_place(answer.translated(1, 0), player.gameplay.playfield.field),
            "no-hold answer is not landed");

    context.config.handling.gravityInterval = Runtime::GravityInterval::instant();
    require(!Bot::init(context), "20G must be reported unsupported by the upstream search");
    context.config.handling.gravityInterval=Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(.6));
    require(Bot::init(context), "ordinary gravity must initialize");
    answer = start;
    answerRotation = Runtime::RotationRecord::none();
    const Piece::Type at20g = Bot::getpos(
        context, player, player.gameplay.playfield.field, sequence, {}, start,
        Autoplay::HoldInput{false, Piece::Type::none()}, answer, answerRotation);
    require(at20g == sequence.front(), "20G search changed the active piece");
    const Piece::Definition piece20g = Piece::get_definition(at20g);
    require(piece20g.can_place(answer, player.gameplay.playfield.field),
            "20G answer is illegal");
    require(!piece20g.can_place(answer.translated(1, 0), player.gameplay.playfield.field),
            "20G answer is not landed");
    const std::string script20g = Bot::getop(
        context, player.gameplay.playfield.field, piece20g, start, answer, answerRotation);
    require(!script20g.empty() && script20g.back() == 'H',
            "20G answer is not executable by the host movement model");

    context.config.handling.gravityInterval =
        Runtime::GravityInterval::periodic(Runtime::Duration::from_seconds(0.6));

    // Exercise persistent tree updates and host/upstream board conversion over
    // enough placements to create and clear non-trivial terrain.
    require(Bot::init(context), "simulation rules must initialize");
    Runtime::Playfield simulation = player.gameplay.playfield.field;
    constexpr int bag[] = {5, 0, 3, 2, 4, 6, 1};
    int nonFallbackPlacements = 0;
    for (int turn = 0; turn < 35; ++turn) {
        std::vector<Piece::Type> preview;
        for (int offset = 0; offset < 6; ++offset) {
            preview.push_back(Piece::Type::from_index(bag[(turn + offset) % 7]));
        }
        Piece::Pose turnAnswer = start;
        Runtime::RotationRecord turnRotation = Runtime::RotationRecord::none();
        const Piece::Type turnSelected = Bot::getpos(
            context, player, simulation, preview, {}, start,
            Autoplay::HoldInput{false, Piece::Type::none()},
            turnAnswer, turnRotation);
        require(turnSelected == preview.front(), "simulation changed the active piece");
        const Piece::Definition turnPiece = Piece::get_definition(turnSelected);
        require(turnPiece.can_place(turnAnswer, simulation), "simulation answer is illegal");
        require(!turnPiece.can_place(turnAnswer.translated(1, 0), simulation),
                "simulation answer is not landed");
        if (turnAnswer.position.col != start.position.col ||
            turnAnswer.rotation != start.rotation) {
            ++nonFallbackPlacements;
        }
        const int cleared = place_and_clear(simulation, turnPiece, turnAnswer);
        player.gameplay.scoring.combo = cleared == 0
            ? 0
            : player.gameplay.scoring.combo + 1;
    }
    require(nonFallbackPlacements > 0, "simulation only used fallback placements");

    context.config.field.width = 11;
    require(!Bot::init(context), "11-column field must be rejected");
    context.config.field.width = 10;
    context.config.rotationSystem = Runtime::RotationSystem::Srs;
    require(!Bot::init(context), "plain SRS must be rejected");
    return 0;
}
} // namespace TetrTests::bot_zzztoj_test
