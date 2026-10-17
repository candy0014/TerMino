#ifndef TETR_FRONTEND_TEXT_INPUT_H
#define TETR_FRONTEND_TEXT_INPUT_H
#include <optional>
#include <string>
#include <utility>

namespace Frontend {

enum class TextEditKind { Insert, Left, Right, Home, End, Backspace, Delete, Confirm, Cancel, SelectAll };
struct TextEditEvent {TextEditKind kind=TextEditKind::Insert;std::string text;};
class TextField {
public:
    std::string value;
    std::size_t caret=0;
    bool replace=false,done=false,cancelled=false;
    explicit TextField(std::string initial={}):value(std::move(initial)),caret(value.size()),replace(!value.empty()) {}
    void apply(const TextEditEvent& event);
};

// A frontend owns text-entry mode and restores its own input state. Gameplay
// actions and UI text entry remain distinct; no platform key codes belong here.
class TextInputSession {
public:
    virtual ~TextInputSession() = default;
    virtual void begin_text_input() = 0;
    virtual void end_text_input() = 0;
    virtual bool owns_text_events() const {return false;}
    virtual std::optional<TextEditEvent> poll_text_event() {return std::nullopt;}
};

class TextInputScope {
public:
    explicit TextInputScope(TextInputSession* session) : session_(session) {
        if (session_) session_->begin_text_input();
    }
    ~TextInputScope() { if (session_) session_->end_text_input(); }
    TextInputScope(const TextInputScope&) = delete;
    TextInputScope& operator=(const TextInputScope&) = delete;
private:
    TextInputSession* session_;
};

} // namespace Frontend
#endif
