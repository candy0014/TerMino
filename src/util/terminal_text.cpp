#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <cstdint>

namespace Util::TerminalText {
namespace {
struct Character { std::uint32_t code; std::size_t bytes; };
Character next(std::string_view text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) return {first, 1};
    const int count = first >= 0xf0 && first <= 0xf4 ? 4 :
        first >= 0xe0 && first <= 0xef ? 3 : first >= 0xc2 && first <= 0xdf ? 2 : 0;
    if (!count || offset + count > text.size()) return {0xfffd, 1};
    std::uint32_t code = first & ((1u << (7 - count)) - 1);
    for (int i = 1; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + i]);
        if ((byte & 0xc0) != 0x80) return {0xfffd, 1};
        code = (code << 6) | (byte & 0x3f);
    }
    if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800) ||
        (count == 4 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        return {0xfffd, 1};
    return {code, static_cast<std::size_t>(count)};
}
std::size_t cells(std::uint32_t code) {
    if ((code >= 0x300 && code <= 0x36f) || (code >= 0x1ab0 && code <= 0x1aff) ||
        (code >= 0x1dc0 && code <= 0x1dff) || (code >= 0x20d0 && code <= 0x20ff) ||
        (code >= 0xfe00 && code <= 0xfe0f) || (code >= 0xfe20 && code <= 0xfe2f) ||
        (code >= 0xe0100 && code <= 0xe01ef) || code == 0x200d) return 0;
    // Emoji arrows use a full-width terminal cell.
    if (code == 0x2b05 || (code >= 0x1100 && code <= 0x115f) || code == 0x2329 || code == 0x232a ||
        (code >= 0x2e80 && code <= 0xa4cf && code != 0x303f) ||
        (code >= 0xac00 && code <= 0xd7a3) || (code >= 0xf900 && code <= 0xfaff) ||
        (code >= 0xfe10 && code <= 0xfe19) || (code >= 0xfe30 && code <= 0xfe6f) ||
        (code >= 0xff01 && code <= 0xff60) || (code >= 0xffe0 && code <= 0xffe6) ||
        (code >= 0x1f300 && code <= 0x1faff) || (code >= 0x20000 && code <= 0x3fffd)) return 2;
    return 1;
}
void append(std::string& output, std::string_view text, std::size_t offset, Character ch) {
    if (ch.code < 0x20 || (ch.code >= 0x7f && ch.code < 0xa0)) output += '?';
    else if (ch.code == 0xfffd && ch.bytes == 1) output += "\xef\xbf\xbd";
    else output.append(text.substr(offset, ch.bytes));
}
std::string prefix(std::string_view text, std::size_t columns) {
    std::string output;
    std::size_t used = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto ch = next(text, i);
        if (used + cells(ch.code) > columns) break;
        append(output, text, i, ch);
        used += cells(ch.code); i += ch.bytes;
    }
    return output;
}
}
std::size_t width(std::string_view text) {
    std::size_t result = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto ch = next(text, i);
        result += cells(ch.code); i += ch.bytes;
    }
    return result;
}
std::string clip(std::string_view text, std::size_t columns, std::string_view suffix) {
    if (width(text) <= columns) return prefix(text, columns);
    const auto tail = prefix(suffix, columns);
    return prefix(text, columns - width(tail)) + tail;
}
std::string slice(std::string_view text, std::size_t firstColumn, std::size_t columns) {
    std::string output;
    const auto end=firstColumn+columns;
    std::size_t at=0;
    bool included=false;
    for (std::size_t i=0;i<text.size();) {
        const auto ch=next(text,i);
        const auto span=cells(ch.code);
        if (!span) {
            if (included) append(output,text,i,ch);
        } else {
            if (at>=end) break;
            included=at>=firstColumn && at+span<=end;
            if (included) append(output,text,i,ch);
            else if (at+span>firstColumn)
                output.append(std::min(at+span,end)-std::max(at,firstColumn),' ');
            at+=span;
        }
        i+=ch.bytes;
    }
    return output;
}
std::vector<std::string> wrap(std::string_view text, std::size_t columns) {
    std::vector<std::string> rows(1);
    if (columns == 0) return rows;
    std::size_t used = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto ch = next(text, i);
        if (used && used + cells(ch.code) > columns) { rows.emplace_back(); used = 0; }
        append(rows.back(), text, i, ch);
        used += cells(ch.code); i += ch.bytes;
    }
    return rows;
}
std::vector<std::string> wrap_words(std::string_view text, std::size_t columns) {
    std::vector<std::string> rows;
    if (!columns) return {""};
    do {
        const auto newline=text.find('\n');
        auto remaining=text.substr(0,newline);
        while (width(remaining)>columns) {
            auto part=prefix(remaining,columns);
            if (part.empty()) {
                remaining.remove_prefix(next(remaining,0).bytes);
                continue;
            }
            const auto space=part.find_last_of(' ');
            if (space!=std::string::npos && space>0) part.resize(space);
            rows.push_back(part);
            remaining.remove_prefix(part.size());
            while (!remaining.empty() && remaining.front()==' ') remaining.remove_prefix(1);
        }
        rows.push_back(prefix(remaining,columns));
        if (newline==std::string_view::npos) break;
        text.remove_prefix(newline+1);
    } while (true);
    return rows;
}
}
