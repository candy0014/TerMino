#ifndef TETR_FRONTEND_TEXT_INPUT_H
#define TETR_FRONTEND_TEXT_INPUT_H

namespace Frontend {

// A frontend owns text-entry mode and restores its own input state. Gameplay
// actions and UI text entry remain distinct; no platform key codes belong here.
class TextInputSession {
public:
    virtual ~TextInputSession() = default;
    virtual void begin_text_input() = 0;
    virtual void end_text_input() = 0;
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
