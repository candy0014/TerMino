#include <windows.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cwchar>
#include <optional>

namespace {
const HWND consoleWindow = reinterpret_cast<HWND>(1);
const HWND terminalWindow = reinterpret_cast<HWND>(2);
HWND foregroundWindow = terminalWindow;
const wchar_t* windowClass = L"PseudoConsoleWindow";
bool visible = true;
std::array<bool, 256> keys{};
std::optional<bool> focusReport;

HWND WINAPI test_console_window() { return consoleWindow; }
BOOL WINAPI test_window_visible(HWND) { return visible; }
HWND WINAPI test_foreground_window() { return foregroundWindow; }
int WINAPI test_window_class(HWND, LPWSTR name, int capacity) {
    const auto length = std::min<int>(static_cast<int>(std::wcslen(windowClass)), capacity - 1);
    std::wmemcpy(name, windowClass, length);
    name[length] = L'\0';
    return length;
}
SHORT WINAPI test_async_key(int key) {
    return keys.at(key) ? static_cast<SHORT>(0x8000) : 0;
}
BOOL WINAPI test_event_count(HANDLE, LPDWORD available) {
    *available = focusReport.has_value() ? 1 : 0;
    return TRUE;
}
BOOL WINAPI test_peek_events(HANDLE, PINPUT_RECORD records, DWORD capacity, LPDWORD count) {
    *count = focusReport && capacity ? 1 : 0;
    if (*count) {
        records[0] = {};
        records[0].EventType = FOCUS_EVENT;
        records[0].Event.FocusEvent.bSetFocus = *focusReport;
    }
    return TRUE;
}
}

// Exercise the real input service against deterministic Win32 responses. A
// visible ConPTY compatibility HWND is different from its terminal's HWND.
#define GetConsoleWindow test_console_window
#define IsWindowVisible test_window_visible
#define GetForegroundWindow test_foreground_window
#define GetClassNameW test_window_class
#define GetAsyncKeyState test_async_key
#define GetNumberOfConsoleInputEvents test_event_count
#define PeekConsoleInputW test_peek_events
#include "../src/input/services.cpp"

int main() {
    for (const bool shown : {false, true}) {
        visible = shown;
        Input::Services input;
        assert(input.accepting_input());
        for (const int key : std::array<int, 4>{'R', 'P', VK_ESCAPE, VK_RETURN}) {
            keys[key] = true;
            assert(input.key_down(key));
            keys[key] = false;
            assert(!input.key_down(key));
        }
        focusReport = false;
        assert(!input.accepting_input());
        keys['R'] = true;
        assert(!input.key_down('R'));
        focusReport = true;
        assert(input.accepting_input());
        assert(!input.key_down('R')); // Held background keys stay suppressed.
        keys['R'] = false;
        assert(!input.key_down('R'));
        keys['R'] = true;
        assert(input.key_down('R'));
        keys['R'] = false;
        focusReport.reset();
    }

    windowClass = L"ConsoleWindowClass";
    visible = true;
    foregroundWindow = consoleWindow;
    Input::Services classic;
    keys['R'] = true;
    assert(classic.key_down('R'));
    foregroundWindow = terminalWindow;
    assert(!classic.accepting_input() && !classic.key_down('R'));
    foregroundWindow = consoleWindow;
    assert(classic.accepting_input() && !classic.key_down('R'));
    keys['R'] = false;
    assert(!classic.key_down('R'));
    keys['R'] = true;
    assert(classic.key_down('R'));
}
