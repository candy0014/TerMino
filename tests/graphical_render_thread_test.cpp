// Opt-in real GL check. Hidden by default; --present requires a dedicated
// virtual display and an explicit environment guard, never the user's desktop.
#include "render_thread.h"
#include "renderer.h"
#include "input.h"
#include "tetr/frontend/graphical/session.h"
#include <SDL3/SDL_main.h>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
namespace {
template<class P> Graphical::SessionView wait(Graphical::Session& session,const Graphical::EventClock& clock,P predicate) {
    const auto deadline=std::chrono::steady_clock::now()+8s;
    while(std::chrono::steady_clock::now()<deadline) {
        Graphical::poll_input_events(session.input(),clock);
        auto view=session.view();
        if(view.status==Graphical::SessionStatus::Error)throw std::runtime_error(view.message);
        if(predicate(view))return view;
        SDL_WaitEventTimeout(nullptr,1);
    }
    throw std::runtime_error("Input/render test timed out");
}
void tap(SDL_Scancode key) {
    const auto time=SDL_GetTicksNS();
    for(bool down:{true,false}) {
        SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
        event.key.scancode=key;event.key.down=down;event.key.timestamp=time;
        assert(SDL_PushEvent(&event));
    }
}
}
int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"Usage: graphical_render_thread_test FONT [--present | --hidden-swap]\n";return 2;}
    const auto font=std::filesystem::absolute(std::filesystem::u8path(argv[1])).generic_u8string();
    const bool present=argc>2 && std::string(argv[2])=="--present";
    const bool hiddenSwap=argc>2 && std::string(argv[2])=="--hidden-swap";
    const bool swap=present || hiddenSwap;
    if(present && !std::getenv("TETR_TEST_ISOLATED_DISPLAY")) {
        std::cerr<<"--present requires TETR_TEST_ISOLATED_DISPLAY on a virtual display\n";return 2;
    }
    const auto original=std::filesystem::current_path();
    const auto directory=std::filesystem::temp_directory_path()/("termino-render-thread-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);std::filesystem::current_path(directory);
    SDL_Window* window=nullptr;SDL_GLContext gl=nullptr;
    try {
        assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
        if(hiddenSwap && std::string(SDL_GetCurrentVideoDriver())!="wayland")
            throw std::runtime_error("Hidden native swap check is Wayland-only");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        window=SDL_CreateWindow("TerMino isolated input/render check",800,600,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|(present?0:SDL_WINDOW_HIDDEN));
        if(!window)throw std::runtime_error(SDL_GetError());
        gl=SDL_GL_CreateContext(window);if(!gl)throw std::runtime_error(SDL_GetError());
        assert(gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)));
        assert(SDL_GL_SetSwapInterval(swap?1:0));
        UserConfig::Settings settings;settings.keyCodes=UserConfig::default_key_codes();settings.persistenceEnabled=false;
        settings.goal=0;settings.gameMode=0;settings.gravityInterval=10;settings.das=.09;settings.arr=.004;
        {
            Graphical::Session session(settings);Graphical::EventClock clock;
            Graphical::complete_input_sample(session.input(),clock,SDL_GetTicksNS());
            std::promise<void> blocked,release;auto entered=blocked.get_future();auto released=release.get_future().share();
            std::atomic<bool> first=true;
            Graphical::RenderThread renderer(window,session,font,{800,600,swap},30,swap,0,{},[&] {
                assert(!SDL_IsMainThread());
                if(first.exchange(false)) {
                    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
                    unsigned char pixel[4]{};glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    assert(glGetError()==GL_NO_ERROR && pixel[0]==255 && pixel[1]==0 && pixel[2]==0);
                    blocked.set_value();released.wait();
                }
            });
            struct Unblock {
                std::promise<void>& signal;
                ~Unblock() {try{signal.set_value();}catch(const std::future_error&) {}}
            } unblock{release};
            // Keep pumping even while the worker initializes its GL/font state.
            wait(session,clock,[&](const auto&){return entered.wait_for(0ms)==std::future_status::ready;});
            assert(!renderer.feedback());
            const auto playing=wait(session,clock,[](const auto& v){return v.status==Graphical::SessionStatus::Playing && v.board.active.has_value();});
            const int column=playing.board.active->pose.position.col;
            tap(SDL_SCANCODE_LEFT);
            wait(session,clock,[&](const auto& v){return v.board.active && v.board.active->pose.position.col==column-1;});
            // More than a full DAS passes while the renderer is still blocked.
            const auto until=std::chrono::steady_clock::now()+130ms;
            while(std::chrono::steady_clock::now()<until) {
                Graphical::poll_input_events(session.input(),clock);SDL_WaitEventTimeout(nullptr,1);
            }
            assert(session.view().board.active->pose.position.col==column-1 && !renderer.feedback());
            tap(SDL_SCANCODE_C);
            wait(session,clock,[](const auto& v){return v.board.hold.valid();});
            tap(SDL_SCANCODE_SPACE);
            wait(session,clock,[](const auto& v){return v.board.pieces>=1;});
            assert(!renderer.feedback());
            release.set_value();
            wait(session,clock,[&](const auto&){return bool(renderer.feedback());});
            SDL_SetWindowSize(window,960,640);
            int w=0,h=0,logicalW=0,logicalH=0;
            wait(session,clock,[&](const auto&) {
                SDL_GetWindowSize(window,&logicalW,&logicalH);
                SDL_GetWindowSizeInPixels(window,&w,&h);renderer.submit({w,h,swap});
                const auto f=renderer.feedback();return logicalW==960 && logicalH==640 && f && f->width==w && f->height==h;
            });
            renderer.stop();(void)renderer.feedback();
            std::cerr<<"RENDER_THREAD_INPUT_OK backend="<<renderer.backend()<<" present="<<present<<" nativeSwap="<<swap<<"\n";
        }
        SDL_GL_DestroyContext(gl);gl=nullptr;SDL_DestroyWindow(window);window=nullptr;SDL_Quit();
        std::filesystem::current_path(original);std::filesystem::remove_all(directory);return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        if(gl)SDL_GL_DestroyContext(gl);if(window)SDL_DestroyWindow(window);SDL_Quit();
        std::filesystem::current_path(original);std::filesystem::remove_all(directory);return 1;
    }
}
