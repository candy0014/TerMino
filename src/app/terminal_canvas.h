#ifndef TETR_APP_TERMINAL_CANVAS_H
#define TETR_APP_TERMINAL_CANVAS_H
#include "tetr/render/terminal_renderer.h"
#include "tetr/frontend/ui_surface.h"
#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace App {
struct TerminalCanvas {
    using Color = std::array<int,3>;
    using Cell=Frontend::UiCell;
    using Frame = std::vector<std::vector<Cell>>;
    Frame shown, pending;
    bool initialized = false;
    std::optional<Render::TerminalSize> terminalSize;
    std::optional<int> columnsLimit;
    std::optional<int> rowsLimit;
    // Message-only settings updates reuse the layout of the displayed frame.
    std::optional<int> hintRow;
    int leftLimit=0;
    Frontend::UiMetadata metadata;

    void begin_frame() { pending.clear(); }
    void begin_update() { pending = shown; }
    void box(int top,int left,int width,int height,const std::string& title,Color heading,Color text,Color background) {
        width=std::max(0,width);height=std::max(0,height);
        std::string horizontal;for(int i=0;i<width+2;++i) horizontal+="─";
        write(top,left,"┌"+horizontal+"┐",width+4,heading,background);
        if(!title.empty()) {
            const auto caption=" "+Util::TerminalText::clip(title,std::max(0,width-2))+" ";
            write(top,left+2,caption,static_cast<int>(Util::TerminalText::width(caption)),heading,background);
        }
        for(int row=0;row<height;++row) write(top+1+row,left,"│ "+std::string(width,' ')+" │",width+4,text,background);
        write(top+1+height,left,"└"+horizontal+"┘",width+4,text,background);
    }
    void write(int row, int column, const std::string& text, int columns, Color color, Color background,
        int leftClip=0, std::string_view suffix="...") {
        leftClip=std::max(leftLimit,leftClip);
        if (columnsLimit) columns=std::min(columns,*columnsLimit-column);
        if (row < 0 || (rowsLimit && row>=*rowsLimit) || columns <= 0 || column + columns <= leftClip) return;
        if (pending.size() <= static_cast<std::size_t>(row)) pending.resize(row + 1);
        auto& cells = pending[row];
        if (cells.size() < static_cast<std::size_t>(column + columns)) cells.resize(column + columns);
        for (int i = std::max(leftClip,column); i < column + columns; ++i) cells[i] = {" ",color,background,false};
        int at = column;
        int previous = -1;
        const auto clipped = Util::TerminalText::clip(text,columns,suffix);
        for (std::size_t i = 0; i < clipped.size();) {
            const auto first = static_cast<unsigned char>(clipped[i]);
            const std::size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            const auto glyph = clipped.substr(i,bytes);
            const int span = static_cast<int>(Util::TerminalText::width(glyph));
            if (span == 0) {
                if (previous >= 0) cells[previous].glyph += glyph;
            } else {
                // Clip at the viewport edge without displaying half a wide
                // glyph or attaching combining marks to an off-screen glyph.
                if (at >= leftClip) {
                    cells[at] = {glyph,color,background,false};
                    if (span == 2) cells[at + 1] = {"",color,background,true};
                    previous = at;
                } else previous = -1;
                at += span;
            }
            i += bytes;
        }
    }
    void present(std::optional<std::pair<int,int>> cursor, bool obscuredColors) {
        if(Frontend::ui_surface_active()) {
            Frontend::UiFrame frame{pending,cursor,obscuredColors};frame.metadata=metadata;
            Frontend::present_ui(std::move(frame));
            shown=pending;initialized=true;return;
        }
        // Erasure must always use the normal background, including the first
        // frame after leaving an input prompt or another screen.
        Render::set_ui_color(Render::ColorRole::Text,obscuredColors);
        if (!initialized) {
            Render::clear_screen();
            initialized = true;
        }
        const Cell blank;
        const auto cell = [&](const Frame& frame, std::size_t row, std::size_t column) -> const Cell& {
            return row < frame.size() && column < frame[row].size() ? frame[row][column] : blank;
        };
        for (std::size_t row = 0; row < std::max(shown.size(),pending.size()); ++row) {
            const std::size_t columns = std::max(row < shown.size() ? shown[row].size() : 0,
                row < pending.size() ? pending[row].size() : 0);
            const auto changed = [&](std::size_t column) {
                return !(cell(shown,row,column) == cell(pending,row,column));
            };
            for (std::size_t column = 0; column < columns;) {
                if (!changed(column) || cell(pending,row,column).continuation) { ++column; continue; }
                const auto start = column;
                const auto color = cell(pending,row,column).color;
                const auto background = cell(pending,row,column).background;
                std::string text;
                do {
                    text += cell(pending,row,column).glyph;
                    ++column;
                } while (column < columns && cell(pending,row,column).color == color &&
                    cell(pending,row,column).background == background &&
                    (changed(column) || cell(pending,row,column).glyph == " " ||
                     cell(pending,row,column).continuation));
                Render::set_rgb(color[0],color[1],color[2],background[0],background[1],background[2]);
                Render::move_cursor(static_cast<int>(row),static_cast<int>(start));
                std::cout << text;
            }
        }
        shown = pending;
        Render::set_ui_color(Render::ColorRole::Text,obscuredColors);
        if (cursor) Render::move_cursor(cursor->first,
            columnsLimit ? std::clamp(cursor->second,leftLimit,*columnsLimit-1) : cursor->second);
        Render::flush();
    }
};

}
#endif
