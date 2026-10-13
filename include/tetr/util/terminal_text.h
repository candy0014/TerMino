#ifndef TETR_UTIL_TERMINAL_TEXT_H
#define TETR_UTIL_TERMINAL_TEXT_H
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Util::TerminalText {
// Terminal cells, rather than UTF-8 bytes. Independent of the host locale.
std::size_t width(std::string_view text);
std::string clip(std::string_view text, std::size_t columns, std::string_view suffix = "...");
// A viewport measured in terminal cells; partial wide glyphs become blanks.
std::string slice(std::string_view text, std::size_t firstColumn, std::size_t columns);
std::vector<std::string> wrap(std::string_view text, std::size_t columns);
// Keep words together where possible; explicit newlines start a new row.
std::vector<std::string> wrap_words(std::string_view text, std::size_t columns);
}
#endif
