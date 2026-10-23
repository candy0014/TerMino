#include "native_context.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#else
#include <cerrno>
#include <poll.h>
#endif

namespace Graphical {
struct NativeContext::Impl {
    const char* name="";
    std::function<void()> bind,release,swap;
};
namespace {
template<class F> F egl_proc(const char* name) {
    const auto proc=SDL_EGL_GetProcAddress(name);
    if(!proc)throw std::runtime_error(std::string("EGL function unavailable: ")+name);
    return reinterpret_cast<F>(proc);
}
#ifndef _WIN32
template<class F> F glx_proc(const char* name) {
    const auto proc=SDL_GL_GetProcAddress(name);
    if(!proc)throw std::runtime_error(std::string("GLX function unavailable: ")+name);
    return reinterpret_cast<F>(proc);
}
// Wayland EGL swap interval zero avoids a driver's unbounded wait for an
// occluded/minimized surface. A private frame-callback queue supplies VSync,
// with the same 50 ms maximum wait as SDL's Wayland presentation backend.
// It never dispatches SDL's keyboard/IME queue on the rendering thread.
class WaylandFrames {
public:
    WaylandFrames(void* display,void* surface):display_(display) {
        library_=SDL_LoadObject("libwayland-client.so.0");
        if(!library_)throw std::runtime_error(SDL_GetError());
        try {
            createQueue_=symbol<void*(*)(void*)>("wl_display_create_queue");
            destroyQueue_=symbol<void(*)(void*)>("wl_event_queue_destroy");
            createWrapper_=symbol<void*(*)(void*)>("wl_proxy_create_wrapper");
            destroyWrapper_=symbol<void(*)(void*)>("wl_proxy_wrapper_destroy");
            setQueue_=symbol<void(*)(void*,void*)>("wl_proxy_set_queue");
            // Keep SDL's Wayland 1.18 baseline; marshal_flags needs 1.20.
            marshal_=symbol<void*(*)(void*,unsigned,const void*,unsigned,...)>("wl_proxy_marshal_constructor_versioned");
            version_=symbol<unsigned(*)(void*)>("wl_proxy_get_version");
            addListener_=symbol<int(*)(void*,void(**)(void),void*)>("wl_proxy_add_listener");
            destroyProxy_=symbol<void(*)(void*)>("wl_proxy_destroy");
            prepare_=symbol<int(*)(void*,void*)>("wl_display_prepare_read_queue");
            cancel_=symbol<void(*)(void*)>("wl_display_cancel_read");
            read_=symbol<int(*)(void*)>("wl_display_read_events");
            dispatch_=symbol<int(*)(void*,void*)>("wl_display_dispatch_queue_pending");
            flush_=symbol<int(*)(void*)>("wl_display_flush");
            fd_=symbol<int(*)(void*)>("wl_display_get_fd");
            interface_=reinterpret_cast<const void*>(SDL_LoadFunction(library_,"wl_callback_interface"));
            if(!interface_)throw std::runtime_error("Wayland callback interface unavailable");
            queue_=createQueue_(display_);wrapper_=createWrapper_(surface);
            if(!queue_ || !wrapper_)throw std::runtime_error("Cannot create Wayland presentation queue");
            setQueue_(wrapper_,queue_);
        } catch(...) {cleanup();throw;}
    }
    ~WaylandFrames() {cleanup();}
    void request() {
        ready_=false;
        // wl_surface.frame, using only a wrapper assigned to our own queue.
        callback_=marshal_(wrapper_,3,interface_,version_(wrapper_),nullptr);
        if(!callback_ || addListener_(callback_,reinterpret_cast<void(**)(void)>(&listener_),this)<0)
            throw std::runtime_error("Cannot request Wayland frame callback");
    }
    void wait() {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(50);
        flush_(display_);
        while(!ready_ && std::chrono::steady_clock::now()<deadline) {
            if(prepare_(display_,queue_)!=0) {
                if(dispatch_(display_,queue_)<0)throw std::runtime_error("Wayland presentation queue failed");
                continue;
            }
            flush_(display_);
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            pollfd source{fd_(display_),POLLIN,0};
            const int result=poll(&source,1,static_cast<int>(std::max<std::int64_t>(0,left)));
            if(result>0 && (source.revents&POLLIN)) {
                if(read_(display_)<0)throw std::runtime_error("Wayland presentation connection failed");
                if(dispatch_(display_,queue_)<0)throw std::runtime_error("Wayland presentation dispatch failed");
            } else {
                cancel_(display_);
                if(result<0 && errno!=EINTR)throw std::runtime_error("Wayland presentation wait failed");
                break;
            }
        }
        if(callback_) {destroyProxy_(callback_);callback_=nullptr;}
    }
private:
    template<class F> F symbol(const char* name) {
        auto proc=SDL_LoadFunction(library_,name);
        if(!proc)throw std::runtime_error(std::string("Wayland function unavailable: ")+name);
        return reinterpret_cast<F>(proc);
    }
    void cleanup() {
        if(callback_)destroyProxy_(callback_);
        if(wrapper_)destroyWrapper_(wrapper_);
        if(queue_)destroyQueue_(queue_);
        if(library_)SDL_UnloadObject(library_);
    }
    static void done(void* data,void* callback,std::uint32_t) {
        auto& frames=*static_cast<WaylandFrames*>(data);
        frames.ready_=true;frames.destroyProxy_(callback);frames.callback_=nullptr;
    }
    struct Listener {void(*done)(void*,void*,std::uint32_t);} listener_{done};
    SDL_SharedObject* library_=nullptr;
    void *display_=nullptr,*queue_=nullptr,*wrapper_=nullptr,*callback_=nullptr;
    const void* interface_=nullptr;
    bool ready_=false;
    void*(*createQueue_)(void*)=nullptr;
    void(*destroyQueue_)(void*)=nullptr;
    void*(*createWrapper_)(void*)=nullptr;
    void(*destroyWrapper_)(void*)=nullptr;
    void(*setQueue_)(void*,void*)=nullptr;
    void*(*marshal_)(void*,unsigned,const void*,unsigned,...)=nullptr;
    unsigned(*version_)(void*)=nullptr;
    int(*addListener_)(void*,void(**)(void),void*)=nullptr;
    void(*destroyProxy_)(void*)=nullptr;
    int(*prepare_)(void*,void*)=nullptr;
    void(*cancel_)(void*)=nullptr;
    int(*read_)(void*)=nullptr;
    int(*dispatch_)(void*,void*)=nullptr;
    int(*flush_)(void*)=nullptr;
    int(*fd_)(void*)=nullptr;
};
#endif
}

NativeContext NativeContext::detach(SDL_Window* window,bool vsync) {
    if(!SDL_IsMainThread())throw std::runtime_error("GL handoff must start on the SDL main thread");
    auto impl=std::make_shared<Impl>();
#ifdef _WIN32
    const auto context=wglGetCurrentContext();const auto dc=wglGetCurrentDC();
    if(!context || !dc)throw std::runtime_error("No current WGL context to hand off");
    impl->name="WGL";
    impl->bind=[=] {if(!wglMakeCurrent(dc,context))throw std::runtime_error("Cannot bind rendering WGL context");};
    // Keep the HDC even when unbinding, matching SDL's workaround for Windows
    // drivers that reject a null DC with a null context.
    impl->release=[=] {if(!wglMakeCurrent(dc,nullptr))throw std::runtime_error("Cannot release rendering WGL context");};
    impl->swap=[=] {if(!SwapBuffers(dc))throw std::runtime_error("WGL SwapBuffers failed");};
#else
    const auto display=SDL_EGL_GetCurrentDisplay();
    if(display) {
        const auto current=egl_proc<void*(*)()>("eglGetCurrentContext")();
        const auto getSurface=egl_proc<void*(*)(int)>("eglGetCurrentSurface");
        const auto surface=getSurface(0x3059),readSurface=getSurface(0x305A); // EGL_DRAW / EGL_READ
        if(!current)throw std::runtime_error("No EGL context to hand off");
        const auto makeCurrent=egl_proc<unsigned(*)(void*,void*,void*,void*)>("eglMakeCurrent");
        const auto swap=egl_proc<unsigned(*)(void*,void*)>("eglSwapBuffers");
        const auto interval=egl_proc<unsigned(*)(void*,int)>("eglSwapInterval");
        const auto api=egl_proc<unsigned(*)()>("eglQueryAPI")();
        const auto bindApi=egl_proc<unsigned(*)(unsigned)>("eglBindAPI");
        const auto props=SDL_GetWindowProperties(window);
        void* waylandDisplay=SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,nullptr);
        void* waylandSurface=SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,nullptr);
        auto frames=vsync && waylandDisplay && waylandSurface ? std::make_shared<WaylandFrames>(waylandDisplay,waylandSurface):nullptr;
        impl->name=waylandDisplay?"EGL/Wayland":"EGL";
        impl->bind=[=] {
            if(!bindApi(api))throw std::runtime_error("Cannot select rendering EGL client API");
            if(!makeCurrent(display,surface,readSurface,current))throw std::runtime_error("Cannot bind rendering EGL context");
            // Wayland frame callbacks provide bounded VSync independently of
            // input. Other EGL backends keep their normal native interval.
            interval(display,waylandDisplay?0:vsync?1:0);
        };
        impl->release=[=] {if(!makeCurrent(display,nullptr,nullptr,nullptr))throw std::runtime_error("Cannot release rendering EGL context");};
        impl->swap=[=] {
            if(frames)frames->request();
            if(!swap(display,surface))throw std::runtime_error("EGL SwapBuffers failed");
            if(frames)frames->wait();
        };
    } else {
        const auto context=glx_proc<void*(*)()>("glXGetCurrentContext")();
        const auto display=glx_proc<void*(*)()>("glXGetCurrentDisplay")();
        const auto drawable=glx_proc<unsigned long(*)()>("glXGetCurrentDrawable")();
        const auto makeCurrent=glx_proc<int(*)(void*,unsigned long,void*)>("glXMakeCurrent");
        const auto swap=glx_proc<void(*)(void*,unsigned long)>("glXSwapBuffers");
        if(!context || !display || !drawable)throw std::runtime_error("No GLX context/drawable to hand off");
        impl->name="GLX";
        impl->bind=[=] {if(!makeCurrent(display,drawable,context))throw std::runtime_error("Cannot bind rendering GLX context");};
        impl->release=[=] {if(!makeCurrent(display,0,nullptr))throw std::runtime_error("Cannot release rendering GLX context");};
        impl->swap=[=] {swap(display,drawable);};
    }
#endif
    // Clear SDL's current-context bookkeeping too. No SDL video or window
    // function is called from the worker, even when its driver blocks.
    if(!SDL_GL_MakeCurrent(window,nullptr))throw std::runtime_error(SDL_GetError());
    return NativeContext(std::move(impl));
}
void NativeContext::bind() const {impl_->bind();}
void NativeContext::release() const {impl_->release();}
void NativeContext::swap() const {impl_->swap();}
const char* NativeContext::backend() const {return impl_->name;}
}
