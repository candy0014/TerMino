#include "tetr/render/piece_renderer.h"
#include "tetr/runtime/game_context.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <iostream>
#include <sstream>

namespace {
struct Buffer:std::stringbuf {
    int flushes=0;
    int sync() override { ++flushes; return std::stringbuf::sync(); }
};
struct Capture {
    Buffer buffer;
    std::streambuf* old=std::cout.rdbuf(&buffer);
    ~Capture() { std::cout.rdbuf(old); }
};
}
int main() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context{session,services};
    auto& p=session.player;
    p.gameplay.playfield.width=context.config.field.width=10;
    p.gameplay.playfield.nextCount=context.config.field.nextCount=3;
    p.presentation.margin=20;
    p.presentation.blockMaxHeight=2;p.presentation.blockMaxWidth=3;
    auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    const auto eventTypes=Piece::april_fools_piece_types();
    for (std::size_t i:{std::size_t(0),std::size_t(1),std::size_t(5),std::size_t(11),eventTypes.size()-1})
        if (i<eventTypes.size()) types.push_back(eventTypes[i]);
    for (auto type:types) {
        const auto metadata=Piece::metadata(type);
        p.presentation.blockMaxHeight=std::max(p.presentation.blockMaxHeight,metadata.displayHeight);
        p.presentation.blockMaxWidth=std::max(p.presentation.blockMaxWidth,metadata.displayWidth);
    }
    // An opponent's advertised NEXT count is independent of the viewer's.
    p.pieces.nextPreview={types[0],types[1],types[2]};
    std::string standard;
    { Capture c; Render::draw_board_next(context,p,0); standard=c.buffer.str(); }
    assert(!standard.empty());
    context.config.field.nextCount=0;
    { Capture c; Render::draw_board_next(context,p,0); assert(c.buffer.str()==standard); }
    p.gameplay.playfield.nextCount=0;
    context.config.field.nextCount=3;
    { Capture c; Render::draw_board_next(context,p,0); assert(c.buffer.str().empty()); }
    p.gameplay.playfield.nextCount=3;
    for (int style:{0,1}) for (bool obscured:{false,true}) for (auto a:types) for (auto b:types) {
        context.config.display.squareStyle=style;context.config.display.obscuredColors=obscured;
        p.pieces.nextPreview={a,b,a};
        const auto previous=p.pieces.nextPreview;
        Test::TerminalScreen actual,expected;
        { Capture c; for (int i=0;i<3;++i) Render::draw_board_next(context,p,i); actual.feed(c.buffer.str()); }
        p.pieces.nextPreview={b,a,b};
        { Capture c;
          Render::draw_next_preview_transition(context,p,previous);
          assert(c.buffer.flushes<=2); // One update, no per-piece blank frame.
          actual.feed(c.buffer.str()); }
        { Capture c; for (int i=0;i<3;++i) Render::draw_board_next(context,p,i); expected.feed(c.buffer.str()); }
        for (int y=1;y<=50;++y) for (int x=39;x<=75;++x) {
            const auto got=actual.at(y,x),want=expected.at(y,x);
            assert(got.glyph==want.glyph && got.background==want.background);
            if (got.glyph!=" " && !got.glyph.empty()) assert(got.foreground==want.foreground);
        }
        { Capture c;
          Render::draw_next_preview_transition(context,p,p.pieces.nextPreview);
          assert(c.buffer.str().empty() && c.buffer.flushes==0); }
        const auto full=p.pieces.nextPreview;
        p.pieces.nextPreview.clear();
        { Capture c; Render::draw_next_preview_transition(context,p,full); actual.feed(c.buffer.str()); }
        for (int y=1;y<=50;++y) for (int x=39;x<=75;++x)
            assert(actual.at(y,x).glyph==" " || actual.at(y,x).glyph.empty());
    }
}
