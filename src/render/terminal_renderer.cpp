#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/input/services.h"
#include "terminal_viewport.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <streambuf>
#include <string>
#include <stdexcept>
#include <array>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include <csignal>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace Render {

std::optional<TerminalSize> terminal_size() noexcept {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),&info)) {
        const int columns=info.srWindow.Right-info.srWindow.Left+1;
        const int rows=info.srWindow.Bottom-info.srWindow.Top+1;
        if (columns>0 && rows>0) return TerminalSize{columns,rows};
    }
#endif
#ifdef __linux__
    winsize size{};
    if (::ioctl(STDOUT_FILENO,TIOCGWINSZ,&size)==0 && size.ws_col>0 && size.ws_row>0)
        return TerminalSize{size.ws_col,size.ws_row};
#endif
    return std::nullopt;
}

struct TerminalSessionState {
    Input::Services* input=nullptr;
    bool focusTracking=false;
#ifdef __linux__
    termios originalTermios{};
    bool originalTermiosCaptured = false;
    bool alternateScreenActive = false;
    using SignalHandler = void(*)(int);
    SignalHandler previousSigint = SIG_DFL;
    SignalHandler previousSigterm = SIG_DFL;
    SignalHandler previousSighup = SIG_DFL;
    bool handlersInstalled = false;
#endif
#ifdef _WIN32
    HANDLE inputHandle = INVALID_HANDLE_VALUE;
    HANDLE outputHandle = INVALID_HANDLE_VALUE;
    DWORD originalInputMode = 0;
    DWORD originalOutputMode = 0;
    bool inputModeCaptured = false;
    bool outputModeCaptured = false;
    CONSOLE_CURSOR_INFO originalCursor{};
    bool cursorCaptured = false;
    UINT originalInputCodePage = 0;
    UINT originalOutputCodePage = 0;
#endif
};

