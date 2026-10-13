#ifndef TETR_BOT_CHATGPT_ROOT_SNAPSHOT_H
#define TETR_BOT_CHATGPT_ROOT_SNAPSHOT_H

#include "evaluation.h"
#include "../native_host.h"
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Bot_chatgpt {
inline std::vector<std::string> split_root(const std::string& s, char separator) {
    std::vector<std::string> parts;
    std::istringstream stream(s);
    for (std::string part; std::getline(stream, part, separator);) parts.push_back(part);
    return parts;
}
inline double root_number(const std::string& s) {
    std::size_t used = 0;
    const auto value = std::stod(s, &used);
    if (used != s.size() || !std::isfinite(value)) throw std::runtime_error("Invalid root number");
    return value;
}
inline int root_integer(const std::string& s) {
    const double value = root_number(s);
    if (value != std::floor(value) || value < -1000000000 || value > 1000000000)
        throw std::runtime_error("Invalid root integer");
    return static_cast<int>(value);
}
struct RootSnapshot {
    Runtime::Playfield field;
    Evaluation::Status status;
    std::vector<VisiblePacket> packets;
    std::vector<int> next;
    Piece::Pose pose;
    int hold = -2;
    double step = 0;
    int liveSelected = 0, liveRow = 0, liveCol = 0, liveRotation = 0;

    static RootSnapshot parse(const std::string& line) {
        std::istringstream input(line);
        std::string word;
        input >> word;
        if (word != "TETR_BOT_DIAGNOSTIC") throw std::runtime_error("Not a root trace");
        std::map<std::string, std::string> values;
        while (input >> word) {
            const auto at = word.find('=');
            if (at == word.npos || !values.emplace(word.substr(0, at), word.substr(at + 1)).second)
                throw std::runtime_error("Malformed or duplicate root field");
        }
        const auto integer = [&](const char* key) { return root_integer(values.at(key)); };
        if (integer("trace_version") != 1) throw std::runtime_error("Unsupported root version");
        RootSnapshot r;
        r.field.initialize(10, 20); r.field.clear();
        const auto rows = split_root(values.at("board"), ',');
        if (rows.size() != 40) throw std::runtime_error("Truncated root board");
        for (int y = 0; y < 40; ++y) {
            const int bits = root_integer(rows[y]);
            if (bits < 0 || bits > 1023) throw std::runtime_error("Invalid board bits");
            for (int x = 0; x < 10; ++x)
                if (bits & (1 << x)) r.field.set_cell(19 - y, x, Runtime::BoardCell::garbage());
        }
        for (const auto& item : split_root(values.at("root_next"), ',')) {
            const int type = root_integer(item);
            if (type < 0 || type >= 7) throw std::runtime_error("Invalid visible piece");
            r.next.push_back(type);
        }
        if (r.next.empty()) throw std::runtime_error("Empty visible preview");
        r.hold = integer("root_hold");
        const int rotation = integer("root_rotation");
        if (r.hold < -2 || r.hold > 6 || rotation < 0 || rotation > 3)
            throw std::runtime_error("Invalid HOLD / rotation");
        r.pose = {{integer("root_row"), integer("root_col")}, Runtime::Rotation::from_index(rotation)};
        r.status.combo = integer("root_combo"); r.status.b2b = integer("root_b2b");
        r.status.garbageHeight = integer("root_garbage_height");
        r.status.placedPieces = integer("pieces");
        r.status.incoming = integer("incoming"); r.status.readyIncoming = integer("ready");
        const auto boolean = [&](const char* key) {
            const int value = integer(key);
            if (value != 0 && value != 1) throw std::runtime_error("Invalid root boolean");
            return value != 0;
        };
        r.status.previousCleared = boolean("root_cleared");
        r.status.openerDoubleCancelEnabled = boolean("opener_double");
        r.step = root_number(values.at("forecast_step"));
        if (values.at("root_packets") != "-") {
            for (const auto& item : split_root(values.at("root_packets"), ',')) {
                const auto parts = split_root(item, ':');
                if (parts.size() != 2) throw std::runtime_error("Invalid root packet");
                const int lines = root_integer(parts[0]);
                if (lines < 0) throw std::runtime_error("Negative packet");
                r.packets.push_back({lines, root_number(parts[1])});
            }
        }
        std::int64_t total = 0;
        for (const auto& p : r.packets) total += p.lines;
        if (r.status.incoming != total || r.status.readyIncoming < 0 || r.status.readyIncoming > total ||
            r.status.b2b < 0 || r.status.combo < 0 || r.status.garbageHeight < 0 ||
            r.status.garbageHeight > 40 || r.status.placedPieces < 0 || r.step < 0)
            throw std::runtime_error("Inconsistent root state");
        r.liveSelected = integer("selected"); r.liveRow = integer("row");
        r.liveCol = integer("col"); r.liveRotation = integer("rotation");
        return r;
    }
};
}
#endif
