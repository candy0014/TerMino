#ifndef TETR_RENDER_TERMINAL_VIEWPORT_H
#define TETR_RENDER_TERMINAL_VIEWPORT_H
#include "tetr/render/terminal_renderer.h"
#include <array>
#include <streambuf>
#include <sstream>
#include <string>

namespace Render {
// Clips positioned output, rather than clamping off-screen cursor positions
// onto visible cells. UTF-8 glyphs and terminal colors remain intact.
class TerminalViewportBuffer final : public std::streambuf {
public:
    TerminalViewportBuffer(std::streambuf* target, TerminalSize size) : target_(target), size_(size) {}
    void move(int row,int column);
private:
    int_type overflow(int_type value) override;
    std::streamsize xsputn(const char* text,std::streamsize count) override;
    int sync() override { return target_->pubsync(); }
    void consume(char value);
    void glyph();
    void escape();
    void emit(std::string_view text,int width);
    std::streambuf* target_;
    TerminalSize size_;
    int row_=0,column_=0,physicalRow_=-1,physicalColumn_=-1;
    bool lastVisible_=false;
    std::array<char,4> utf8_{};
    int utf8Count_=0,utf8Expected_=0;
    std::string escape_;
};
struct TerminalViewportState {
    std::streambuf* previous;
    std::unique_ptr<std::stringbuf> pending;
    TerminalViewportBuffer buffer;
    TerminalViewportState(std::streambuf* target,TerminalSize size,bool batch=false)
        : previous(target),pending(batch?std::make_unique<std::stringbuf>():nullptr),buffer(pending?pending.get():target,size) {}
    void commit() {
        buffer.pubsync();
        if(pending) {const auto text=pending->str();previous->sputn(text.data(),text.size());previous->pubsync();}
    }
};
}
#endif
