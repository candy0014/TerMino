#include "tetr/compat/legacy_encoding.h"
#include "tetr/input/services.h"
#include "tetr/render/board_renderer.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/runtime_services.h"
#include "tetr/runtime/session_state.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/map_definition.h"
#include "support/terminal_screen.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <memory>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __linux__
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
struct Capture {
    std::ostringstream stream;
    std::streambuf* old=nullptr;
    Capture():old(std::cout.rdbuf(stream.rdbuf())){}
    ~Capture(){std::cout.rdbuf(old);}
    std::string str() const{return stream.str();}
};
std::string capture_square(int version,Render::SquareFill fill){Capture c;Render::draw_square(version,fill);return c.str();}

#ifdef __linux__
struct TerminalInputPty {
    int savedInput=::dup(STDIN_FILENO);
    int master=::posix_openpt(O_RDWR|O_NOCTTY|O_CLOEXEC);
    int slave=-1;
    int savedOutput=-1;

    explicit TerminalInputPty(bool redirectOutput=false) {
        assert(savedInput>=0 && master>=0);
        assert(::grantpt(master)==0 && ::unlockpt(master)==0);
        slave=::open(::ptsname(master),O_RDWR|O_NOCTTY|O_CLOEXEC);
        assert(slave>=0 && ::dup2(slave,STDIN_FILENO)>=0);
        if (redirectOutput) {
            std::cout.flush();
            savedOutput=::dup(STDOUT_FILENO);
            assert(savedOutput>=0 && ::dup2(slave,STDOUT_FILENO)>=0);
        }
    }
    ~TerminalInputPty() {
        assert(::dup2(savedInput,STDIN_FILENO)>=0);
        if (savedOutput>=0) {
            std::cout.flush();
            assert(::dup2(savedOutput,STDOUT_FILENO)>=0);
            ::close(savedOutput);
        }
        ::close(savedInput); ::close(slave); ::close(master);
    }
    termios attributes() const {
        termios result{};
        assert(::tcgetattr(slave,&result)==0);
        return result;
    }
    bool readable(int timeout=0) const {
        pollfd input{slave,POLLIN,0};
        const int ready=::poll(&input,1,timeout);
        assert(ready>=0);
        return ready>0 && (input.revents&POLLIN);
    }
    void queue(const std::string& text) const {
        assert(::write(master,text.data(),text.size())==static_cast<ssize_t>(text.size()));
        // Wait until the line discipline has received the bytes before exiting.
        assert(readable(1000));
    }
    std::string read() const {
        assert(readable(1000));
        char buffer[128];
        const auto count=::read(slave,buffer,sizeof(buffer));
        assert(count>0);
        return {buffer,static_cast<std::size_t>(count)};
    }
    void expect_attributes(const termios& expected) const {
        const auto actual=attributes();
        assert(actual.c_iflag==expected.c_iflag && actual.c_oflag==expected.c_oflag);
        assert(actual.c_cflag==expected.c_cflag && actual.c_lflag==expected.c_lflag);
        for (int i=0;i<NCCS;++i) assert(actual.c_cc[i]==expected.c_cc[i]);
        assert(::cfgetispeed(&actual)==::cfgetispeed(&expected));
        assert(::cfgetospeed(&actual)==::cfgetospeed(&expected));
    }
};

void test_terminal_input_cleanup() {
    for (bool canonical:{false,true}) for (bool unwind:{false,true}) {
        TerminalInputPty terminal;
        auto original=terminal.attributes();
        if (canonical) original.c_lflag|=ICANON|ECHO;
        else original.c_lflag&=~static_cast<tcflag_t>(ICANON|ECHO);
        assert(::tcsetattr(terminal.slave,TCSANOW,&original)==0);
        Capture capture;
        try {
            Render::TerminalSession session;
            assert(!(terminal.attributes().c_lflag&(ICANON|ECHO)));
            // Includes Enter, arrow sequences, Escape and an unfinished line.
            terminal.queue("wasd\n\x1b[A\x1b[D\x1bpartial-command");
            if (unwind) throw 1;
        } catch (int) {}
        terminal.expect_attributes(original);
        assert(!terminal.readable());
        // A later Enter must not execute a partial command left by the game.
        terminal.queue("\n");
        assert(terminal.read()=="\n");
        terminal.queue("fresh-shell-input\n");
        assert(terminal.read()=="fresh-shell-input\n");
    }
}

void test_terminal_text_input_cleanup() {
    TerminalInputPty terminal;
    const auto original=terminal.attributes();
    Capture capture;
    Render::TerminalSession session;
    terminal.queue("game-keys\npartial");
    session.begin_text_input();
    terminal.expect_attributes(original);
    assert(!terminal.readable());
    terminal.queue("fresh-text\n");
    assert(terminal.read()=="fresh-text\n");
    terminal.queue("leftover-text\npartial");
    session.end_text_input();
    assert(!(terminal.attributes().c_lflag&(ICANON|ECHO)));
    assert(!terminal.readable());
}