namespace {
bool terminalSessionActive = false;

class NullOutputBuffer final : public std::streambuf {
protected:
    int overflow(int character) override {
        return traits_type::not_eof(character);
    }
};

NullOutputBuffer nullOutputBuffer;
std::streambuf* savedOutputBuffer = nullptr;
std::mutex outputSuppressionMutex;
thread_local TerminalViewportState* currentViewport=nullptr;

#ifdef __linux__
constexpr char kEnterAlternateScreen[]="\x1b[?1049h";
constexpr char kLeaveAlternateScreen[]="\x1b[?1049l";
constexpr char kHideCursor[]="\x1b[?25l";
constexpr char kShowCursor[]="\x1b[?25h";
constexpr char kResetStyle[]="\x1b[0m";
constexpr char kClearAndHome[]="\x1b[2J\x1b[H";

struct EmergencyTerminalState {
    termios originalTermios{};
    volatile std::sig_atomic_t originalTermiosCaptured = 0;
};
EmergencyTerminalState emergencyState;

void apply_input_mode(const termios& mode){
    ::tcsetattr(STDIN_FILENO,TCSANOW,&mode);
    // Gameplay reads evdev, leaving a second copy of the keys in the tty.
    // Discard it before handing input to the shell or another input scope.
    // Keep restoration immediate even when terminal output is flow-controlled.
    ::tcflush(STDIN_FILENO,TCIFLUSH);
}

void apply_game_input_mode(TerminalSessionState& state){
    if(!state.originalTermiosCaptured) return;
    termios term=state.originalTermios;
    term.c_lflag&=~static_cast<tcflag_t>(ECHO|ICANON);
    apply_input_mode(term);
}

void apply_original_input_mode(TerminalSessionState& state){
    if(state.originalTermiosCaptured) apply_input_mode(state.originalTermios);
}

void emergency_restore_terminal(int signalNumber){
    if(emergencyState.originalTermiosCaptured)
        apply_input_mode(emergencyState.originalTermios);
    static constexpr char restoreSequence[]="\x1b[?1004l\x1b[0m\x1b[?25h\x1b[?1049l";
    (void)::write(STDOUT_FILENO,restoreSequence,sizeof(restoreSequence)-1);
    ::_exit(128+signalNumber);
}
#endif

#ifdef _WIN32
void apply_windows_game_mode(TerminalSessionState& state){
    if(state.inputModeCaptured) {
        // ReadConsoleInputW needs key records. Quick Edit can suspend the game
        // when the user clicks the console; line input and echo belong to text entry.
        const DWORD mode = (state.originalInputMode | ENABLE_EXTENDED_FLAGS) &
            ~(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
              ENABLE_VIRTUAL_TERMINAL_INPUT);
        SetConsoleMode(state.inputHandle,mode);
    }
    if(state.outputModeCaptured) SetConsoleMode(state.outputHandle,
        state.originalOutputMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    CONSOLE_CURSOR_INFO cur{1,FALSE};
    SetConsoleCursorInfo(state.outputHandle,&cur);
}
#endif
} // namespace

OutputGuard::OutputGuard(const Runtime::GameContext& context, bool batch)
    : lock_(context.output_mutex() ?
          std::unique_lock<std::recursive_mutex>(*context.output_mutex()) :
          std::unique_lock<std::recursive_mutex>{}) {
    const auto& p=context.session.player.presentation;
#ifdef _WIN32
    // Clipping emits individual UTF-8 glyphs. Commit a positioned update in
    // one write rather than making a console write for every glyph.
    if(p.dynamicLayout) batch=true;
#endif
    if (currentViewport || (!p.dynamicLayout && !batch) || output_suppressed()) return;
    // Batched output still clips to the configured layout when the console
    // is redirected; batching must not silently replace its dimensions.
    const auto fallback=p.dynamicLayout?TerminalSize{p.terminalColumns,p.terminalRows}:TerminalSize{120,40};
    const auto size=terminal_size().value_or(fallback);
    if (size.columns<=0 || size.rows<=0) return;
    viewport_=std::make_unique<TerminalViewportState>(std::cout.rdbuf(),size,batch);
    currentViewport=viewport_.get();
    std::cout.rdbuf(&viewport_->buffer);
}

OutputGuard::~OutputGuard() {
    if (!viewport_) return;
    viewport_->commit();
    if (std::cout.rdbuf()==&viewport_->buffer) std::cout.rdbuf(viewport_->previous);
    else {
        // A seek can begin suppression while a positioned update is active.
        std::lock_guard<std::mutex> lock(outputSuppressionMutex);
        if (savedOutputBuffer==&viewport_->buffer) savedOutputBuffer=viewport_->previous;
    }
    currentViewport=nullptr;
}

TerminalSession::TerminalSession(Input::Services* input):state_(std::make_unique<TerminalSessionState>()){
    if(terminalSessionActive) throw std::logic_error("only one TerminalSession may be active");
    terminalSessionActive=true;
    state_->input=input;
#ifdef __linux__
    if(::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO,&state_->originalTermios)==0) {
        state_->originalTermiosCaptured=true;
        emergencyState.originalTermios=state_->originalTermios;
        emergencyState.originalTermiosCaptured=1;
    } else {
        emergencyState.originalTermiosCaptured=0;
    }
    state_->previousSigint=std::signal(SIGINT,emergency_restore_terminal);
    state_->previousSigterm=std::signal(SIGTERM,emergency_restore_terminal);
    state_->previousSighup=std::signal(SIGHUP,emergency_restore_terminal);
    state_->handlersInstalled=true;
    if(::isatty(STDOUT_FILENO)){
        std::cout<<kEnterAlternateScreen;
        state_->alternateScreenActive=true;
    }
    apply_game_input_mode(*state_);
    std::cout<<"\x1b[48;2;12;12;12m"<<kClearAndHome<<kHideCursor;
    std::fflush(stdout);
#endif
#ifdef _WIN32
    state_->originalInputCodePage = GetConsoleCP();
    state_->originalOutputCodePage = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    state_->inputHandle=GetStdHandle(STD_INPUT_HANDLE);
    state_->outputHandle=GetStdHandle(STD_OUTPUT_HANDLE);
    state_->inputModeCaptured=GetConsoleMode(state_->inputHandle,&state_->originalInputMode)!=0;
    state_->outputModeCaptured=GetConsoleMode(state_->outputHandle,&state_->originalOutputMode)!=0;
    state_->cursorCaptured=GetConsoleCursorInfo(state_->outputHandle,&state_->originalCursor)!=0;
    apply_windows_game_mode(*state_);
#endif
    if (input) {
#ifdef __linux__
        state_->focusTracking=state_->originalTermiosCaptured && ::isatty(STDOUT_FILENO);
        if (state_->focusTracking) input->start_terminal_input();
#endif
#ifdef _WIN32
        state_->focusTracking=state_->inputModeCaptured && state_->outputModeCaptured;
#endif
        if (state_->focusTracking) {
            std::cout<<"\x1b[?1004h";
            std::cout.flush();
        }
    }
}

