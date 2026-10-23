#include "tetr/input/services.h"
#include "tetr/config/user_config.h"
#include "tetr/app/application_state.h"
#include "settings_input.h"
#include "settings_navigation.h"
#include "tetr/frontend/ui_surface.h"

#include "tetr/frontend/text_input.h"
#include "tetr/frontend/file_dialog.h"
#include "tetr/runtime/game_context.h"
#include "tetr/util/terminal_text.h"
#include "tetr/replay/file_path.h"
#include "terminal_canvas.h"
#include "ui_text.h"
#include <algorithm>
#include <filesystem>

#include <iostream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <linux/input.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#endif

namespace App::Settings {
std::optional<std::string> choose_replay_file(Runtime::GameContext& context,const CheckMenuControls& checkControls,std::string* errorMessage) {
    auto* dialogs=context.app_component()?context.app_component()->fileDialogs:nullptr;
    if(!dialogs)return {};
    std::error_code error;auto directory=std::filesystem::absolute(Replay::replay_directory(),error);
    if(error)directory=Replay::replay_directory();
    const auto request=dialogs->open_file(directory.generic_u8string());
    if(!request)return {};
    struct CancelOnExit {std::shared_ptr<Frontend::FileDialog> request;~CancelOnExit(){request->complete();}} guard{request};
    for(;;) {
        checkControls();
        if(const auto result=request->result()) {
            if(!result->error.empty() && errorMessage)
                *errorMessage=UiText::tr("Could not open the system file picker.")+" "+result->error;
            if(result->path.empty())return {};
            if(!std::filesystem::is_regular_file(std::filesystem::u8path(result->path),error) || error) {
                if(errorMessage)*errorMessage=UiText::tr("The replay file is not available.");
                return {};
            }
            return Replay::replay_setting_path(result->path);
        }
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(.002));
    }
}