void test_terminal_signal_input_cleanup() {
    for (int signal:{SIGINT,SIGTERM,SIGHUP}) {
        TerminalInputPty terminal;
        const auto original=terminal.attributes();
        int ready[2];
        assert(::pipe(ready)==0);
        std::cout.flush();
        const pid_t child=::fork();
        assert(child>=0);
        if (child==0) {
            ::close(ready[0]);
            // Bound the test even if a signal handler stops exiting correctly.
            ::alarm(5);
            assert(::dup2(terminal.slave,STDOUT_FILENO)>=0);
            Render::TerminalSession session;
            assert(::write(ready[1],"r",1)==1);
            for (;;) ::pause();
        }
        ::close(ready[1]);
        pollfd notification{ready[0],POLLIN,0};
        assert(::poll(&notification,1,1000)==1);
        char byte;
        assert(::read(ready[0],&byte,1)==1);
        ::close(ready[0]);
        terminal.queue("wasd\n\x1b[A\x1bpartial-command");
        assert(::kill(child,signal)==0);
        int status=0;
        assert(::waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==128+signal);
        terminal.expect_attributes(original);
        assert(!terminal.readable());
        terminal.queue("\n");
        assert(terminal.read()=="\n");
    }
}

void test_terminal_focus_input() {
    TerminalInputPty terminal(true);
    const auto original=terminal.attributes();
    Input::Services input;
    auto& focus=input.keyboard.focus_state();
    Capture capture;
    {
        Render::TerminalSession session(&input);
        assert(input.terminal_input_active());
        assert(capture.str().find("\x1b[?1004h")!=std::string::npos);
        focus.update(30,true);
        assert(input.key_down(30));
        // Focus reports can be split across reads and ordinary keyboard bytes.
        terminal.queue("game-keys\x1b[");
        assert(input.accepting_input());
        terminal.queue("Oignored-background-keys");
        assert(!input.accepting_input() && !input.key_down(30));
        focus.update(31,true); focus.update(31,false);
        terminal.queue("\x1b[I");
        assert(input.accepting_input() && !input.key_down(30) && !input.key_down(31));
        focus.update(30,false); focus.update(30,true);
        assert(input.key_down(30));
        session.begin_text_input();
        terminal.queue("old-text\x1b[Odiscarded\x1b[I新文本\x1b[D");
        std::string text;
        for (int character;(character=input.read_terminal_character())>=0;)
            text+=static_cast<char>(character);
        assert(text=="新文本\x1b[D");
        terminal.queue("\x1b[O");
        assert(!input.accepting_input());
        input.set_continue_input_unfocused(true);
        focus.update(32,true);
        assert(input.key_down(32));
        terminal.queue("continued-text");
        text.clear();
        for (int character;(character=input.read_terminal_character())>=0;)
            text+=static_cast<char>(character);
        assert(text=="continued-text");
        input.set_continue_input_unfocused(false);
        assert(!input.key_down(32));
        terminal.queue("\x1b[I");
        assert(input.accepting_input() && !input.key_down(32));
        session.end_text_input();
        terminal.queue("leftover-game-keys\npartial-command");
    }
    assert(!input.terminal_input_active());
    assert(capture.str().find("\x1b[?1004l")!=std::string::npos);
    terminal.expect_attributes(original);
    terminal.queue("\n");
    assert(terminal.read()=="\n");
}
#endif

void test_terminal_size() {
#ifdef __linux__
    // A real PTY exercises the OS query, including a resize and redirection.
    std::cout.flush();
    const int saved=::dup(STDOUT_FILENO);
    const int terminal=::posix_openpt(O_RDWR|O_NOCTTY);
    assert(saved>=0 && terminal>=0);
    assert(::dup2(terminal,STDOUT_FILENO)>=0);
    for (const auto expected:{Render::TerminalSize{120,60},Render::TerminalSize{80,40}}) {
        winsize size{};
        size.ws_col=expected.columns;
        size.ws_row=expected.rows;
        assert(::ioctl(terminal,TIOCSWINSZ,&size)==0);
        assert(Render::terminal_size()==expected);
    }
    int pipe[2];
    assert(::pipe(pipe)==0 && ::dup2(pipe[1],STDOUT_FILENO)>=0);
    assert(!Render::terminal_size());
    assert(::dup2(saved,STDOUT_FILENO)>=0);
    ::close(saved);
    ::close(terminal);
    ::close(pipe[0]);
    ::close(pipe[1]);
#endif
}

void test_short_next_preview() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    auto& player=session.player;
    player.gameplay.playfield.nextCount=5;
    context.config.field.nextCount=5;
    const auto draw=[&](int index) {
        Capture c;
        Render::draw_board_next(context,player,index);
        return c.str();
    };
    // A remote frame can contain fewer NEXT entries than the lobby advertised.
    assert(draw(0).empty());
    player.pieces.nextPreview={Piece::Type::from_index(0)};
    assert(!draw(0).empty());
    assert(draw(-1).empty());
    assert(draw(1).empty());
    assert(draw(5).empty());
    player.pieces.nextPreview={Piece::Type::none()};
    assert(draw(0).empty());
}

