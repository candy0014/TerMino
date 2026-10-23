#include "tetr/input/services.h"
#include <algorithm>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Input {
namespace {
thread_local unsigned controlInputDepth = 0;
thread_local bool pollingControlInterrupt = false;
}

ControlInputScope::ControlInputScope(Services* first, Services* second)
    : first_(first), second_(second) { ++controlInputDepth; }
ControlInputScope::~ControlInputScope() {
    if (first_) first_->discard_control_events();
    if (second_) second_->discard_control_events();
    --controlInputDepth;
}

void Services::poll_control_interrupt() {
    if (controlInputDepth || pollingControlInterrupt || !controlInterrupt) return;
    struct Guard {
        Guard() { pollingControlInterrupt = true; }
        ~Guard() { pollingControlInterrupt = false; }
    } guard;
    controlInterrupt();
}

void Services::discard_control_events() {
    // Menus/text use the console directly on Windows; consume their queued
    // records before a later gameplay capture can mistake Escape for a new press.
    poll_platform_events();
    (void)event_buffer().read(controlEventCursor_);
    controlPressedKeys_.clear();
}

bool Services::control_key_pressed(int keyCode) {
    poll_focus(); synchronize_focus();
    if (gameplayInputActive_) poll_platform_events();
    for (const auto& event : event_buffer().read(controlEventCursor_)) {
        if (!event.down) suppressedControlKeys_.erase(event.key);
        else if(!suppressedControlKeys_.count(event.key)) controlPressedKeys_.insert(event.key);
    }
    const bool queued = controlPressedKeys_.erase(keyCode) != 0;
    const bool down=raw_key_down(keyCode);
    if(suppressedControlKeys_.count(keyCode)) {
        if(!down) suppressedControlKeys_.erase(keyCode);
        return false;
    }
    return down || queued;
}
void Services::suppress_control_key_until_release(int keyCode) {
    controlPressedKeys_.erase(keyCode);
    if(raw_key_down(keyCode)) suppressedControlKeys_.insert(keyCode);
}

std::vector<KeyEvent> Services::read_control_events(std::uint64_t& cursor) {
    poll_control_interrupt();
    poll_focus(); synchronize_focus();
    if (gameplayInputActive_) poll_platform_events();
    return event_buffer().read(cursor);
}

Services& Services::physical_source() {
    return keyStateSource && keyStateSource != this ? keyStateSource->physical_source() : *this;
}
const Services& Services::physical_source() const {
    return keyStateSource && keyStateSource!=this ? keyStateSource->physical_source() : *this;
}

FocusState& Services::focus_state() {
    auto& source=physical_source();
    if (source.focusSource) return *source.focusSource;
#if defined(__linux__) && !defined(__ANDROID__)
    return source.keyboard.focus_state();
#elif defined(_WIN32)
    return source.consoleFocus_;
#else
    return source.unavailableFocus_;
#endif
}
const FocusState& Services::focus_state() const {
    const auto& source=physical_source();
    if (source.focusSource) return *source.focusSource;
#if defined(__linux__) && !defined(__ANDROID__)
    return source.keyboard.focus_state();
#elif defined(_WIN32)
    return source.consoleFocus_;
#else
    return source.unavailableFocus_;
#endif
}

#ifdef _WIN32
namespace {
void set_console_focus(FocusState& focus,bool focused) {
    if (!focused) focus.set_focused(false);
    if (!focus.accepting_input()) {
        // Keys pressed in another window stay suppressed until released,
        // including keys that produced no records in this console.
        for (int key=1;key<255;++key)
            focus.update(key,(GetAsyncKeyState(key)&0x8000)!=0);
    }
    if (focused) focus.set_focused(true);
}
}
#endif

