#include "scene.h"
#include "board_layout.h"
#include "tetr/runtime/piece.h"
#include "tetr/rules/spike.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <sstream>
#include <iomanip>
namespace Graphical {
Color field_color(Runtime::BoardCell cell,bool obscured) {
    // Release cells share the legacy color ID with timed solids, but use the
    // numbered-cell foreground (white) once their counter reaches zero.
    if(cell.is_timed_release() && !obscured)return {1,1,1};
    return cell_color(cell.color_id(),obscured);
}
Color displayed_cell_color(const Presentation::CellView& cell,bool obscured) {
    auto color=field_color(cell.value,obscured);
    color.a=std::clamp(cell.appearance==Presentation::CellAppearance::Dimmed?std::min(.35f,cell.opacity):cell.opacity,0.f,1.f);
    return color;
}
void block(Renderer& r,float x,float y,float size,Color color,bool ghost=false,int style=0) {
    r.mino(x,y,size,color,ghost,style);
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
void garbage_bar(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,bool small=false) {
    const float width=small?std::clamp(g.unit*.55f,3.f,7.f):std::clamp(g.unit*.55f,10.f,14.f);
    const float x=g.left-width-5;
    r.rect(x-1,g.top-1,width+2,g.height+2,borderColor);
    r.rect(x,g.top,width,g.height,wellColor);
    float used=0;int total=0;for(int lines:b.garbage)total+=std::max(0,lines);
    for(std::size_t i=0;i<b.garbage.size();++i) {
        const float h=std::min(float(b.garbage[i])*g.unit,g.height-used);if(h<=0)continue;
        const double delay=i<b.garbageReadyIn.size()?b.garbageReadyIn[i]:0;
        const auto phase=i<b.garbagePhases.size()?b.garbagePhases[i]:delay>.3?Presentation::GarbagePhase::Early:
            delay>0?Presentation::GarbagePhase::Incoming:Presentation::GarbagePhase::Ready;
        const Color color=b.obscuredColors?muted:phase==Presentation::GarbagePhase::Early?Color{1,.80f,.23f}:
            phase==Presentation::GarbagePhase::Incoming?Color{.70f,.22f,.26f}:Color{1,.30f,.38f};
        r.rect(x,g.bottom()-used-h,width,h,color);
        if(used>0)r.rect(x,g.bottom()-used-1,width,std::min(2.f,h),wellColor);
        used+=h;
    }
    if(total && !small) {
        const std::string value=std::to_string(total);const float size=12;
        r.text(x+width/2-r.measure(value,size)/2,g.bottom()+7,value,size,{1,.57f,.60f});
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
void background(Renderer& r,bool header,float contentScale) {
    r.gradient(r.scene_left(),0,r.scene_right()-r.scene_left(),800,backgroundTop,backgroundBottom);
    // Broad, static geometric lighting stays outside the playfield.
    for(int i=0;i<36;++i) {
        const float x=690+i*16;
        r.line(x,0,x-330,800,{accent.r,accent.g,accent.b,.025f},12);
    }
    if(header) {
        const float left=r.scene_left(),right=r.scene_right();
        const float s=contentScale;
        r.rect(left+40,25,9*s,9*s,accent);r.rect(left+40+11*s,25,9*s,9*s,accent);
        r.rect(left+40+11*s,25+11*s,9*s,9*s,accent);r.rect(left+40+22*s,25+11*s,9*s,9*s,accent);
        r.text(left+40+45*s,21,"TerMino",23*s,textColor);
        r.text(right-40-r.measure("PLAY / CREATE",11*s),29,"PLAY / CREATE",11*s,muted);
        r.rect(left+40,68,right-left-80,1,borderColor);
    }
}
std::string game_time(double seconds);
void key_hints(Renderer& r,const SessionView& view) {
    if(r.touch_controls())return;
    if(view.ui && view.ui->overlay && view.ui->metadata.kind==Frontend::UiKind::Notice)return;
    auto hints=view.controlHints;
    if(view.replay) {
        hints={{"pause","Pause",true,"Space / F2"},{"seek","Seek",true,"← / →"},
            {"speed","Speed",true,"- / +"},{"retry","Retry",true,"R"},
            {"menu","Menu",true,"P / Esc"},{"save","Save",true,"S"},
            {"results","Results / focus",true,"Tab"}};
    } else if(view.room || !view.peers.empty()) {
        hints.push_back({"focus","Focus / statistics",true,"Tab"});
        if(view.room)hints.push_back({"host-end","Host ends match",true,"Ctrl+Shift+F12"});
    }
    const auto notice=view.replay?game_time(view.replayPosition)+" / "+game_time(view.replayDuration)+
        "\n"+decimal(view.replaySpeed)+"x":std::string{};
    r.controls(hints,notice);
}
// Right-aligned labels and a large value with its smaller rate match the
// information hierarchy of TETR.IO. All rails have fixed geometry.
void right_text(Renderer& r,float right,float y,const std::string& value,float size,Color color=textColor,float width=10000) {
    size=std::min(size,size*std::max(1.f,width)/std::max(1.f,r.measure(value,size)));
    r.fitted(right-std::min(width,r.measure(value,size)),y,value,width,size,color);
}
std::string game_time(double seconds) {
    const auto ms=static_cast<long long>(std::max(0.,seconds)*1000);
    std::ostringstream out;out<<ms/60000<<':'<<std::setfill('0')<<std::setw(2)<<ms/1000%60<<'.'<<std::setw(3)<<ms%1000;
    return out.str();
}
void preview_box(Renderer& r,Piece::Type type,Rect box,bool available,bool obscured,int style) {
    if(!type.valid())return;
    const auto cells=Piece::preview_cells(type);int left=999,top=999,right=-999,bottom=-999;
    for(const auto& c:cells){left=std::min(left,c[1]);right=std::max(right,c[1]);top=std::min(top,c[0]);bottom=std::max(bottom,c[0]);}
    const float unit=std::min({26.f,box.w/(right-left+1),box.h/(bottom-top+1)});
    piece_preview(r,type,box.x+box.w/2,box.y+(box.h-(bottom-top+1)*unit)/2,unit,available,obscured,style);
}
Color b2b_color(int count,bool obscured) {
    if(obscured)return textColor;
    // TETR.IO's normal B2B charge gradient. Counts are the displayed xN.
    // This is a visual palette only; shared attack and charging rules own play.
    constexpr int stops[]={4,8,12,30,60,100,150,250,500};
    constexpr unsigned rgb[]={0x00ffcc,0x00ff00,0xffff00,0xff0000,0xff00ff,
        0x0091ff,0x94ffe4,0xffdd87,0xffffff};
    const auto color=[](unsigned value){return Color{float(value>>16)/255,float(value>>8&255)/255,float(value&255)/255};};
    if(count<4)return color(0xffd964);
    for(std::size_t i=1;i<std::size(stops);++i)if(count<stops[i]) {
        const float t=float(count-stops[i-1])/(stops[i]-stops[i-1]);
        const auto from=color(rgb[i-1]),to=color(rgb[i]);
        return {from.r+(to.r-from.r)*t,from.g+(to.g-from.g)*t,from.b+(to.b-from.b)*t};
    }
    return color(rgb[std::size(rgb)-1]);
}
struct ProgressCue {
    std::string title,unit;
    int remaining=0;
    double distance=0;
    double progress=0;
    Color color=accent;
    bool fieldMarker=true;
    std::string value;
};
std::vector<ProgressCue> progress_cues(const Presentation::BoardView& b) {
    std::vector<ProgressCue> cues;
    if(b.goal.type!=Runtime::GameGoal::None && b.goal.target>0) {
        const char* title="GOAL";
        switch(b.goal.type) {
        case Runtime::GameGoal::Time:title="TIME LEFT";break;
        case Runtime::GameGoal::Lines:title="LINES LEFT";break;
        case Runtime::GameGoal::Garbage:title="GARBAGE LEFT";break;
        case Runtime::GameGoal::Pieces:title="PIECES LEFT";break;
        case Runtime::GameGoal::Attack:title="ATTACK LEFT";break;
        case Runtime::GameGoal::AllClear:title="ALL CLEARS LEFT";break;
        case Runtime::GameGoal::InitialCells:title="CELLS LEFT";break;
        default:break;
        }
        // The time label and finish line follow the presentation clock's
        // full precision, including during replay and pause.
        const bool time=b.goal.type==Runtime::GameGoal::Time;
        const double distance=time?
            std::max(0.,b.goal.target-b.elapsed.seconds()):std::max(0,b.goal.remaining);
        cues.push_back({title,time?"":b.goal.unit,std::max(0,b.goal.remaining),distance,
            std::clamp(double(b.goal.progress)/b.goal.target,0.,1.),b.obscuredColors?textColor:accent,
            b.goal.supports_field_marker(),time?game_time(distance):""});
    }
    if(b.growth.nextSize>0) cues.push_back({"NEXT "+std::to_string(b.growth.nextSize)+"×"+std::to_string(b.growth.nextSize),
        "L",b.growth.remaining,double(b.growth.remaining),std::clamp(b.growth.progress,0.,1.),b.obscuredColors?textColor:secondary});
    return cues;
}
void progress_field(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,bool foreground) {
    if(b.finished || b.result!=Presentation::RoundResult::None)return;
    for(const auto& cue:progress_cues(b)) {
        if(!cue.fieldMarker)continue;
        const auto y=target_line_y(g,cue.distance);if(!y)continue;
        if(foreground) {
            // Techmino-style finish line: remaining rows measured from the
            // floor, including on enlarged, shallow fields. No ceiling clamp.
            auto color=cue.color;color.a=.9f;
            r.rect(g.left,*y,g.width,std::max(1.f,g.unit*.045f),color);
        } else {
            // Fade to transparency within three rows, stopping at the floor.
            auto top=cue.color,bottom=cue.color;top.a=.20f;bottom.a=0;
            r.gradient(g.left,*y,g.width,std::min(3*g.unit,g.bottom()-*y),top,bottom);
        }
    }
}
void progress_panel(Renderer& r,const ProgressCue& cue,Rect box) {
    const float valueTop=std::max(13.f,r.text_size(10)+3),barTop=std::max(45.f,valueTop+30);
    r.fitted(box.x+4,box.y,cue.title,box.w-8,10,cue.color);
    r.fitted(box.x+4,box.y+valueTop,cue.value.empty()?std::to_string(cue.remaining):cue.value,
        (cue.unit.empty()?box.w:box.w*.68f)-8,26,textColor);
    right_text(r,box.x+box.w-4,box.y+26,cue.unit,11,muted,box.w*.3f);
    r.rect(box.x+4,box.y+barTop,box.w-8,3,borderColor);
    r.rect(box.x+4,box.y+barTop,(box.w-8)*float(cue.progress),3,cue.color);
}
struct HudLayout {
    float eventTop,eventScale,statsBottom,statsScale,top;
};
std::vector<std::pair<std::string,Color>> board_notes(const Presentation::BoardView& b,
    const std::vector<std::string>& messages={}) {
    std::vector<std::pair<std::string,Color>> notes;
    const auto note=[&](std::string value,Color c=muted){if(!value.empty())notes.emplace_back(std::move(value),c);};
    if(b.population)note("ALIVE "+std::to_string(b.rank)+" / "+std::to_string(b.population));
    if(b.clutch)note("CLUTCH ×"+std::to_string(b.clutch),accent);
    if(b.blighted)note("BLIGHTED",secondary);
    if(!b.stageTask.empty())note("STAGE "+std::to_string(b.stages)+"/4  "+b.stageTask);
    if(!b.spinHistory.empty())note(spin_history(b),secondary);
    for(const auto& message:messages)note(message);
    return notes;
}
struct NextLayout {float top,step,notesTop;};
NextLayout next_layout(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,bool solo) {
    const auto count=next_slot_count(b),cues=progress_cues(b).size();
    float top=next_rail_top(g,count,cues),reserved=0;
    if(!solo && b.quickPlay) {
        const float size=r.text_size(12*std::min(1.f,g.rail/160));
        // Keep room for the three usual QP notes even while some are absent.
        // The warning has a separate band below this rail, including in duels.
        reserved=3*(size*1.5f+4);
        float height=0;
        for(const auto& [value,color]:board_notes(b))height+=r.wrap(value,g.next,size).size()*size*1.5f+4;
        reserved=std::min(std::max(reserved,height),
            std::max(0.f,railHeight-54*cues-40-12*count));
        top=std::min(top,std::max(railTop,railTop+railHeight-40-54*cues-24*count-reserved));
    }
    const float step=std::min(77.f,(railTop+railHeight-top-54*cues-40-reserved)/std::max<std::size_t>(1,count));
    return {top,step,top+34+step*count};
}
float hold_panel_height(const BoardGeometry& g) {
    const float scale=std::min(1.f,g.rail/160);
    return std::min(96*scale,std::max(40*scale,fieldFloor+40-g.top));
}
float hud_stats_height(const Renderer& r,float scale) {
    const float info=.88f*scale;
    return r.text_size(11*info)>11*info?5*(r.text_size(11*info)+r.text_size(32*info)+8):242*info;
}
HudLayout hud_layout(const Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g) {
    const float scale=std::min(1.f,g.rail/160),gap=10*scale;
    const float warning=b.quickPlay?82.f:0.f;
    if(!b.holdEnabled)return {railTop+warning,scale,railTop+railHeight,scale,railTop+warning};
    const float events=158*scale,stats=hud_stats_height(r,scale);
    const float before=g.top-(topPreviewCeiling+16)-gap-warning;
    const float after=std::max(0.f,fieldFloor-(g.top+hold_panel_height(g)+gap));
    HudLayout layout{g.top+hold_panel_height(g)+gap,scale,fieldFloor,scale,g.top};
    // Keep the two fixed information groups outside HOLD as the well moves
    // down on shallow fields. Transient messages never move these groups.
    if(before>=events+stats+gap) {
        layout.statsBottom=g.top-gap-warning;
        layout.eventTop=layout.statsBottom-stats-gap-events;
    } else if(before>=stats) {
        layout.statsBottom=g.top-gap-warning;
        layout.eventScale*=std::min(1.f,after/events);
    } else {
        float low=0,high=1;
        for(int i=0;i<16;++i) {
            const float mid=(low+high)/2;
            if(events*mid+hud_stats_height(r,scale*mid)+gap<=after)low=mid;else high=mid;
        }
        const float fit=r.text_size(11*.88f*scale)>11*.88f*scale?low:std::min(1.f,after/(events+stats+gap));
        layout.eventScale*=fit;layout.statsScale*=fit;
    }
    layout.top=std::min({g.top,layout.eventTop,layout.statsBottom-hud_stats_height(r,layout.statsScale)});
    return layout;
}
void board_hud(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,bool solo,const BoardEvents& events,
    const std::vector<std::string>& messages={}) {
    const auto layout=hud_layout(r,b,g);
    const float right=g.left-26,left=right-g.rail,bottom=layout.statsBottom;
    const float scale=layout.eventScale,infoScale=.88f*layout.statsScale;
    const bool readable=r.text_size(11*infoScale)>11*infoScale;
    const float rowStep=hud_stats_height(r,layout.statsScale)/5;
    const double seconds=b.elapsed.seconds();
    const auto metric=[&](float y,const std::string& label,const std::string& value,const std::string& rate={},float size=29) {
        right_text(r,right,y,label,11*infoScale,muted,g.rail);
        const float small=16*infoScale;
        const float rateSize=std::min(small,small*(g.rail*.55f)/std::max(1.f,r.measure(rate,small)));
        const float rateWidth=rate.empty()?0:std::min(g.rail*.55f,r.measure(rate,rateSize))+7;
        const float valueTop=readable?r.text_size(11*infoScale)+3:16*infoScale;
        const float rateTop=readable?valueTop+(r.text_size(size*infoScale)-r.text_size(rateSize))/2:26*infoScale;
        right_text(r,right-rateWidth,y+valueTop,value,size*infoScale,textColor,g.rail-rateWidth);
        if(!rate.empty())right_text(r,right,y+rateTop,rate,rateSize,accent,g.rail*.55f);
    };
    // These positions never depend on the number/length of event messages.
    const auto top=[&](int row,float legacy){return bottom-(readable?row*rowStep:legacy*infoScale);};
    metric(top(1,50),"TIME",game_time(b.goal.type==Runtime::GameGoal::Time?std::max(0.,b.goal.target-seconds):seconds),{},32);
    metric(top(2,98),"ATTACK",std::to_string(b.attack),decimal(seconds>0?b.attack*60/seconds:0)+" /m");
    metric(top(3,146),"PIECES",std::to_string(b.pieces),decimal(seconds>0?b.pieces/seconds:0)+" /s");
    metric(top(4,194),"LINES",std::to_string(b.lines)+(b.goal.type==Runtime::GameGoal::Lines?" / "+std::to_string(b.goal.target):""),decimal(seconds>0?b.lines*60/seconds:0)+" LPM");
    metric(top(5,242),"SCORE",std::to_string(b.score));
    const float eventTop=layout.eventTop;
    const Color gold=b.obscuredColors?textColor:Color{1.f,.85f,.25f};
    // TETR.IO action hierarchy: tracked piece/spin above the larger clear,
    // gold B2B, a tall combo count, then the old yellow side SPIKE counter.
    // Empty events retain their slots so every baseline stays in place.
    const Rect eventBounds{left-40*scale,eventTop-2*scale,g.rail+42*scale,155*scale};
    r.clip(eventBounds);
    r.text_effect(events.motion(EventKind::Action),right,eventTop+34*scale,[&] {
        if(events.action.spin.valid()) {
            const auto color=cell_color(Piece::metadata(events.action.spin).colorId,b.obscuredColors);
            const float w=r.hud_text(right,eventTop+18*scale,std::string(Piece::metadata(events.action.spin).name)+"-SPIN",
                19*scale,color,g.rail,3*scale);
            if(events.action.mini)r.hud_text(right-w-8*scale,eventTop+18*scale,"MINI",12*scale,color,g.rail-w-8*scale,1*scale);
        }
        if(events.action.lines) {
            const char* names[]={"","SINGLE","DOUBLE","TRIPLE","QUAD"};
            const auto clear=events.action.lines<0?std::string("VOID"):events.action.lines<=4?std::string(names[events.action.lines]):std::to_string(events.action.lines)+" LINES";
            r.hud_text(right,eventTop+50*scale,clear,32*scale,textColor,g.rail,4*scale,.85f);
        }
    });r.clip({});
    if(b.b2b>1) {
        const Color chain=b2b_color(b.b2b-1,b.obscuredColors);
        const float w=r.hud_text(right,eventTop+78*scale,"x"+std::to_string(b.b2b-1),21*scale,chain,g.rail*.6f,1*scale);
        r.hud_text(right-w-9*scale,eventTop+78*scale,"B2B",23*scale,chain,g.rail-w-9*scale,2*scale);
    }
    r.clip(eventBounds);
    r.text_effect(events.motion(EventKind::Combo),right,eventTop+115*scale,[&] {
        if(events.combo>1) {
            const float w=r.hud_text(right,eventTop+115*scale,"COMBO",22*scale,textColor,g.rail*.62f,1*scale);
            r.hud_text(right-w-12*scale,eventTop+115*scale,std::to_string(events.combo-1),38*scale,textColor,g.rail-w-12*scale);
        }
    });
    r.text_effect(events.motion(EventKind::Spike),right,eventTop+146*scale,[&] {
        if(events.spike>=Rules::kMinimumVisibleSpike) {
            const float w=r.hud_text(right,eventTop+146*scale,"SPIKE",19*scale,gold,g.rail*.57f,1*scale);
            r.hud_text(right-w-12*scale,eventTop+146*scale,std::to_string(events.spike),28*scale,gold,g.rail-w-12*scale);
        }
    });r.clip({});
    float y=eventTop+158*scale;
    const auto notes=board_notes(b,messages);
    // Extended challenge objectives have their own left-hand area. They cannot
    // push the core statistics or the playfield around as the text changes.
    float nx=left,nw=g.rail,ny=y,size=12*scale;
    const float statsTop=bottom-242*infoScale-10;
    if(solo && (notes.size()>2 || y+notes.size()*22>statsTop)) {
        nx=g.left+g.width+20+g.next+24;nw=std::max(120.f,r.scene_right()-40-nx);ny=g.top+112;size=14;
    }
    // In a narrow duel rail, extended objectives continue below NEXT rather
    // than colliding with the fixed score/rate positions.
    if(!solo && y+notes.size()*22>statsTop) {
        nx=g.left+g.width+20;nw=g.next;ny=next_layout(r,b,g,solo).notesTop;
    }
    const float limit=nx==left?statsTop:!solo && b.quickPlay?railTop+railHeight-54*progress_cues(b).size()-6:bottom;
    for(;;) {
        float height=0;for(const auto& [value,color]:notes)height+=r.wrap(value,nw,size).size()*size*1.5f+4;
        if(ny+height<=limit || size<=8)break;size-=.5f;
    }
    for(const auto& [value,color]:notes) {
        for(const auto& line:r.wrap(value,nw,size)) {right_text(r,nx+nw,ny,line,size,color,nw);ny+=size*1.5f;}ny+=4;
    }
}
void windup_warning(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,SessionStatus status) {
    if(!b.quickPlay || b.windup<=0 || b.finished || b.result!=Presentation::RoundResult::None ||
        status==SessionStatus::Countdown || status==SessionStatus::Finished)return;
    const int count=std::clamp(b.windup,1,4);
    const double age=b.windupAge.seconds(),ending=1+.5*count;
    if(!std::isfinite(age) || age<0 || age>=ending+1)return;
    const float fade=std::clamp(float((age-ending)/.35),0.f,1.f);
    if(fade>=1)return;
    const float motion=std::clamp(b.effects.intensity/100.f,0.f,1.f);
    constexpr double steps[]={0,.345,.545,.745};
    int stage=1;for(int i=1;i<count;++i)if(age>=steps[i])stage=i+1;
    const double since=age-steps[stage-1];
    const float duration=stage==1?.25f:.15f;
    const float pulse=since<duration?std::sin(float(since)/duration*3.14159265f):0;
    const float appear=age<.125?float(age/.125):1;
    const float radius=std::min(23.f,g.rail*.15f)*(1+.1f*stage*motion*pulse)*
        (1-(1-appear)*std::min(1.f,motion))*(1-.7f*fade);
    const float cx=g.left-26-g.rail/2,cy=(b.holdEnabled?g.top-62:railTop+26);
    constexpr unsigned colors[]={0xFFBF00,0xFF7700,0xFF2A00,0xFF0073};
    auto color=b.obscuredColors?textColor:rgb(colors[stage-1]);color.a=1-fade;
    // Every arriving segment turns the rim through a quarter turn with a
    // fading duplicate. The one/two-mark glyph stays upright, as in the video.
    const float turn=age>=1 && age<ending?std::min(1.f,float(std::fmod(age-1,.5))/.25f):0;
    const float angle=turn*1.570796327f*std::min(1.f,motion),cs=std::cos(angle),sn=std::sin(angle);
    constexpr std::array<std::array<float,2>,16> rim{{
        {0,-1},{.26f,-.7f},{.7f,-.7f},{.7f,-.26f},
        {1,0},{.7f,.26f},{.7f,.7f},{.26f,.7f},
        {0,1},{-.26f,.7f},{-.7f,.7f},{-.7f,.26f},
        {-1,0},{-.7f,-.26f},{-.7f,-.7f},{-.26f,-.7f}}};
    for(const bool shadow:{true,false})for(std::size_t i=0;i<rim.size();++i) {
        const auto point=[&](auto p){return std::array<float,2>{cx+radius*(p[0]*cs-p[1]*sn),cy+radius*(p[0]*sn+p[1]*cs)};};
        const auto a=point(rim[i]),z=point(rim[(i+1)%rim.size()]);
        r.line(a[0],a[1],z[0],z[1],shadow?Color{wellColor.r,wellColor.g,wellColor.b,1-fade}:color,shadow?5.f:2.5f);
        if(!shadow && turn>0 && turn<1 && motion>0) {
            auto flash=color;flash.a*=.4f*(1-turn)*std::min(1.f,motion);
            r.line(cx+(a[0]-cx)*1.12f,cy+(a[1]-cy)*1.12f,cx+(z[0]-cx)*1.12f,cy+(z[1]-cy)*1.12f,flash,2);
        }
    }
    const int marks=stage>=3?2:1;
    const float unit=radius/27*(1+.1f*motion*pulse),step=9*unit,width=4.5f*unit;
    auto ink=textColor;ink.a=1-fade;
    for(int i=0;i<marks;++i) {
        const float x=cx-(step*(marks-1)+width)/2+i*step;
        r.rect(x,cy-17*unit,width,23*unit,ink);
        r.rect(x-.25f*unit,cy+11*unit,5*unit,6*unit,ink);
    }
}
void booster_emblem(Renderer& r,int level,float center,float gap,float space,float top,Color color) {
    if(level<2)return;
    // Clean, swept feathers with open space between tiers, like the QP rank badge.
    // Both halves keep the same aspect ratio and leave the altitude clear.
    const float scale=std::min(1.f,std::max(0.f,space)/72);
    const auto feather=[&](std::initializer_list<std::array<float,2>> points,Color ink) {
        for(const float side:{-1.f,1.f}) {
            std::vector<std::array<float,2>> shape;
            for(auto p:points)shape.push_back({center+side*(gap+p[0]*scale),top+3+p[1]*.8f});
            r.polygon(shape,ink);
        }
    };
    if(level==2){feather({{0,10},{13,10},{0,24}},color);return;}
    const float reach=level==3?42:level==4?56:level==5?64:72;
    feather({{0,25},{16,10},{reach,8},{reach-16,16}},color);
    if(level>=5)feather({{2,32},{13,25},{43,23},{33,29}},color);
    if(level>=7)feather({{28,31},{38,26},{61,23},{50,29}},color);
    for(int mark=0;mark<std::clamp(level-7,0,3);++mark) {
        const float x=12+mark*15;
        feather({{x,5},{x+11,4},{x+6,9},{x-5,10}},color);
    }
    if(level>=11)feather({{61,0},{70,0},{65,5}},color);
}
void booster(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g) {
    if(!b.quickPlay)return;
    constexpr Color colors[]={{.92f,.95f,1},{.92f,.15f,.10f},{1,.46f,.10f},{.67f,.86f,.24f},
        {.24f,.53f,1},{.69f,.36f,1},{1,.69f,.21f},{.22f,.93f,.66f},{.20f,.79f,.90f},{.86f,.40f,1},{1,1,1}};
    const int level=std::max(1,int(b.booster));const auto color=b.obscuredColors?muted:colors[std::min(level,11)-1];
    // Replace the floor's two-pixel accent with XP, without a gap. All rank
    // decorations remain below the well and above the keyboard/replay footer.
    const float x=g.left,w=g.width,center=x+w/2,y=g.bottom()+5;
    r.rect(x,g.bottom()+1,w,2,borderColor);
    r.rect(x,g.bottom()+1,w*float(b.boosterProgress),2,color);
    const auto altitude=decimal(b.altitude,1)+" m";
    const float altitudeWidth=std::min(w*.6f,r.measure(altitude,24)+12);
    r.centered_text({center-altitudeWidth/2,y+4,altitudeWidth,24},altitude,24,textColor);
    r.centered_text({x,y+31,w*.45f,14},"BOOST "+std::to_string(level),13,color);
    r.centered_text({x+w*.5f,y+31,w*.5f,14},decimal(b.experience,1)+" / "+decimal(std::max(1.,b.booster)*4,0)+" XP",11,muted);
    const float gap=altitudeWidth/2+4;
    booster_emblem(r,level,center,gap,w/2-gap-2,y,color);
}
void field(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,bool miniature=false,bool showProgress=true) {
    if(b.width<=0 || b.bottom<0)return;
    r.rect(g.left-2,g.top,g.width+4,g.height+3,wellColor);
    r.rect(g.left-2,g.top,2,g.height+3,borderColor);r.rect(g.left+g.width,g.top,2,g.height+3,borderColor);
    r.rect(g.left,g.bottom()+1,g.width,2,accent);
    const int first=first_display_row(b,g);const float unit=g.unit;
    r.clip(Rect{g.left,g.ceiling,g.width,std::max(0.f,g.bottom()-g.ceiling)});
    for(int row=0;row<b.bottom;++row)for(int col=0;col<b.width;++col)
        r.rect(g.left+col*unit,g.top+row*unit,unit-std::min(1.f,unit*.1f),unit-std::min(1.f,unit*.1f),gridColor);
    if(!miniature && showProgress)progress_field(r,b,g,false);
    for(int row=first;row<b.bottom;++row)for(int col=0;col<b.width;++col) {
        const auto& cell=b.at(row,col);const float px=g.left+col*unit,py=g.top+row*unit;
        if(cell.appearance!=Presentation::CellAppearance::Empty) {
            auto c=displayed_cell_color(cell,b.obscuredColors);
            block(r,px,py,unit,c,false,b.squareStyle);
            if(cell.value.is_timed_number() && !miniature) {
                const float tileWidth=unit*(b.squareStyle?.65f:1.f);
                const Color ink=b.obscuredColors?wellColor:Color{1,1,1};
                r.centered_text({px+(unit-tileWidth)/2+2,py+2,tileWidth-4,unit-4},
                    std::to_string(cell.value.timed_countdown()),unit*.96f,ink);
            }
        }
    }
    const auto draw=[&](const Presentation::PieceView& p,bool ghost) {
        const auto definition=Piece::get_definition(p.type);auto c=cell_color(definition.color_id(),b.obscuredColors);
        if(!ghost && p.appearance==Presentation::CellAppearance::Dimmed){c.r*=.35f;c.g*=.35f;c.b*=.35f;}
        for(const auto& cell:definition.cells(p.pose.rotation)) {
            const int row=p.pose.position.row+cell[0],col=p.pose.position.col+cell[1];
            if(row>=first && row<b.bottom && col>=0 && col<b.width)block(r,g.left+col*unit,g.top+row*unit,unit,c,ghost,b.squareStyle);
        }
    };
    if(b.ghost)draw(*b.ghost,true);if(b.active)draw(*b.active,false);
    // Markers are the final field layer, above settled, ghost and active cells.
    // On numbered solids a corner dot leaves the centered countdown readable.
    for(int row=first;row<b.bottom;++row)for(int col=0;col<b.width;++col) {
        const auto& cell=b.at(row,col);if(!cell.hint)continue;
        const bool numbered=cell.value.is_timed_number();
        const float size=unit*(numbered?.14f:.2f);
        const float x=g.left+(col+(numbered?.72f:.5f))*unit-size/2;
        const float y=g.top+(row+(numbered?.2f:.5f))*unit-size/2;
        r.rounded(x,y,size,size,size/2,{.45f,.45f,.45f});
    }
    if(!miniature && showProgress)progress_field(r,b,g,true);
    r.clip({});
}
void board_rails(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,bool solo,const BoardEvents& events,
    const std::vector<std::string>& messages={}) {
    const float left=g.left-26-g.rail,scale=std::min(1.f,g.rail/160);
    const auto cues=progress_cues(b);
    // Preview rails have their own height, including on very short fields.
    const float cueHeight=cues.size()*54.f;
    if(b.holdEnabled) {
        const float height=hold_panel_height(g),previewTop=std::max(std::min(31*scale,height*.55f),r.text_size(12*scale)+12);
        r.rect(left,g.top,g.rail,height,panelColor);r.rect(left,g.top,g.rail,2,borderColor);
        right_text(r,left+g.rail-12,g.top+9,"HOLD",12*scale,b.holdAvailable?accent:muted);
        preview_box(r,b.hold,{left+12,g.top+previewTop,g.rail-24,std::min(54*scale,height-previewTop-8*scale)},b.holdAvailable,b.obscuredColors,b.squareStyle);
    }
    const auto nextCount=next_slot_count(b);
    if(nextCount) {
        const auto layout=next_layout(r,b,g,solo);
        const float top=layout.top,x=g.left+g.width+20,step=layout.step;
        r.rect(x,top,g.next,30+step*nextCount,panelColor);r.rect(x,top,g.next,2,borderColor);
        r.text(x+12,top+9,"NEXT",12*scale,accent);
        for(std::size_t i=0;i<b.next.size();++i)preview_box(r,b.next[i],{x+6,top+31+i*step,g.next-12,step-std::min(8.f,step*.2f)},true,b.obscuredColors,b.squareStyle);
    }
    for(std::size_t i=0;i<cues.size();++i)
        progress_panel(r,cues[i],{g.left+g.width+20,railTop+railHeight-cueHeight+i*54,g.next,48});
    board_hud(r,b,g,solo,events,messages);booster(r,b,g);
    if(!b.quickPlay && !b.username.empty()) {
        const float size=13,y=g.bottom()+10;
        const auto wins=solo?std::string{}:"W "+std::to_string(b.wins);
        const float badge=wins.empty()?0:r.measure(wins,size)+14;
        const float width=std::min(g.width-badge,r.measure(b.username,size)),x=g.left+(g.width-width-badge)/2;
        r.fitted(x,y,b.username,width,size,muted);
        if(!wins.empty())r.text(x+width+14,y,wins,size,accent);
    }

}
void clear_celebration(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,SessionStatus status,const BoardEvents& events) {
    if((!events.perfectClear && !events.halfClear) || status==SessionStatus::Paused || status==SessionStatus::Countdown)return;
    const float h=std::min(108.f,std::max(52.f,g.height*.25f));
    const float center=g.top+g.height*(status==SessionStatus::Finished?.27f:.4f),top=center-h/2;
    const Color ink=b.obscuredColors?textColor:Color{1,.96f,.67f};
    // Two centered display lines, as in TETR.IO's ALL / CLEAR action text.
    // No opaque panel or animation hides the board behind this message.
    r.clip(Rect{g.left+3,g.ceiling,g.width-6,g.bottom()-g.ceiling});
    r.text_effect(events.motion(EventKind::AllClear),g.left+g.width/2,center,[&] {
        r.centered_text({g.left+8,top,g.width-16,h*.44f},events.perfectClear?"ALL":"HALF",66,ink,1,.10f);
        r.centered_text({g.left+8,top+h*.51f,g.width-16,h*.49f},"CLEAR",66,ink,1,.10f);
    });r.clip({});
}
SessionStatus board_status(const SessionView& view,const Presentation::BoardView& board) {
    if(view.status==SessionStatus::Paused || view.status==SessionStatus::Countdown)return view.status;
    if(board.finished || board.result!=Presentation::RoundResult::None)return SessionStatus::Finished;
    return view.room || !view.peers.empty()?SessionStatus::Playing:view.status;
}
void state_overlay(Renderer& r,const Presentation::BoardView& b,SessionStatus status,const BoardGeometry& g,bool battle=false) {
    if(status!=SessionStatus::Paused && status!=SessionStatus::Finished && b.headline.empty())return;
    const float h=64,y=g.top+(g.height-h)/2;
    std::string title=status==SessionStatus::Paused?"PAUSED":b.headline.empty()?status_name(status):b.headline;
    if(status!=SessionStatus::Paused && battle) {
        if(b.result==Presentation::RoundResult::Win || b.headline=="YOU WIN" || b.headline=="WIN")title="WIN";
        else if(b.result==Presentation::RoundResult::Defeat || b.headline=="GAME OVER" || b.headline=="LOSE")title="DEFEAT";
        else if(b.headline.empty())return; // Preserve explicit mode/disconnect messages.
    }
    r.rect(g.left,y,g.width,h,{panelColor.r,panelColor.g,panelColor.b,.95f});
    r.outline(g.left,y,g.width,h,accent);
    r.centered_text({g.left+12,y+10,g.width-24,h-20},title,26,accent);
}
void countdown_overlay(Renderer& r,int digit,const BoardGeometry& g) {
    if(digit<1 || digit>3)return;
    // Two-row custom fields still need a readable countdown. Only the
    // overlay may extend past the well; the field geometry remains fixed.
    const float h=std::clamp(g.height*.65f,100.f,148.f),w=std::min(150.f,g.width*.8f);
    const float y=g.top+(g.height-h)/2;
    r.rounded(g.left+(g.width-w)/2,y,w,h,10,{panelColor.r,panelColor.g,panelColor.b,.94f});
    r.centered_text({g.left,y+12,g.width,h-42},std::to_string(digit),104,accent);
    r.centered_text({g.left,y+h-24,g.width,14},"READY",11,muted);
    for(int i=0;i<3;++i)r.rect(g.left+g.width/2-17+i*13,y+h+9,8,3,i<digit?accent:muted);
}
void overflow_preview(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,bool solo=true) {
    if(b.width<=0 || b.bottom<0)return;
    // Fill NEXT's width, using all space from the header to its actual top.
    // Well headroom is measured in full-size cells, not preview-size cells.
    const auto preview=top_preview_geometry(g,b.width,next_layout(r,b,g,solo).top);
    const float u=preview.unit;
    const auto x=[&](int col){return preview.left+col*u;};
    const auto y=[&](int row){return preview.top+(row+preview.rows)*u;};
    r.fitted(preview.left,preview.top-r.text_size(9)-6,"TOP "+std::to_string(preview.rows),preview.width,9,muted);
    r.rect(preview.left,preview.top,preview.width,preview.height,panelColor);
    r.outline(preview.left,preview.top,preview.width,preview.height,borderColor);
    r.clip(Rect{preview.left,preview.top,preview.width,preview.height});
    for(int row=-preview.rows;row<0;++row)for(int col=0;col<b.width;++col)
        r.rect(x(col),y(row),std::max(0.f,u-.5f),std::max(0.f,u-.5f),gridColor);
    const auto cell=[&](int row,int col,Color color,bool ghost=false){
        if(row>=-preview.rows && row<0 && col>=0 && col<b.width)
            r.mino(x(col),y(row),u,color,ghost,b.squareStyle);
    };
    const int first=std::max(b.firstRow,-preview.rows);
    for(int row=first;row<0;++row)for(int col=0;col<b.width;++col) {
        const auto& c=b.at(row,col);if(c.appearance!=Presentation::CellAppearance::Empty) {
            auto color=displayed_cell_color(c,b.obscuredColors);cell(row,col,color);
        }
    }
    const auto draw=[&](const std::optional<Presentation::PieceView>& p,bool ghost){if(p) {
        const auto d=Piece::get_definition(p->type);auto color=cell_color(d.color_id(),b.obscuredColors);
        if(p->appearance==Presentation::CellAppearance::Dimmed){color.r*=.35f;color.g*=.35f;color.b*=.35f;}
        for(const auto& c:d.cells(p->pose.rotation))cell(p->pose.position.row+c[0],p->pose.position.col+c[1],color,ghost);
    }};draw(b.ghost,true);draw(b.active,false);
    // Consume shared spawn/construction hints, including the actual next
    // spawn pose and the user's visibility setting. Dots are the top layer.
    for(int row=first;row<0;++row)for(int col=0;col<b.width;++col)if(b.at(row,col).hint) {
        const float size=std::min(u,std::max(2.f,u*.3f));
        r.rounded(x(col)+(u-size)/2,y(row)+(u-size)/2,size,size,size/2,{.45f,.45f,.45f});
    }
    r.clip({});
}
void field_feedback(Renderer& r,const Presentation::BoardView& b,const BoardGeometry& g,const FieldEffects& effects) {
    if(b.width<=0 || g.height<=0)return;
    const float landing=effects.strength(effects.settings.landing),clear=effects.strength(effects.settings.clear);
    const float trail=effects.strength(effects.settings.trail),particles=effects.strength(effects.settings.particles);
    const auto tint=[&](int id,float alpha) {
        auto c=b.obscuredColors?textColor:id<0?Color{1,.95f,.82f}:cell_color(id,false);
        c.a=std::clamp(alpha,0.f,1.f);return c;
    };
    const auto ring=[&](float x,float y,float rx,float ry,Color color,float thickness) {
        for(int i=0;i<48;++i) {
            const float a=i*6.2831853f/48,z=(i+1)*6.2831853f/48;
            r.line(x+std::cos(a)*rx,y+std::sin(a)*ry,x+std::cos(z)*rx,y+std::sin(z)*ry,color,thickness);
        }
    };
    // Preserve ordering: light is added after the stack, with HUD drawn above.
    r.additive([&] {
        r.clip(Rect{g.left,g.ceiling,g.width,g.bottom()-g.ceiling});
        for(const auto& pulse:effects.pulses()) {
            const float age=float(effects.time()-pulse.born),energy=std::sqrt(pulse.energy);
            if(age<0)continue;
            if(trail>0 && age<.30f && pulse.dropDistance>1) {
                const float opacity=std::pow(1-age/.30f,1.7f)*trail;
                std::map<int,int> columns;
                for(const auto& cell:pulse.cells) {
                    const auto it=columns.find(cell.col);
                    if(it==columns.end() || it->second<cell.row)columns[cell.col]=cell.row;
                }
                for(const auto& [col,row]:columns) {
                    const float end=g.top+(row+1)*g.unit,start=g.top+(row-pulse.dropDistance)*g.unit;
                    const float x=g.left+(col+.5f)*g.unit;
                    r.glow(x,(start+end)/2,1.25f*g.unit,(end-start)*.8f,tint(pulse.color,.20f*opacity));
                    r.gradient(x-.48f*g.unit,start,.96f*g.unit,end-start,tint(pulse.color,0),tint(pulse.color,.65f*opacity));
                    r.gradient(x-.12f*g.unit,start,.24f*g.unit,end-start,tint(-1,0),tint(-1,.85f*opacity));
                    // Thin parallel striations give the beam a fast, directional edge.
                    for(float edge:{-.42f,.42f})r.gradient(x+edge*g.unit,start,.035f*g.unit,end-start,tint(-1,0),tint(-1,.7f*opacity));
                }
            }
            if(landing>0 && age<.46f && !pulse.cells.empty()) {
                const float fade=std::pow(1-age/.46f,2.f)*landing*energy;
                for(const auto& cell:pulse.cells) {
                    const float x=g.left+(cell.col+.5f)*g.unit,y=g.top+(cell.row+.5f)*g.unit;
                    r.glow(x,y,1.9f*g.unit,1.2f*g.unit,tint(pulse.color,.55f*fade));
                    r.rect(x-g.unit/2,y-g.unit/2,g.unit,g.unit,tint(-1,.42f*fade));
                    r.outline(x-g.unit/2+1,y-g.unit/2+1,g.unit-2,g.unit-2,tint(-1,.9f*fade),std::max(1.f,g.unit*.07f));
                }
            }
            if(clear>0 && age<.78f && !pulse.rows.empty()) {
                const float fade=std::pow(1-age/.78f,1.6f)*clear*energy;
                const float flash=std::exp(-age*18)*clear;
                for(int row:pulse.rows) {
                    const float y=g.top+(row+.5f)*g.unit;
                    r.glow(g.left+g.width/2,y,g.width*.8f,(1.3f+age*7)*g.unit,tint(pulse.color,.60f*fade));
                    r.rect(g.left,y-g.unit/2,g.width,g.unit,tint(-1,.65f*flash));
                    const float length=g.width*std::min(1.f,.12f+age*12);
                    r.rect(g.left+(g.width-length)/2,y-g.unit*.05f,length,std::max(1.5f,g.unit*.10f),tint(-1,.95f*fade));
                    r.gradient(g.left,y-(.4f+age*5)*g.unit,g.width,(.4f+age*5)*g.unit,tint(pulse.color,0),tint(pulse.color,.25f*fade));
                    r.gradient(g.left,y,g.width,(.4f+age*5)*g.unit,tint(pulse.color,.25f*fade),tint(pulse.color,0));
                }
                if(pulse.special) {
                    r.glow(g.left+g.width/2,g.bottom(),g.width*.9f,g.height*.48f,tint(pulse.color,.22f*fade));
                    r.outline(g.left+1,g.top,g.width-2,g.height,tint(pulse.color,.32f*fade),std::max(1.f,g.unit*.075f));
                }
            }
        }
        // Let fragments and wave fronts leave the well, but stop short of the
        // adjacent statistics; the full board transform also carries this light.
        const Rect bounds{g.left-g.unit*1.8f,g.ceiling,g.width+3.6f*g.unit,g.bottom()-g.ceiling+g.unit*1.3f};
        r.clip(bounds);
        for(const auto& pulse:effects.pulses()) {
            const float age=float(effects.time()-pulse.born);
            if(age<0)continue;
            if(landing>0 && age<.6f && !pulse.cells.empty()) {
                float x=0,y=g.top;
                for(const auto& cell:pulse.cells){x+=g.left+(cell.col+.5f)*g.unit;y=std::max(y,g.top+(cell.row+1)*g.unit);}
                x/=pulse.cells.size();
                const float fade=std::pow(1-age/.6f,2.f)*landing*std::sqrt(pulse.energy);
                const float rx=(.7f+age*15)*g.unit,ry=(.12f+age*.8f)*g.unit;
                r.glow(x,y,rx*1.4f,ry*3,tint(pulse.color,.65f*fade));
                // The broad glow, coloured rim and fine white core expand at
                // different rates, so a lock reads as an impact, not a flat ring.
                ring(x,y,rx,ry,tint(pulse.color,.27f*fade),std::max(2.f,g.unit*.19f));
                ring(x,y,rx,ry,tint(-1,.78f*fade),std::max(1.f,g.unit*.045f));
            }
            if(clear>0 && age<.65f)for(int row:pulse.rows) {
                const float y=g.top+(row+.5f)*g.unit,fade=std::pow(1-age/.65f,2.f)*clear;
                for(float side:{0.f,1.f}) {
                    const float x=g.left+g.width*side;
                    r.glow(x,y,(1+age*4)*g.unit,(1.7f+age*6)*g.unit,tint(pulse.color,.75f*fade));
                    const float direction=side?1.f:-1.f,reach=std::min(1.65f,age*17)*g.unit;
                    r.line(x,y,x+direction*reach,y,tint(-1,.8f*fade),std::max(1.f,g.unit*.06f));
                }
            }
        }
        for(const auto& p:effects.particles()) {
            const float age=float(effects.time()-p.born),life=age/p.life;
            if(life<0 || life>=1 || particles<=0)continue;
            const auto position=p.position(age);
            const float x=g.left+position.x*g.unit,y=g.top+position.y*g.unit;
            const float size=std::max(.8f,p.size*g.unit*(1-life*.5f));
            if(x<bounds.x+size || x>bounds.x+bounds.w-size || y<bounds.y+size || y>bounds.y+bounds.h-size)continue;
            const float opacity=std::min(1.7f,std::sqrt(particles))*std::pow(1-life,1.2f);
            r.glow(x,y,size*6,size*6,tint(p.color,.32f*opacity));
            if(p.kind==ParticleKind::Fragment) {
                r.shard(x,y,size*2,p.angle+p.spin*age,tint(p.color,.75f*opacity));
                r.glow(x-size*.2f,y-size*.2f,size*.8f,size*.8f,tint(-1,.9f*opacity));
            } else if(p.kind==ParticleKind::Spark) {
                const auto tail=p.position(std::max(0.f,age-.035f));
                const float tx=std::clamp(g.left+tail.x*g.unit,bounds.x,bounds.x+bounds.w),ty=std::clamp(g.top+tail.y*g.unit,bounds.y,bounds.y+bounds.h);
                r.line(tx,ty,x,y,tint(p.color,.42f*opacity),size*2.2f);
                r.line(tx,ty,x,y,tint(-1,.95f*opacity),std::max(.7f,size*.6f));
                r.glow(x,y,size*1.5f,size*1.5f,tint(-1,opacity));
            } else {
                const float twinkle=.65f+.35f*std::sin(age*17+p.angle);
                r.glow(x,y,size*3.5f,size*3.5f,tint(-1,.75f*opacity*twinkle));
            }
        }
        r.clip({});
    });
}
void game_view(Renderer& r,const SessionView& view) {
    const auto& b=view.board;
    if(!b.width || b.cells.empty()){r.text(40,220,view.message.empty()?"Initializing shared game engine...":view.message,16);return;}
    const auto& events=r.events(-1,b,view);
    const auto& effects=r.field_effects(-1,b,view);
    const auto g=solo_geometry(b.width,b.bottom);
    r.board_effect(g.left+g.width/2,g.top+g.height/2,g.unit,effects.transform(),[&] {
        field(r,b,g,false,(view.status==SessionStatus::Playing || view.status==SessionStatus::Countdown));
        garbage_bar(r,b,g);field_feedback(r,b,g,effects);
        board_rails(r,b,g,true,events,{view.challengeNotice,view.message});overflow_preview(r,b,g);
        windup_warning(r,b,g,view.status);
        right_text(r,g.left-26,hud_layout(r,b,g).top-25,std::string(view.replay?"REPLAY / ":"")+status_name(view.status),11,accent,g.rail);
        clear_celebration(r,b,g,view.status,events);
        countdown_overlay(r,view.countdownDigit,g);
        state_overlay(r,b,view.status,g);
    });
    if(view.replay) {
        const float ratio=view.replayDuration>0?std::clamp(view.replayPosition/view.replayDuration,0.,1.):0;
        r.rect(350,746,500,3,borderColor);r.rect(350,746,500*ratio,3,accent);
    }
    key_hints(r,view);
}
void miniature(Renderer& r,const SceneBoard& peer,const BoardGeometry& g,const SessionView& view) {
    const auto& b=peer.board;
    if(peer.logicalWidth<=0)field(r,b,g,true);
    else {
        r.rect(g.left-1,g.top,g.width+2,g.height+2,borderColor);r.rect(g.left,g.top,g.width,g.height,wellColor);
        r.clip(Rect{g.left,g.ceiling,g.width,std::max(0.f,g.bottom()-g.ceiling)});
        const float cw=g.width/std::max(1,b.width),ch=g.unit*peer.sampleRows;
        for(int row=0;row<b.bottom;++row)for(int col=0;col<b.width;++col) {
            const auto& c=b.at(row,col);if(c.appearance==Presentation::CellAppearance::Empty)continue;
            auto color=displayed_cell_color(c,b.obscuredColors);
            r.mino(Rect{g.left+col*cw,g.bottom()-(b.bottom-row)*ch,cw,ch},color,false,b.squareStyle);
        }r.clip({});r.rect(g.left,g.bottom(),g.width,1,accent);
    }
    const float size=std::clamp(g.width/8,8.f,12.f);
    const std::string name=b.username+"  W "+std::to_string(b.wins);
    const float width=std::min(g.width+12,r.measure(name,size));
    r.fitted(g.left+(g.width-width)/2,g.bottom()+5,name,g.width+12,size,muted);
    const auto& events=r.events(peer.slot,b,view);
    if(events.spike>=Rules::kMinimumVisibleSpike) {
        r.clip(Rect{g.left-4,g.ceiling,g.width+8,g.bottom()-g.ceiling});
        r.text_effect(events.motion(EventKind::Spike),g.left+g.width,g.top-9,[&] {
            r.hud_text(g.left+g.width,g.top-9,std::to_string(events.spike)+" SPIKE",std::clamp(g.unit*1.1f,8.f,18.f),
                b.obscuredColors?textColor:Color{1,.85f,.25f},g.width,0);
        });r.clip({});
    }
    if(view.countdownDigit) {
        r.centered_text({g.left,g.top,g.width,g.height},std::to_string(view.countdownDigit),std::min(80.f,g.width*.7f),accent,1);
    } else if(b.result!=Presentation::RoundResult::None) {
        const float height=std::min(32.f,g.height),y=g.top+(g.height-height)/2;
        r.rect(g.left,y,g.width,height,{panelColor.r,panelColor.g,panelColor.b,.95f});
        r.centered_text({g.left+2,y+2,g.width-4,height-4},b.result==Presentation::RoundResult::Win?"WIN":"DEFEAT",18,accent);
    }
}
void scene_view(Renderer& r,const SessionView& view) {
    std::vector<const Presentation::BoardView*> full;std::vector<int> slots;std::vector<const SceneBoard*> mini;
    if(!view.spectator && view.ownBoardVisible){full.push_back(&view.board);slots.push_back(-1);}
    for(const auto& p:view.peers)if(p.miniature)mini.push_back(&p);else {full.push_back(&p.board);slots.push_back(p.slot);}
    std::vector<FieldSize> sizes,small;
    for(const auto* b:full)sizes.push_back({b->width,b->bottom});
    for(const auto* p:mini)small.push_back({p->logicalWidth>0?p->logicalWidth:p->board.width,p->logicalHeight>0?p->logicalHeight:p->board.bottom});
    // Touch controls float over the scene; only desktop reserves its help rail.
    const float left=r.scene_left()+(r.touch_controls()?40.f:gameplayLeft);
    const auto layout=multiplayer_geometry(sizes,small,left,r.scene_right()-40);
    for(std::size_t i=0;i<layout.full.size();++i) {
        const auto& events=r.events(slots[i],*full[i],view);
        const auto& effects=r.field_effects(slots[i],*full[i],view);
        const auto& g=layout.full[i];
        r.board_effect(g.left+g.width/2,g.top+g.height/2,g.unit,effects.transform(),[&] {
            field(r,*full[i],g,false,(board_status(view,*full[i])==SessionStatus::Playing || board_status(view,*full[i])==SessionStatus::Countdown));
            garbage_bar(r,*full[i],g);field_feedback(r,*full[i],g,effects);
            board_rails(r,*full[i],g,false,events);overflow_preview(r,*full[i],g,false);
            const auto status=board_status(view,*full[i]);
            windup_warning(r,*full[i],g,status);
            clear_celebration(r,*full[i],g,status,events);
            countdown_overlay(r,view.countdownDigit,g);state_overlay(r,*full[i],status,g,true);
        });
    }
    for(std::size_t i=0;i<layout.mini.size();++i)miniature(r,*mini[i],layout.mini[i],view);
    r.fitted(left,94,view.roomStatus,r.scene_right()-left-40,13,accent);
    key_hints(r,view);
}
}