void test_viewport_clips_without_wrapping() {
#ifdef _WIN32
    // Capture redirects cout, but the Win32 standard handle still describes
    // the real console. Hide it so this test uses the supplied 10x4 viewport
    // rather than having OutputGuard replace it with the live console size.
    struct CapturedOutput {
        HANDLE original=GetStdHandle(STD_OUTPUT_HANDLE);
        CapturedOutput() { assert(SetStdHandle(STD_OUTPUT_HANDLE,INVALID_HANDLE_VALUE)); }
        ~CapturedOutput() { assert(SetStdHandle(STD_OUTPUT_HANDLE,original)); }
    } redirected;
#endif
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    auto& p=session.player.presentation;
    p.dynamicLayout=true; p.terminalColumns=10; p.terminalRows=4;
    Capture capture;
    {
        Render::OutputGuard output(context);
        Render::set_cell_color(0,false);
        Render::move_cursor(1,-2); std::cout<<"ABCDE";
        Render::move_cursor(-1,0); std::cout<<"HIDDEN";
        Render::move_cursor(2,8); std::cout<<"ABCD";
        Render::move_cursor(4,0); std::cout<<"HIDDEN";
        Render::move_cursor(3,9); std::cout<<"末"; // Partial wide glyph becomes a blank.
        Render::move_cursor(0,-1); std::cout<<"方Z"; // Left half is outside.
        Render::move_cursor(0,4); std::cout<<"x\bY\n";
    }
    Test::TerminalScreen screen;
    screen.feed(capture.str());
    assert(screen.rows()<=4 && screen.columns()<=10);
    assert(screen.at(2,1).glyph=="C" && screen.at(2,3).glyph=="E");
    assert(screen.at(3,9).glyph=="A" && screen.at(3,10).glyph=="B");
    assert(screen.at(4,10).glyph==" " && screen.at(1,1).glyph==" " && screen.at(1,2).glyph=="Z");
    assert(screen.at(1,5).glyph=="Y");
    assert((screen.at(2,1).foreground==Test::TerminalScreen::Color{{54,235,168}}));
    assert(capture.str().find("HIDDEN")==std::string::npos && capture.str().find('\n')==std::string::npos);
    // Starting a seek inside a clipped update must not retain a dead buffer.
    { Render::OutputGuard output(context); Render::begin_output_suppression(); std::cout<<"HIDDEN"; }
    Render::end_output_suppression();
    { Render::OutputGuard output(context); Render::move_cursor(0,0); std::cout<<"OK"; }
    assert(capture.str().find("OK")!=std::string::npos);
}

void test_replay_invisible_colors() {
    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    auto& player=session.player;
    auto& board=player.gameplay.playfield;
    board.width=10; board.visibleHeight=board.activeHeight=20;
    board.field.initialize(10,20);
    player.presentation.margin=30;
    player.presentation.blockMaxHeight=player.presentation.blockMaxWidth=4;
    context.config.field.invisible=true;
    const std::array<Test::TerminalScreen::Color,7> colors{{
        {{54,235,168}},{{95,78,179}},{{197,117,68}},{{193,168,65}},
        {{139,185,61}},{{176,78,166}},{{202,70,77}}
    }};
    for (int color=0;color<7;++color) board.field.set_cell(19,color,Runtime::BoardCell::piece(color));
    for (int style:{0,1}) {
        context.config.display.squareStyle=style;
        Test::TerminalScreen screen;
        for (bool replay:{false,true}) for (bool flash:{false,true,false}) {
            context.config.mode.replayMode=replay;
            player.presentation.invisibleFlash=flash;
            { Capture c; Render::redraw_player_board(context,player); screen.feed(c.str()); }
            for (int color=0;color<7;++color) {
                const auto cell=screen.at(28,31+color*2);
                assert(cell.glyph==(replay || flash ? "█" : " "));
                auto expected=colors[color];
                if (!flash) for (auto& channel:expected) channel/=3;
                if (replay || flash) assert(cell.foreground==expected);
            }
            assert(screen.at(28,45).glyph==" "); // Empty cells stay empty.
        }
    }
    context.config.mode.replayMode=true;
    player.presentation.invisibleFlash=false;
    const auto piece=Runtime::BoardCell::piece(0), garbage=Runtime::BoardCell::garbage();
    assert(Render::board_cell_appearance(context,player,19,0,piece)==Render::BoardCellAppearance::Dimmed);
    assert(Render::board_cell_appearance(context,player,19,0,garbage)==Render::BoardCellAppearance::Visible);
    context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
    context.config.quickPlay.invisible=Runtime::ModifierMode::Reverse;
    board.garbageHeight=6;
    assert(Render::board_cell_appearance(context,player,16,0,garbage)==Render::BoardCellAppearance::Visible);
    assert(Render::board_cell_appearance(context,player,17,0,garbage)==Render::BoardCellAppearance::Dimmed);
    Test::TerminalScreen screen;
    { Capture c; Render::draw_board_cell(context,player,17,0,garbage); screen.feed(c.str()); }
    assert((screen.at(26,31).foreground==Test::TerminalScreen::Color{{26,26,26}}));
    context.config.display.obscuredColors=true;
    { Capture c; Render::draw_board_cell(context,player,19,0,piece); screen.feed(c.str()); }
    assert((screen.at(28,31).foreground==Test::TerminalScreen::Color{{0,0,0}}));
}