void Services::poll_focus() {
    auto& source=physical_source();
    if (source.focusSource || source.keyStateProvider || source.keyEventSource) return;
#if defined(__linux__) && !defined(__ANDROID__)
    source.keyboard.terminal_input().poll();
#elif defined(_WIN32)
    auto& focus=source.consoleFocus_;
    const HWND window=GetConsoleWindow();
    wchar_t windowClass[32]{};
    // ConPTY's compatibility HWND can be marked visible while the actual
    // foreground window belongs to Windows Terminal. Only a classic console
    // HWND can be compared directly with GetForegroundWindow().
    const bool classicConsole=window && GetClassNameW(window,windowClass,32) &&
        std::wstring_view(windowClass)==L"ConsoleWindowClass";
    if (classicConsole && IsWindowVisible(window)) {
        set_console_focus(focus,GetForegroundWindow()==window);
        return;
    }
    // ConPTY has no interactive console HWND. Its terminal forwards focus reports
    // as console focus records; peek them without stealing menu/text keys.
    std::lock_guard<std::mutex> lock(source.consoleMutex_);
    DWORD available=0,count=0;
    const HANDLE handle=GetStdHandle(STD_INPUT_HANDLE);
    if (GetNumberOfConsoleInputEvents(handle,&available) && available) {
        std::vector<INPUT_RECORD> records(available);
        if (PeekConsoleInputW(handle,records.data(),available,&count)) {
            std::optional<bool> focused;
            for (DWORD i=0;i<count;++i)
                if (records[i].EventType==FOCUS_EVENT)
                    focused=records[i].Event.FocusEvent.bSetFocus!=FALSE;
            if (focused && *focused!=focus.focused()) set_console_focus(focus,*focused);
        }
    }
#endif
}

void Services::synchronize_focus() {
    const auto generation=focus_state().generation();
    if (generation==focusGeneration_) return;
    focusGeneration_=generation;
    pendingEvents_.clear(); gameplayDown_.clear(); gameplayPressedAt_.clear();
    controlPressedKeys_.clear();
}
bool Services::accepting_input() {
    poll_focus(); synchronize_focus();
    return focus_state().accepting_input();
}
void Services::set_continue_input_unfocused(bool enabled) {
    focus_state().set_continue_unfocused(enabled);
    synchronize_focus();
}
void Services::start_terminal_input() {
#if defined(__linux__) && !defined(__ANDROID__)
    physical_source().keyboard.terminal_input().start();
#endif
}
void Services::stop_terminal_input() {
#if defined(__linux__) && !defined(__ANDROID__)
    physical_source().keyboard.terminal_input().stop();
#endif
}
bool Services::terminal_input_active() {
#if defined(__linux__) && !defined(__ANDROID__)
    return physical_source().keyboard.terminal_input().active();
#else
    return false;
#endif
}
void Services::set_terminal_text_input(bool enabled) {
#if defined(__linux__) && !defined(__ANDROID__)
    physical_source().keyboard.terminal_input().set_text_input(enabled);
#else
    (void)enabled;
#endif
}
int Services::read_terminal_character() {
#if defined(__linux__) && !defined(__ANDROID__)
    return physical_source().keyboard.terminal_input().read_character();
#else
    return -1;
#endif
}

KeyEventBuffer& Services::event_buffer() {
    auto& source = physical_source();
    if (source.keyEventSource) return *source.keyEventSource;
    if (source.focusSource) return source.focusSource->events();
#ifdef _WIN32
    return source.focus_state().events();
#elif defined(__linux__) && !defined(__ANDROID__)
    return source.keyboard.events();
#else
    return source.unavailableEvents_;
#endif
}

void Services::poll_platform_events() {
    poll_focus(); synchronize_focus();
#ifdef _WIN32
    auto& source = physical_source();
    if (source.keyEventSource || source.focusSource) return;
    std::lock_guard<std::mutex> lock(source.consoleMutex_);
    const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD available = 0;
    // Read console records only during gameplay. Menus and text entry retain
    // their existing input path; queued KEY_EVENTs survive a slow render.
    while (GetNumberOfConsoleInputEvents(handle, &available) && available) {
        INPUT_RECORD records[64];
        DWORD count = 0;
        if (!ReadConsoleInputW(handle, records, std::min<DWORD>(available, 64), &count) || !count) break;
        for (DWORD i = 0; i < count; ++i) {
            if (records[i].EventType==FOCUS_EVENT) {
                set_console_focus(source.consoleFocus_,records[i].Event.FocusEvent.bSetFocus!=FALSE);
                synchronize_focus();
                continue;
            }
            if (records[i].EventType != KEY_EVENT) continue;
            const auto& key = records[i].Event.KeyEvent;
            const int code = key.wVirtualKeyCode;
            const bool down = key.bKeyDown != FALSE;
            source.consoleFocus_.update(code, down);
            // Configurable side-specific modifiers use VK_L*/VK_R* codes.
            if (code == VK_SHIFT) {
                source.consoleFocus_.update(MapVirtualKeyW(key.wVirtualScanCode, MAPVK_VSC_TO_VK_EX), down);
            } else if (code == VK_CONTROL || code == VK_MENU) {
                const bool right = (key.dwControlKeyState & ENHANCED_KEY) != 0;
                const int side = code == VK_CONTROL ? (right ? VK_RCONTROL : VK_LCONTROL)
                    : (right ? VK_RMENU : VK_LMENU);
                source.consoleFocus_.update(side, down);
            }
        }
    }
#endif
}

