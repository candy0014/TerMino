#include "renderer.h"
#include "input.h"
#include "tetr/frontend/graphical/session.h"
#include "tetr/input/action.h"
#include "tetr/runtime/piece.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/normalization.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <chrono>
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
void block(Renderer& r,float x,float y,float size,Color color,bool ghost=false,int style=0) {
    const float gap=std::min(1.f,size*.05f),inset=std::min(3.f,size*.12f);
    const float w=size*(style?.65f:1.f);x+=(size-w)/2;
    if(ghost) {r.outline(x+inset,y+inset,w-2*inset,size-2*inset,{color.r,color.g,color.b,.5f},std::min(1.f,size*.1f));return;}
    r.rect(x+gap,y+gap,w-2*gap,size-2*gap,{color.r*.32f,color.g*.32f,color.b*.32f});
    r.rect(x+inset,y+inset,w-2*inset,size-2*inset,color);
    r.rect(x+inset,y+inset,w-2*inset,std::min(2.f,size*.1f),{1,1,1,.22f});
}
void piece_preview(Renderer& r,Piece::Type type,float x,float y,float unit,bool available,bool obscured,int style) {
    if(!type.valid()) return;
    const auto cells=Piece::preview_cells(type);
    int left=999,top=999,right=-999,bottom=-999;
    for(const auto& cell:cells) {top=std::min(top,cell[0]);left=std::min(left,cell[1]);bottom=std::max(bottom,cell[0]);right=std::max(right,cell[1]);}
    auto color=cell_color(Piece::metadata(type).colorId,obscured);
    if(!available) {color.r*=.4f;color.g*=.4f;color.b*=.4f;}
    for(const auto& cell:cells) block(r,x+(cell[1]-left-(right-left+1)/2.f)*unit,
        y+(cell[0]-top)*unit,unit,color,false,style);
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
std::string spin_history(const Presentation::BoardView& b) {
    std::string text;constexpr const char* names[]={"IJLOSTZ-","ijlostz-"};
    for(const auto& operation:b.spinHistory) {
        if(!text.empty())text+=' ';
        text+=names[operation.second&1][operation.first&7];
        text+=std::to_string((operation.first>>3)&7);
    }
    return text;
}
void garbage_bar(Renderer& r,const Presentation::BoardView& b,float x,float bottom,float unit,float height) {
    float used=0;
    for(std::size_t i=0;i<b.garbage.size();++i) {
        const float h=std::min(float(b.garbage[i])*unit,height-used);if(h<=0)break;
        const double delay=i<b.garbageReadyIn.size()?b.garbageReadyIn[i]:0;
        const Color color=b.obscuredColors?muted:delay>.3?Color{1,.82f,.2f}:delay>0?Color{.5f,.12f,.16f}:Color{1,.25f,.35f};
        r.rect(x,bottom-used-h,3,h,color);used+=h;
    }
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
    r.text(935,37,"PHASE 03    /    GL 3.3",13,muted);
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
        if(cell.hint)r.rect(px+unit*.4f,py+unit*.4f,unit*.2f,unit*.2f,muted);
        if(cell.appearance!=Presentation::CellAppearance::Empty) {
            auto color=cell_color(cell.value.color_id(),b.obscuredColors);
            if(cell.appearance==Presentation::CellAppearance::Dimmed) {color.r*=.35f;color.g*=.35f;color.b*=.35f;}
            block(r,px,py,unit,color,false,b.squareStyle);
            if(cell.hint)r.rect(px+unit*.4f,py+unit*.4f,unit*.2f,unit*.2f,panelColor);
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
                block(r,x+col*unit,y+(row-b.firstRow)*unit,unit,color,ghost,b.squareStyle);
        }
    };
    if(b.ghost) drawPiece(*b.ghost,true);
    if(b.active) drawPiece(*b.active,false);
    r.rect(x,y-b.firstRow*unit,width,1,{.25f,.64f,.72f,.4f});
    if(b.holdEnabled) {
        r.text(x-170,y,"HOLD",14,b.holdAvailable?cyan:muted);
        piece_preview(r,b.hold,x-103,y+45,20,b.holdAvailable,b.obscuredColors,b.squareStyle);
    }
    r.text(x+width+42,y,"NEXT",14,cyan);
    const float step=std::min(94.f,500.f/std::max<std::size_t>(1,b.next.size()));
    for(std::size_t i=0;i<b.next.size();++i)
        piece_preview(r,b.next[i],x+width+100,y+42+i*step,std::min(20.f,step/4),true,b.obscuredColors,b.squareStyle);
    r.text(40,240,"TIME",12,muted);r.text(40,262,decimal(b.goal==Runtime::GameGoal::Time?std::max(0.,b.goalTarget-b.elapsed.seconds()):b.elapsed.seconds()),30);
    r.text(40,325,"SCORE",12,muted);r.text(40,346,std::to_string(b.score),25);
    r.text(40,405,"LINES / PIECES",12,muted);r.text(40,428,std::to_string(b.lines)+" / "+std::to_string(b.pieces),23);
    r.text(40,487,"ATTACK / B2B / COMBO",12,muted);
    r.text(40,510,std::to_string(b.attack)+" / "+std::to_string(std::max(0,b.b2b-1))+" / "+std::to_string(std::max(0,b.combo-1)),23);
    if(view.status==SessionStatus::Paused || view.status==SessionStatus::Finished) {
        r.rect(x-2,y+height*.4f,width+4,64,{.025f,.045f,.085f,.95f});
        r.outline(x-2,y+height*.4f,width+4,64,cyan);
        r.text(x+12,y+height*.4f+17,status_name(view.status),std::min(21.f,width/10),cyan);
    }
    garbage_bar(r,b,x-5,y+height,unit,height);
    float hud=565;
    if(b.goalTarget) {r.text(40,hud,"GOAL "+std::to_string(b.goalProgress)+" / "+std::to_string(b.goalTarget),16,cyan);hud+=25;}
    if(b.quickPlay) {r.text(40,hud,"ALT "+decimal(b.altitude,1)+"  FLOOR "+std::to_string(b.floor),14,cyan);hud+=24;r.text(40,hud,"BOOST "+decimal(b.booster,1)+"  XP "+decimal(b.experience,1),14);hud+=24;}
    if(b.windup) {r.text(40,hud,"WINDUP "+std::to_string(b.windup),16,{1,.4f,.5f});hud+=24;}
    if(b.spinPiece.valid()) {r.text(900,175,std::string(Piece::metadata(b.spinPiece).name)+"-SPIN"+(b.mini?" MINI":""),17,purple);}
    if(b.clearLines)r.text(900,207,b.clearLines<0?"VOID":std::to_string(b.clearLines)+" LINES",17,cyan);
    r.text(900,250,"PPS "+decimal(b.elapsed.seconds()>0?b.pieces/b.elapsed.seconds():0),15);
    r.text(900,281,"APM "+decimal(b.elapsed.seconds()>0?b.attack*60/b.elapsed.seconds():0),15);
    if(b.perfectClear || b.halfClear)r.text(900,320,b.perfectClear?"ALL CLEAR":"HALF CLEAR",18,purple);
    if(b.clutch)r.text(900,360,"CLUTCH "+std::to_string(b.clutch),16,cyan);
    if(b.blighted)r.text(900,394,"BLIGHTED",16,purple);
    if(b.spike)r.text(900,427,"SPIKE "+std::to_string(b.spike),16,cyan);
    if(!b.headline.empty())r.text(350,72,b.headline,18,cyan);
    if(!view.challengeNotice.empty())r.text(40,708,view.challengeNotice,12,purple);
    if(b.population)r.text(900,465,"ALIVE "+std::to_string(b.rank)+" / "+std::to_string(b.population),16,cyan);
    if(!b.stageTask.empty())r.text(900,465,"STAGE "+std::to_string(b.stages)+"/4  "+b.stageTask,13,cyan);
    if(!b.spinHistory.empty())r.text(900,490,wrapped(spin_history(b),20),13,purple);
    if(view.status==SessionStatus::Finished)r.text(900,580,"Tab: Statistics\nS: Save As\nR: Retry\nP: Settings",15,muted);
    if(view.replay) {
        const float ratio=view.replayDuration>0?std::clamp(view.replayPosition/view.replayDuration,0.,1.):0;
        r.rect(350,705,500,3,{.13f,.2f,.3f});r.rect(350,705,500*ratio,3,cyan);
        r.text(350,716,decimal(view.replayPosition)+" / "+decimal(view.replayDuration)+"s    "+decimal(view.replaySpeed)+"x",14);
        r.text(40,755,"Space/F2 pause    Left/Right seek 10s    -/+ speed    R restart    P/Esc menu    S save after finish",13,muted);
    } else r.text(32,756,controls,12,muted);
    if(!view.message.empty()) r.text(350,668,view.message,12,cyan);
}
void scene_tile(Renderer& r,const Presentation::BoardView& b,float x,float y,float w,float h,bool miniature=false) {
    r.rect(x,y,w,h,panelColor);r.outline(x,y,w,h,{.12f,.27f,.38f});
    if(b.width<=0 || b.bottom<=b.firstRow)return;
    r.text(x+8,y+4,b.username.empty()?"PLAYER":b.username,miniature?9:16,cyan);
    const float unit=std::max(.5f,std::min((w-(miniature?12.f:110.f))/b.width,(h-(miniature?30.f:175.f))/(b.bottom-b.firstRow)));
    const float left=x+(miniature?6:38),top=y+(miniature?20:36);
    for(int row=b.firstRow;row<b.bottom;++row)for(int col=0;col<b.width;++col) {
        const auto& cell=b.at(row,col);const float px=left+col*unit,py=top+(row-b.firstRow)*unit;
        r.rect(px,py,unit-.4f,unit-.4f,{.045f,.075f,.10f});
        if(cell.hint)r.rect(px+unit*.4f,py+unit*.4f,unit*.2f,unit*.2f,muted);
        if(cell.appearance!=Presentation::CellAppearance::Empty) {
            auto c=cell_color(cell.value.color_id(),b.obscuredColors);
            if(cell.appearance==Presentation::CellAppearance::Dimmed) {c.r*=.35f;c.g*=.35f;c.b*=.35f;}
            block(r,px,py,unit,c,false,b.squareStyle);
            if(cell.hint)r.rect(px+unit*.4f,py+unit*.4f,unit*.2f,unit*.2f,panelColor);
            if(cell.value.is_timed_number() && !miniature)r.text(px,py,std::to_string(cell.value.timed_countdown()),unit*.6f);
        }
    }
    const auto draw=[&](const Presentation::PieceView& p,bool ghost) {
        const auto piece=Piece::get_definition(p.type);
        for(const auto& cell:piece.cells(p.pose.rotation)) {
            const int row=p.pose.position.row+cell[0],col=p.pose.position.col+cell[1];
            if(row>=b.firstRow && row<b.bottom && col>=0 && col<b.width)
                {auto color=cell_color(piece.color_id(),b.obscuredColors);
                if(p.appearance==Presentation::CellAppearance::Dimmed){color.r*=.35f;color.g*=.35f;color.b*=.35f;}
                block(r,left+col*unit,top+(row-b.firstRow)*unit,unit,color,ghost,b.squareStyle);}
        }
    };
    if(b.ghost)draw(*b.ghost,true);if(b.active)draw(*b.active,false);
    garbage_bar(r,b,left-5,top+(b.bottom-b.firstRow)*unit,unit,(b.bottom-b.firstRow)*unit);
    if(!miniature) {
        const float right=left+b.width*unit+16;
        r.text(right,top,"NEXT",10,muted);
        float ny=top+23;
        for(auto next:b.next) {piece_preview(r,next,right+25,ny,9,true,b.obscuredColors,b.squareStyle);ny+=std::min(48.f,400.f/std::max<std::size_t>(1,b.next.size()));}
        if(b.holdEnabled) {r.text(x+8,y+h-90,"HOLD",10,muted);piece_preview(r,b.hold,x+60,y+h-73,9,b.holdAvailable,b.obscuredColors,b.squareStyle);}
        r.text(x+8,y+h-47,std::to_string(b.pieces)+"P  "+std::to_string(b.lines)+"L  "+std::to_string(b.attack)+"A   "+decimal(b.elapsed.seconds())+"s",12);
        std::string event=b.spinPiece.valid()?std::string(Piece::metadata(b.spinPiece).name)+"-SPIN ":"";
        if(b.clearLines)event+=b.clearLines<0?"VOID ":std::to_string(b.clearLines)+"L ";
        if(b.perfectClear || b.halfClear)event+=b.perfectClear?"PC ":"HC ";
        if(b.clutch)event+="CLUTCH "+std::to_string(b.clutch)+" ";
        r.text(x+8,y+h-138,event,11,cyan);
        if(!b.spinHistory.empty())r.text(x+8,y+h-121,spin_history(b),10,purple);
        r.text(x+8,y+h-27,"B2B "+std::to_string(std::max(0,b.b2b-1))+"   COMBO "+std::to_string(std::max(0,b.combo-1))+"   WINS "+std::to_string(b.wins)+"   "+b.headline,11,purple);
    }
}
void scene_view(Renderer& r,const SessionView& view) {
    std::vector<const Presentation::BoardView*> full;
    std::vector<const Presentation::BoardView*> mini;
    if(!view.spectator)full.push_back(&view.board);
    for(const auto& peer:view.peers)(peer.miniature?mini:full).push_back(&peer.board);
    const float area=mini.empty()?1136.f:810.f;
    const int columns=std::max(1,std::min(3,static_cast<int>(full.size())));
    const int rows=std::max(1,(static_cast<int>(full.size())+columns-1)/columns);
    for(std::size_t i=0;i<full.size();++i)scene_tile(r,*full[i],32+(i%columns)*area/columns,100+(i/columns)*610.f/rows,area/columns-12,610.f/rows-8);
    if(!mini.empty()) {
        const int cols=std::max(1,static_cast<int>(std::ceil(std::sqrt(mini.size()/2.f))));
        const int rows=(static_cast<int>(mini.size())+cols-1)/cols;
        for(std::size_t i=0;i<mini.size();++i)scene_tile(r,*mini[i],850+(i%cols)*320.f/cols,100+(i/cols)*610.f/rows,320.f/cols-3,610.f/rows-3,true);
    }
    r.text(32,718,view.roomStatus,13,cyan);
    r.text(32,742,view.replay?"Replay: Space pause   -/+ speed   Left/Right seek   Tab results / focus":"Tab: focus / statistics   P: settings   R: retry   Ctrl+Shift+F12: host ends match",13,muted);
    r.text(32,770,view.controls,10,muted);
}
struct Options {
    std::string dataDir,profile,profileTwo,replay,font,screenshot;
    std::vector<std::string> bindings;
    int frames=0,smoke=0;
    bool play=false,vsync=true,hidden=false,continuous=false;
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
        else if(arg=="--screenshot") result.screenshot=value();
        else if(arg=="--frames") {result.frames=std::stoi(value());result.hidden=true;}
        else if(arg=="--smoke-test") {result.smoke=1;result.vsync=false;}
        else if(arg=="--play") result.play=true;
        else if(arg=="--no-vsync") result.vsync=false;
        else if(arg=="--hidden") result.hidden=true;
        else if(arg=="--help") {
            std::cerr<<"TerMino OpenGL / shared full application\n"
                "[player1-keybindings] [player2-keybindings] (same native key format as tetr)\n"
                "--data-dir DIR  isolated runtime directory\n"
                "--profile FILE  read original platform profile and bindings\n"
                "--profile2 FILE read Player 2 profile\n"
                "--replay FILE   open single-board or local-versus recording\n"
                "--play         start with saved configuration\n--font FILE    UTF-8 font override\n"
                "--qp-continuous-cancel  enable shared Quick Play cancellation penalties\n"
                "--no-vsync     render at up to 240 Hz\n"
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
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
        if(opt.font.empty())opt.font=std::string(SDL_GetBasePath())+"resources/fonts/NotoSansCJKsc-Regular.otf";
        if(!opt.dataDir.empty()) {std::filesystem::create_directories(std::filesystem::u8path(opt.dataDir));std::filesystem::current_path(std::filesystem::u8path(opt.dataDir));}
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        const SDL_WindowFlags flags=SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(opt.hidden?SDL_WINDOW_HIDDEN:0);
        window=SDL_CreateWindow("TerMino / OpenGL",1200,800,flags);
        if(!window)throw std::runtime_error(SDL_GetError());SDL_SetWindowMinimumSize(window,840,560);
        gl=SDL_GL_CreateContext(window);if(!gl)throw std::runtime_error(SDL_GetError());
        if(!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) || !GLAD_GL_VERSION_3_3)throw std::runtime_error("OpenGL 3.3 Core is required");
        std::cerr<<"TerMino GL: "<<glGetString(GL_VERSION)<<" / "<<glGetString(GL_RENDERER)<<" / video="<<SDL_GetCurrentVideoDriver()<<" / hidden="<<opt.hidden<<'\n';
        SDL_GL_SetSwapInterval(opt.vsync?1:0);
        {
            Renderer renderer(opt.font);EventClock eventClock;
            UserConfig::Settings first,second;
            UserConfig::load_keybindings(first,opt.bindings.empty()?UserConfig::kDefaultKeybindingPath:opt.bindings[0]);
            const auto profile=[](const char* name){const char* p=std::getenv(name);return p?std::string(p):std::string{};};
            UserConfig::load_startup_settings(first,false,opt.profile.empty()?profile("TETR_USER_CONFIG"):opt.profile);
            if(!opt.profile.empty())first.persistencePath.clear();
            second=first;
            UserConfig::load_player_two_keybindings(second,opt.bindings.size()<2?UserConfig::kDefaultPlayerTwoKeybindingPath:opt.bindings[1]);
            UserConfig::load_startup_settings(second,true,opt.profileTwo.empty()?profile("TETR_USER_CONFIG_PLAYER2"):opt.profileTwo);
            if(!opt.profileTwo.empty())second.persistencePath.clear();
            if(!opt.replay.empty()) {first.gameMode=5;first.replayPath=opt.replay;}
            LaunchOptions launch;launch.startScreen=!opt.play && opt.replay.empty();launch.continuousCancellation=opt.continuous;launch.audio=!opt.hidden;
            if(opt.profile.empty() && !std::getenv("TETR_USER_CONFIG")) {int owner=0;std::ifstream(".tetr/active_player")>>owner;launch.playerTwoActive=owner==1;}
            Session session(std::move(first),std::move(second),launch);
            bool running=true,textActive=false;int frame=0,smokeStep=0;
            std::string composition;const auto smokeStart=std::chrono::steady_clock::now();
            while(running) {
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
                SDL_Event event;
                while(SDL_PollEvent(&event)) {
                    if(event.type==SDL_EVENT_QUIT)running=false;
                    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST)session.input().set_focused(false);
                    if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)regain_focus(session.input());
                    if(textActive && event.type==SDL_EVENT_TEXT_INPUT) {session.text_event({Frontend::TextEditKind::Insert,event.text.text});composition.clear();}
                    if(textActive && event.type==SDL_EVENT_TEXT_EDITING)composition=event.edit.text;
                    if(event.type!=SDL_EVENT_KEY_DOWN && event.type!=SDL_EVENT_KEY_UP)continue;
                    deliver_key(session.input(),event.key,eventClock);
                    if(!textActive || event.type!=SDL_EVENT_KEY_DOWN)continue;
                    using K=Frontend::TextEditKind;
                    std::optional<K> edit;
                    switch(event.key.scancode) {
                    case SDL_SCANCODE_LEFT:edit=K::Left;break;case SDL_SCANCODE_RIGHT:edit=K::Right;break;
                    case SDL_SCANCODE_HOME:edit=K::Home;break;case SDL_SCANCODE_END:edit=K::End;break;
                    case SDL_SCANCODE_BACKSPACE:if(composition.empty())edit=K::Backspace;break;
                    case SDL_SCANCODE_DELETE:edit=K::Delete;break;
                    case SDL_SCANCODE_RETURN:case SDL_SCANCODE_KP_ENTER:if(composition.empty())edit=K::Confirm;break;
                    case SDL_SCANCODE_ESCAPE:if(composition.empty())edit=K::Cancel;break;
                    case SDL_SCANCODE_A:if(event.key.mod&SDL_KMOD_CTRL)edit=K::SelectAll;break;
                    case SDL_SCANCODE_V:if(event.key.mod&SDL_KMOD_CTRL) {
                        char* clipboard=SDL_GetClipboardText();if(clipboard){session.text_event({K::Insert,clipboard});SDL_free(clipboard);}
                    }break;
                    default:break;
                    }
                    if(edit)session.text_event({*edit,{}});
                }
                const auto view=session.view();
                if(view.status==SessionStatus::Closed)running=false;
                if(view.status==SessionStatus::Error)throw std::runtime_error(view.message);
                int width=0,height=0;SDL_GetWindowSizeInPixels(window,&width,&height);
                if(width>0 && height>0) {
                    renderer.begin(width,height);background(renderer);
                    if(!view.ui || view.ui->overlay) {
                        if(view.peers.empty())game_view(renderer,view,view.controls);
                        else scene_view(renderer,view);
                    }
                    if(view.ui)renderer.surface(*view.ui);
                    if(textActive && view.ui && view.ui->caret) {
                        const float cw=1136.f/view.ui->extent.first,ch=650.f/view.ui->extent.second;
                        const float x=32+view.ui->caret->second*cw,y=92+(view.ui->caret->first+1)*ch;
                        if(!composition.empty()) {renderer.rect(x,y,500,30,panelColor);renderer.text(x,y,composition,20,cyan);}
                        int logicalW=0,logicalH=0;SDL_GetWindowSize(window,&logicalW,&logicalH);
                        const float scale=std::min(logicalW/1200.f,logicalH/800.f);
                        SDL_Rect area{int((logicalW-1200*scale)/2+x*scale),int((logicalH-800*scale)/2+y*scale),std::max(1,int(400*scale)),std::max(1,int(24*scale))};
                        SDL_SetTextInputArea(window,&area,0);
                    }
                    renderer.end();++frame;
                    if(opt.frames && frame>=opt.frames)running=false;
                    if(!running && !opt.screenshot.empty())renderer.screenshot(opt.screenshot,width,height);
                    SDL_GL_SwapWindow(window);
                }
                if(opt.smoke && !running) {
                    if(view.board.pieces<1 || !view.board.hold.valid())throw std::runtime_error("SDL smoke failed: shared engine did not receive drop/Hold");
                    std::cerr<<"OPENGL_SMOKE_OK pieces="<<view.board.pieces<<" hold="<<view.board.hold.index()<<'\n';
                }
                std::this_thread::sleep_until(frameStart+std::chrono::microseconds(4167));
            }
            if(textActive)SDL_StopTextInput(window);
        }
        SDL_GL_DestroyContext(gl);SDL_DestroyWindow(window);SDL_Quit();return 0;
    } catch(const std::exception& error) {
        std::cerr<<"TerMino OpenGL: "<<error.what()<<'\n';
        if(gl)SDL_GL_DestroyContext(gl);if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
}