void test_racing_markers() {
    for (int height:{4,20}) for (int style:{0,1}) for (bool obscured:{false,true})
        for (bool cheese:{false,true}) {
        Runtime::SessionState session;
        Runtime::RuntimeServices services;
        Runtime::GameContext context(session,services);
        auto& player=session.player;
        auto& field=player.gameplay.playfield;
        auto& score=player.gameplay.scoring;
        field.width=10; field.activeHeight=field.visibleHeight=height;
        field.field.initialize(10,height);
        field.nextCount=0; field.holdEnabled=false;
        player.presentation.margin=30;
        context.config.field.height=height;
        context.config.mode.goal=cheese ? Runtime::GameGoal::Garbage : Runtime::GameGoal::Lines;
        context.config.mode.racingDistance=40;
        context.config.display.squareStyle=style;
        context.config.display.obscuredColors=obscured;
        context.config.garbage.mode=cheese ? Runtime::GarbageMode::CheeseLayer : Runtime::GarbageMode::None;
        int& completed=cheese ? score.totalGarbageLines : score.totalLines;
        // Exactly a full field remaining: mark all rows, including the top border.
        completed=40-height;
        if (cheese) score.totalLines=40;
        Test::TerminalScreen screen;
        {
            Capture c;
            Render::print_border(context,player);
            auto piece=Piece::get_definition(Piece::Type::from_index(3));
            Render::draw_piece(context,piece,player,Piece::Pose{{0,3},Runtime::Rotation{}},field.field);
            screen.feed(c.str());
        }
        assert(player.presentation.racingGoalRows==height);
        const auto initial=screen;
        completed=37;
        { Capture c; Render::print_racing_goal_marker(context,player); screen.feed(c.str()); }
        const int top=Runtime::kDisplayHiddenRows+height-3+1;
        const int bottom=Runtime::kDisplayHiddenRows+height+1;
        assert(screen.at(top,30).glyph=="╔" && screen.at(top,51).glyph=="╗");
        assert(screen.at(top+1,30).glyph=="║" && screen.at(bottom,30).glyph=="╚");
        assert(screen.at(bottom,51).glyph=="╝");
        assert(screen.at(bottom,31).glyph=="═");
        assert(screen.position_of("3L")==std::make_pair(bottom,40));
        const Test::TerminalScreen::Color expectedColor=obscured
            ? Test::TerminalScreen::Color{{54,57,62}} : Test::TerminalScreen::Color{{96,216,240}};
        assert(screen.at(top,30).foreground==expectedColor);
        assert(screen.at(bottom,40).foreground==expectedColor);
        // Only borders and the floor change, including through the ghost.
        for (int row=1;row<=40;++row) for (int column=1;column<=70;++column) {
            if ((column==30 || column==51) && row>=9 && row<=bottom) continue;
            if (row==bottom && column>=31 && column<=50) continue;
            const auto a=screen.at(row,column), b=initial.at(row,column);
            assert(a.glyph==b.glyph && a.foreground==b.foreground && a.background==b.background);
        }
        { Capture c; Render::print_racing_goal_marker(context,player); assert(c.str().empty()); }
        // A full board redraw must reapply the marker despite its cache.
        { Capture c; Render::redraw_player_board(context,player); screen.feed(c.str()); }
        assert(screen.at(top,30).glyph=="╔");
        completed=39;
        { Capture c; Render::print_message(context,player); screen.feed(c.str()); }
        assert(screen.at(top,30).glyph=="|");
        assert(screen.at(bottom-1,30).glyph=="╔");
        assert(screen.position_of("1L")==std::make_pair(bottom,40));
        assert(screen.position_of("3L")==std::make_pair(0,0));
        completed=40;
        { Capture c; Render::print_message(context,player); screen.feed(c.str()); }
        assert(player.presentation.racingGoalRows==0);
        assert(screen.at(bottom-1,30).glyph=="|" && screen.at(bottom,30).glyph=="+");
        assert(screen.position_of("1L")==std::make_pair(0,0));
        for (int column=31;column<=50;++column) assert(screen.at(bottom,column).glyph=="-");
        completed=41;
        { Capture c; Render::print_racing_goal_marker(context,player); assert(c.str().empty()); }
        completed=38;
        { Capture c; Render::print_racing_goal_marker(context,player); screen.feed(c.str()); }
        if (height==20) {
            completed=28;
            { Capture c; Render::print_racing_goal_marker(context,player); screen.feed(c.str()); }
            assert(screen.position_of("12L")==std::make_pair(bottom,39));
            assert(screen.position_of("2L")==std::make_pair(bottom,40));
            completed=38;
            { Capture c; Render::print_racing_goal_marker(context,player); screen.feed(c.str()); }
            assert(screen.position_of("12L")==std::make_pair(0,0));
            assert(screen.position_of("2L")==std::make_pair(bottom,40));
        }
        context.config.mode.goal=Runtime::GameGoal::None;
        { Capture c; Render::print_racing_goal_marker(context,player); screen.feed(c.str()); }
        assert(screen.at(bottom-2,30).glyph=="|" && screen.at(bottom,51).glyph=="+");
        for (int column=31;column<=50;++column) assert(screen.at(bottom,column).glyph=="-");
    }
}

