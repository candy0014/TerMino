# Stage 112: Terminal Session Ownership

The terminal renderer no longer exposes process-global `begin_text_input()` / `end_text_input()` functions backed by a mutable `activeSession` pointer.

`TerminalSession` owns those operations as instance methods. The application composition root attaches the live session to `ApplicationState`, and Settings/replay-save text input uses that explicit handle. Linux emergency signal restoration uses a small process-global POD snapshot of the original termios state, which is the minimum process-level state required by an async signal handler.
