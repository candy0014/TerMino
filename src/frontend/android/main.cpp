#include "mobile_ui.h"
#include "../opengl/render_thread.h"
#include "../opengl/input.h"
#include "../opengl/text_events.h"
#include "../opengl/viewport.h"
#include "../opengl/scroll_gesture.h"
#include "../opengl/native_file_dialog.h"
#include "tetr/input/action.h"
#include "tetr/replay/recent.h"
#include "tetr/render/audio_cues.h"
#include "tetr/util/runtime_directory_lock.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <filesystem>
#include <fstream>
#include <map>

namespace {
using namespace Graphical;
using namespace Graphical::Mobile;
struct Lifecycle {
    SDL_Window* window=nullptr;
    SDL_GLContext gl=nullptr;
    std::unique_ptr<RenderThread> renderer;
    Session* session=nullptr;
    bool background=false,rebuild=false;
    std::string error;
    static bool SDLCALL watch(void* data,SDL_Event* event) {
        auto& s=*static_cast<Lifecycle*>(data);
        try {
            // SDL queues this before it backs up GL and frees the surface.
            // Return the context from the worker to SDL main for that handoff.
            if(event->type==SDL_EVENT_WILL_ENTER_BACKGROUND) {
                s.renderer.reset();s.background=true;
                if(s.gl && !SDL_GL_MakeCurrent(s.window,s.gl))s.error=SDL_GetError();
                if(s.session)s.session->input().set_focused(false);
            }
            if(event->type==SDL_EVENT_DID_ENTER_FOREGROUND) {
                const auto restored=SDL_GL_GetCurrentContext();
                if(restored && s.gl && restored!=s.gl)SDL_GL_DestroyContext(s.gl);
                if(restored)s.gl=restored;
                s.background=false;s.rebuild=true;
                if(s.session)regain_focus(s.session->input());
            }
        }catch(const std::exception& e){s.error=e.what();}
        return true;
    }
};
struct WatchScope {
    Lifecycle& state;
    explicit WatchScope(Lifecycle& s):state(s){SDL_AddEventWatch(Lifecycle::watch,&state);}
    ~WatchScope(){SDL_RemoveEventWatch(Lifecycle::watch,&state);}
};
void extract_asset(const char* asset,const char* destination) {
    size_t size=0;void* bytes=SDL_LoadFile(asset,&size);
    if(!bytes)throw std::runtime_error(std::string("Missing bundled asset: ")+asset);
    std::ofstream file(destination,std::ios::binary|std::ios::trunc);
    file.write(static_cast<const char*>(bytes),size);SDL_free(bytes);
    if(!file)throw std::runtime_error("Could not initialize bundled asset");
}
struct ExportRequest {std::filesystem::path source;};
void SDLCALL export_complete(void* data,const char* const* files,int) {
    std::unique_ptr<ExportRequest> request(static_cast<ExportRequest*>(data));
    if(!files || !*files)return;
    auto* output=SDL_IOFromFile(*files,"wb");if(!output)return;
    std::ifstream input(request->source,std::ios::binary);char bytes[65536];bool ok=bool(input);
    while(input && ok) {
        input.read(bytes,sizeof(bytes));const auto count=input.gcount();
        if(count>0)ok=SDL_WriteIO(output,bytes,std::size_t(count))==std::size_t(count);
    }
    SDL_CloseIO(output);
    if(!ok || input.bad())SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Could not export replay");
}
void export_recent(SDL_Window* window) {
    const auto recent=Replay::recent_replays();if(recent.empty())return;
    SDL_ShowSaveFileDialog(export_complete,new ExportRequest{recent.front().path},
        window,nullptr,0,"termino-replay.txt");
}
}
int main(int,char**) {
    Lifecycle life;
    try {
        const auto* directory=SDL_GetAndroidInternalStoragePath();
        if(!directory)throw std::runtime_error(SDL_GetError());
        std::filesystem::current_path(directory);
        Util::RuntimeDirectoryLock lock(".tetr/client.lock",Util::RuntimeDirectoryLock::Access::Shared);
        Util::RuntimeDirectoryLock settingsLock(".tetr/settings.lock");
        if(!lock.acquired())throw std::runtime_error(lock.error());
        SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
        SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"0");
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
        extract_asset("certificates/cacert.pem","ca-bundle.pem");
        if(settingsLock.acquired()) {
            for(const char* name:{"maps.txt","presets.txt"}) {
                const auto destination=std::filesystem::path(".tetr")/name;
                if(!std::filesystem::exists(destination)) {
                    const auto asset=std::string("defaults/")+name;
                    const auto pending=destination.string()+".bundled.tmp";
                    extract_asset(asset.c_str(),pending.c_str());
                    std::filesystem::rename(pending,destination);
                }
            }
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        life.window=SDL_CreateWindow("TerMino",1200,800,SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if(!life.window)throw std::runtime_error(SDL_GetError());
        life.gl=SDL_GL_CreateContext(life.window);if(!life.gl)throw std::runtime_error(SDL_GetError());
        SDL_GL_SetSwapInterval(1);
        SDL_Log("TETR_ANDROID GLES %s / %s",glGetString(GL_VERSION),glGetString(GL_RENDERER));
        {
            UserConfig::Settings first,second;
            first.persistenceEnabled=second.persistenceEnabled=settingsLock.acquired();
            UserConfig::load_keybindings(first);UserConfig::load_startup_settings(first);
            UserConfig::load_player_two_keybindings(second);UserConfig::load_startup_settings(second,true);
            LaunchOptions options;options.audio=true;options.touchControls=true;
            options.playerTwoActive=UserConfig::load_active_player()==1;
            Session session(first,second,options);life.session=&session;
            WatchScope watch(life);EventClock clock;Mobile::Controls controls;
            Frontend::Touch::Router touches;TextEvents text;
            bool textActive=false,running=true,previousEditor=false,wasBackground=false,haveArea=false,editorRequested=false;
            std::uint64_t previousPage=0;
            Area previousArea;
            std::map<int,std::uint64_t> pulses;
            struct Gesture {
                Point start,last;std::optional<UiHit> hit;
                ScrollGesture scroll;
                std::optional<Command> command;
                std::uint64_t page=0;bool map=false,rail=false,dragged=false;
            };
            std::map<Frontend::Touch::Router::Finger,Gesture> gestures;
            const auto deliver=[&](const std::vector<Transition>& changes,std::uint64_t stamp) {
                for(const auto& t:changes)if(const int i=action_index(t.action);i>=0)
                    session.input().update(Input::virtual_action_key(std::size_t(i),t.player),t.down,clock.at(stamp));
            };
            const auto cancel=[&](bool all=false) {
                deliver(touches.cancel(),SDL_GetTicksNS());gestures.clear();controls.editor_up();
                if(all) {
                    for(const auto& p:pulses)session.input().update(p.first,false);
                    pulses.clear();
                }
            };
            const auto edit_controls=[&] {
                cancel(true);
                session.input().set_focused(false);session.input().set_focused(true);
                controls.edit();
            };
            const auto pulse=[&](int code) {
                session.input().update(code,true,clock.at(SDL_GetTicksNS()));
                pulses[code]=SDL_GetTicksNS()+45000000;
            };
            const auto key=[&](SDL_Scancode code){pulse(native_key(code));};
            const auto action=[&](std::size_t i){pulse(Input::virtual_action_key(i));};
            const auto command=[&](Command c,const SessionView& view) {
                if(c==Command::ToggleRail){controls.command(c);return;}
                if(controls.editing()) {
                    if(c==Command::Confirm)controls.accept_field();else controls.command(c);return;
                }
                if(session.text_input_active() && view.ui && view.ui->metadata.kind==Frontend::UiKind::Text) {
                    using T=Frontend::TextEditKind;
                    if(c==Command::Confirm){session.text_event({T::Confirm,{}});return;}
                    if(c==Command::Back){session.text_event({T::Cancel,{}});return;}
                    if(c==Command::Left){session.text_event({T::Left,{}});return;}
                    if(c==Command::Right){session.text_event({T::Right,{}});return;}
                    if(c==Command::Home){session.text_event({T::Home,{}});return;}
                    if(c==Command::End){session.text_event({T::End,{}});return;}
                    if(c==Command::Backspace){session.text_event({T::Backspace,{}});return;}
                    if(c==Command::Delete){session.text_event({T::Delete,{}});return;}
                    if(c==Command::SelectAll){session.text_event({T::SelectAll,{}});return;}
                    if(c==Command::Paste){char* s=SDL_GetClipboardText();if(s){session.text_event({T::Insert,s});SDL_free(s);}return;}
                }
                switch(c) {
                    case Command::Back:
                        if(!view.ui && view.status==SessionStatus::Playing)action(12);
                        else if(view.ui && (view.ui->metadata.kind==Frontend::UiKind::Menu ||
                            view.ui->metadata.kind==Frontend::UiKind::Start || view.ui->metadata.kind==Frontend::UiKind::Choices ||
                            view.ui->metadata.kind==Frontend::UiKind::Text || view.ui->metadata.kind==Frontend::UiKind::KeyCapture ||
                            view.ui->metadata.kind==Frontend::UiKind::MapEditor))
                            session.ui_command({Frontend::UiCommandKind::Back,0,{},view.ui->metadata.page});
                        else key(SDL_SCANCODE_ESCAPE);break;
                    case Command::Confirm:
                        if(content_page(view))key(SDL_SCANCODE_RETURN);
                        else if(view.replay)action(12);
                        else if(view.status==SessionStatus::Finished)action(8);
                        else key(SDL_SCANCODE_RETURN);break;
                    case Command::Settings:
                        if(view.ui && view.ui->metadata.kind==Frontend::UiKind::Start)
                            session.ui_command({Frontend::UiCommandKind::Activate,1,{},view.ui->metadata.page});
                        else if(gameplay_surface(view) || (view.ui && (view.ui->metadata.kind==Frontend::UiKind::Lobby ||
                            view.ui->metadata.kind==Frontend::UiKind::Statistics)))action(9);break;
                    case Command::Root:action(9);break;
                    case Command::SwitchTarget:key(SDL_SCANCODE_TAB);break;
                    case Command::Up:key(SDL_SCANCODE_UP);break;
                    case Command::Down:key(SDL_SCANCODE_DOWN);break;
                    case Command::Left:key(SDL_SCANCODE_LEFT);break;
                    case Command::Right:key(SDL_SCANCODE_RIGHT);break;
                    case Command::Tab:key(SDL_SCANCODE_TAB);break;
                    case Command::PageUp:key(SDL_SCANCODE_PAGEUP);break;
                    case Command::PageDown:key(SDL_SCANCODE_PAGEDOWN);break;
                    case Command::Undo:action(11);break;
                    case Command::Save:action(10);break;
                    case Command::Slower:key(SDL_SCANCODE_MINUS);break;
                    case Command::Faster:key(SDL_SCANCODE_EQUALS);break;
                    case Command::Layout:cancel();action(9);editorRequested=true;break;
                    case Command::OpenFile:
                        if(view.ui)if(const auto r=file_browse_command(view.ui->metadata))session.ui_command(*r);break;
                    case Command::Export:export_recent(life.window);break;
                    case Command::EndMatch:
                        key(SDL_SCANCODE_LCTRL);key(SDL_SCANCODE_LSHIFT);key(SDL_SCANCODE_F12);break;
                    case Command::MapOptions:key(SDL_SCANCODE_P);break;
                    case Command::MapSave:case Command::SavePreset:key(SDL_SCANCODE_S);break;
                    case Command::MapUndo:key(SDL_SCANCODE_LCTRL);key(SDL_SCANCODE_Z);break;
                    case Command::MapRedo:key(SDL_SCANCODE_LCTRL);key(SDL_SCANCODE_Y);break;
                    case Command::MapUndoSave:key(SDL_SCANCODE_F8);break;
                    case Command::Search:key(SDL_SCANCODE_SLASH);break;
                    case Command::Delete:key(SDL_SCANCODE_DELETE);break;
                    case Command::Backspace:key(SDL_SCANCODE_BACKSPACE);break;
                    case Command::Home:key(SDL_SCANCODE_HOME);break;
                    case Command::End:key(SDL_SCANCODE_END);break;
                    case Command::Undefined:key(SDL_SCANCODE_U);break;
                    case Command::Exit:key(SDL_SCANCODE_ESCAPE);break;
                    case Command::Retry:action(8);break;
                    default:break;
                }
            };
            complete_input_sample(session.input(),clock,SDL_GetTicksNS());
            SDL_Log("TETR_ANDROID READY");
            while(running) {
                if(!life.error.empty())throw std::runtime_error(life.error);
                const auto start=SDL_GetTicksNS();
                for(auto it=pulses.begin();it!=pulses.end();)
                    if(start>=it->second){session.input().update(it->first,false,clock.at(start));it=pulses.erase(it);}
                    else ++it;
                // Publish keyboard and touch together, after routing fingers.
                const auto events=poll_input_events(session.input(),clock,{},false,!controls.editing());
                const auto current=session.controls();
                SessionView view;view.status=current.status;view.ui=current.ui;view.message=current.message;
                view.replay=current.replay;view.spectator=current.spectator;view.room=current.room;
                view.actionEnabled=current.actionEnabled;view.switchTarget=current.switchTarget;view.endMatch=current.endMatch;
                if(view.status==SessionStatus::Closed)break;
                if(view.status==SessionStatus::Error)throw std::runtime_error(view.message);
                if(session.take_virtual_controls_editor())edit_controls();
                if(editorRequested && view.ui && !view.ui->overlay) {
                    editorRequested=false;edit_controls();
                }
                int w=0,h=0,lw=0,lh=0;
                SDL_GetWindowSizeInPixels(life.window,&w,&h);SDL_GetWindowSize(life.window,&lw,&lh);
                const auto viewport=Viewport::fit(lw,lh,true);
                SDL_Rect safe{0,0,lw,lh};SDL_GetWindowSafeArea(life.window,&safe);
                const double scale=std::max(.001f,viewport.scale);
                const double windowLeft=-viewport.x/scale;
                Area area{(safe.x-viewport.x)/scale,(safe.y-viewport.y)/scale,safe.w/scale,safe.h/scale};
                if(area.width<=0 || area.height<=0)area={0,0,1200,800};
                const auto page=view.ui?view.ui->metadata.page:0;
                if(previousPage!=page || previousEditor!=controls.editing() || wasBackground!=life.background ||
                    (haveArea && (area.x!=previousArea.x || area.y!=previousArea.y ||
                        area.width!=previousArea.width || area.height!=previousArea.height)))cancel(life.background);
                previousPage=page;previousEditor=controls.editing();wasBackground=life.background;
                previousArea=area;haveArea=true;
                auto frame=controls.frame(view,area,windowLeft);
                deliver(touches.cancel_unavailable(frame.layout),start);
                const bool gameplay=!editorRequested && !view.replay && !view.spectator && gameplay_surface(view) &&
                    (view.status==SessionStatus::Playing || view.status==SessionStatus::Paused || view.status==SessionStatus::Countdown);
                std::vector<Command> commands;std::vector<Frontend::UiCommand> uiCommands;
                bool keyboardRequested=false;
                std::uint64_t sampled=start;
                for(const auto& polled:events) {
                    const auto& e=polled.event;
                    if(e.type==SDL_EVENT_QUIT)running=false;
                    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST || e.type==SDL_EVENT_WILL_ENTER_BACKGROUND){cancel(true);text.reset();}
                    if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP)sampled=std::max(sampled,e.key.timestamp);
                    if(e.type==SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                        if(e.key.scancode==SDL_SCANCODE_AC_BACK)commands.push_back(Command::Back);
                        if(controls.text_active()) {
                            if(e.key.scancode==SDL_SCANCODE_BACKSPACE)controls.backspace();
                            if(e.key.scancode==SDL_SCANCODE_RETURN || e.key.scancode==SDL_SCANCODE_KP_ENTER)controls.accept_field();
                            if(e.key.scancode==SDL_SCANCODE_ESCAPE)controls.cancel_field();
                        } else if(controls.editing()) {
                            switch(e.key.scancode) {
                                case SDL_SCANCODE_ESCAPE:commands.push_back(Command::Back);break;
                                case SDL_SCANCODE_RETURN:commands.push_back(Command::Apply);break;
                                case SDL_SCANCODE_TAB:commands.push_back(Command::Properties);break;
                                case SDL_SCANCODE_PAGEUP:commands.push_back(Command::Previous);break;
                                case SDL_SCANCODE_PAGEDOWN:commands.push_back(Command::Next);break;
                                case SDL_SCANCODE_INSERT:commands.push_back(Command::Duplicate);break;
                                case SDL_SCANCODE_DELETE:commands.push_back(Command::Remove);break;
                                case SDL_SCANCODE_HOME:commands.push_back(Command::Reset);break;
                                case SDL_SCANCODE_P:commands.push_back(Command::Player);break;
                                case SDL_SCANCODE_H:commands.push_back(Command::Enabled);break;
                                default:break;
                            }
                        }
                    }
                    if(controls.editing()) {
                        if(e.type==SDL_EVENT_TEXT_INPUT && controls.text_active())controls.text(e.text.text?e.text.text:"");
                    } else if(session.text_input_active() && polled.acceptingInput)
                        text.consume(e,[&](Frontend::TextEditEvent edit){session.text_event(std::move(edit));},[]{
                            char* p=SDL_GetClipboardText();std::string s=p?p:"";SDL_free(p);return s;});
                    const bool finger=e.type==SDL_EVENT_FINGER_DOWN || e.type==SDL_EVENT_FINGER_UP ||
                        e.type==SDL_EVENT_FINGER_MOTION || e.type==SDL_EVENT_FINGER_CANCELED;
                    if(!finger)continue;
                    sampled=std::max(sampled,e.tfinger.timestamp);
                    const auto id=Frontend::Touch::Router::Finger{e.tfinger.touchID,e.tfinger.fingerID};
                    const Point p{(e.tfinger.x*lw-viewport.x)/scale,(e.tfinger.y*lh-viewport.y)/scale};
                    if(e.type==SDL_EVENT_FINGER_UP || e.type==SDL_EVENT_FINGER_CANCELED) {
                        deliver(touches.up(id),e.tfinger.timestamp);controls.editor_up();
                        if(const auto it=gestures.find(id);it!=gestures.end()) {
                            const auto g=it->second;
                            if(g.rail) {
                                if(e.type!=SDL_EVENT_FINGER_CANCELED && !g.dragged && g.page==page && g.command && rail_hit(frame,p)==g.command)
                                    commands.push_back(*g.command);
                            } else if(g.map)uiCommands.push_back({Frontend::UiCommandKind::MapEndStroke,0,{},g.page});
                            else if(e.type!=SDL_EVENT_FINGER_CANCELED && view.ui && g.page==page) {
                                if(!g.dragged && !g.scroll.dragged() && !g.scroll.scrollbar() && g.hit) {
                                    Frontend::UiCommand c{g.hit->back?Frontend::UiCommandKind::Back:g.hit->kind,
                                        g.hit->index,g.hit->ancestor,g.page};
                                    c.row=g.hit->row;c.column=g.hit->column;uiCommands.push_back(c);
                                }
                            }
                            gestures.erase(it);
                        }
                        continue;
                    }
                    if(!polled.acceptingInput || life.background)continue;
                    if(e.type==SDL_EVENT_FINGER_DOWN) {
                        if(frame.railVisible && !frame.rail.empty() && contains(frame.railViewport,float(p.x),float(p.y))) {
                            Gesture g;g.start=g.last=p;g.page=page;g.rail=true;g.command=rail_hit(frame,p);gestures[id]=g;continue;
                        }
                        if(const auto c=rail_hit(frame,p)){commands.push_back(*c);continue;}
                        const auto rail=frame.railBounds;
                        if(frame.railVisible && !frame.rail.empty() && p.x>=rail.x && p.x<rail.x+rail.w &&
                            p.y>=rail.y && p.y<rail.y+rail.h)continue;
                        bool inputHit=false;
                        if(textActive && life.renderer)if(const auto feedback=life.renderer->feedback();feedback && feedback->interaction.textRect) {
                            const auto b=*feedback->interaction.textRect;
                            inputHit=p.x>=b.x && p.x<b.x+b.w && p.y>=b.y && p.y<b.y+b.h;
                            keyboardRequested|=inputHit;
                        }
                        if(controls.editing()){controls.editor_down(p,area);continue;}
                        if(gameplay){deliver(touches.down(id,frame.layout,area,p),e.tfinger.timestamp);continue;}
                        Gesture g;g.start=g.last=p;g.page=page;
                        if(view.ui && life.renderer) {
                            const auto f=life.renderer->feedback();
                            if(f && f->width==w && f->height==h) {
                                if(!inputHit)g.hit=f->interaction.hit(float(p.x),float(p.y),view.ui->metadata);
                                g.scroll=ScrollGesture(f->interaction,view.ui->metadata,float(p.x),float(p.y));
                            }
                            if(g.hit && g.hit->kind==Frontend::UiCommandKind::MapPaint) {
                                g.map=true;
                                Frontend::UiCommand c{g.hit->kind,g.hit->index,g.hit->ancestor,page};
                                c.row=g.hit->row;c.column=g.hit->column;uiCommands.push_back(c);
                            }
                        }
                        gestures[id]=g;
                    } else if(e.type==SDL_EVENT_FINGER_MOTION) {
                        if(auto it=gestures.find(id);it!=gestures.end() && it->second.rail) {
                            auto& g=it->second;g.dragged|=std::hypot(p.x-g.start.x,p.y-g.start.y)>6;
                            if(g.dragged)controls.scroll_rail(float(g.last.y-p.y));
                            g.last=p;continue;
                        }
                        if(controls.editing()){controls.editor_move(p,area);continue;}
                        if(auto it=gestures.find(id);it!=gestures.end()) {
                            auto& g=it->second;g.last=p;g.dragged|=std::hypot(p.x-g.start.x,p.y-g.start.y)>6;
                            if(!g.map && life.renderer)if(auto movement=g.scroll.move(float(p.x),float(p.y)))life.renderer->scroll(*movement);
                            if(g.map && view.ui && life.renderer)
                                if(const auto f=life.renderer->feedback();f)
                                    if(const auto hit=f->interaction.hit(float(p.x),float(p.y),view.ui->metadata);
                                        hit && hit->kind==Frontend::UiCommandKind::MapPaint) {
                                        Frontend::UiCommand c{hit->kind,hit->index,hit->ancestor,page};
                                        c.row=hit->row;c.column=hit->column;c.alternate=true;uiCommands.push_back(c);
                                    }
                        }
                    }
                }
                complete_input_sample(session.input(),clock,sampled);
                for(const auto& c:uiCommands)session.ui_command(c);
                for(auto c:commands)command(c,view);
                if(auto request=session.take_file_dialog())show_file_dialog(life.window,std::move(request));
                const bool wantsText=controls.text_active() || (!controls.editing() && session.text_input_active());
                if(wantsText!=textActive || (wantsText && keyboardRequested)) {
                    textActive=wantsText;
                    if(textActive) {
                        const auto properties=SDL_CreateProperties();
                        SDL_SetBooleanProperty(properties,SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN,false);
                        SDL_SetBooleanProperty(properties,SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN,false);
                        SDL_SetNumberProperty(properties,SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER,SDL_CAPITALIZE_NONE);
                        if(controls.numeric_text())
                            SDL_SetNumberProperty(properties,SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER,0x3002); // signed decimal number
                        SDL_StartTextInputWithProperties(life.window,properties);SDL_DestroyProperties(properties);
                    }else SDL_StopTextInput(life.window);
                }
                if(life.background){SDL_Delay(5);continue;}
                FrameRequest request;request.width=w;request.height=h;request.presentable=true;
                request.fullWindow=true;
                request.touchControls=true;
                request.contentScale=Mobile::contentScale;
                request.minimumTextSize=Mobile::minimumTextSize;
                request.textActive=textActive;request.composition=text.composition;
                frame=controls.frame(view,area,windowLeft);
                request.surface=[frame](Renderer& r,const Frontend::UiFrame& ui){return Mobile::surface(r,ui,frame);};
                request.overlay=[frame,gameplay](Renderer& r){Mobile::overlay(r,frame,gameplay);};
                if(!life.renderer || life.rebuild) {
                    life.rebuild=false;
                    life.renderer=std::make_unique<RenderThread>(life.window,session,
                        "fonts/NotoSansCJKsc-Regular.otf",request,120,true);
                } else life.renderer->submit(request);
                if(const auto f=life.renderer->feedback();f && textActive && f->interaction.textRect) {
                    const auto b=*f->interaction.textRect;
                    SDL_Rect rect{viewport.screen_x(b.x),viewport.screen_y(b.y),int(b.w*scale),int(b.h*scale)};
                    SDL_SetTextInputArea(life.window,&rect,0);
                }
                if(life.renderer->finished())throw std::runtime_error("Renderer stopped");
                SDL_Delay(1);
            }
            cancel(true);life.renderer.reset();life.session=nullptr;
        }
        Render::shutdown_audio();
        SDL_GL_DestroyContext(life.gl);SDL_DestroyWindow(life.window);SDL_Quit();return 0;
    }catch(const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"TETR_ANDROID ERROR: %s",error.what());
        life.renderer.reset();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"TerMino",error.what(),life.window);
        Render::shutdown_audio();
        if(life.gl)SDL_GL_DestroyContext(life.gl);
        if(life.window)SDL_DestroyWindow(life.window);
        SDL_Quit();return 1;
    }
}