void test_goal_marker_units_and_time() {
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};auto& p=session->player;auto& field=p.gameplay.playfield;
    field.width=10;field.activeHeight=field.visibleHeight=20;field.field.initialize(10,20);
    p.presentation.margin=30;p.presentation.topMargin=8;
    const int floor=p.presentation.topMargin+field.activeHeight+1;
    context.config.mode.goal=Runtime::GameGoal::Pieces;context.config.mode.pieceTarget=22;p.gameplay.scoring.placedBlocks=2;
    Test::TerminalScreen screen;
    {Capture output;Render::print_border(context,p);screen.feed(output.str());}
    assert(p.presentation.racingGoalRows==20 && screen.position_of("20P").first==floor);
    context.config.mode.goal=Runtime::GameGoal::Attack;context.config.mode.attackTarget=22;p.gameplay.scoring.totalAttack=2;
    {Capture output;Render::print_racing_goal_marker(context,p);screen.feed(output.str());}
    assert(p.presentation.racingGoalRows==20 && screen.position_of("20A").first==floor && !screen.position_of("20P").first);
    context.config.mode.goal=Runtime::GameGoal::Time;context.config.mode.blitzTime=Runtime::Duration::from_seconds(22);
    p.timing.completedGameTime=Runtime::Duration::from_seconds(2.2);
    {Capture output;Render::print_racing_goal_marker(context,p);screen.feed(output.str());}
    assert(p.presentation.racingGoalRows==20 && screen.position_of("20s").first==floor && !screen.position_of("20A").first);
    p.timing.completedGameTime=Runtime::Duration::from_seconds(2.99);
    {Capture output;Render::print_racing_goal_marker(context,p);assert(output.str().empty());}
    p.timing.completedGameTime=Runtime::Duration::from_seconds(3);
    {Capture output;Render::print_racing_goal_marker(context,p);screen.feed(output.str());}
    assert(p.presentation.racingGoalRows==19 && screen.position_of("19s").first==floor);
    context.config.mode.goal=Runtime::GameGoal::Garbage;context.config.mode.garbageTarget=22;p.gameplay.scoring.totalGarbageLines=2;
    {Capture output;Render::print_racing_goal_marker(context,p);screen.feed(output.str());}
    assert(p.presentation.racingGoalRows==20 && screen.position_of("20L").first==floor);
    context.config.mode.goal=Runtime::GameGoal::Time;context.config.mode.blitzTime=Runtime::Duration::from_seconds(24);
    {Capture output;Render::print_racing_goal_marker(context,p);screen.feed(output.str());}
    assert(p.presentation.racingGoalRows==0 && !screen.position_of("20L").first); // 21 seconds exceed the field height.
    p.timing.completedGameTime=Runtime::Duration::from_seconds(25);
    {Capture output;Render::print_racing_goal_marker(context,p);assert(output.str().empty());}
}

void test_piece_transitions(Runtime::GameContext& context, Runtime::PlayerSessionState& player) {
    auto& field=player.gameplay.playfield.field;
    field.initialize(10,20);
    context.config.garbage.mode=Runtime::GarbageMode::None;
    std::size_t fullBytes=0, updateBytes=0;
    for (const auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus)) {
        auto piece=Piece::get_definition(type);
        for (bool ghost:{false,true}) for (int style:{0,1}) for (bool obscured:{false,true}) {
            context.config.field.ghost=ghost;
            context.config.display.squareStyle=style;
            context.config.display.obscuredColors=obscured;
            for (int rotation=0;rotation<4;++rotation) for (int row:{0,16}) {
                const Piece::Pose before{{row,3},Runtime::Rotation{rotation}};
                if (!piece.can_place(before,field)) continue;
                for (const Piece::Pose after:std::vector<Piece::Pose>{
                        before.translated(0,1), before.translated(1,0),
                        {{row,3},Runtime::Rotation{rotation+1}}, before}) {
                    if (!piece.can_place(after,field)) continue;
                    std::string initial, full, update;
                    {Capture c; Render::draw_piece(context,piece,player,before,field); initial=c.str();}
                    {Capture c;
                        Render::draw_piece(context,piece,player,before,field,Render::PieceDrawMode::Erase);
                        Render::draw_piece(context,piece,player,after,field); full=c.str();}
                    {Capture c; Render::draw_piece_transition(context,piece,player,before,after,field); update=c.str();}
                    Test::TerminalScreen expected, actual;
                    expected.feed(initial); actual.feed(initial);
                    expected.feed(full); actual.feed(update);
                    for (int r=1;r<=40;++r) for (int col=1;col<=65;++col) {
                        const auto a=actual.at(r,col), e=expected.at(r,col);
                        assert(a.glyph==e.glyph && a.continuation==e.continuation);
                        if (a.glyph!=" " && !a.glyph.empty())
                            assert(a.foreground==e.foreground && a.background==e.background);
                    }
                    fullBytes+=full.size(); updateBytes+=update.size();
                }
            }
        }
    }
    assert(updateBytes*2<fullBytes);
    std::cerr<<"piece transition output: "<<updateBytes<<" / "<<fullBytes<<" bytes\n";
}
}

