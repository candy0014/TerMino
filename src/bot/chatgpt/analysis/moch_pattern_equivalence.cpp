// Independent runtime side of local-pattern encoding validation. Boards only:
// no Python counts, teacher labels or fitted weights are inputs.
#include "../local_pattern_model.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace B = Bot_chatgpt;

void row(m_tetris::TetrisMap& map, int y, unsigned bits) {
    map.row[y] = bits;
    if (bits) map.roof = y + 1;
    for (int x = 0; x < 10; ++x)
        if (bits & (1u << x)) { map.top[x] = y + 1; ++map.count; }
}

int main(int argc, char** argv) try {
    std::cout << std::setprecision(17);
    if (argc == 2 && std::string(argv[1]) == "--encoding") {
        for (int i = 0; i < 256; ++i)
            std::cout << (i ? " " : "") << unsigned(B::local_pattern_encoding.classOf[i]);
        std::cout << '\n';
        for (int i = 0; i < 144; ++i)
            std::cout << (i ? " " : "") << unsigned(B::local_pattern_encoding.canonicalCodes[i]);
        std::cout << '\n';
        return 0;
    }
    if (argc != 1) throw std::runtime_error("usage: moch_pattern_equivalence [--encoding]");
    B::LocalPatternWeights weights{};
    // Deterministic synthetic table, not a fitted model. All classes, including
    // class zero, are nonzero so accidental zero-class use cannot hide.
    for (int i = 0; i < 144; ++i) weights[i] = (i % 2 ? -1.0 : 1.0) * (i + 1) / 7.0;
    for (std::string line; std::getline(std::cin, line);) {
        if (line.empty()) continue;
        std::istringstream input(line);
        long long id;
        if (!(input >> id) || id < 0) throw std::runtime_error("Bad board ID");
        m_tetris::TetrisMap map(10, 40), reflected(10, 40);
        for (int y = 0; y < 40; ++y) {
            int bits;
            if (!(input >> bits) || bits < 0 || bits > 1023) throw std::runtime_error("Bad board row");
            row(map, y, unsigned(bits));
            unsigned reverse = 0;
            for (int x = 0; x < 10; ++x) if (bits & (1 << x)) reverse |= 1u << (9 - x);
            row(reflected, y, reverse);
        }
        std::string extra;
        if (input >> extra) throw std::runtime_error("Unexpected input");
        const auto features = B::local_pattern_counts(map);
        const auto mirror = B::local_pattern_counts(reflected);
        if (features != mirror) throw std::runtime_error("Runtime count mirror mismatch");
        std::cout << id;
        for (int value : features) std::cout << ' ' << value;
        std::cout << ' ' << B::local_pattern_value(map, weights) << '\n';
    }
} catch (const std::exception& error) {
    std::cerr << "moch_pattern_equivalence: " << error.what() << '\n';
    return 1;
}