TerminalSession::~TerminalSession(){
    if(!state_) return;
    end_output_suppression();
    if (state_->focusTracking) {
        std::cout<<"\x1b[?1004l";
        std::cout.flush();
        state_->input->stop_terminal_input();
    }
#ifdef __linux__
    apply_original_input_mode(*state_);
    std::cout<<kResetStyle<<kShowCursor;
    if(state_->alternateScreenActive) std::cout<<kLeaveAlternateScreen;
    std::fflush(stdout);
    if(state_->handlersInstalled){
        std::signal(SIGINT,state_->previousSigint);
        std::signal(SIGTERM,state_->previousSigterm);
        std::signal(SIGHUP,state_->previousSighup);
    }
#endif
#ifdef _WIN32
    std::cout<<"\x1b[0m";
    std::cout.flush();
    if(state_->inputModeCaptured) SetConsoleMode(state_->inputHandle,state_->originalInputMode);
    if(state_->outputModeCaptured) SetConsoleMode(state_->outputHandle,state_->originalOutputMode);
    if(state_->cursorCaptured) SetConsoleCursorInfo(state_->outputHandle,&state_->originalCursor);
    if(state_->originalInputCodePage) SetConsoleCP(state_->originalInputCodePage);
    if(state_->originalOutputCodePage) SetConsoleOutputCP(state_->originalOutputCodePage);
#endif
#ifdef __linux__
    emergencyState.originalTermiosCaptured=0;
#endif
    terminalSessionActive=false;
}

void TerminalSession::begin_text_input(){
    if(!state_) return;
#ifdef __linux__
    if (state_->focusTracking) state_->input->set_terminal_text_input(true);
    else apply_original_input_mode(*state_);
    std::cout<<kShowCursor;
    std::fflush(stdout);
#endif
#ifdef _WIN32
    if(state_->inputModeCaptured) SetConsoleMode(state_->inputHandle,state_->originalInputMode);
    CONSOLE_CURSOR_INFO cur{1,TRUE};
    SetConsoleCursorInfo(state_->outputHandle,&cur);
#endif
}

void TerminalSession::end_text_input(){
    if(!state_) return;
#ifdef __linux__
    if (state_->focusTracking) state_->input->set_terminal_text_input(false);
    else apply_game_input_mode(*state_);
    std::cout<<kHideCursor;
    std::fflush(stdout);
#endif
#ifdef _WIN32
    apply_windows_game_mode(*state_);
#endif
}

void clear_screen(){
    if(output_suppressed()) return;
    std::fflush(stdout);
#ifdef _WIN32
    std::cout<<"\x1b[2J\x1b[H";
    std::fflush(stdout);
#endif
#ifdef __linux__
    std::cout<<kClearAndHome;
    std::fflush(stdout);
#endif
}

void move_cursor(int row,int col){
    if (currentViewport && std::cout.rdbuf()==&currentViewport->buffer) {
        currentViewport->buffer.move(row,col);
        return;
    }
#ifdef _WIN32
    std::cout<<"\033["<<row+1<<";"<<col+1<<"H";
#endif
#ifdef __linux__
    std::cout<<"\x1b["<<row+1<<";"<<col+1<<"H";
#endif
}

void set_rgb(int wr,int wg,int wb,int br,int bg,int bb){
#ifdef _WIN32
    std::cout<<"\033[38;2;"+std::to_string(wr)+";"+std::to_string(wg)+";"+std::to_string(wb)+"m";
    std::cout<<"\033[48;2;"+std::to_string(br)+";"+std::to_string(bg)+";"+std::to_string(bb)+"m";
#endif
#ifdef __linux__
    std::cout<<"\x1b[38;2;"+std::to_string(wr)+";"+std::to_string(wg)+";"+std::to_string(wb)+"m";
    std::cout<<"\x1b[48;2;"+std::to_string(br)+";"+std::to_string(bg)+";"+std::to_string(bb)+"m";
#endif
}

namespace {
TerminalColors colors(int r,int g,int b,int br=12,int bg=12,int bb=12) {
    return {{{r,g,b}},{{br,bg,bb}}};
}
void set_hole_colors(const std::array<int,3>& body) {
    // Colored blocks use a dark hole. Obscured blocks are black, so use the
    // empty spawn marker's neutral gray to keep the dot visible on them.
    const auto dot=body==std::array<int,3>{{0,0,0}}
        ? std::array<int,3>{{64,68,74}} : std::array<int,3>{{12,12,12}};
    set_rgb(dot[0],dot[1],dot[2],body[0],body[1],body[2]);
}
}

TerminalColors ui_colors(ColorRole role,bool obscuredColors){
    // Obscured UI colors hide the original hues and use neutral gray highlights.
    if(role==ColorRole::Ghost || (obscuredColors &&
        (role==ColorRole::RacingGoal || role==ColorRole::Danger))) return colors(54,57,62);
    if(role==ColorRole::RacingGoal){
        return colors(96,216,240);
    }
    if(role==ColorRole::NumberedCell){
        return obscuredColors ? colors(54,57,62,0,0,0) : colors(255,255,255,77,0,59);
    }
    if(obscuredColors){
        return colors(0,0,0);
    }
    if(role==ColorRole::Orange) return colors(237,109,0);
    if(role==ColorRole::IncomingGarbage) return colors(157,15,0);
    if(role==ColorRole::Danger) return colors(233,22,0);
    if(role==ColorRole::Accent) return colors(193,168,65);
    return colors(255,255,255);
}

