#ifndef TETR_BOT_PIECE_ACCESS_H
#define TETR_BOT_PIECE_ACCESS_H

// Private BOT bridge for the few historical integer rotation/kick call sites.
// Piece geometry and ordinary collision stay owned by Piece::Definition.
// The bitboard helpers serve the private bitboard search boards used by BOT strategies.
#include "tetr/runtime/piece.h"
#include "tetr/bot/environment.h"

#include <vector>

namespace BotPiece {

inline Piece::Definition definition(int type) {
    return Piece::get_definition(Piece::Type::from_index(type));
}

inline Piece::Definition definition(int type,const Bot::Environment& environment) {
    return Piece::get_definition(type>=0 && type<7 ? environment.pieceTypes[type] : Piece::Type::from_index(type));
}

inline int logical_type(const Piece::Definition& piece,const Bot::Environment& environment) {
    for (int family=0;family<7;++family) if (environment.pieceTypes[family]==piece.type()) return family;
    return piece.type().index();
}

inline bool can_place(const Piece::Definition& piece, int row, int col, int rotation,
                      const Runtime::Playfield& field) {
    return piece.can_place(
        Piece::Pose{{row, col}, Runtime::Rotation::from_index(rotation)}, field);
}

inline bool try_rotate(const Piece::Definition& piece, int& row, int& col,
                       int fromRotation, int toRotation,
                       const Runtime::Playfield& field, int& kickIndex) {
    Piece::Pose pose{{row, col}, Runtime::Rotation::from_index(fromRotation)};
    Piece::KickIndex kick;
    if (!piece.try_rotate(pose, Runtime::Rotation::from_index(toRotation), field, kick)) {
        return false;
    }
    row = pose.position.row;
    col = pose.position.col;
    kickIndex = kick.index();
    return true;
}

inline int rotation_transition(int from, int to) {
    if(from==0&&to==1) return 0;
    if(from==1&&to==0) return 1;
    if(from==1&&to==2) return 2;
    if(from==2&&to==1) return 3;
    if(from==2&&to==3) return 4;
    if(from==3&&to==2) return 5;
    if(from==3&&to==0) return 6;
    if(from==0&&to==3) return 7;
    if(from==0&&to==2) return 8;
    if(from==2&&to==0) return 9;
    if(from==1&&to==3) return 10;
    if(from==3&&to==1) return 11;
    return 8;
}

template <class BitRows>
inline bool bitboard_can_place(const Piece::Definition& piece,
                               int row, int col, int rotation,
                               const BitRows& fieldBits,
                               int fieldWidth, int fieldHeight) {
    for (const auto& offset : piece.cells(Runtime::Rotation::from_index(rotation))) {
        const int testRow = row + offset[0];
        const int testCol = col + offset[1];
        if (testCol < 0 || testCol >= fieldWidth || testRow >= fieldHeight) return false;
        const int bitRow = fieldHeight - testRow - 1;
        if (bitRow >= 0 && bitRow < static_cast<int>(fieldBits.size()) &&
            ((fieldBits[bitRow] >> testCol) & 1)) return false;
    }
    return true;
}

template <class BitRows>
inline bool bitboard_try_rotate(const Piece::Definition& piece,
                                int& row, int& col, int fromRotation, int toRotation,
                                const BitRows& fieldBits,
                                int fieldWidth, int fieldHeight, int& kickIndex) {
    const int operation = rotation_transition(fromRotation, toRotation);
    const Piece::Metadata metadata = Piece::metadata(piece.type());
    if (!metadata.tetrioRotation && metadata.rotationSystem == Runtime::RotationSystem::Ars &&
        (metadata.familyIndex == 1 || metadata.familyIndex == 2 || metadata.familyIndex == 5)) {
        for (int testRow = row - 1; testRow <= row + 1; ++testRow) {
            for (int testCol = col - 1; testCol <= col + 1; ++testCol) {
                const int bitRow = fieldHeight - testRow - 1;
                const bool occupied = testRow >= fieldHeight || testCol < 0 || testCol >= fieldWidth ||
                    (bitRow >= 0 && bitRow < static_cast<int>(fieldBits.size()) &&
                     ((fieldBits[bitRow] >> testCol) & 1));
                if (occupied) {
                    if (testCol == col) return false;
                    testRow = row + 1;
                    break;
                }
            }
        }
    }
    int index = 0;
    for (const auto& kick : piece.kick_tests()[operation]) {
        if (bitboard_can_place(piece, row - kick[1], col + kick[0], toRotation,
                               fieldBits, fieldWidth, fieldHeight)) {
            if (index > 0 && metadata.tetrioRotation && metadata.rotationSystem == Runtime::RotationSystem::Ars &&
                (metadata.familyIndex == 1 || metadata.familyIndex == 2 || metadata.familyIndex == 5)) {
                const auto& offset = metadata.rotationOrigins[fromRotation];
                bool forbidden = false, found = false;
                for (int dy = -1; dy <= 1 && !found; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const int y = row - offset[0] + dy, x = col - offset[1] + dx;
                    const int bitRow = fieldHeight - y - 1;
                    if (y >= fieldHeight || x < 0 || x >= fieldWidth ||
                        (bitRow >= 0 && bitRow < static_cast<int>(fieldBits.size()) && ((fieldBits[bitRow] >> x) & 1))) {
                        forbidden = dx == 0;
                        found = true;
                        break;
                    }
                }
                if (forbidden) { ++index; continue; }
            }
            row -= kick[1];
            col += kick[0];
            kickIndex = index;
            return true;
        }
        ++index;
    }
    return false;
}

} // namespace BotPiece

#endif
