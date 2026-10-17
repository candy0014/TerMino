#ifndef TETR_RENDER_TERMINAL_RENDERER_H
#define TETR_RENDER_TERMINAL_RENDERER_H

#include "tetr/runtime/board_cell.h"
#include "tetr/frontend/text_input.h"

#include <memory>
#include <array>
#include <mutex>
#include <optional>

namespace Runtime { class GameContext; }
namespace Input { struct Services; }

namespace Render {

struct TerminalSessionState;
struct TerminalViewportState;

struct TerminalSize {
    int columns;
    int rows;
    friend bool operator==(TerminalSize a, TerminalSize b) { return a.columns==b.columns && a.rows==b.rows; }
    friend bool operator!=(TerminalSize a, TerminalSize b) { return !(a==b); }
};
// The visible console window, in cells; unavailable for redirected output.
std::optional<TerminalSize> terminal_size() noexcept;

enum class ColorRole {
    Ghost,
    Accent,
    Text,
    Danger,
    IncomingGarbage,
    Orange,
    NumberedCell,
    RacingGoal
};

enum class SquareFill { Empty, Filled, LowerSevenEighths };

// Serializes one logical terminal update when local-versus runs both players
// concurrently. It is a no-op for contexts without a shared output mutex.
class OutputGuard {
public:
    explicit OutputGuard(const Runtime::GameContext& context, bool batch = false);
    ~OutputGuard();

private:
    std::unique_lock<std::recursive_mutex> lock_;
    std::unique_ptr<TerminalViewportState> viewport_;
};

// Owns the full-screen terminal UI lifecycle. On ANSI terminals this uses the
// alternate screen buffer so game/settings redraws do not pollute the shell's
// scrollback. Destruction restores the exact console/terminal state captured
// on construction. Linux input-mode transitions discard pending tty input so
// gameplay keys cannot leak into a text prompt or the shell after exit.
class TerminalSession final : public Frontend::TextInputSession {
public:
    explicit TerminalSession(Input::Services* input=nullptr);
    ~TerminalSession() override;

    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    void begin_text_input() override;
    void end_text_input() override;

private:
    std::unique_ptr<TerminalSessionState> state_;
};

void clear_screen();
void move_cursor(int row,int col);
void set_rgb(int wr,int wg,int wb,int br=12,int bg=12,int bb=12);
struct TerminalColors {
    std::array<int,3> foreground{{255,255,255}};
    std::array<int,3> background{{12,12,12}};
};
TerminalColors ui_colors(ColorRole role,bool obscuredColors);
TerminalColors cell_colors(int colorId,bool obscuredColors,bool dimmed=false);
TerminalColors board_cell_colors(Runtime::BoardCell cell,bool obscuredColors,bool dimmed=false);
void set_colors(const TerminalColors& colors);
void set_ui_color(ColorRole role,bool obscuredColors);
void set_cell_color(int colorId,bool obscuredColors,bool dimmed=false);
void set_board_cell_color(Runtime::BoardCell cell,bool obscuredColors,bool dimmed=false);
void draw_square(int squareStyle,SquareFill fill=SquareFill::Filled);
void draw_number(int squareStyle,int value);
// ASCII dot inside the original block color, with the normal colors restored
// afterwards. The spaced style keeps its second character empty.
void draw_spawn_square(int squareStyle,const TerminalColors& colors);
void draw_spawn_number(int squareStyle,int value,const TerminalColors& colors);
void flush();

// Replay seeking rebuilds gameplay state from time zero. Suppression keeps
// those intermediate terminal frames invisible until one final redraw.
void begin_output_suppression();
void end_output_suppression();
bool output_suppressed() noexcept;

// Migration bridge for a non-console session using the existing orchestration.
// Replay seeking cannot restore ANSI output while this scope is alive.
class DetachedOutputScope {
public:
    explicit DetachedOutputScope(bool allowAudio=false);
    ~DetachedOutputScope();
    DetachedOutputScope(const DetachedOutputScope&) = delete;
    DetachedOutputScope& operator=(const DetachedOutputScope&) = delete;
};
bool detached_audio_enabled() noexcept;

} // namespace Render

#endif
