#ifndef TETR_OPENGL_RENDER_THREAD_H
#define TETR_OPENGL_RENDER_THREAD_H
#include "frame_worker.h"
#include "native_context.h"
#include <SDL3/SDL.h>

namespace Graphical {
class Session;
class RenderThread {
public:
    // beforeDraw is used by hidden integration checks to stall the real
    // renderer without stalling the SDL input loop or changing game time.
    RenderThread(SDL_Window* window,Session& session,const std::string& font,
        FrameRequest initial,int rate,bool vsync,int frames=0,std::string screenshot={},
        std::function<void()> beforeDraw={});
    ~RenderThread();
    void submit(FrameRequest request) {worker_->submit(std::move(request));}
    void scroll(MenuScroll movement) {worker_->scroll(std::move(movement));}
    std::shared_ptr<const FrameFeedback> feedback() const {return worker_->feedback();}
    bool finished() const {return worker_->finished();}
    void stop() {worker_->stop();}
    const char* backend() const;
private:
    struct Resources;
    std::shared_ptr<Resources> resources_;
    std::unique_ptr<FrameWorker> worker_;
};
}
#endif