TerminalColors cell_colors(int type,bool obscuredColors,bool dimmed){
    if(obscuredColors) return colors(0,0,0);
    static constexpr std::array<std::array<int,3>,10> palette{{
        {{54,235,168}},{{95,78,179}},{{197,117,68}},{{193,168,65}},{{139,185,61}},
        {{176,78,166}},{{202,70,77}},{{78,78,78}},{{39,39,39}},{{77,0,59}}
    }};
    if(type<0 || type>=static_cast<int>(palette.size())) return ui_colors(ColorRole::Ghost,false);
    const auto& rgb=palette[type];
    const int divisor=dimmed ? 3 : 1;
    return colors(rgb[0]/divisor,rgb[1]/divisor,rgb[2]/divisor);
}

TerminalColors board_cell_colors(Runtime::BoardCell cell,bool obscuredColors,bool dimmed){
    if(cell.empty_cell()) return ui_colors(ColorRole::Ghost,obscuredColors);
    if(cell.is_timed_number() || cell.is_timed_release()){
        return ui_colors(ColorRole::NumberedCell,obscuredColors);
    }
    return cell_colors(cell.color_id(),obscuredColors,dimmed);
}

void set_colors(const TerminalColors& colors) {
    const auto& fg=colors.foreground;
    const auto& bg=colors.background;
    set_rgb(fg[0],fg[1],fg[2],bg[0],bg[1],bg[2]);
}
void set_ui_color(ColorRole role,bool obscuredColors) {
    set_colors(ui_colors(role,obscuredColors));
}
void set_cell_color(int type,bool obscuredColors,bool dimmed) {
    if (!obscuredColors && (type<0 || type>=10)) return;
    set_colors(cell_colors(type,obscuredColors,dimmed));
}
void set_board_cell_color(Runtime::BoardCell cell,bool obscuredColors,bool dimmed) {
    if (cell.occupied() && !cell.is_timed_number() && !cell.is_timed_release())
        set_cell_color(cell.color_id(),obscuredColors,dimmed);
    else set_colors(board_cell_colors(cell,obscuredColors,dimmed));
}

void draw_square(int squareStyle,SquareFill fill){
    if(fill==SquareFill::Empty){std::cout<<"  ";return;}
    const char* block=fill==SquareFill::LowerSevenEighths ? "▇" : "█";
    if(squareStyle==0) std::cout<<block<<block;
    if(squareStyle==1) std::cout<<block<<' ';
}

void draw_number(int squareStyle,int value){
    static const char* numbers[] = {"","①","②","③","④","⑤","⑥","⑦","⑧","⑨","⑩","⑪","⑫","⑬","⑭","⑮","⑯"};
    if(value>=1&&value<=16) std::cout<<numbers[value];
    if(squareStyle==0) std::cout<<" ";
}

void draw_spawn_square(int squareStyle,const TerminalColors& colors) {
    set_hole_colors(colors.foreground);
    if (squareStyle==0) std::cout<<" .";
    else if (squareStyle==1) {
        std::cout<<'.';
        set_colors(colors);
        std::cout<<' ';
    }
    set_colors(colors);
}

void draw_spawn_number(int squareStyle,int value,const TerminalColors& colors) {
    set_colors(colors);
    if (value<1 || value>16) {
        draw_number(squareStyle,value);
        return;
    }
    draw_number(1,value); // Preserve the number, without the filled-style space.
    if (squareStyle==0) {
        set_hole_colors(colors.background);
        std::cout<<'.';
    }
    set_colors(colors);
}

void flush(){std::cout.flush();std::fflush(stdout);}

void begin_output_suppression(){
    std::lock_guard<std::mutex> lock(outputSuppressionMutex);
    if(savedOutputBuffer) return;
    std::cout.flush();
    savedOutputBuffer=std::cout.rdbuf(&nullOutputBuffer);
}

void end_output_suppression(){
    std::lock_guard<std::mutex> lock(outputSuppressionMutex);
    if(!savedOutputBuffer) return;
    std::cout.rdbuf(savedOutputBuffer);
    savedOutputBuffer=nullptr;
    std::cout.flush();
}

bool output_suppressed() noexcept {
    std::lock_guard<std::mutex> lock(outputSuppressionMutex);
    return savedOutputBuffer!=nullptr;
}

} // namespace Render