void Services::begin_gameplay_input() {
    end_gameplay_input();
    auto& source = physical_source();
    // A state-only injected backend cannot capture transitions between polls.
    if (source.keyStateProvider && !source.keyEventSource) return;
    poll_platform_events();
#ifdef _WIN32
    if (!source.keyEventSource) {
        for (const auto& [key, action] : bindings.entries())
            (void)source.read_key_state(key);
    }
#endif
    gameplayDown_ = event_buffer().snapshot(eventCursor_, &gameplayPressedAt_, &gameplaySampleTime_);
    gameplayInputActive_ = true;
}

void Services::end_gameplay_input() {
    gameplayInputActive_ = false;
    pendingEvents_.clear();
    gameplayDown_.clear();
    gameplayPressedAt_.clear();
    gameplaySampleTime_.reset();
}

bool Services::gameplay_key_down(int key) const {
    if (focus_state().generation()!=focusGeneration_ || !focus_state().accepting_input()) return false;
    const auto it = gameplayDown_.find(key);
    return it != gameplayDown_.end() && it->second;
}

std::optional<std::chrono::steady_clock::time_point> Services::gameplay_press_time(int key) const {
    if (focus_state().generation()!=focusGeneration_ || !focus_state().accepting_input()) return std::nullopt;
    const auto it=gameplayPressedAt_.find(key);
    return it==gameplayPressedAt_.end() ? std::nullopt : std::optional{it->second};
}

void Services::poll_gameplay_events() {
    poll_control_interrupt();
    if (!gameplayInputActive_) return;
    poll_platform_events();
#ifdef _WIN32
    auto& source = physical_source();
    if (!source.keyEventSource) {
        for (const auto& [key, action] : bindings.entries())
            (void)source.read_key_state(key);
    }
#endif
    for (const auto& event : event_buffer().read(eventCursor_, &gameplaySampleTime_)) {
        if (bindings.action_for(event.key) != Action::Unknown) pendingEvents_.push_back(event);
    }
}

const KeyEvent* Services::next_gameplay_event() const {
    if (focus_state().generation()!=focusGeneration_ || !focus_state().accepting_input()) return nullptr;
    return pendingEvents_.empty() ? nullptr : &pendingEvents_.front();
}

bool Services::gameplay_input_pending() {
    if(!gameplayInputActive_)return false;
    if(next_gameplay_event())return true;
#ifdef _WIN32
    auto& source=physical_source();
    if(!source.keyEventSource && !source.focusSource) {
        DWORD available=0;
        {
            std::lock_guard<std::mutex> lock(source.consoleMutex_);
            if(!GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE),&available) || !available)return false;
        }
    }
#endif
    poll_platform_events();
    for(const auto& event:event_buffer().read(eventCursor_, &gameplaySampleTime_))
        if(bindings.action_for(event.key)!=Action::Unknown)pendingEvents_.push_back(event);
    return next_gameplay_event()!=nullptr;
}

void Services::consume_gameplay_event() {
    poll_focus(); synchronize_focus();
    if (pendingEvents_.empty()) return;
    const auto event = pendingEvents_.front();
    gameplayDown_[event.key] = event.down;
    if (event.down) gameplayPressedAt_[event.key]=event.time;
    else gameplayPressedAt_.erase(event.key);
    pendingEvents_.pop_front();
}

