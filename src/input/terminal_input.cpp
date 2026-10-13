#include "tetr/input/terminal_input.h"

#ifdef __linux__
#include <cerrno>
#include <poll.h>
#include <unistd.h>

namespace Input {

void TerminalInput::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_=::isatty(STDIN_FILENO);
    if (active_) focus_.set_focused(true);
    textInput_=false; eof_=false; text_.clear(); escape_.clear();
}
void TerminalInput::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    active_=false; textInput_=false; text_.clear(); escape_.clear();
}
bool TerminalInput::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}
void TerminalInput::set_text_input(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) poll_locked(); // Preserve focus changes already in the tty.
    textInput_=enabled;
    text_.clear();
    // Preserve a fragmented focus report; do not carry an old lone Esc into
    // a newly opened text editor.
    if (escape_!="\x1b[") escape_.clear();
}
void TerminalInput::emit(const std::string& bytes) {
    if (!textInput_ || !focus_.accepting_input()) return;
    for (unsigned char byte:bytes) {
        if (text_.size()>=65536) break;
        text_.push_back(byte);
    }
}
void TerminalInput::flush_escape() {
    emit(escape_);
    escape_.clear();
}
void TerminalInput::feed(unsigned char byte) {
    if (escape_=="\x1b[" && (byte=='I' || byte=='O')) {
        escape_.clear();
        focus_.set_focused(byte=='I');
        if (!focus_.accepting_input()) text_.clear();
        return;
    }
    if (escape_=="\x1b" && byte=='[') {
        escape_+='[';
        return;
    }
    if (!escape_.empty()) flush_escape();
    if (byte==27) {
        escape_="\x1b";
        escapeStarted_=std::chrono::steady_clock::now();
    } else emit(std::string(1,static_cast<char>(byte)));
}
void TerminalInput::poll_locked() {
    if (!active_ || eof_) return;
    // Bound work for large pastes so keyboard device events are still serviced.
    for (int batch=0;batch<8;++batch) {
        pollfd input{STDIN_FILENO,POLLIN,0};
        if (::poll(&input,1,0)<=0 || !(input.revents&(POLLIN|POLLHUP))) break;
        char buffer[1024];
        const auto count=::read(STDIN_FILENO,buffer,sizeof(buffer));
        if (count>0) {
            for (ssize_t i=0;i<count;++i) feed(static_cast<unsigned char>(buffer[i]));
        } else {
            if (count==0 && (input.revents&POLLHUP)) eof_=true;
            if (count<0 && errno==EINTR) continue;
            break;
        }
    }
    if (textInput_ && escape_=="\x1b" && std::chrono::steady_clock::now()-escapeStarted_>=std::chrono::milliseconds(20))
        flush_escape();
}
void TerminalInput::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    poll_locked();
}
int TerminalInput::read_character() {
    std::lock_guard<std::mutex> lock(mutex_);
    poll_locked();
    if (!focus_.accepting_input()) text_.clear();
    if (text_.empty()) return eof_ ? -2 : -1;
    const int result=text_.front(); text_.pop_front();
    return result;
}

} // namespace Input
#endif
