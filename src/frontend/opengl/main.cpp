#include "renderer.h"
#include "input.h"
#include "tetr/frontend/graphical/session.h"
#include "tetr/input/action.h"
#include "tetr/runtime/piece.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace Graphical;
void block(Renderer& r,float x,float y,float size,Color color,bool ghost=false) {
    const float gap=std::min(1.f,size*.05f),inset=std::min(3.f,size*.12f);
    if(ghost) {r.outline(x+inset,y+inset,size-2*inset,size-2*inset,{color.r,color.g,color.b,.5f},std::min(1.f,size*.1f));return;}
    r.rect(x+gap,y+gap,size-2*gap,size-2*gap,{color.r*.32f,color.g*.32f,color.b*.32f});
    r.rect(x+inset,y+inset,size-2*inset,size-2*inset,color);
    r.rect(x+inset,y+inset,size-2*inset,std::min(2.f,size*.1f),{1,1,1,.22f});
}
void piece_preview(Renderer& r,Piece::Type type,float x,float y,float unit,bool available,bool obscured) {
    if(!type.valid()) return;
    const auto cells=Piece::preview_cells(type);
    int left=999,top=999,right=-999,bottom=-999;
    for(const auto& cell:cells) {top=std::min(top,cell[0]);left=std::min(left,cell[1]);bottom=std::max(bottom,cell[0]);right=std::max(right,cell[1]);}
    auto color=cell_color(Piece::metadata(type).colorId,obscured);
    if(!available) {color.r*=.4f;color.g*=.4f;color.b*=.4f;}
    for(const auto& cell:cells) block(r,x+(cell[1]-left-(right-left+1)/2.f)*unit,
        y+(cell[0]-top)*unit,unit,color);
}
std::string decimal(double value,int precision=2) {
    std::ostringstream out;out.setf(std::ios::fixed);out.precision(precision);out<<value;return out.str();
}
std::string wrapped(const std::string& value,std::size_t columns=95) {
    std::istringstream words(value);std::string word,result;std::size_t count=0;
    while(words>>word) {
        if(count && count+word.size()+1>columns) {result+='\n';count=0;}
        else if(count) {result+=' ';++count;}
        result+=word;count+=word.size();
    }
    return result;
}
const char* status_name(SessionStatus status) {
    switch(status) {
    case SessionStatus::Loading:return "LOADING";
    case SessionStatus::Countdown:return "GET READY";
    case SessionStatus::Paused:return "PAUSED";
    case SessionStatus::Finished:return "ROUND COMPLETE";
    case SessionStatus::Error:return "SESSION ERROR";
    default:return "LIVE";
    }
}
void background(Renderer& r) {
    for(int x=0;x<1200;x+=40) r.rect(x,0,1,800,{.10f,.21f,.3f,.16f});
    r.rect(0,0,1200,3,cyan);r.rect(0,797,1200,3,purple);
    r.text(36,25,"TerMino",32);r.text(226,38,"/ OPENGL PREVIEW",13,cyan);
    r.text(935,37,"PHASE 02    /    GL 3.3",13,muted);
}
void game_view(Renderer& r,const SessionView& view,const std::string& controls) {
    const auto& b=view.board;
    r.text(40,100,view.replay?"REPLAY DECK":"PLAY SESSION",14,cyan);
    r.text(40,129,status_name(view.status),20);
    if(!b.width || b.bottom<=0) {
        r.text(40,220,view.message.empty()?"Initializing shared game engine...":view.message,16);return;
    }
    // All visible and display-hidden rows fit. No fixed 10x20 assumption.
    const float unit=std::min({27.f,580.f/(b.bottom-b.firstRow),420.f/b.width});
    const float width=b.width*unit,height=(b.bottom-b.firstRow)*unit;
    const float x=600-width/2,y=110+(580-height)/2;
    r.rect(x-8,y-8,width+16,height+16,panelColor);
    r.outline(x-8,y-8,width+16,height+16,{.13f,.32f,.42f});
    for(int row=b.firstRow;row<b.bottom;++row) for(int col=0;col<b.width;++col) {
        const float px=x+col*unit,py=y+(row-b.firstRow)*unit;
        r.rect(px,py,unit-1,unit-1,row<0?Color{.027f,.043f,.073f}:Color{.035f,.065f,.095f});
        const auto& cell=b.at(row,col);
        if(cell.appearance!=Presentation::CellAppearance::Empty) {
            auto color=cell_color(cell.value.color_id(),b.obscuredColors);
            if(cell.appearance==Presentation::CellAppearance::Dimmed) {color.r*=.35f;color.g*=.35f;color.b*=.35f;}
            block(r,px,py,unit,color);
            if(cell.value.is_timed_number()) r.text(px+2,py,std::to_string(cell.value.timed_countdown()),unit*.55f);
        }
    }
    const auto drawPiece=[&](const Presentation::PieceView& p,bool ghost) {
        const auto piece=Piece::get_definition(p.type);
        auto color=cell_color(piece.color_id(),b.obscuredColors);
        if(!ghost && p.appearance==Presentation::CellAppearance::Dimmed) {color.r*=.35f;color.g*=.35f;color.b*=.35f;}
        for(const auto& cell:piece.cells(p.pose.rotation)) {
            const int row=p.pose.position.row+cell[0],col=p.pose.position.col+cell[1];
            if(row>=b.firstRow && row<b.bottom && col>=0 && col<b.width)
                block(r,x+col*unit,y+(row-b.firstRow)*unit,unit,color,ghost);
        }
    };
    if(b.ghost) drawPiece(*b.ghost,true);
    if(b.active) drawPiece(*b.active,false);
    r.rect(x,y-b.firstRow*unit,width,1,{.25f,.64f,.72f,.4f});
    if(b.holdEnabled) {
        r.text(x-170,y,"HOLD",14,b.holdAvailable?cyan:muted);
        piece_preview(r,b.hold,x-103,y+45,20,b.holdAvailable,b.obscuredColors);
    }
    r.text(x+width+42,y,"NEXT",14,cyan);
    const float step=std::min(94.f,500.f/std::max<std::size_t>(1,b.next.size()));
    for(std::size_t i=0;i<b.next.size();++i)
        piece_preview(r,b.next[i],x+width+100,y+42+i*step,std::min(20.f,step/4),true,b.obscuredColors);
    r.text(40,240,"TIME",12,muted);r.text(40,262,decimal(b.elapsed.seconds()),30);
    r.text(40,325,"SCORE",12,muted);r.text(40,346,std::to_string(b.score),25);
    r.text(40,405,"LINES / PIECES",12,muted);r.text(40,428,std::to_string(b.lines)+" / "+std::to_string(b.pieces),23);
    r.text(40,487,"ATTACK / B2B / COMBO",12,muted);
    r.text(40,510,std::to_string(b.attack)+" / "+std::to_string(b.b2b)+" / "+std::to_string(std::max(0,b.combo)),23);
    if(view.status==SessionStatus::Paused || view.status==SessionStatus::Finished) {
        r.rect(x-2,y+height*.4f,width+4,64,{.025f,.045f,.085f,.95f});
        r.outline(x-2,y+height*.4f,width+4,64,cyan);
        r.text(x+12,y+height*.4f+17,status_name(view.status),std::min(21.f,width/10),cyan);
    }
    if(view.replay) {
        const float ratio=view.replayDuration>0?std::clamp(view.replayPosition/view.replayDuration,0.,1.):0;
        r.rect(350,705,500,3,{.13f,.2f,.3f});r.rect(350,705,500*ratio,3,cyan);
        r.text(350,716,decimal(view.replayPosition)+" / "+decimal(view.replayDuration)+"s    "+decimal(view.replaySpeed)+"x",14);
        r.text(40,755,"Space/F2 pause    Left/Right seek 10s    -/+ speed    R restart    P/Esc menu    S save after finish",13,muted);
    } else r.text(32,756,controls,12,muted);
    if(!view.message.empty()) r.text(350,668,view.message,12,cyan);
}
struct Options {
    std::string dataDir,profile,replay,font,screenshot;
    int frames=0,smoke=0;
    bool play=false,vsync=true,hidden=false;
};
Options options(int argc,char** argv) {
    Options result;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        const auto value=[&]() -> std::string {if(++i>=argc) throw std::runtime_error("Missing value for "+arg);return argv[i];};
        if(arg=="--data-dir") result.dataDir=value();
        else if(arg=="--profile") result.profile=value();
        else if(arg=="--replay") result.replay=value();
        else if(arg=="--font") result.font=value();
        else if(arg=="--screenshot") result.screenshot=value();
        else if(arg=="--frames") {result.frames=std::stoi(value());result.hidden=true;}
        else if(arg=="--smoke-test") {result.smoke=1;result.vsync=false;}
        else if(arg=="--play") result.play=true;
        else if(arg=="--no-vsync") result.vsync=false;
        else if(arg=="--hidden") result.hidden=true;
        else if(arg=="--help") {
            std::cerr<<"TerMino OpenGL phase-2 preview\n"
                "--data-dir DIR  isolated runtime directory (settings are read only)\n"
                "--profile FILE  read original platform profile and bindings\n"
                "--replay FILE   open a single-board recording\n"
                "--play         start Zen immediately\n--font FILE    UTF-8 font override\n"
                "--no-vsync     render at up to 240 Hz\n"
                "--hidden       do not display the window (test/offscreen use)\n"
                "--frames N --screenshot FILE.bmp  bounded visual check\n"
                "--smoke-test   exercise SDL keyboard input and exit\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown argument: "+arg);
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
        // Resolve resources and explicitly supplied paths before changing CWD.
        const auto absolute=[](const std::string& path) {return std::filesystem::absolute(std::filesystem::u8path(path)).generic_u8string();};
        if(!opt.font.empty()) opt.font=absolute(opt.font);
        if(!opt.profile.empty()) opt.profile=absolute(opt.profile);
        if(!opt.replay.empty()) opt.replay=absolute(opt.replay);
        if(!opt.screenshot.empty()) opt.screenshot=absolute(opt.screenshot);
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)) throw std::runtime_error(SDL_GetError());
        if(opt.font.empty()) opt.font=std::string(SDL_GetBasePath())+"resources/fonts/DejaVuSans.ttf";
        if(!opt.dataDir.empty()) {std::filesystem::create_directories(std::filesystem::u8path(opt.dataDir));std::filesystem::current_path(std::filesystem::u8path(opt.dataDir));}
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        const SDL_WindowFlags flags=SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|
            (opt.hidden?SDL_WINDOW_HIDDEN:0);
        window=SDL_CreateWindow("TerMino / OpenGL Preview",1200,800,flags);
        if(!window) throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window,840,560);
        gl=SDL_GL_CreateContext(window);if(!gl) throw std::runtime_error(SDL_GetError());
        if(!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) || !GLAD_GL_VERSION_3_3)
            throw std::runtime_error("OpenGL 3.3 Core is required");
        std::cerr<<"TerMino GL: "<<glGetString(GL_VERSION)<<" / "<<glGetString(GL_RENDERER)
            <<" / video="<<SDL_GetCurrentVideoDriver()<<" / hidden="<<opt.hidden<<'\n';
        if(!SDL_GL_SetSwapInterval(opt.vsync?1:0) && opt.vsync) std::cerr<<"VSync unavailable; using frame limiter\n";
        {
            Renderer renderer(opt.font);
            UserConfig::Settings settings;settings.keyCodes=UserConfig::default_key_codes();
            if(!opt.profile.empty()) {
                if(!std::filesystem::is_regular_file(std::filesystem::u8path(opt.profile))) throw std::runtime_error("Profile does not exist");
                UserConfig::load(settings,opt.profile);settings.persistencePath.clear();
                // Phase 2 exposes single-player manual sessions. Full mode/Bot
                // menus are scheduled for phase 3, without changing the profile.
                settings.autoplayMode=0;settings.gameMode=0;settings.challengeRules=0;
            }
            const auto bindings=Input::BindingSet::from_key_codes(settings.keyCodes,settings.additionalKeyCodes);
            const auto controls=controls_text(settings);
            EventClock eventClock;
            std::unique_ptr<Session> session;
            const auto start=[&](int mode,const std::string& replay={}) {
                auto current=settings;current.gameMode=mode;
                session=std::make_unique<Session>(std::move(current),replay);
            };
            if(opt.play || !opt.replay.empty()) start(0,opt.replay);
            bool running=true,editing=false;
            int selected=0,frame=0,smokeStep=0;
            std::string path,composition,notice;
            auto smokeStart=std::chrono::steady_clock::now();
            while(running) {
                const auto frameStart=std::chrono::steady_clock::now();
                if(opt.smoke) {
                    const double elapsed=std::chrono::duration<double>(frameStart-smokeStart).count();
                    // Use the same SDL event delivery path as real keyboards.
                    static constexpr SDL_Scancode keys[]={SDL_SCANCODE_TAB,SDL_SCANCODE_TAB,SDL_SCANCODE_UP,SDL_SCANCODE_UP,
                        SDL_SCANCODE_RETURN,SDL_SCANCODE_RETURN,
                        SDL_SCANCODE_LEFT,SDL_SCANCODE_LEFT,SDL_SCANCODE_UP,SDL_SCANCODE_UP,
                        SDL_SCANCODE_SPACE,SDL_SCANCODE_SPACE,SDL_SCANCODE_C,SDL_SCANCODE_C};
                    if(smokeStep<14 && elapsed>.15+smokeStep*.10) {
                        SDL_Event key{};key.type=smokeStep%2?SDL_EVENT_KEY_UP:SDL_EVENT_KEY_DOWN;
                        key.key.scancode=keys[smokeStep];key.key.down=smokeStep%2==0;key.key.timestamp=SDL_GetTicksNS();
                        SDL_PushEvent(&key);++smokeStep;
                    }
                    if(elapsed>2.1) running=false;
                }
                SDL_Event event;
                while(SDL_PollEvent(&event)) {
                    if(event.type==SDL_EVENT_QUIT) running=false;
                    if(session && event.type==SDL_EVENT_WINDOW_FOCUS_LOST) session->input().set_focused(false);
                    if(session && event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) regain_focus(session->input());
                    if(editing) {
                        if(event.type==SDL_EVENT_TEXT_INPUT) {if(path.size()+std::char_traits<char>::length(event.text.text)<=4096) path+=event.text.text;composition.clear();}
                        if(event.type==SDL_EVENT_TEXT_EDITING) composition=event.edit.text;
                    }
                    if(event.type!=SDL_EVENT_KEY_DOWN && event.type!=SDL_EVENT_KEY_UP) continue;
                    if(event.key.repeat && !editing) continue;
                    const bool pressed=event.type==SDL_EVENT_KEY_DOWN;
                    const auto key=event.key.scancode;
                    if(session) {
                        if(pressed && key==SDL_SCANCODE_ESCAPE && bindings.action_for(native_key(key))==Input::Action::Unknown)
                            session->request_menu();
                        else {
                            deliver_key(session->input(),event.key,eventClock);
                            if(pressed && bindings.action_for(native_key(key))==Input::Action::SaveReplay) session->request_save();
                        }
                    } else if(pressed) {
                        if(editing) {
                            if(key==SDL_SCANCODE_ESCAPE) {editing=false;SDL_StopTextInput(window);composition.clear();}
                            else if(key==SDL_SCANCODE_BACKSPACE && !path.empty() && composition.empty()) {
                                do {const auto c=static_cast<unsigned char>(path.back());path.pop_back();if((c&0xc0)!=0x80) break;} while(!path.empty());
                            } else if(key==SDL_SCANCODE_V && (event.key.mod&SDL_KMOD_CTRL)) {
                                char* clipboard=SDL_GetClipboardText();if(clipboard) {if(path.size()+std::char_traits<char>::length(clipboard)<=4096) path+=clipboard;SDL_free(clipboard);}
                            } else if((key==SDL_SCANCODE_RETURN || key==SDL_SCANCODE_KP_ENTER) && !path.empty() && composition.empty()) {
                                SDL_StopTextInput(window);editing=false;start(5,path);
                            }
                        } else {
                            if(key==SDL_SCANCODE_UP || (key==SDL_SCANCODE_TAB && (event.key.mod&SDL_KMOD_SHIFT))) selected=(selected+4)%5;
                            if(key==SDL_SCANCODE_DOWN || (key==SDL_SCANCODE_TAB && !(event.key.mod&SDL_KMOD_SHIFT))) selected=(selected+1)%5;
                            if(key==SDL_SCANCODE_ESCAPE) running=false;
                            if(key==SDL_SCANCODE_RETURN || key==SDL_SCANCODE_KP_ENTER) {
                                if(selected<3) start(selected);
                                else if(selected==3) {editing=true;composition.clear();SDL_StartTextInput(window);}
                                else running=false;
                            }
                        }
                    }
                }
                SessionView view;
                if(session) {
                    view=session->view();
                    if(view.status==SessionStatus::Menu || view.status==SessionStatus::Error) {
                        notice=view.message;session.reset();
                    }
                }
                int width=0,height=0;SDL_GetWindowSizeInPixels(window,&width,&height);
                if(width>0 && height>0) {
                    renderer.begin(width,height);background(renderer);
                    if(session) game_view(renderer,view,controls);
                    else {
                        renderer.text(88,139,"ENTER THE GRID",41);
                        renderer.text(91,203,"One engine. Two frontends. Every move on the keyboard.",17,muted);
                        static constexpr const char* labels[]={"ZEN / FREE PLAY","SPRINT / 40 LINES","BLITZ / 120 SECONDS","OPEN REPLAY","EXIT"};
                        for(int i=0;i<5;++i) {
                            const float y=275+i*72;
                            renderer.rect(88,y,530,56,i==selected?Color{.05f,.15f,.2f}:panelColor);
                            if(i==selected) {renderer.outline(88,y,530,56,cyan,2);renderer.rect(88,y,4,56,cyan);}
                            renderer.text(110,y+16,(i==selected?">  ":"   ")+std::string(labels[i]),19,i==selected?cyan:textColor);
                        }
                        renderer.text(716,287,"PHASE 02",15,purple);
                        renderer.text(716,327,"Shared rules\nHold + Next + Ghost\nPause + Undo + Retry\nOriginal replay format\nNo mouse required",19);
                        renderer.text(716,531,"Full settings, challenges, bots,\nonline and two-player UI:\nphase 3. Terminal stays complete.",14,muted);
                        renderer.text(88,657,wrapped(notice),13,cyan);
                        renderer.text(88,751,"Up/Down or Tab: focus    Enter: select    Esc: back / exit",16,muted);
                    }
                    if(editing) {
                        renderer.rect(0,0,1200,800,{0,0,0,.72f});renderer.rect(130,250,940,260,panelColor);
                        renderer.outline(130,250,940,260,cyan,2);renderer.text(160,276,"OPEN REPLAY / PATH",22,cyan);
                        renderer.text(160,320,"Absolute path, ./relative path, or a name in .tetr/replay",16,muted);
                        renderer.outline(160,361,875,55,purple);
                        std::size_t first=path.size()>80?path.size()-80:0;
                        while(first<path.size() && (static_cast<unsigned char>(path[first])&0xc0)==0x80) ++first;
                        renderer.text(175,375,(first?"...":"")+path.substr(first)+"|"+composition,17);
                        renderer.text(160,449,"Enter: open    Esc: cancel    Backspace: edit    Ctrl+V: paste",16,muted);
                    }
                    renderer.end();
                    ++frame;
                    if(opt.frames && frame>=opt.frames) running=false;
                    if(!running && !opt.screenshot.empty()) renderer.screenshot(opt.screenshot,width,height);
                    SDL_GL_SwapWindow(window);
                }
                if(opt.smoke && !running) {
                    if(!session || view.board.pieces<1 || view.board.hold.valid()==false)
                        throw std::runtime_error("SDL smoke failed: drop/hold did not reach the shared engine");
                    std::cerr<<"OPENGL_SMOKE_OK pieces="<<view.board.pieces<<" hold="<<view.board.hold.index()<<'\n';
                }
                std::this_thread::sleep_until(frameStart+std::chrono::microseconds(4167));
            }
            session.reset();
        }
        SDL_GL_DestroyContext(gl);SDL_DestroyWindow(window);SDL_Quit();return 0;
    } catch(const std::exception& error) {
        std::cerr<<"TerMino OpenGL: "<<error.what()<<'\n';
        if(gl) SDL_GL_DestroyContext(gl);if(window) SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
}