std::vector<KeyEvent> Services::take_spawn_horizontal_events() {
    poll_gameplay_events();
    std::vector<KeyEvent> result;
    for (auto it=pendingEvents_.begin(); it!=pendingEvents_.end();) {
        if (!is_horizontal(bindings.action_for(it->key))) { ++it; continue; }
        gameplayDown_[it->key]=it->down;
        if (it->down) gameplayPressedAt_[it->key]=it->time;
        else gameplayPressedAt_.erase(it->key);
        result.push_back(*it);
        it=pendingEvents_.erase(it);
    }
    return result;
}

void Services::discard_prestart_actions() {
    poll_gameplay_events();
    for (auto it=pendingEvents_.begin(); it!=pendingEvents_.end();) {
        if (is_horizontal(bindings.action_for(it->key))) { ++it; continue; }
        gameplayDown_[it->key]=it->down;
        if (it->down) gameplayPressedAt_[it->key]=it->time;
        else gameplayPressedAt_.erase(it->key);
        it=pendingEvents_.erase(it);
    }
}

GameplayInputScope::GameplayInputScope(Services* services, bool enabled)
    : services_(services), owns_(enabled && services && !services->gameplay_input_active()) {
    if (owns_) services_->begin_gameplay_input();
}
GameplayInputScope::~GameplayInputScope() { end(); }
void GameplayInputScope::end() {
    if (owns_) services_->end_gameplay_input();
    owns_=false;
}
void GameplayInputScope::restart() {
    if (owns_) services_->begin_gameplay_input();
}

void Services::wait_for_gameplay_input(std::chrono::microseconds timeout) {
    poll_control_interrupt();
    if (!pendingEvents_.empty() || timeout.count()<=0) return;
    if (!gameplayInputActive_) { std::this_thread::sleep_for(timeout); return; }
#ifdef _WIN32
    if (!physical_source().keyEventSource) {
        if (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE),static_cast<DWORD>((timeout.count()+999)/1000))==WAIT_FAILED)
            std::this_thread::sleep_for(timeout);
        return;
    }
#endif
    event_buffer().wait(eventCursor_,timeout);
}

bool Services::key_down(int keyCode) {
    poll_control_interrupt();
    const bool down=raw_key_down(keyCode);
    if(suppressedControlKeys_.count(keyCode)) {
        if(!down) suppressedControlKeys_.erase(keyCode);
        return false;
    }
    return down;
}

bool Services::raw_key_down(int keyCode) {
    poll_focus(); synchronize_focus();
    return read_key_state(keyCode);
}

bool Services::read_key_state(int keyCode) {
    // The caller has sampled focus for this operation. In particular a bound
    // key scan must not query the console HWND/foreground window for each key.
    if (keyStateSource && keyStateSource != this) {
        return keyStateSource->read_key_state(keyCode);
    }
    if (focusSource) return focusSource->key_down(keyCode);
    if (keyStateProvider) return focus_state().accepting_input() && keyStateProvider(keyCode);
#ifdef _WIN32
    consoleFocus_.update(keyCode,(GetAsyncKeyState(keyCode)&0x8000)!=0);
    return consoleFocus_.key_down(keyCode);
#elif defined(__linux__) && !defined(__ANDROID__)
    return keyboard.keydown(keyCode);
#else
    (void)keyCode;
    return false;
#endif
}

void share_key_state(Services& target, Services& source) {
    if (&target == &source) {
        target.keyStateSource = nullptr;
        return;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    target.keyboard.stop();
#endif
    target.keyStateSource = &source;
    // The target is fully usable through the source and must not start a
    // second platform monitor if initialize_services() is called later.
    target.initialized = true;
}

bool shares_key_state_with(
    const Services& target,
    const Services& source) noexcept {
    return target.keyStateSource == &source;
}

void initialize_services(Services& services) {
    if (services.initialized) return;
#if defined(__linux__) && !defined(__ANDROID__)
    services.keyboard.init();
#endif
    services.initialized = true;
}

void refresh_bindings(Services& services, const std::array<int, kBindableActionCount>& keyCodes,
    const std::array<std::vector<int>, kBindableActionCount>& additional) {
    services.bindings = BindingSet::from_key_codes(keyCodes, additional);
    services.bindings.add_virtual_actions(services.virtualPlayerSlot);
}

} // namespace Input
