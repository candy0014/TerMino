#include "mobile_ui.h"
#include "../opengl/scene.h"
#include "tetr/util/text_codec.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Graphical::Mobile {
namespace {
std::string tr(bool zh,const char* english,const char* chinese) {return zh?chinese:english;}
std::string number(double n) {std::ostringstream s;s<<std::setprecision(6)<<n;return s.str();}
const std::array<const char*,11> english={"Left","Right","Soft drop","Hard drop","CW","CCW","180","Hold","Pause","Retry","Undo"};
const std::array<const char*,11> chinese={"左移","右移","软降","硬降","顺转","逆转","180°","暂存","暂停","重开","撤回"};
std::string label(const Button& b,bool zh) {
    const auto i=std::find(actions.begin(),actions.end(),b.action);
    return i==actions.end()?b.action:(zh?chinese:english)[std::size_t(i-actions.begin())];
}
bool inside(Rect r,Point p) {return p.x>=r.x && p.x<r.x+r.w && p.y>=r.y && p.y<r.y+r.h;}
Point local(Point p,const Button& b,const Area& a) {
    const double u=std::min(a.width,a.height),angle=-b.rotation*3.141592653589793/180;
    const double dx=p.x-a.x-b.center.x*a.width,dy=p.y-a.y-b.center.y*a.height;
    return {std::clamp((dx*std::cos(angle)-dy*std::sin(angle))/(u*b.width)+.5,0.,1.),
        std::clamp((dx*std::sin(angle)+dy*std::cos(angle))/(u*b.height)+.5,0.,1.)};
}
double signed_area(Point a,Point b,Point c) {return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
// Ear clipping keeps arbitrary concave custom buttons consistent with hit tests.
void fill(Renderer& r,std::vector<Point> points,Color color) {
    double area=0;for(std::size_t i=0;i<points.size();++i){auto a=points[i],b=points[(i+1)%points.size()];area+=a.x*b.y-a.y*b.x;}
    if(area<0)std::reverse(points.begin(),points.end());
    while(points.size()>=3) {
        bool cut=false;
        for(std::size_t i=0;i<points.size();++i) {
            const auto a=points[(i+points.size()-1)%points.size()],b=points[i],c=points[(i+1)%points.size()];
            if(signed_area(a,b,c)<=1e-8)continue;
            bool occupied=false;
            for(std::size_t j=0;j<points.size();++j)if(j!=i && j!=(i+1)%points.size() && j!=(i+points.size()-1)%points.size()) {
                const auto p=points[j];if(signed_area(a,b,p)>=0 && signed_area(b,c,p)>=0 && signed_area(c,a,p)>=0){occupied=true;break;}
            }
            if(occupied)continue;
            r.polygon({{float(a.x),float(a.y)},{float(b.x),float(b.y)},{float(c.x),float(c.y)}},color);
            points.erase(points.begin()+i);cut=true;break;
        }
        if(!cut)break;
    }
}
std::string value(const Button& b,int field) {
    switch(field) {
        case 0:return number(b.center.x);case 1:return number(b.center.y);
        case 2:return number(b.width);case 3:return number(b.height);case 4:return number(b.opacity);
        case 5:return number(b.rotation);case 6:return number(b.cornerRadius);case 7:return std::to_string(b.z);
        case 8:return std::array<const char*,4>{"RECT","ROUND","ELLIPSE","POLYGON"}[int(b.shape)];
        case 9:return b.player?"P2":"P1";case 10:return b.enabled?"ON":"OFF";case 11:return b.action;
        case 12:{std::string result;for(auto p:b.vertices){if(!result.empty())result+=';';result+=number(p.x)+","+number(p.y);}return result;}
    }return {};
}
void button_icon(Renderer& r,const Button& b,const Area& area,Color color) {
    const double angle=b.rotation*3.141592653589793/180;
    const float side=float(std::min(area.width,area.height)*std::min(b.width,b.height));
    const float radius=side*.25f,stroke=std::max(1.3f,side*.022f);
    const auto point=[&](double x,double y) {
        return Point{area.x+b.center.x*area.width+radius*(x*std::cos(angle)-y*std::sin(angle)),
            area.y+b.center.y*area.height+radius*(x*std::sin(angle)+y*std::cos(angle))};
    };
    const auto line=[&](Point a,Point z) {
        a=point(a.x,a.y);z=point(z.x,z.y);r.line(float(a.x),float(a.y),float(z.x),float(z.y),color,stroke);
    };
    const auto path=[&](std::initializer_list<Point> points) {
        auto a=points.begin();for(auto z=a+1;z!=points.end();a=z++)line(*a,*z);
    };
    const auto arrow=[&](double start,double end,bool mirrored=false) {
        const double sign=mirrored?-1:1,pi=3.141592653589793/180;
        const auto at=[&](double a){return Point{sign*.84*std::cos(a*pi),.84*std::sin(a*pi)};};
        Point a=at(start);
        for(int i=1;i<=32;++i){const auto z=at(start+(end-start)*i/32);line(a,z);a=z;}
        const auto tip=at(end),previous=at(end-(end-start)*.025);
        const double dx=tip.x-previous.x,dy=tip.y-previous.y,length=std::hypot(dx,dy);
        const double ux=dx/length,uy=dy/length;
        path({{tip.x-.30*ux+.25*uy,tip.y-.30*uy-.25*ux},tip,
            {tip.x-.30*ux-.25*uy,tip.y-.30*uy+.25*ux}});
    };
    const auto piece=[&] {
        path({{-.24,-.30},{.04,-.30},{.04,.05},{.30,.05},{.30,.30},{-.24,.30},{-.24,-.30}});
    };
    if(b.action=="MoveLeft")path({{.40,-.65},{-.40,0},{.40,.65}});
    else if(b.action=="MoveRight")path({{-.40,-.65},{.40,0},{-.40,.65}});
    else if(b.action=="SoftDrop")path({{-.68,-.35},{0,.35},{.68,-.35}});
    else if(b.action=="HardDrop") {
        path({{-.68,-.52},{0,.18},{.68,-.52}});path({{-.68,.65},{.68,.65}});
    } else if(b.action=="RotateClockwise" || b.action=="RotateCounterclockwise") {
        arrow(-140,140,b.action=="RotateCounterclockwise");piece();
    } else if(b.action=="Rotate180") {
        arrow(-165,-15);arrow(15,165);piece();
    } else if(b.action=="Hold") {
        path({{-.72,.0},{-.72,.65},{.72,.65},{.72,.0}});
        path({{-.25,-.8},{.25,-.8},{.25,-.30},{-.25,-.30},{-.25,-.8}});
        path({{-.30,-.10},{0,.20},{.30,-.10}});
    } else if(b.action=="Pause") {
        line({-.32,-.70},{-.32,.70});line({.32,-.70},{.32,.70});
    } else if(b.action=="Retry") {
        arrow(-140,140,true);
        const auto a=point(-.16,-.28),z=point(.28,0),c=point(-.16,.28);
        r.polygon({{float(a.x),float(a.y)},{float(z.x),float(z.y)},{float(c.x),float(c.y)}},color);
    } else if(b.action=="Undo") {
        path({{-.22,-.76},{-.76,-.22},{-.22,.32}});
        path({{-.76,-.22},{.13,-.22},{.59,-.02},{.76,.36},{.59,.73},{.13,.83}});
    }
}
}
Rect editor_field_bounds(const Area& a) {
    const float unit=float(std::min(a.height*.86/20,(a.width-32)/10));
    return {float(a.x+a.width/2)-5*unit,float(a.y+a.height/2)-10*unit,10*unit,20*unit};
}
Rect property_bounds(const Area& a,const Button& selected) {
    const float width=std::min(414.f,std::max(180.f,float(a.width/2)-180));
    const float x=selected.center.x<.5?float(a.x+a.width)-width-14:float(a.x)+168;
    return {x,float(a.y+55),width,float(std::min(730.,a.height-65))};
}
int action_index(const std::string& a) {
    static constexpr std::array<const char*,13> names={"MoveLeft","MoveRight","SoftDrop","HardDrop",
        "RotateClockwise","RotateCounterclockwise","Rotate180","Hold","Retry","OpenSettings","SaveReplay","Undo","Pause"};
    const auto i=std::find(names.begin(),names.end(),a);return i==names.end()?-1:int(i-names.begin());
}
Controls::Controls() {
    try{config_=UserConfig::load_virtual_controls();}
    catch(const std::exception& e){config_=defaults();message_=e.what();}
}
Layout& Controls::draft_layout() {for(auto& l:draft_.layouts)if(l.id==draft_.active)return l;throw std::runtime_error("Missing layout");}
Button* Controls::selected() {auto& buttons=draft_layout().buttons;return selected_<buttons.size()?&buttons[selected_]:nullptr;}
void Controls::edit() {
    draft_=config_;editing_=true;properties_=false;railOpen_=false;selected_=0;railOffset_=0;field_=-1;drag_=0;message_.clear();
}
Frame Controls::frame(const SessionView& view,Area a,std::optional<double> windowLeft) {
    if(view.ui) {
        const auto& m=view.ui->metadata;chinese_=m.chinese;
        if(page_!=m.page){page_=m.page;railOffset_=0;}
    }
    Frame f;f.layout=editing_?active_layout(draft_):active_layout(config_);f.area=a;f.editor=editing_;
    if(!editing_)for(auto& b:f.layout.buttons) {
        const int action=action_index(b.action);
        b.available=action>=0 && view.actionEnabled[b.player][std::size_t(action)];
    }
    f.railBounds={float(windowLeft.value_or(a.x)),float(a.y),156,float(a.height)};
    const bool playing=gameplay_surface(view) && (view.status==SessionStatus::Playing ||
        view.status==SessionStatus::Paused || view.status==SessionStatus::Countdown);
    const bool finished=gameplay_surface(view) && view.status==SessionStatus::Finished;
    const int context=editing_?3:finished?2:playing?1:0;
    if(context!=railContext_){railOpen_=false;railOffset_=0;}
    railContext_=context;f.railToggle=context!=0;f.railVisible=!f.railToggle || railOpen_;
    f.properties=properties_ && drag_==0;f.chinese=chinese_;f.selected=selected_;f.page=page_;f.field=field_;
    const float top=f.railToggle?84:12;
    f.railViewport={f.railBounds.x,f.railBounds.y+top,f.railBounds.w,std::max(0.f,f.railBounds.h-top-12)};
    f.input=input_;f.message=message_;f.rail=rail(f,view);
    std::vector<Command> commands;for(const auto& b:f.rail)commands.push_back(b.command);
    if(commands!=railCommands_){railOffset_=0;railCommands_=std::move(commands);}
    railMaxOffset_=std::max(0.f,float(f.rail.size())*82-f.railViewport.h);
    railOffset_=std::clamp(railOffset_,0.f,railMaxOffset_);
    f.railOffset=railOffset_;f.railMaxOffset=railMaxOffset_;f.rail=rail(f,view);
    if(!editing_ && view.status==SessionStatus::Finished && (!view.ui || view.ui->metadata.kind==Frontend::UiKind::Notice)) {
        const float x=float(std::max(a.x+174,a.x+a.width/2-267)),y=float(a.y+a.height-65);
        f.tools={{Command::Tab,tr(chinese_,"Statistics","统计"),{x,y,166,53}},
            {Command::Save,tr(chinese_,"Save replay","保存录像"),{x+180,y,166,53}},
            {Command::Export,tr(chinese_,"Export replay","导出录像"),{x+360,y,166,53}}};
    }
    return f;
}
void Controls::command(Command c) {
    if(c==Command::ToggleRail){railOpen_=!railOpen_;return;}
    if(!editing_)return;
    if(c==Command::Back){if(field_>=0){cancel_field();return;}editing_=false;return;}
    if(c==Command::Apply) {
        if(field_>=0){accept_field();return;}
        std::string error;if(!valid(draft_,&error)){message_=error;return;}
        if(!UserConfig::save_virtual_controls(draft_)){message_="Could not save; existing config preserved";return;}
        config_=draft_;editing_=false;return;
    }
    if(c==Command::Properties){if(field_>=0)cancel_field();properties_=!properties_;return;}
    if(field_>=0){accept_field();if(field_>=0)return;}
    if(c==Command::Reset){draft_=defaults();selected_=0;field_=-1;return;}
    auto& buttons=draft_layout().buttons;
    if(c==Command::Previous && !buttons.empty()){selected_=(selected_+buttons.size()-1)%buttons.size();return;}
    if(c==Command::Next && !buttons.empty()){selected_=(selected_+1)%buttons.size();return;}
    if(c==Command::Duplicate && buttons.size()<256) {
        auto b=selected()?*selected():defaults().layouts[0].buttons[0];
        b.center.x=std::min(.95,b.center.x+.03);
        unsigned suffix=1;do{b.id="button-"+std::to_string(suffix++);}while(std::any_of(buttons.begin(),buttons.end(),[&](const auto& v){return v.id==b.id;}));
        buttons.push_back(b);selected_=buttons.size()-1;return;
    }
    if(c==Command::Remove && selected()){buttons.erase(buttons.begin()+selected_);selected_=buttons.empty()?0:std::min(selected_,buttons.size()-1);return;}
    if(c==Command::Player && selected())selected()->player=1-selected()->player;
    if(c==Command::Enabled && selected())selected()->enabled=!selected()->enabled;
}
void Controls::editor_down(Point p,const Area& a) {
    message_.clear();
    if(field_>=0)return;
    if(properties_ && selected() && inside(property_bounds(a,*selected()),p)) {
        const auto box=property_bounds(a,*selected());const int row=int((p.y-box.y-46)/propertyRowHeight);
        if(p.y<box.y+40 && p.x>box.x+box.w-50){properties_=false;return;}
        if(row<0 || row>12 || !selected())return;
        auto& b=*selected();
        if(row==8){b.shape=Shape((int(b.shape)+1)%4);if(b.shape==Shape::Polygon && b.vertices.empty())b.vertices={{0,.5},{.25,0},{.75,0},{1,.5},{.75,1},{.25,1}};return;}
        if(row==9){b.player=1-b.player;return;}if(row==10){b.enabled=!b.enabled;return;}
        if(row==11){const auto i=std::find(actions.begin(),actions.end(),b.action);b.action=actions[(std::size_t(i-actions.begin())+1)%actions.size()];return;}
        if(row==12 && b.shape!=Shape::Polygon)return;
        field_=row;input_=value(b,row);replaceInput_=true;return;
    }
    if(auto* b=selected()) {
        const double u=std::min(a.width,a.height);
        const auto handle=Point{a.x+b->center.x*a.width+b->width*u/2,a.y+b->center.y*a.height+b->height*u/2};
        if(std::hypot(p.x-handle.x,p.y-handle.y)<25){drag_=2;return;}
        if(b->shape==Shape::Polygon) {
            const auto points=outline(*b,a);
            for(std::size_t i=0;i<points.size();++i)if(std::hypot(p.x-points[i].x,p.y-points[i].y)<20){drag_=3;vertex_=int(i);return;}
        }
    }
    if(const auto i=hit(draft_layout(),a,p,true)){selected_=*i;drag_=1;dragStart_=p;buttonStart_=selected()->center;}
}
void Controls::editor_move(Point p,const Area& a) {
    auto* b=selected();if(!b)return;
    if(drag_==1)b->center={std::clamp(buttonStart_.x+(p.x-dragStart_.x)/a.width,0.,1.),std::clamp(buttonStart_.y+(p.y-dragStart_.y)/a.height,0.,1.)};
    if(drag_==2){const double u=std::min(a.width,a.height);b->width=std::clamp(2*std::abs(p.x-a.x-b->center.x*a.width)/u,.02,8.);b->height=std::clamp(2*std::abs(p.y-a.y-b->center.y*a.height)/u,.02,8.);}
    if(drag_==3 && vertex_>=0 && std::size_t(vertex_)<b->vertices.size())b->vertices[vertex_]=local(p,*b,a);
}
void Controls::backspace(){if(replaceInput_){input_.clear();replaceInput_=false;}else if(!input_.empty())input_.pop_back();}
void Controls::accept_field() {
    auto* b=selected();if(field_<0 || !b)return;
    auto old=*b;bool accepted=true;
    if(field_==12) {
        b->vertices.clear();for(const auto point:Util::Text::split(input_,';')) {
            const auto pair=Util::Text::split_exact<2>(point,',');if(!pair){accepted=false;break;}
            const auto x=Util::Text::parse_finite_real((*pair)[0]),y=Util::Text::parse_finite_real((*pair)[1]);
            if(!x || !y){accepted=false;break;}b->vertices.push_back({*x,*y});
        }
    } else {
        const auto n=Util::Text::parse_finite_real(input_);if(!n)accepted=false;
        else switch(field_) {
            case 0:b->center.x=*n;break;case 1:b->center.y=*n;break;case 2:b->width=*n;break;
            case 3:b->height=*n;break;case 4:b->opacity=*n;break;case 5:b->rotation=*n;break;
            case 6:b->cornerRadius=*n;break;case 7:if(*n>=-1000000 && *n<=1000000 && std::floor(*n)==*n)b->z=int(*n);else accepted=false;break;
        }
    }
    std::string error;if(!accepted || !valid(draft_,&error)){*b=old;message_=error.empty()?"Invalid number":error;return;}
    field_=-1;input_.clear();message_.clear();
}
std::vector<RailButton> rail(const Frame& f,const SessionView& view) {
    using C=Command;using K=Frontend::UiKind;using A=Frontend::UiKeyAction;
    std::vector<std::pair<C,std::string>> buttons;
    const auto add=[&](C c,const char* en,const char* zh){buttons.emplace_back(c,tr(f.chinese,en,zh));};
    if(!f.railVisible)return {};
    const auto* m=view.ui?&view.ui->metadata:nullptr;
    if(f.editor) {
        add(C::Back,"Back","返回");add(C::Apply,"Apply","应用");
        if(f.field<0) {
            add(C::Properties,"Properties","属性");
            if(!f.layout.buttons.empty()) {add(C::Previous,"Previous button","上一个键");add(C::Next,"Next button","下一个键");}
            if(f.layout.buttons.size()<256)add(C::Duplicate,"Add / copy","添加/复制");
            add(C::Reset,"Defaults","默认布局");
            if(f.selected<f.layout.buttons.size()) {
                add(C::Remove,"Delete button","删除按键");add(C::Player,"P1 / P2","切换玩家");add(C::Enabled,"Show / hide","启用/隐藏");
            }
        }
    } else if(m && m->kind==K::Start)return {};
    else if(m && (m->kind==K::Text || m->kind==K::KeyCapture)) {
        add(C::Back,"Back","返回");if(m->kind==K::Text)add(C::Confirm,"Confirm","确认");
    } else if(m && (m->kind==K::Menu || m->kind==K::Choices)) {
        add(C::Back,"Back","返回");
        if(m->selected<m->entries.size() && m->entries[m->selected].enabled && m->entries[m->selected].control!=Frontend::UiControl::Separator)
            add(C::Confirm,"Enter","进入");
        for(const auto action:m->keyActions)switch(action) {
            case A::Retry:add(C::Retry,"Retry","重开");break;
            case A::Root:if(m->id!="root")add(C::Root,"Root settings","设置根目录");break;
            case A::Exit:add(C::Exit,"Exit","退出");break;
            case A::SwitchTarget:add(C::SwitchTarget,"Switch target","切换对象");break;
            case A::SavePreset:add(C::SavePreset,"Save preset","保存预设");break;
            case A::Undefined:add(C::Undefined,"Undefine","恢复未定义");break;
            case A::SaveMap:add(C::MapSave,"Save map","保存地图");break;
        }
    } else if(m && m->kind==K::Statistics) {
        add(C::Back,"Back","返回");
        if(view.actionEnabled[0][10]){add(C::Save,"Save replay","保存录像");add(C::Export,"Export replay","导出录像");}
        if(view.actionEnabled[0][8])add(C::Retry,"Retry","重开");
        if(view.actionEnabled[0][9])add(C::Settings,"Settings","设置");
    } else if(m && m->kind==K::MapEditor && m->editor) {
        add(C::Back,"Back","返回");add(C::MapOptions,"Options","地图选项");add(C::MapSave,"Save map","保存地图");
        add(C::MapUndo,"Undo","撤销");add(C::MapRedo,"Redo","重做");add(C::MapUndoSave,"Undo map save","撤销地图保存");
        const auto& e=*m->editor;
        if(e.sequenceFocus && !e.generated) {
            add(C::Search,"Find piece","检索方块");
            if(e.caret<e.sequenceSize)add(C::Delete,"Delete piece","删除方块");
            if(e.caret>0)add(C::Backspace,"Delete previous","删除前一块");
            add(C::Home,"First position","首个位置");add(C::End,"Last position","最后位置");
            if(e.candidates.size()>std::size_t(e.candidateRows*e.candidateColumns)) {
                add(C::PageUp,"Page up","上一页");add(C::PageDown,"Page down","下一页");
            }
        }
    } else if(m && m->kind==K::Lobby && m->lobby) {
        const auto& l=*m->lobby;
        add(C::Back,"Back","返回");
        if(l.canStart && l.startEnabled)add(C::Confirm,"Start match","开始匹配");
        add(C::Settings,"Settings","设置");add(C::Retry,"Prepare again","重新准备");
        if(l.offset>0)add(C::PageUp,"Previous users","上一页成员");
        if(l.offset+l.pageSize<l.members.size())add(C::PageDown,"Next users","下一页成员");
    } else if(gameplay_surface(view) && (view.status==SessionStatus::Playing || view.status==SessionStatus::Paused ||
        view.status==SessionStatus::Countdown || view.status==SessionStatus::Finished)) {
        if(view.replay) {
            add(C::Confirm,"Pause / play","暂停/播放");
            add(C::Left,"Rewind","后退");add(C::Right,"Forward","前进");
            add(C::Slower,"Slower","减速");add(C::Faster,"Faster","加速");
        }
        if(view.switchTarget)add(C::SwitchTarget,"Switch target","切换对象");
        if(view.actionEnabled[0][8])add(C::Retry,"Retry","重开");
        if(view.actionEnabled[0][9])add(C::Settings,"Settings","设置");
        if(view.endMatch)add(C::EndMatch,"End match","结束本局");
        add(C::Exit,"Exit","退出");
    } else if(m && m->kind==K::Notice)add(C::Back,"Back","返回");
    std::vector<RailButton> result;
    for(std::size_t i=0;i<buttons.size();++i)result.push_back({buttons[i].first,buttons[i].second,
        {f.railBounds.x+8,f.railViewport.y+i*82-f.railOffset,140,76}});
    return result;
}
std::optional<Command> rail_hit(const Frame& f,Point p) {
    if(f.railToggle && inside({f.railBounds.x+8,f.railBounds.y+12,64,56},p))return Command::ToggleRail;
    if(inside(f.railViewport,p))for(const auto& b:f.rail)if(inside(b.bounds,p))return b.command;
    for(const auto& b:f.tools)if(inside(b.bounds,p))return b.command;
    return {};
}
Frontend::UiMetadata touch_metadata(Frontend::UiMetadata m) {
    const auto adapt=[](std::string text) {
        const std::pair<const char*,const char*> replacements[]={
            {"Choose Player 1, Player 2 or Both players; Tab cycles targets without leaving the current item.","Choose Player 1, Player 2 or Both players."},
            {"选择玩家1、玩家2或双方；Tab 循环切换对象并保留当前条目。","选择玩家1、玩家2或双方。"},
            {"Right / Enter cycles the value.",""},{"→ / Enter 切换取值。",""},
            {"Right / Enter opens the available values.",""},{"→ / Enter 打开可选值。",""},
            {"Right / Enter edits; blank input keeps the value.","Leave blank to keep the value."},
            {"→ / Enter 编辑；留空保留当前值。","留空保留当前值。"},
            {"Text input supports Chinese; Enter saves, Esc cancels.","Text input supports Chinese."},
            {"支持中文输入；Enter 保存，Esc 取消。","支持中文输入。"},
            {"Text input supports Chinese; Enter saves.","Text input supports Chinese."},
            {"支持中文输入；Enter 保存。","支持中文输入。"},
            {"Changes apply after confirmation; Left / Backspace goes back.","Changes apply after confirmation."},
            {"确认后应用修改；← / Backspace 返回上层。","确认后应用修改。"},
            {"Changes apply after confirmation; Left / Esc cancels.","Changes apply after confirmation."},
            {"确认后应用修改；← / Esc 取消。","确认后应用修改。"},
            {"Type to replace; arrows edit the full address.","Enter the server address."},
            {"输入替换地址；方向键编辑完整地址。","输入服务器地址。"},
            {"Quick Play modifiers; Right / Enter cycles each value.","Quick Play modifiers."},
            {"快速游戏 Mod；→ / Enter 切换取值。","快速游戏 Mod。"},
            {"U restores this parameter to undefined.","Use the page controls to restore an undefined parameter."},
            {"U 将此参数恢复未定义。","可通过页面操作将此参数恢复未定义。"},
            {"S saves; U makes the selected parameter undefined; Esc exits.","Use the page controls to save or undefine parameters."},
            {"S 保存，U 将选中参数恢复未定义，Esc 退出。","使用页面操作保存参数或恢复未定义。"},
            {"Press Enter again to ","Tap again to "},{"Press Enter twice to ","Tap twice to "},
            {"Enter twice to ","Tap twice to "},{"再次按 Enter ","再次点按"},
            {"再次按Enter","再次点按"},{"再按 Enter ","再次点按"},
            {"再次按回车","再次点按"},{"连续按两次回车","点按两次"},
            {"按两次回车","点按两次"},{"按回车选中","点按选中"},
            {"Arrow keys cancel.","Select another item to cancel."},
            {"方向键取消。","选择其他项目可取消。"},
            {"Enter confirms the preset name. Esc cancels.","Confirm the preset name using the page controls."},
            {"Enter 确认预设名称，Esc 取消。","使用左侧按钮确认预设名称或返回。"},
            {"Enter confirms the map name. Esc cancels.","Confirm the map name using the page controls."},
            {"Enter 确认地图名称，Esc 取消。","使用左侧按钮确认地图名称或返回。"},
            {"Tab cycles targets without leaving the current item.",""}
        };
        for(const auto& [from,to]:replacements) {
            const std::string needle=from;std::size_t at=0;
            while((at=text.find(needle,at))!=std::string::npos){text.replace(at,needle.size(),to);at+=std::char_traits<char>::length(to);}
        }
        const auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
        return first==std::string::npos?std::string{}:text.substr(first,last-first+1);
    };
    m.hint=adapt(m.hint);m.notice=adapt(m.notice);m.shortcuts.clear();
    if(m.kind==Frontend::UiKind::Statistics &&
       (m.hint.find("保存回放键[")!=std::string::npos || m.hint.find("save replay key")!=std::string::npos))
        m.hint=m.chinese?"可通过左侧操作栏保存或导出本局回放。":"Use the page controls to save or export this replay.";
    return m;
}
bool surface(Renderer& r,const Frontend::UiFrame& ui,const Frame& f) {
    r.minimum_text_size(minimumTextSize);
    r.touch_controls(true);
    using K=Frontend::UiKind;const auto kind=ui.metadata.kind;
    if(ui.overlay && kind!=K::Statistics && kind!=K::MapEditor && kind!=K::Lobby &&
        kind!=K::Menu && kind!=K::Choices && kind!=K::Text && kind!=K::KeyCapture)return false;
    const float left=f.rail.empty()?float(f.area.x):std::max(float(f.area.x),f.railBounds.x+f.railBounds.w);
    Frontend::UiFrame page;page.metadata=touch_metadata(ui.metadata);
    page.caret=ui.caret;page.obscured=ui.obscured;page.overlay=ui.overlay;page.extent=ui.extent;
    r.surface_in(page,{left,float(f.area.y),float(f.area.x+f.area.width)-left,float(f.area.height)},contentScale,88);
    return true;
}
void overlay(Renderer& r,const Frame& f,bool gameplay) {
    r.minimum_text_size(minimumTextSize);
    r.touch_controls(true);
    if(f.editor) {
        background(r,false);
        const auto box=editor_field_bounds(f.area);const float unit=box.w/10;
        r.rect(box.x,box.y,box.w,box.h,wellColor);
        for(int column=0;column<=10;++column)
            r.line(box.x+column*unit,box.y,box.x+column*unit,box.y+box.h,gridColor);
        for(int row=0;row<=20;++row)
            r.line(box.x,box.y+row*unit,box.x+box.w,box.y+row*unit,gridColor);
        r.outline(box.x,box.y,box.w,box.h,borderColor,1.5f);
    }
    if(gameplay || f.editor) {
        std::vector<std::size_t> order(f.layout.buttons.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return f.layout.buttons[a].z<f.layout.buttons[b].z;});
        r.clip(Rect{float(f.area.x),float(f.area.y),float(f.area.width),float(f.area.height)});
        for(const auto i:order) {
            const auto& b=f.layout.buttons[i];if(!b.enabled && !f.editor)continue;
            const auto points=outline(b,f.area);const float alpha=float(f.editor?std::max(.18,b.opacity):b.opacity);
            const auto ink=!f.editor && !b.available?rgb(0x777777):f.editor && i==f.selected?accent:b.player?secondary:textColor;
            fill(r,points,{ink.r,ink.g,ink.b,alpha*.08f});
            for(std::size_t j=0;j<points.size();++j) {
                const auto a=points[j],d=points[(j+1)%points.size()];
                r.line(float(a.x),float(a.y),float(d.x),float(d.y),{ink.r,ink.g,ink.b,alpha},f.editor && i==f.selected?3:1.5f);
                if(b.shape==Shape::Ellipse) {
                    const Point center{f.area.x+b.center.x*f.area.width,f.area.y+b.center.y*f.area.height};
                    r.line(float(center.x+(a.x-center.x)*.95),float(center.y+(a.y-center.y)*.95),
                        float(center.x+(d.x-center.x)*.95),float(center.y+(d.y-center.y)*.95),{ink.r,ink.g,ink.b,alpha*.8f},1);
                }
            }
            button_icon(r,b,f.area,{ink.r,ink.g,ink.b,alpha});
            const float u=float(std::min(f.area.width,f.area.height));
            if(f.editor && i==f.selected) {
                r.rect(float(f.area.x+b.center.x*f.area.width+b.width*u/2-10),float(f.area.y+b.center.y*f.area.height+b.height*u/2-10),20,20,textColor);
                if(b.shape==Shape::Polygon)for(const auto p:points)r.rounded(float(p.x-8),float(p.y-8),16,16,8,secondary);
            }
        }
        r.clip({});
    }
    if(f.editor) {
        r.fitted(float(f.area.x+174),float(f.area.y+18),tr(f.chinese,"Virtual buttons — drag to move; square handle resizes","虚拟按键：拖动移动，方形手柄调整宽高"),float(f.area.width-190),25*contentScale,accent);
        if(f.properties && f.selected<f.layout.buttons.size()) {
            const auto& b=f.layout.buttons[f.selected];const auto box=property_bounds(f.area,b);
            r.rounded(box.x,box.y,box.w,box.h,10,panelColor);r.fitted(box.x+12,box.y+10,label(b,f.chinese),box.w-66,25*contentScale,accent);
            r.centered_text({box.x+box.w-46,box.y+4,40,36},"×",28*contentScale,accent);
            static constexpr const char* en[]={"X","Y","Width","Height","Opacity","Rotation","Radius","Layer","Shape","Player","Enabled","Action","Vertices"};
            static constexpr const char* zh[]={"中心 X","中心 Y","宽度","高度","不透明度","旋转角","圆角半径","层级","形状","玩家","启用","动作","顶点"};
            for(int i=0;i<13;++i) {
                const float y=box.y+46+i*propertyRowHeight;r.rule(box.x+10,y,box.w-20,borderColor);
                r.text(box.x+12,y+9,f.chinese?zh[i]:en[i],20*contentScale,muted);
                auto display=value(b,i);
                if(i==8)display=f.chinese?std::array<const char*,4>{"矩形","圆角矩形","椭圆","自定义多边形"}[int(b.shape)]:display;
                if(i==11)display=label(b,f.chinese);
                if(f.field==i) {
                    const Rect field{box.x+132,y+3,box.w-144,38};
                    r.rounded(field.x,field.y,field.w,field.h,4,raisedColor);r.outline(field.x,field.y,field.w,field.h,accent);
                    r.fitted(field.x+6,field.y+6,f.input+"|",field.w-12,21*contentScale,textColor);r.input_target(field);
                } else r.fitted(box.x+138,y+9,display,box.w-150,21*contentScale,textColor);
            }
        }
    }
    if(f.railVisible && !f.rail.empty()) {
        const auto b=f.railBounds;
        r.rect(b.x,b.y,b.w,b.h,panelColor);
        r.rect(b.x+b.w-1,b.y,1,b.h,borderColor);
    }
    r.clip(f.railViewport);
    for(const auto& b:f.rail) {
        r.rounded(b.bounds.x,b.bounds.y,b.bounds.w,b.bounds.h,7,raisedColor);
        r.centered_text(b.bounds,b.label,24,textColor);
    }
    if(f.railMaxOffset>0) {
        const auto v=f.railViewport;const float height=std::max(24.f,v.h*v.h/(v.h+f.railMaxOffset));
        r.rounded(v.x+v.w-5,v.y,3,v.h,1,borderColor);
        r.rounded(v.x+v.w-5,v.y+(v.h-height)*f.railOffset/f.railMaxOffset,3,height,1,accent);
    }
    r.clip({});
    for(const auto& b:f.tools) {
        r.rounded(b.bounds.x,b.bounds.y,b.bounds.w,b.bounds.h,7,raisedColor);
        r.centered_text(b.bounds,b.label,20*contentScale,textColor);
    }
    if(f.railToggle) {
        const Rect toggle{f.railBounds.x+8,f.railBounds.y+12,64,56};
        r.rounded(toggle.x,toggle.y,toggle.w,toggle.h,7,raisedColor);
        if(f.railVisible) {
            r.line(toggle.x+22,toggle.y+18,toggle.x+42,toggle.y+38,textColor,2);
            r.line(toggle.x+42,toggle.y+18,toggle.x+22,toggle.y+38,textColor,2);
        } else for(int i=0;i<3;++i)r.rect(toggle.x+18,toggle.y+18+i*9,28,2,textColor);
    }
    if(!f.message.empty())r.paragraph(float(f.area.x+174),float(f.area.y+f.area.height-86),f.message,float(f.area.width-190),21*contentScale,accent,3);
}
}
