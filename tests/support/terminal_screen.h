#ifndef TETR_TEST_TERMINAL_SCREEN_H
#define TETR_TEST_TERMINAL_SCREEN_H

#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Test {
// Interpret terminal output independently of the settings renderer so updates
// can be checked against a freshly drawn screen, including UTF-8 and colors.
class TerminalScreen {
public:
    using Color = std::array<int,3>;
    struct Cell {
        std::string glyph = " ";
        Color foreground{{255,255,255}}, background{{12,12,12}};
        bool continuation = false;
    };
    void feed(const std::string& output) {
        for (std::size_t i = 0; i < output.size();) {
            if (output[i] == '\x1b' && i + 1 < output.size() && output[i + 1] == '[') {
                const auto first = i + 2;
                i = first;
                while (i < output.size() && (output[i] < '@' || output[i] > '~')) ++i;
                if (i == output.size()) break;
                const auto params = output.substr(first,i-first);
                const char command = output[i++];
                if (command == 'J' && params == "2") { cells.clear(); continue; }
                if (command != 'H' && command != 'm') continue;
                std::vector<int> values;
                for (std::size_t start = 0; start <= params.size();) {
                    const auto end = params.find(';',start);
                    const auto part = params.substr(start,end-start);
                    values.push_back(part.empty() ? 0 : std::stoi(part));
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                if (command == 'H') {
                    row = std::max(1,values[0]);
                    column = values.size() > 1 ? std::max(1,values[1]) : 1;
                } else {
                    for (std::size_t p = 0; p < values.size(); ++p) {
                        if ((values[p] == 38 || values[p] == 48) && p + 4 < values.size() && values[p+1] == 2) {
                            auto& color = values[p] == 38 ? foreground : background;
                            color = {{values[p+2],values[p+3],values[p+4]}};
                            p += 4;
                        }
                    }
                }
                continue;
            }
            const auto first = static_cast<unsigned char>(output[i]);
            if (first == '\n') { ++row; column = 1; ++i; continue; }
            if (first == '\r') { column = 1; ++i; continue; }
            if (first < ' ') { ++i; continue; }
            const std::size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            const auto glyph = output.substr(i,bytes);
            const int width = static_cast<int>(Util::TerminalText::width(glyph));
            if (width == 0) {
                auto previous = column - 1;
                if (at(row,previous).continuation) --previous;
                cells[{row,previous}].glyph += glyph;
            } else {
                std::vector<int> erased;
                for (int offset=0; offset<width; ++offset) {
                    const auto old=at(row,column+offset);
                    if (old.continuation) erased.push_back(column+offset-1);
                    if (Util::TerminalText::width(old.glyph)==2) erased.push_back(column+offset+1);
                }
                for (const auto position : erased) cells[{row,position}] = {};
                cells[{row,column}] = {glyph,foreground,background,false};
                if (width == 2) cells[{row,column+1}] = {"",foreground,background,true};
                column += width;
            }
            i += bytes;
        }
    }
    Cell at(int row, int column) const {
        const auto found = cells.find({row,column});
        return found == cells.end() ? Cell{} : found->second;
    }
    std::pair<int,int> position_of(const std::string& text) const {
        for (int r = 1; r <= rows(); ++r) {
            const auto line = row_text(r);
            const auto at = line.find(text);
            if (at != std::string::npos)
                return {r,static_cast<int>(Util::TerminalText::width(line.substr(0,at))) + 1};
        }
        return {0,0};
    }
    std::string text() const {
        std::string result;
        for (int r = 1; r <= rows(); ++r) result += row_text(r) + '\n';
        return result;
    }
    int rows() const { return cells.empty() ? 0 : cells.rbegin()->first.first; }
    int columns() const {
        int result = 0;
        for (const auto& [position,cell] : cells) result = std::max(result,position.second);
        return result;
    }
    Color foreground{{255,255,255}}, background{{12,12,12}};
    int row = 1, column = 1;
private:
    std::string row_text(int row) const {
        std::string result;
        const int count = columns();
        for (int column = 1; column <= count; ++column) result += at(row,column).glyph;
        return result;
    }
    std::map<std::pair<int,int>,Cell> cells;
};
} // namespace Test
#endif