namespace {

int confirm_key_code() {
#ifdef _WIN32
    return VK_RETURN;
#else
    return KEY_ENTER;
#endif
}

void wait_for_confirm_release(Runtime::GameContext& context, const CheckMenuControls& checkControls) {
    const int enter = confirm_key_code();
#ifdef _WIN32
    const int right = VK_RIGHT;
#else
    const int right = KEY_RIGHT;
#endif
    while (context.input_services().key_down(enter) || context.input_services().key_down(right)) {
        checkControls();
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}

#ifndef _WIN32
bool text_line_ready() {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(STDIN_FILENO, &readable);
    timeval timeout{};
    return select(STDIN_FILENO + 1, &readable, nullptr, nullptr, &timeout) > 0;
}
#endif

#ifndef _WIN32
struct InlineInputScope {
    termios saved{};
    bool restore = false;
    InlineInputScope() {
        if (tcgetattr(STDIN_FILENO, &saved) != 0) return;
        auto raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        restore = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
    ~InlineInputScope() {
        if (restore) tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
};
#else
struct InlineInputScope {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD saved = 0;
    bool restore = false;
    InlineInputScope() {
        if (GetConsoleMode(input, &saved))
            restore = SetConsoleMode(input, saved & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) != 0;
    }
    ~InlineInputScope() {
        if (restore) SetConsoleMode(input, saved);
    }
};
#endif

std::string read_inline_text(Runtime::GameContext& context, const CheckMenuControls& checkControls,
                            const ScalarInputRedraw& redraw, bool* savedByEscape,const std::string& initialValue) {
    InlineInputScope inlineInput;
#ifndef _WIN32
    std::string escapeSequence;
    std::string utf8Sequence;
    std::size_t utf8Length = 0;
    std::chrono::steady_clock::time_point escapeStarted;
#else
    wchar_t highSurrogate = 0;
#endif
    std::string value=initialValue;
    std::size_t caret=value.size();
    bool replaceOnInput=!value.empty();
    redraw(value, caret);
    const auto previous_character = [&](std::size_t at) {
        if (at) --at;
        while (at && (static_cast<unsigned char>(value[at]) & 0xc0) == 0x80) --at;
        return at;
    };
    const auto next_character = [&](std::size_t at) {
        if (at < value.size()) ++at;
        while (at < value.size() && (static_cast<unsigned char>(value[at]) & 0xc0) == 0x80) ++at;
        return at;
    };
    enum { Left = 0x110000, Right, Home, End, Delete };
    for (;;) {
        checkControls();
        int key = -1;
        std::string inserted;
        bool receivedCharacter = false;
#ifdef _WIN32
        if (context.input_services().accepting_input() && _kbhit()) {
            receivedCharacter = true;
            key = _getwch();
            if (key == 0 || key == 0xe0) {
                switch (_getwch()) {
                    case 75: key = Left; break;
                    case 77: key = Right; break;
                    case 71: key = Home; break;
                    case 79: key = End; break;
                    case 83: key = Delete; break;
                    default: key = -1; break;
                }
            }
            else if (key >= 0x80) {
                if (key >= 0xd800 && key <= 0xdbff) highSurrogate = static_cast<wchar_t>(key);
                else {
                    const wchar_t characters[] = {highSurrogate, static_cast<wchar_t>(key)};
                    char utf8[8]{};
                    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                        characters + (highSurrogate ? 0 : 1), highSurrogate ? 2 : 1,
                        utf8, sizeof utf8, nullptr, nullptr);
                    if (bytes > 0) inserted.assign(utf8, bytes);
                    highSurrogate = 0;
                }
                key = -1;
            }
        }
#else
        auto& input=context.input_services();
        const bool monitored=input.terminal_input_active();
        const int character=monitored ? input.read_terminal_character() : -1;
        if (character==-2) { std::cin.setstate(std::ios::eofbit); break; }
        if (character>=0 || (!monitored && text_line_ready())) {
            unsigned char ch = 0;
            const auto bytes = monitored ? (ch=static_cast<unsigned char>(character),1) : ::read(STDIN_FILENO, &ch, 1);
            if (bytes == 0) { std::cin.setstate(std::ios::eofbit); break; }
            if (bytes == 1) {
                receivedCharacter = true;
                key = ch;
                if (ch == 27) {
                    escapeSequence = "\x1b";
                    escapeStarted = std::chrono::steady_clock::now();
                    key = -1;
                }
                else if (!escapeSequence.empty()) {
                    escapeSequence += static_cast<char>(ch);
                    key = -1;
                    if (escapeSequence.size() == 2 && ch != '[' && ch != 'O') {
                        escapeSequence.clear();
                        key = 27;
                    } else if (escapeSequence.size() > 2 && ch >= '@' && ch <= '~') {
                        if (escapeSequence == "\x1b[D" || escapeSequence == "\x1bOD") key = Left;
                        else if (escapeSequence == "\x1b[C" || escapeSequence == "\x1bOC") key = Right;
                        else if (escapeSequence == "\x1b[H" || escapeSequence == "\x1bOH" || escapeSequence == "\x1b[1~") key = Home;
                        else if (escapeSequence == "\x1b[F" || escapeSequence == "\x1bOF" || escapeSequence == "\x1b[4~") key = End;
                        else if (escapeSequence == "\x1b[3~") key = Delete;
                        escapeSequence.clear();
                    } else if (escapeSequence.size() > 16) escapeSequence.clear();
                }
                else if (ch >= 0x80) {
                    if (utf8Sequence.empty()) {
                        utf8Length = ch >= 0xc2 && ch <= 0xdf ? 2 :
                            ch >= 0xe0 && ch <= 0xef ? 3 : ch >= 0xf0 && ch <= 0xf4 ? 4 : 0;
                        if (utf8Length) utf8Sequence += static_cast<char>(ch);
                    } else if ((ch & 0xc0) == 0x80) {
                        utf8Sequence += static_cast<char>(ch);
                        if (utf8Sequence.size() == utf8Length) {
                            inserted = std::move(utf8Sequence);
                            utf8Sequence.clear();
                        }
                    } else utf8Sequence.clear();
                    key = -1;
                } else utf8Sequence.clear();
            }
        }
        // A lone Escape commits; CSI/SS3 sequences still edit the caret.
        if (escapeSequence == "\x1b" && !receivedCharacter &&
            std::chrono::steady_clock::now() - escapeStarted >= std::chrono::milliseconds(40))
            key = 27;
#endif
        if (!receivedCharacter
#ifndef _WIN32
            && escapeSequence.empty()
#endif
        ) {
#ifdef _WIN32
            constexpr int escape = VK_ESCAPE;
#else
            constexpr int escape = KEY_ESC;
#endif
            if (context.input_services().key_down(escape)) key = 27;
        }
        if (key == 27) {
            if (savedByEscape) *savedByEscape = true;
            break;
        }
        if (key == '\r' || key == '\n') break;
        bool changed = key != -1 || !inserted.empty();
        if(key==Left || key==Right || key==Home || key==End) replaceOnInput=false;
        if(replaceOnInput && (key==Delete || key=='\b' || key==127)) {
            value.clear();caret=0;replaceOnInput=false;
        }
        if (key == Left) caret = previous_character(caret);
        else if (key == Right) caret = next_character(caret);
        else if (key == Home) caret = 0;
        else if (key == End) caret = value.size();
        else if (key == Delete) value.erase(caret, next_character(caret) - caret);
        else if (key == '\b' || key == 127) {
            const auto previous = previous_character(caret);
            value.erase(previous, caret - previous);
            caret = previous;
        }
        else if (key >= ' ' && key <= '~') inserted.assign(1, static_cast<char>(key));
        else if (inserted.empty()) changed = false;
        if (!inserted.empty()) {
            // The full address starts selected: typing replaces it; cursor
            // movement keeps it for editing. Other scalar inputs start blank.
            if(replaceOnInput) {value.clear();caret=0;replaceOnInput=false;}
            value.insert(caret, inserted);
            caret += inserted.size();
        }
        if (changed) redraw(value, caret);
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
    return value;
}

} // namespace


int capture_key(Runtime::GameContext& context, Input::Action action, const CheckMenuControls& checkControls) {
    // Do not capture the Enter (or a still-held navigation key) that opened
    // the key-binding editor. A binding must come from a fresh press.
    auto any_key_down = [&] {
#ifdef __linux__
        for (int key = 0; key <= KEY_MAX; ++key) {
            if (context.input_services().key_down(key)) return true;
        }
#else
        for (int key = 1; key < 0xff; ++key) {
            if (context.input_services().key_down(key)) return true;
        }
#endif
        return false;
    };
    while (any_key_down()) {
        checkControls();
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }

    int captured = -1;
    for (;;) {
        checkControls();
#ifdef __linux__
        for (int key = 0; key <= KEY_MAX && captured == -1; ++key) {
            if (Input::capture_key_code(key) && !Input::reserved_binding_key(key,action) && context.input_services().key_down(key)) captured = key;
        }
#else
        for (int key = 1; key < 0xff && captured == -1; ++key) {
            if (Input::capture_key_code(key) && !Input::reserved_binding_key(key,action) && context.input_services().key_down(key)) captured = key;
        }
#endif
        if (captured != -1 && !context.input_services().key_down(captured)) return captured;
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.001));
    }
}

void refresh_runtime_bindings(Runtime::GameContext& context) {
    Input::refresh_bindings(context.input_services(), context.user_settings().keyCodes, context.user_settings().additionalKeyCodes);
}

std::string read_scalar_text(Runtime::GameContext& context, const CheckMenuControls& checkControls,
                             const ScalarInputRedraw& redraw, bool* savedByEscape,const std::string& initialValue) {
    if (savedByEscape) *savedByEscape = false;
    // Release the action that opened the editor, then discard its queued text.
    wait_for_confirm_release(context, checkControls);
    Frontend::TextInputScope textInput(context.app_component() ? context.app_component()->textInputSession : nullptr);
    if(auto* session=context.app_component()?context.app_component()->textInputSession:nullptr;
        session && session->owns_text_events()) {
        Frontend::TextField field(initialValue);
        if(redraw)redraw(field.value,field.caret);
        while(!field.done) {
            checkControls();
            if(const auto command=Frontend::poll_ui_command([](const auto& c){return c.ancestor || c.kind==Frontend::UiCommandKind::Back;})) {
                if(command->ancestor)throw MenuJump{*command->ancestor,command->index};
                field.apply({Frontend::TextEditKind::Cancel});
                break;
            }
            if(auto event=session->poll_text_event()) {
                field.apply(*event);
                if(redraw)redraw(field.value,field.caret);
            } else context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
        }
        if(savedByEscape)*savedByEscape=field.cancelled;
        return field.value;
    }
#ifdef _WIN32
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#else
    if (!context.input_services().terminal_input_active()) tcflush(STDIN_FILENO, TCIFLUSH);
#endif
    std::cin.clear();
    std::string previous;
    std::size_t previousCaret = 0;
    const ScalarInputRedraw echo = [&](const std::string& value, std::size_t caret) {
        const auto back = Util::TerminalText::width(previous.substr(0, previousCaret));
        const auto oldWidth = Util::TerminalText::width(previous);
        const auto newWidth = Util::TerminalText::width(value);
        const auto erased = oldWidth > newWidth ? oldWidth - newWidth : 0;
        std::cout << std::string(back, '\b') << value << std::string(erased, ' ')
                  << std::string(newWidth + erased - Util::TerminalText::width(value.substr(0, caret)), '\b');
        std::cout.flush();
        previous = value;
        previousCaret = caret;
    };
    const auto value = read_inline_text(context, checkControls, redraw ? redraw : echo, savedByEscape,initialValue);
    context.services.clock.sleep_for(Runtime::Duration::from_seconds(0.1));
    return value;
}

} // namespace App::Settings