void test_map_progress_stays_outside_field() {
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    auto& player=session->player;auto& field=player.gameplay.playfield;
    player.presentation.margin=30;player.presentation.blockMaxHeight=4;field.width=10;field.nextCount=0;
    context.config.mode.mode=Runtime::GameMode::Racing;context.config.initialMap=std::make_shared<Runtime::MapDefinition>();
    for(int style:{0,1}) for(int height:{4,20}) {
        context.config.display.squareStyle=style;field.visibleHeight=height;field.activeHeight=height;
        player.presentation.messageWidths={};
        Capture border;Render::print_border(context,player);Test::TerminalScreen expected;expected.feed(border.str());
        Test::TerminalScreen actual=expected;
        for(const auto goal:{Runtime::MapGoal::ClearInitial,Runtime::MapGoal::AllClear,Runtime::MapGoal::Disabled}) {
            context.config.mode.mapGoal=goal;Capture stats;Render::print_message(context,player);actual.feed(stats.str());
            for(int row=1;row<=expected.rows();++row) for(int col=player.presentation.margin;col<=player.presentation.margin+field.width*2+1;++col)
                assert(actual.at(row,col).glyph==expected.at(row,col).glyph);
            if(goal==Runtime::MapGoal::AllClear) assert(!actual.position_of("MAP CELLS").first);
            if(goal==Runtime::MapGoal::Disabled) assert(!actual.position_of("ALLCLEAR").first);
        }
        context.config.mode.mode=Runtime::GameMode::Zen;Capture stats;Render::print_message(context,player);actual.feed(stats.str());
        assert(!actual.position_of("LINES").first && !actual.position_of("ALLCLEAR").first && !actual.position_of("MAP CELLS").first);
        context.config.mode.mode=Runtime::GameMode::Racing;
    }
}
void test_information_region() {
    for(const auto mode:{Runtime::GameMode::Zen,Runtime::GameMode::Racing,Runtime::GameMode::Blitz,
        Runtime::GameMode::QuickPlay,Runtime::GameMode::Multiplayer,Runtime::GameMode::Replay,Runtime::GameMode::LocalVersus})
        for(int height:{4,20,40,100})for(int holdHeight:{5,11}) {
            auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
            Runtime::GameContext context{*session,services};auto& p=session->player;
            context.config.mode.mode=mode;context.config.mode.blitzTime=Runtime::Duration::from_seconds(64);
            context.config.mode.racingDistance=40;
            context.config.garbage.mode=mode==Runtime::GameMode::Multiplayer || mode==Runtime::GameMode::LocalVersus?
                Runtime::GarbageMode::Multiplayer:Runtime::GarbageMode::None;
            p.presentation.margin=38;p.presentation.topMargin=4;p.presentation.blockMaxHeight=holdHeight-3;
            p.gameplay.playfield.width=10;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=height;
            p.gameplay.playfield.field.initialize(10,height);
            p.gameplay.scoring.placedBlocks=80;p.gameplay.scoring.totalAttack=50;p.gameplay.scoring.totalLines=5;
            p.timing.completedGameTime=Runtime::Duration::from_seconds(4);
            p.presentation.spike={8,Runtime::TimePoint::from_seconds(1)};
            const int start=Render::board_info_top_row(p),distance=height-holdHeight;
            if(distance<15)assert(start==holdHeight+1);
            else {assert(start>holdHeight && start+14<=height);assert(std::abs((start-holdHeight-1)-(height-start-14))<=1);}
            Capture output;Render::print_spike(context,p);Render::print_spin(context,p,Piece::Type{0},false);
            Render::print_clear(context,p,1);Render::print_b2b(context,p,3);Render::print_combo(context,p,4);
            Render::print_clutch(context,p,1);Render::print_all_clear(context,p,true);Render::print_message(context,p);
            Test::TerminalScreen screen;screen.feed(output.str());
            const auto row=[&](const std::string& text,int offset){assert(screen.position_of(text).first==p.presentation.topMargin+start+offset+1);};
            row("8 SPIKE",0);row(std::string(Piece::metadata(Piece::Type{0}).name)+"-Spin",1);
            row("ＳＩＮＧＬＥ",2);row("B2B",3);row("ＣＯＭＢＯ",5);row("CLUTCH",6);row("ＡＬＬ",7);row("ＣＬＥＡＲ",8);
            const int stats=mode==Runtime::GameMode::Racing?9:10;
            row("80 P",stats);row("50 A",stats+1);row("20.00 PPS",stats+2);row("750.00 APM",stats+3);
            row(mode==Runtime::GameMode::Blitz?"01:00.000":"00:04.000",stats+4);
            if(mode==Runtime::GameMode::Racing) row("5/40 LINES",14);
        }
    // A map-height change with an unchanged margin and identical data must
    // erase the previous statistics rows and immediately draw the new ones.
    auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};auto& p=session->player;
    context.config.mode.mode=Runtime::GameMode::Zen;p.presentation.margin=38;p.presentation.topMargin=4;
    p.presentation.blockMaxHeight=2;p.gameplay.playfield.width=10;p.gameplay.playfield.visibleHeight=20;
    p.timing.completedGameTime=Runtime::Duration::from_seconds(4);
    Test::TerminalScreen screen;std::pair<int,int> old;
    {Capture output;Render::print_message(context,p);screen.feed(output.str());old=screen.position_of("00:04.000");}
    p.gameplay.playfield.visibleHeight=40;
    {Capture output;Render::print_message(context,p);screen.feed(output.str());}
    assert(screen.at(old.first,old.second).glyph==" ");
    assert(screen.position_of("00:04.000").first==4+Render::board_info_top_row(p)+15);
    context.config.mode.mode=Runtime::GameMode::Blitz;context.config.mode.blitzTime=Runtime::Duration::from_seconds(3);
    {Capture output;Render::print_message(context,p);assert(output.str().find("00:00.000")!=std::string::npos);}
    // After a resize, the old statistics position can coincide with the new
    // HOLD frame. A complete redraw must not erase its freshly painted frame.
    p.presentation.blockMaxWidth=3;p.presentation.blockMaxHeight=2;
    p.gameplay.playfield.holdEnabled=true;p.gameplay.playfield.nextCount=0;
    p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=20;
    p.gameplay.playfield.field.initialize(10,20);p.presentation.topMargin=20;
    {Capture output;Render::print_message(context,p);}
    p.presentation.topMargin=36;
    {Capture output;Render::clear_screen();Render::redraw_player_board(context,p);
        Test::TerminalScreen resized;resized.feed(output.str());
        assert(resized.at(37,p.presentation.margin-9).glyph=="+");
        assert(resized.at(37,p.presentation.margin-8).glyph=="-");
        assert(resized.position_of("+--HOLD--").first==37);}
}
void test_time_below_statistics() {
    for(const auto mode:{Runtime::GameMode::Zen,Runtime::GameMode::Blitz,Runtime::GameMode::Racing,
        Runtime::GameMode::QuickPlay,Runtime::GameMode::Multiplayer,Runtime::GameMode::LocalVersus}) {
        auto session=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
        Runtime::GameContext context{*session,services};auto& player=session->player;
        context.config.mode.mode=mode;context.config.mode.blitzTime=Runtime::Duration::from_seconds(64);
        context.config.mode.racingDistance=40;
        context.config.garbage.mode=mode==Runtime::GameMode::Multiplayer || mode==Runtime::GameMode::LocalVersus
            ?Runtime::GarbageMode::Multiplayer:Runtime::GarbageMode::None;
        player.gameplay.playfield.width=10;player.gameplay.playfield.visibleHeight=20;
        player.gameplay.playfield.holdEnabled=true;player.presentation.blockMaxHeight=2;
        player.presentation.margin=36;player.presentation.topMargin=4;
        player.gameplay.scoring.placedBlocks=80;player.gameplay.scoring.totalAttack=50;player.gameplay.scoring.totalLines=5;
        player.timing.completedGameTime=Runtime::Duration::from_seconds(4);
        Test::TerminalScreen screen;Capture output;Render::print_message(context,player);screen.feed(output.str());
        const std::string time=mode==Runtime::GameMode::Blitz?"01:00.000":"00:04.000";
        const auto timeAt=screen.position_of(time),ppsAt=screen.position_of("20.00 PPS");
        const auto lastAt=screen.position_of("750.00 APM");
        assert(timeAt.first==lastAt.first+1 && lastAt.first==ppsAt.first+1);
        assert(timeAt.second+int(time.size())==ppsAt.second+int(std::string("20.00 PPS").size()));
        assert(timeAt.second+int(time.size())==lastAt.second+int(std::string("750.00 APM").size()));
        if(mode==Runtime::GameMode::Racing) assert(screen.position_of("5/40 LINES").first==timeAt.first+1);
        if(mode==Runtime::GameMode::Zen) {
            player.timing.completedGameTime=Runtime::Duration::from_seconds(6001.234);
            Render::print_message(context,player);screen.feed(output.str());
            const auto longTime=screen.position_of("100:01.234");
            assert(longTime.first==timeAt.first && longTime.second+10==timeAt.second+int(time.size()));
            player.timing.completedGameTime=Runtime::Duration::from_seconds(4);
            Render::print_message(context,player);screen.feed(output.str());
            assert(screen.at(timeAt.first,longTime.second).glyph==" ");
        }
    }
}
int main(){
    test_goal_marker_units_and_time();
    test_information_region();
    test_time_below_statistics();
#ifdef __linux__
    test_terminal_input_cleanup();
    test_terminal_text_input_cleanup();
    test_terminal_signal_input_cleanup();
    test_terminal_focus_input();
#endif
    test_map_progress_stays_outside_field();
    test_terminal_size();
    test_short_next_preview();
    test_viewport_clips_without_wrapping();
    test_replay_invisible_colors();
    assert(capture_square(0,Render::SquareFill::Filled)=="██");
    assert(capture_square(1,Render::SquareFill::Filled)=="█ ");
    assert(capture_square(1,Render::SquareFill::Empty)=="  ");
    assert(capture_square(0,Render::SquareFill::LowerSevenEighths)=="▇▇");
    assert(capture_square(1,Render::SquareFill::LowerSevenEighths)=="▇ ");
    { Capture c; Render::draw_number(0,1); assert(c.str()=="① "); }
    { Capture c; Render::draw_number(1,16); assert(c.str()=="⑯"); }
    { Capture c; Render::move_cursor(2,3); assert(c.str()=="\x1b[3;4H"); }
#if defined(__linux__) || defined(_WIN32)
    { Capture c; Render::clear_screen(); assert(c.str()=="\x1b[2J\x1b[H"); }
#endif
    { Capture c; Render::set_cell_color(0,false); assert(c.str()=="\x1b[38;2;54;235;168m\x1b[48;2;12;12;12m"); }
    { Capture c; Render::set_cell_color(0,true); assert(c.str()=="\x1b[38;2;0;0;0m\x1b[48;2;12;12;12m"); }

    Runtime::SessionState session;
    Runtime::RuntimeServices services;
    Runtime::GameContext context(session,services);
    auto& player=session.player;
    auto& field=player.gameplay.playfield;
    auto& scoring=player.gameplay.scoring;
    player.presentation.margin=30;
    field.width=10;
    field.activeHeight=20;
    field.visibleHeight=20;
    player.presentation.blockMaxHeight=4;
    player.presentation.blockMaxWidth=4;
    field.nextCount=5;
    field.holdEnabled=1;
    context.config.field.width=10;
    context.config.field.height=20;
    context.config.field.nextCount=5;
    context.config.display.squareStyle=0;

    {
        context.config.garbage.mode=Runtime::GarbageMode::None;
        Capture c; Render::move_to(context,player,2,-1);
        assert(c.str()=="\x1b[11;29H");
    }
    {
        context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
        Capture c; Render::move_to(context,player,2,-1);
        assert(c.str()=="\x1b[11;27H");
    }
    const auto originalMode=context.config.mode.mode;
    {
        context.config.mode.mode=Runtime::GameMode::Multiplayer;
        context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
        scoring.score=123;
        scoring.wins=7;
        player.presentation.username="Latency player";
        Capture c; Render::print_score(context,player);
        const auto out=c.str();
        assert(out.find(player.presentation.username)!=std::string::npos);
        assert(out.find("123")==std::string::npos);
    }
    {
        context.config.mode.mode=Runtime::GameMode::Zen;
        context.config.garbage.mode=Runtime::GarbageMode::None;
        scoring.score=123;
        Capture c; Render::print_score(context,player);
        assert(c.str().find("123")!=std::string::npos);
    }
    context.config.mode.mode=originalMode;
    {
        context.config.display.obscuredColors=false;
        Capture c;
        Render::set_cell_color(6,false);
        Render::print_border(context,player);
        const auto out=c.str();
        const auto red=out.find("38;2;202;70;77");
        const auto white=out.find("38;2;255;255;255");
        assert(red!=std::string::npos&&white!=std::string::npos&&white>red);
    }
    {
        context.config.display.obscuredColors=true;
        Capture c; Render::print_border(context,player);
        assert(c.str().find("38;2;54;57;62")!=std::string::npos);
    }
    {
        context.config.garbage.mode=Runtime::GarbageMode::None;
        context.config.field.ghost=false;
        context.config.display.squareStyle=0;
        context.config.display.obscuredColors=false;
        Piece::Definition piece = Piece::get_definition(Compat::piece_type_from_legacy(0));
        Capture c; Render::draw_block(context,piece,player,Piece::Pose{{0,0},Runtime::Rotation{}},Render::PieceDrawMode::Normal);
        const auto out=c.str();
        assert(out.find("██")!=std::string::npos);
        assert(out.find("54;235;168")!=std::string::npos);
    }
    {
        context.config.display.obscuredColors=true;
        Piece::Definition piece = Piece::get_definition(Compat::piece_type_from_legacy(0));
        Capture c; Render::draw_block(context,piece,player,Piece::Pose{{0,0},Runtime::Rotation{}},Render::PieceDrawMode::Normal);
        assert(c.str().find("38;2;0;0;0")!=std::string::npos);
    }
    {
        context.config.garbage.mode=Runtime::GarbageMode::None;
        Capture c; Render::print_clutch(context,player,0);
        const std::string out=c.str();
        assert(out.find(";11H")!=std::string::npos);
        assert(out.size()>=20);
        assert(out.substr(out.size()-19)==std::string(19,' '));
        assert(out[out.size()-20]=='H'); // cursor sequence, then exactly 19 spaces
    }
    {
        context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
        Capture c; Render::print_clutch(context,player,0);
        assert(c.str().find(";9H")!=std::string::npos);
    }
    {
        context.config.garbage.mode=Runtime::GarbageMode::None;
        context.config.display.obscuredColors=false;
        player.presentation.spike.attack=7;
        player.presentation.spike.lastAttackAt=Runtime::TimePoint::from_seconds(1.0);
        Capture c; Render::print_spike(context,player);
        const std::string out=c.str();
        assert(out.find("SPIKE")==std::string::npos);
        assert(out.substr(out.size()-19)==std::string(19,' '));
        // hold_panel_height() is 7, so this is display row 8 plus 8 hidden
        // rows; the 19-column band starts at terminal column 11.
        assert(out.find("\x1b[17;11H")!=std::string::npos);
    }
    {
        context.config.garbage.mode=Runtime::GarbageMode::None;
        context.config.display.obscuredColors=false;
        player.presentation.spike.attack=8;
        Capture c; Render::print_spike(context,player);
        const std::string out=c.str();
        assert(out.find("8 SPIKE")!=std::string::npos);
        assert(out.find("38;2;238;219;47")!=std::string::npos);
        // Right-aligned toward the board, with one blank terminal cell before
        // the board's left border.
        assert(out.find("\x1b[17;22H")!=std::string::npos);
        // Spin is at hold+2 and stats start at hold+10; SPIKE exclusively owns
        // hold+1, preventing vertical overlap with either group.
        assert(out.find("\x1b[17;")!=std::string::npos);
    }
    {
        context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
        player.presentation.spike.attack=1234567890;
        Capture c; Render::print_spike(context,player);
        const std::string out=c.str();
        assert(out.find("1234567890 SPIKE")!=std::string::npos);
        // Garbage modes shift the full band left by two columns.  Even the
        // longest practical label remains within the fixed 19-column band.
        assert(out.find("\x1b[17;9H")!=std::string::npos);
        assert(out.find("\x1b[17;11H")!=std::string::npos);
        assert(std::string("1234567890 SPIKE").size()<=19);
    }
    {
        // The first simulated lock is rendered before time advances. It must
        // not produce a huge PPS/APM value by dividing by an epsilon.
        context.config.simulationPps=2.5;
        scoring.placedBlocks=1;
        scoring.totalAttack=4;
        session.gameStartTime=Runtime::TimePoint{};
        session.simulationTime=Runtime::TimePoint{};
        Capture c; Render::print_message(context,player);
        assert(c.str().find("0.00 PPS")!=std::string::npos);
        assert(c.str().find("0.00 APM")!=std::string::npos);
        assert(c.str().find("100000")==std::string::npos);
    }
    {
        // A long value followed by a short one must clear the old left edge.
        session.simulationTime=Runtime::TimePoint::from_seconds(1.0/131072.0);
        Capture first; Render::print_message(context,player);
        assert(first.str().find("131072.00 PPS")!=std::string::npos);
        session.simulationTime=Runtime::TimePoint::from_seconds(0.4);
        Capture second; Render::print_message(context,player);
        const auto row=second.str().find("2.50 PPS");
        assert(row!=std::string::npos);
        const auto erased=second.str().rfind(std::string(9,' '),row);
        assert(erased!=std::string::npos);
    }
    test_piece_transitions(context,player);
    test_racing_markers();
    return 0;
}
