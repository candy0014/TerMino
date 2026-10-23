#include "renderer.h"
#include "render_thread.h"
#include "scene.h"
#include "menu_pointer.h"
#include "native_file_dialog.h"
#include "input.h"
#include "text_events.h"
#include "window_config.h"
#include "viewport.h"
#include "scroll_gesture.h"
#include "tetr/util/runtime_directory_lock.h"
#include "tetr/frontend/graphical/session.h"
#include "tetr/input/action.h"
#include "tetr/runtime/piece.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/normalization.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <map>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace Graphical;
struct Options {
    std::string dataDir,profile,profileTwo,replay,font,screenshot;
    std::vector<std::string> bindings;
    int frames=0,smoke=0,frameLimit=0;
    std::optional<std::pair<int,int>> windowSize;
    bool migrateConfig=false,play=false,vsync=true,vsyncOverride=false,hidden=false,continuous=false;
};
Options options(int argc,char** argv) {
    Options result;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        const auto value=[&]() -> std::string {if(++i>=argc) throw std::runtime_error("Missing value for "+arg);return argv[i];};
        if(arg=="--data-dir") result.dataDir=value();
        else if(arg=="--profile") result.profile=value();
        else if(arg=="--profile2") result.profileTwo=value();
        else if(arg=="--qp-continuous-cancel")result.continuous=true;
        else if(arg=="--replay") result.replay=value();
        else if(arg=="--font") result.font=value();
        else if(arg=="--frame-limit") {result.frameLimit=std::stoi(value());if(result.frameLimit<30 || result.frameLimit>1000)throw std::runtime_error("--frame-limit requires 30..1000");}
        else if(arg=="--window-size") {
            const auto size=value();const auto x=size.find('x');
            if(x==std::string::npos)throw std::runtime_error("--window-size requires WIDTHxHEIGHT");
            const int w=std::stoi(size.substr(0,x)),h=std::stoi(size.substr(x+1));
            if(w<=0 || h<=0 || w>16384 || h>16384)throw std::runtime_error("Window dimensions must be positive and at most 16384x16384");
            result.windowSize=std::pair{w,h};
        }
        else if(arg=="--screenshot") result.screenshot=value();
        else if(arg=="--frames") {result.frames=std::stoi(value());result.hidden=true;}
        else if(arg=="--smoke-test") {result.smoke=1;result.vsync=false;result.vsyncOverride=true;}
        else if(arg=="--play") result.play=true;
        else if(arg=="--migrate-config")result.migrateConfig=true;
        else if(arg=="--no-vsync") {result.vsync=false;result.vsyncOverride=true;}
        else if(arg=="--vsync") {result.vsync=true;result.vsyncOverride=true;}
        else if(arg=="--hidden") result.hidden=true;
        else if(arg=="--help") {
            std::cerr<<"TerMino OpenGL / shared full application\n"
                "[player1-keybindings] [player2-keybindings] (same native key format as tetr)\n"
                "--data-dir DIR  isolated runtime directory\n"
                "--migrate-config  import profiles into .tetr/config.json and exit\n"
                "Start multiple windows with independent settings and shared scores/replays.\n"
                "Only the first window saves configuration; additional settings are temporary.\n"
                "--profile FILE  read original platform profile and bindings\n"
                "--profile2 FILE read Player 2 profile\n"
                "--replay FILE   open single-board or local-versus recording\n"
                "--play         start with saved configuration\n--font FILE    UTF-8 font override\n"
                "--qp-continuous-cancel  enable shared Quick Play cancellation penalties\n"
                "--vsync / --no-vsync  override saved vertical synchronization\n"
                "--window-size WIDTHxHEIGHT  override saved window geometry\n"
                "--frame-limit FPS  render limit 30..1000 (default 240)\n"
                "--hidden       do not display the window (test/offscreen use)\n"
                "--frames N --screenshot FILE.bmp  bounded visual check\n"
                "--smoke-test   exercise SDL keyboard input and exit\n";
            std::exit(0);
        } else if(arg.rfind("--",0)!=0 && result.bindings.size()<2)result.bindings.push_back(arg);
        else throw std::runtime_error("Unknown argument: "+arg);
    }
    if(result.frames<0) throw std::runtime_error("--frames must be nonnegative");
    if(result.smoke || result.frames) result.hidden=true;
    if(result.smoke && result.dataDir.empty()) throw std::runtime_error("--smoke-test requires an isolated --data-dir");
    return result;
}
std::string controls_text(const UserConfig::Settings& settings) {
    std::string result;
    static constexpr const char* labels[]={"left","right","soft","drop","CW","CCW","180","hold","retry","menu","save*","undo","pause"};
    for(std::size_t i=0;i<settings.keyCodes.size();++i) {
        if(i) result+="  ";
        std::string name="--";
        for(int s=1;s<SDL_SCANCODE_COUNT;++s) if(native_key(static_cast<SDL_Scancode>(s))==settings.keyCodes[i]) {
            name=SDL_GetScancodeName(static_cast<SDL_Scancode>(s));break;
        }
        result+=name+":"+labels[i];
    }
    return result;
}
}
int main(int argc,char** argv) {
    SDL_Window* window=nullptr;SDL_GLContext gl=nullptr;
    try {
        auto opt=options(argc,argv);
        const auto absolute=[](const std::string& path) {return std::filesystem::absolute(std::filesystem::u8path(path)).generic_u8string();};
        for(auto* path:{&opt.font,&opt.profile,&opt.profileTwo,&opt.replay,&opt.screenshot})if(!path->empty())*path=absolute(*path);
        for(auto& binding:opt.bindings)binding=absolute(binding);
        if(!opt.dataDir.empty()) {std::filesystem::create_directories(std::filesystem::u8path(opt.dataDir));std::filesystem::current_path(std::filesystem::u8path(opt.dataDir));}
        Util::RuntimeDirectoryLock directoryLock(".tetr/client.lock",Util::RuntimeDirectoryLock::Access::Shared);
        if(!directoryLock.acquired())throw std::runtime_error(directoryLock.error());
        Util::RuntimeDirectoryLock settingsLock(".tetr/settings.lock");
        const bool persistSettings=settingsLock.acquired();
        if(opt.migrateConfig) {if(!persistSettings || !UserConfig::migrate_client_config())throw std::runtime_error("Could not migrate client settings; existing files were preserved.");return 0;}
        auto windowConfig=load_window_config(UserConfig::kClientConfigPath);
        if(opt.windowSize){windowConfig.width=opt.windowSize->first;windowConfig.height=opt.windowSize->second;windowConfig.maximized=false;}
        if(opt.frameLimit)windowConfig.frameLimit=opt.frameLimit;
        if(opt.vsyncOverride)windowConfig.vsync=opt.vsync;
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
        if(opt.font.empty())opt.font=std::string(SDL_GetBasePath())+"resources/fonts/NotoSansCJKsc-Regular.otf";
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        const SDL_WindowFlags flags=SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(opt.hidden?SDL_WINDOW_HIDDEN:0)|(!opt.hidden && windowConfig.maximized?SDL_WINDOW_MAXIMIZED:0);
        window=SDL_CreateWindow("TerMino",windowConfig.width,windowConfig.height,flags);
        if(!window)throw std::runtime_error(SDL_GetError());
        gl=SDL_GL_CreateContext(window);if(!gl)throw std::runtime_error(SDL_GetError());
        if(!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) || !GLAD_GL_VERSION_3_3)throw std::runtime_error("OpenGL 3.3 Core is required");
        std::cerr<<"TerMino GL: "<<glGetString(GL_VERSION)<<" / "<<glGetString(GL_RENDERER)<<" / video="<<SDL_GetCurrentVideoDriver()<<" / hidden="<<opt.hidden<<'\n';
        SDL_GL_SetSwapInterval(windowConfig.vsync?1:0);
        {
            EventClock eventClock;
            UserConfig::Settings first,second;
            first.persistenceEnabled=persistSettings;
            UserConfig::load_keybindings(first,opt.bindings.empty()?UserConfig::kDefaultKeybindingPath:opt.bindings[0]);
            const auto profile=[](const char* name){const char* p=std::getenv(name);return p?std::string(p):std::string{};};
            UserConfig::load_startup_settings(first,false,opt.profile.empty()?profile("TETR_USER_CONFIG"):opt.profile);
            if(!opt.profile.empty())first.persistencePath.clear();
            second=first;
            UserConfig::load_player_two_keybindings(second,opt.bindings.size()<2?UserConfig::kDefaultPlayerTwoKeybindingPath:opt.bindings[1]);
            UserConfig::load_startup_settings(second,true,opt.profileTwo.empty()?profile("TETR_USER_CONFIG_PLAYER2"):opt.profileTwo);
            if(!opt.profileTwo.empty())second.persistencePath.clear();
            if(!persistSettings)SDL_SetWindowTitle(window,first.uiLanguage?"TerMino - Session settings":"TerMino - 临时设置");
            if(!opt.replay.empty()) {first.gameMode=5;first.replayPath=opt.replay;}
            LaunchOptions launch;launch.startScreen=!opt.play && opt.replay.empty();launch.continuousCancellation=opt.continuous;launch.audio=!opt.hidden;
            if(opt.profile.empty() && !std::getenv("TETR_USER_CONFIG")) {launch.playerTwoActive=UserConfig::load_active_player()==1;}
            Session session(std::move(first),std::move(second),launch);
            complete_input_sample(session.input(),eventClock,SDL_GetTicksNS());
            FrameRequest request;int logicalW=0,logicalH=0;
            const auto geometry=[&] {
                SDL_GetWindowSizeInPixels(window,&request.width,&request.height);
                SDL_GetWindowSize(window,&logicalW,&logicalH);
                const auto flags=SDL_GetWindowFlags(window);
                request.presentable=(flags&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED|SDL_WINDOW_OCCLUDED))==0;
            };
            geometry();
            RenderThread renderer(window,session,opt.font,request,windowConfig.frameLimit,windowConfig.vsync,opt.frames,opt.screenshot);
            std::cerr<<"TerMino GL: rendering="<<renderer.backend()<<" worker / SDL input=event-driven main thread\n";
            bool running=true,textActive=false;int smokeStep=0,imeFrame=-1;
            TextEvents textEvents;MenuPointer pointer;auto& composition=textEvents.composition;const auto smokeStart=std::chrono::steady_clock::now();
            bool mapDrawing=false;std::optional<std::pair<int,int>> lastPaint;
            std::optional<ScrollGesture> mouseScroll;
            struct TouchGesture {ScrollGesture scroll;std::optional<UiHit> hit;std::uint64_t page=0;float x=0,y=0;bool dragged=false;};
            std::map<std::pair<SDL_TouchID,SDL_FingerID>,TouchGesture> fingers;
            const auto cursorDeleter=[](SDL_Cursor* cursor){if(cursor)SDL_DestroyCursor(cursor);};
            std::unique_ptr<SDL_Cursor,decltype(cursorDeleter)> hand(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER),cursorDeleter),
                crosshair(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR),cursorDeleter);
            while(running) {
                if(auto dialog=session.take_file_dialog())show_file_dialog(window,std::move(dialog));
                const auto frameStart=std::chrono::steady_clock::now();
                const bool requested=session.text_input_active();
                if(requested!=textActive) {
                    textActive=requested;composition.clear();
                    if(textActive)SDL_StartTextInput(window);else SDL_StopTextInput(window);
                }
                if(opt.smoke) {
                    const double elapsed=std::chrono::duration<double>(frameStart-smokeStart).count();
                    static constexpr SDL_Scancode keys[]={SDL_SCANCODE_TAB,SDL_SCANCODE_TAB,SDL_SCANCODE_UP,SDL_SCANCODE_UP,
                        SDL_SCANCODE_RETURN,SDL_SCANCODE_RETURN,SDL_SCANCODE_LEFT,SDL_SCANCODE_LEFT,SDL_SCANCODE_UP,SDL_SCANCODE_UP,
                        SDL_SCANCODE_SPACE,SDL_SCANCODE_SPACE,SDL_SCANCODE_C,SDL_SCANCODE_C};
                    if(smokeStep<14 && elapsed>.15+smokeStep*.10) {
                        SDL_Event event{};event.type=smokeStep%2?SDL_EVENT_KEY_UP:SDL_EVENT_KEY_DOWN;
                        event.key.scancode=keys[smokeStep];event.key.down=smokeStep%2==0;event.key.timestamp=SDL_GetTicksNS();SDL_PushEvent(&event);++smokeStep;
                    }
                    if(elapsed>2.5)running=false;
                }
                // Physical input is committed before slower UI/clipboard work.
                // Neither rendering nor its rate/VSync sleeps runs here.
                const auto events=poll_input_events(session.input(),eventClock,[&] {pointer.cancel(session.input());});
                geometry();
                for(const auto& polled:events) {
                    const auto& event=polled.event;
                    if(event.type==SDL_EVENT_QUIT)running=false;
                    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){textEvents.reset();mapDrawing=false;lastPaint.reset();mouseScroll.reset();fingers.clear();}
                    if(event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT)mouseScroll.reset();
                    if(event.type==SDL_EVENT_FINGER_DOWN || event.type==SDL_EVENT_FINGER_MOTION ||
                        event.type==SDL_EVENT_FINGER_UP || event.type==SDL_EVENT_FINGER_CANCELED) {
                        const auto id=std::pair{event.tfinger.touchID,event.tfinger.fingerID};
                        const auto current=session.controls();const auto feedback=renderer.feedback();
                        const auto viewport=Viewport::fit(logicalW,logicalH);
                        if(!polled.acceptingInput || !current.ui || !feedback || viewport.scale<=0 ||
                            feedback->width!=request.width || feedback->height!=request.height){fingers.erase(id);continue;}
                        const auto& m=current.ui->metadata;
                        const float x=(event.tfinger.x*logicalW-viewport.x)/viewport.scale,y=(event.tfinger.y*logicalH-viewport.y)/viewport.scale;
                        if(event.type==SDL_EVENT_FINGER_DOWN) {
                            fingers[id]={ScrollGesture(feedback->interaction,m,x,y),feedback->interaction.hit(x,y,m),m.page,x,y,false};
                            if(textActive && feedback->interaction.textRect && contains(*feedback->interaction.textRect,x,y))fingers[id].hit.reset();
                        }
                        if(const auto it=fingers.find(id);it!=fingers.end()) {
                            auto& g=it->second;
                            if(g.page!=m.page){fingers.erase(it);continue;}
                            const bool paint=g.hit && g.hit->kind==Frontend::UiCommandKind::MapPaint;
                            if(event.type==SDL_EVENT_FINGER_MOTION) {
                                g.dragged|=std::hypot(x-g.x,y-g.y)>6;
                                if(!paint)if(auto movement=g.scroll.move(x,y))renderer.scroll(*movement);
                            }
                            const auto hit=paint?feedback->interaction.hit(x,y,m):g.hit;
                            if(hit && ((paint && (event.type==SDL_EVENT_FINGER_DOWN || event.type==SDL_EVENT_FINGER_MOTION)) ||
                                (event.type==SDL_EVENT_FINGER_UP && !g.dragged && !g.scroll.scrollbar()))) {
                                Frontend::UiCommand command{hit->back?Frontend::UiCommandKind::Back:hit->kind,hit->index,hit->ancestor,m.page};
                                command.row=hit->row;command.column=hit->column;command.alternate=event.type==SDL_EVENT_FINGER_MOTION;
                                session.ui_command(command);
                            }
                            if(event.type==SDL_EVENT_FINGER_UP || event.type==SDL_EVENT_FINGER_CANCELED) {
                                if(paint)session.ui_command({Frontend::UiCommandKind::MapEndStroke,0,{},m.page});
                                fingers.erase(it);
                            }
                        }
                        continue;
                    }
                    if(event.type==SDL_EVENT_MOUSE_MOTION && mouseScroll) {
                        const auto viewport=Viewport::fit(logicalW,logicalH);
                        if(polled.acceptingInput && viewport.scale>0)
                            if(auto movement=mouseScroll->move((event.motion.x-viewport.x)/viewport.scale,(event.motion.y-viewport.y)/viewport.scale))renderer.scroll(*movement);
                        continue;
                    }
                    if(event.type==SDL_EVENT_KEY_DOWN && event.key.scancode==SDL_SCANCODE_F4 && !event.key.repeat &&
                        polled.acceptingInput && composition.empty()) {
                        const auto current=session.controls();
                        if(current.ui)if(const auto command=file_browse_command(current.ui->metadata))session.ui_command(*command);
                    }
                    if(textActive && polled.acceptingInput)textEvents.consume(event,
                        [&](Frontend::TextEditEvent edit){session.text_event(std::move(edit));},[] {
                            char* text=SDL_GetClipboardText();std::string value=text?text:"";SDL_free(text);return value;
                        });
                    if((event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) ||
                       (event.type==SDL_EVENT_MOUSE_MOTION && mapDrawing && !(event.motion.state&SDL_BUTTON_LMASK))) {
                        mapDrawing=false;lastPaint.reset();
                        const auto current=session.controls();
                        if(current.ui)session.ui_command({Frontend::UiCommandKind::MapEndStroke,0,{},current.ui->metadata.page});
                    }
                    if((event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_WHEEL ||
                        (event.type==SDL_EVENT_MOUSE_MOTION && mapDrawing)) && polled.acceptingInput) {
                        const auto current=session.controls();
                        if(current.ui) {
                            const auto& metadata=current.ui->metadata;
                            const auto viewport=Viewport::fit(logicalW,logicalH);
                            const auto feedback=renderer.feedback();
                            if(viewport.scale<=0 || !feedback || feedback->width!=request.width || feedback->height!=request.height)continue;
                            if(event.type==SDL_EVENT_MOUSE_WHEEL) {
                                if(const auto area=feedback->interaction.scroll((event.wheel.mouse_x-viewport.x)/viewport.scale,
                                    (event.wheel.mouse_y-viewport.y)/viewport.scale,metadata);area && event.wheel.y!=0) {
                                    if(const auto command=pointer.editor_wheel(metadata,*area,event.wheel.y))session.ui_command(*command);
                                } else if(const auto area=feedback->interaction.menu_scroll((event.wheel.mouse_x-viewport.x)/viewport.scale,
                                    (event.wheel.mouse_y-viewport.y)/viewport.scale,metadata);area && event.wheel.y!=0) {
                                    renderer.scroll({metadata.page,*area,-event.wheel.y*3});
                                } else if(!textActive)pointer.wheel(session.input(),metadata,event.wheel.y,SDL_GetTicks());
                            } else if(event.type==SDL_EVENT_MOUSE_MOTION || event.button.button==SDL_BUTTON_LEFT) {
                                const bool motion=event.type==SDL_EVENT_MOUSE_MOTION;
                                const float x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
                                if(!motion) {
                                    if(textActive && feedback->interaction.textRect && contains(*feedback->interaction.textRect,(x-viewport.x)/viewport.scale,(y-viewport.y)/viewport.scale))continue;
                                    ScrollGesture scroll(feedback->interaction,metadata,(x-viewport.x)/viewport.scale,(y-viewport.y)/viewport.scale);
                                    if(scroll.scrollbar()){mouseScroll=scroll;continue;}
                                }
                                if(const auto hit=feedback->interaction.hit((x-viewport.x)/viewport.scale,(y-viewport.y)/viewport.scale,metadata)) {
                                    if(motion && hit->kind!=Frontend::UiCommandKind::MapPaint)continue;
                                    pointer.cancel(session.input());
                                    Frontend::UiCommand command{hit->back?Frontend::UiCommandKind::Back:hit->kind,hit->index,hit->ancestor,metadata.page};
                                    command.row=hit->row;command.column=hit->column;
                                    command.alternate=motion || (!motion && event.button.clicks>=2);
                                    if(hit->kind==Frontend::UiCommandKind::MapPaint) {
                                        const auto cell=std::pair{hit->row,hit->column};
                                        if(motion && lastPaint==cell)continue;
                                        command.alternate=motion;mapDrawing=true;lastPaint=cell;
                                    } else {mapDrawing=false;lastPaint.reset();}
                                    session.ui_command(command);
                                }
                            }
                        }
                    }
                }
                const auto view=session.controls();
                pointer.update(session.input(),view.ui?&view.ui->metadata:nullptr,SDL_GetTicks());
                if(view.status==SessionStatus::Closed)running=false;
                if(view.status==SessionStatus::Error)throw std::runtime_error(view.message);
                request.textActive=textActive;request.composition=composition;
                request.pointer.reset();SDL_Cursor* wanted=SDL_GetDefaultCursor();
                if(SDL_GetMouseFocus()==window && session.input().accepting_input()) {
                    float x=0,y=0;SDL_GetMouseState(&x,&y);const auto viewport=Viewport::fit(logicalW,logicalH);
                    if(viewport.scale>0) {
                        request.pointer=std::array<float,2>{(x-viewport.x)/viewport.scale,(y-viewport.y)/viewport.scale};
                        const auto feedback=renderer.feedback();
                        if(view.ui && feedback && feedback->width==request.width && feedback->height==request.height)
                            if(const auto hit=feedback->interaction.hit((*request.pointer)[0],(*request.pointer)[1],view.ui->metadata))
                                wanted=hit->kind==Frontend::UiCommandKind::MapPaint?crosshair.get():hand.get();
                    }
                }
                if(wanted && SDL_GetCursor()!=wanted)SDL_SetCursor(wanted);
                renderer.submit(request);
                if(const auto feedback=renderer.feedback();feedback && textActive && feedback->frames!=imeFrame &&
                    feedback->width==request.width && feedback->height==request.height) {
                    imeFrame=feedback->frames;
                    if(const auto field=feedback->interaction.textRect) {
                        const float x=std::min(field->x,780.f),y=field->y;
                        const auto logical=Viewport::fit(logicalW,logicalH);
                        SDL_Rect area{logical.screen_x(x),logical.screen_y(y),std::max(1,int(400*logical.scale)),std::max(1,int(24*logical.scale))};
                        SDL_SetTextInputArea(window,&area,0);
                    }
                }
                if(renderer.finished())running=false;
                if(opt.smoke && !running) {
                    if(view.pieces<1 || !view.hold.valid())throw std::runtime_error("SDL smoke failed: shared engine did not receive drop/Hold");
                    std::cerr<<"OPENGL_SMOKE_OK pieces="<<view.pieces<<" hold="<<view.hold.index()<<'\n';
                }
                // Wake for input as soon as it is queued. The 1 ms idle bound
                // also publishes held-key time without tying it to a frame.
                if(running)SDL_WaitEventTimeout(nullptr,1);
            }
            renderer.stop();(void)renderer.feedback();
            if(textActive)SDL_StopTextInput(window);
        }
        if(!opt.hidden) {
            windowConfig.maximized=(SDL_GetWindowFlags(window)&SDL_WINDOW_MAXIMIZED)!=0;
            if(!windowConfig.maximized)SDL_GetWindowSize(window,&windowConfig.width,&windowConfig.height);
        }
        if(persistSettings && !save_window_config(UserConfig::kClientConfigPath,windowConfig))std::cerr<<"Could not save graphical window settings.\n";
        SDL_GL_DestroyContext(gl);SDL_DestroyWindow(window);SDL_Quit();return 0;
    } catch(const std::exception& error) {
        std::cerr<<"TerMino OpenGL: "<<error.what()<<'\n';
        if(gl)SDL_GL_DestroyContext(gl);if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
}
