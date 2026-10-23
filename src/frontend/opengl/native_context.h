#ifndef TETR_OPENGL_NATIVE_CONTEXT_H
#define TETR_OPENGL_NATIVE_CONTEXT_H
#include <SDL3/SDL.h>
#include <memory>

namespace Graphical {
// SDL's window/video APIs stay on the main thread. Capture native entry points
// there, detach through SDL, and use only the platform GL APIs on the worker.
// The SDL window/context must outlive the worker and be destroyed on main.
class NativeContext {
public:
    static NativeContext detach(SDL_Window* window,bool vsync);
    void bind() const;
    void release() const;
    void swap() const;
    const char* backend() const;
private:
    struct Impl;
    explicit NativeContext(std::shared_ptr<Impl> impl):impl_(std::move(impl)) {}
    std::shared_ptr<Impl> impl_;
};
}
#endif
