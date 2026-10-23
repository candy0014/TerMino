#include "render_thread.h"
#include "scene.h"
#include "tetr/frontend/graphical/session.h"

namespace Graphical {
struct RenderThread::Resources {
    NativeContext context;
    std::unique_ptr<Renderer> renderer;
    bool bound=false;
    explicit Resources(NativeContext native):context(std::move(native)) {}
};
RenderThread::RenderThread(SDL_Window* window,Session& session,const std::string& font,
    FrameRequest initial,int rate,bool vsync,int frames,std::string screenshot,std::function<void()> beforeDraw)
    :resources_(std::make_shared<Resources>(NativeContext::detach(window,vsync))) {
    auto resources=resources_;
    FrameWorker::Callbacks callbacks;
    callbacks.initialize=[resources,font] {
        resources->context.bind();resources->bound=true;
        resources->renderer=std::make_unique<Renderer>(font);
    };
    callbacks.draw=[resources,&session,screenshot,beforeDraw](const FrameRequest& request,bool last) {
        if(beforeDraw)beforeDraw();
        auto& renderer=*resources->renderer;
        const auto view=session.view();
        renderer.begin(request.width,request.height,-1,request.fullWindow);renderer.minimum_text_size(request.minimumTextSize);
        renderer.touch_controls(request.touchControls);
        renderer.pointer(request.pointer);background(renderer,true,request.contentScale);
        renderer.scroll_menu(request.menuScroll);
        if(!view.ui || view.ui->overlay) {
            renderer.scaled(request.contentScale,600,200,[&] {
                if(view.peers.empty() && !view.room)game_view(renderer,view);
                else scene_view(renderer,view);
            });
        }
        if(view.ui && (!request.surface || !request.surface(renderer,*view.ui)))renderer.surface(*view.ui);
        if(const auto field=renderer.text_input_rect();request.textActive && field && !request.composition.empty()) {
            const float x=std::min(field->x,780.f),y=field->y;
            renderer.rect(x,y,400,30,panelColor);renderer.fitted(x,y,request.composition,400,20,accent);
        }
        if(request.overlay)request.overlay(renderer);
        renderer.end();
        if(last && !screenshot.empty())renderer.screenshot(screenshot,request.width,request.height);
        if(request.presentable)resources->context.swap();
        return renderer.interaction();
    };
    callbacks.shutdown=[resources] {
        resources->renderer.reset();
        if(resources->bound) {resources->context.release();resources->bound=false;}
    };
    worker_=std::make_unique<FrameWorker>(std::move(callbacks),std::move(initial),rate,frames,!screenshot.empty());
}
RenderThread::~RenderThread()=default;
const char* RenderThread::backend() const {return resources_->context.backend();}
}
