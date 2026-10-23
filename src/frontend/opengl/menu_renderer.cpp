#include "renderer.h"
#include <algorithm>
#include <cmath>

namespace Graphical {
namespace {
constexpr Color border=borderColor, raised=raisedColor, dark=wellColor, selected=focusColor;
std::string caption(const std::string& label) {
    const std::pair<const char*,const char*> labels[]={{"SETTINGS","Settings"},{"MODEL","Game mode"},
        {"HANDLING","Handling"},{"GAME","Gameplay"},{"CONTROLS","Controls"},{"NETWORK","Online"},
        {"OTHERS","Appearance & language"},{"RESET","Restore defaults"},{"GARBAGE","Garbage"}};
    for(const auto& pair:labels)if(label==pair.first)return pair.second;
    return label;
}
}
bool Renderer::menu(const Frontend::UiMetadata& m) {
    using Kind=Frontend::UiKind;using Control=Frontend::UiControl;
    if(m.kind!=Kind::Start && m.kind!=Kind::Menu && m.kind!=Kind::Choices && m.kind!=Kind::Text && m.kind!=Kind::KeyCapture)return false;
    hitMetadata_=m;
    const auto tr=[&](const char* en,const char* zh){return std::string(m.chinese?zh:en);};
    const float pageWidth=ui_width(),center=pageWidth/2,right=pageWidth-40;
    const float currentX=std::min(376.f,pageWidth*.32f),currentWidth=right-currentX,navWidth=currentX-64;
    const float focusStroke=std::max(1.f,1.f/viewportScale_);
    const auto card=[&](float x,float y,float w,float h,bool focus=false) {
        const float stroke=focus?focusStroke:1.f;
        rounded(x,y,w,h,12,focus?accent:border);
        rounded(x+stroke,y+stroke,w-2*stroke,h-2*stroke,12-stroke,focus?selected:panelColor);
    };
    const auto arrow=[&](float x,float y,Color c) {line(x,y,x+5,y+5,c,1.6f);line(x+5,y+5,x,y+10,c,1.6f);};
    const auto footer=[&] {
        if(touch_controls() || m.shortcuts.empty())return;
        rule(40,700,pageWidth-80);
        float y=712;
        for(const auto& shortcut:m.shortcuts) {
            if(text_size(13)>13) {
                for(const auto& line:wrap(shortcut,pageWidth-96,13)){text(48,y,line,13,muted);y+=text_size(13)*1.2f;}
            } else y+=paragraph(48,y,shortcut,pageWidth-96,13,muted,2)+3;
        }
    };
    if(m.kind==Kind::Start) {
        gradient(0,0,pageWidth,800,backgroundTop,backgroundBottom);
        // Broad, static light behind the wordmark, with no visible panel edge.
        for(int i=24;i>0;--i) {
            const float w=360+i*24.f,h=120+i*10.f;
            rounded(center-w/2,310-h/2,w,h,h/2,{accent.r,accent.g,accent.b,.008f});
        }

        centered_text({center-399.2f,238,800,150},"TerMino",132,accent);
        centered_text({center-400,238,800,150},"TerMino",132,accent);
        if(!m.currentValue.empty())centered_text({center-360,389,720,28},tr("Mode  ","模式  ")+m.currentValue,15,muted);

        const auto key=[&](std::size_t i) {
            auto value=i<m.entries.size()?m.entries[i].value:std::string{};
            if(value.empty())return std::string("Enter");
            if(value.front()=='[' && value.back()==']')value=value.substr(1,value.size()-2);
            return value;
        };
        const bool playFocus=m.selected==0;
        const Color ink=playFocus?wellColor:textColor;
        const float keyWidth=std::clamp(measure(key(0),17)+26,66.f,160.f);
        const float playWidth=std::max(360.f,keyWidth+262),playX=center-playWidth/2;
        rounded(playX,446,playWidth,66,5,playFocus?accent:borderColor);
        if(!playFocus)rounded(playX+focusStroke,446+focusStroke,playWidth-2*focusStroke,66-2*focusStroke,4,panelColor);
        if(!touch_controls()) {
            rounded(playX+22,461,keyWidth,36,4,playFocus?Color{0,0,0,.10f}:raisedColor);
            centered_text({playX+28,461,keyWidth-12,36},key(0),17,ink);
        }
        centered_text(touch_controls()?Rect{playX+28,457,playWidth-76,44}:
            Rect{playX+keyWidth+38,457,playWidth-keyWidth-100,44},tr("Start game","进入游戏"),24,ink);
        arrow(playX+playWidth-34,474,ink);
        hits_.push_back({{playX,446,playWidth,66},0});
        for(std::size_t i=1;i<std::min<std::size_t>(3,m.entries.size());++i) {
            const float x=center-180+(i-1)*190;
            const bool focus=i==m.selected;
            if(focus) {
                rounded(x,550,170,46,5,accent);
                rounded(x+focusStroke,550+focusStroke,170-2*focusStroke,46-2*focusStroke,4,focusColor);
            }
            const float cap=touch_controls()?0:std::clamp(measure(key(i),13)+18,39.f,78.f);
            if(!touch_controls()) {
                rounded(x+11,561,cap,24,3,raisedColor);
                centered_text({x+16,561,cap-10,24},key(i),13,focus?accent:muted);
            }
            centered_text(touch_controls()?Rect{x,552,170,42}:Rect{x+cap+18,552,141-cap,42},caption(m.entries[i].label),18,focus?accent:textColor);
            hits_.push_back({{x,550,170,46},i});
        }
        if(!m.notice.empty()) {
            centered_text({center-380,613,760,24},tr("Connection failed — check settings and retry","连接失败，请检查设置后重试"),16,{1,.65f,.53f});
            paragraph(center-340,643,m.notice,680,14,muted,2);
        }
        if(!touch_controls())centered_text({center-440,721,880,25},tr("↑ ↓ / Tab  Select    Enter  Confirm","↑ ↓ / Tab  选择    Enter  确认"),13,muted);
        return true;
    }
    const bool editing=m.kind==Kind::Text || m.kind==Kind::KeyCapture;
    const bool inlineText=m.kind==Kind::Text && m.parent && m.path!="play-rooms";
    const bool choices=m.kind==Kind::Choices;
    if(m.id=="map-editor-exit") {
        rect(0,72,pageWidth,728,{wellColor.r,wellColor.g,wellColor.b,.85f});
        const float left=center-326;
        card(left,163,652,468);fitted(left+32,193,m.title,588,31);
        paragraph(left+32,246,m.hint,588,19,muted,2);
        for(std::size_t i=0;i<m.entries.size();++i) {
            const float y=307+i*76;card(left+28,y,596,62,i==m.selected);
            fitted(left+52,y+18,m.entries[i].label,530,22,i==m.selected?accent:textColor);
            hits_.push_back({{left+28,y,596,62},i});
        }
        paragraph(left+32,553,m.notice,588,14,secondary,2);
        if(!touch_controls())fitted(left+32,595,tr("↑ ↓  Select    Enter  Apply    Esc  Keep editing","↑ ↓  选择    Enter  确认    Esc  继续编辑"),588,13,muted);
        return true;
    }
    const auto inputField=[&](float x,float y,float w) {
        const float height=touch_controls()?50:38,font=touch_controls()?21:17;
        rounded(x,y,w,height,6,accent);
        rounded(x+focusStroke,y+focusStroke,w-2*focusStroke,height-2*focusStroke,6-focusStroke,dark);
        if(m.kind==Kind::KeyCapture)fitted(x+12,y+9,tr("Press a key…","等待按键…"),w-28,15,accent);
        else {
            const auto caret=std::min(m.inputCaret,m.input.size());
            const float advance=measure(m.input.substr(0,caret),font);
            const float offset=std::max(0.f,advance-(w-34));
            const auto previousClip=clip_;
            Rect inner{x+10,y+3,w-20,height-7};
            if(previousClip) {
                const float bottom=std::min(inner.y+inner.h,previousClip->y+previousClip->h);
                inner.y=std::max(inner.y,previousClip->y);inner.h=std::max(0.f,bottom-inner.y);
            }
            clip(inner);text(x+12-offset,y+(touch_controls()?(height-font)/2-2:8),m.input,font);
            const float cx=x+12-offset+advance;rect(cx,y+8,2,height-15,accent);clip(previousClip);
            inputRect_=Rect{x,y,w,height};
        }
    };
    if(m.path=="play-rooms") {
        rect(0,0,pageWidth,800,dark);
        text(96,98,tr("MULTIPLAYER","联机对战"),15,accent);
        fitted(96,139,m.title,pageWidth-192,34);
        paragraph(96,201,m.hint.empty()?tr("Create or join a room to start multiplayer.","创建或加入房间，随后直接准备开局。"):m.hint,pageWidth-192,16,muted,2);
        if(editing) {
            card(96,265,pageWidth-192,230);inputField(128,335,pageWidth-256);
            paragraph(128,411,m.currentValue,pageWidth-256,16,muted,2);
        } else {
            const auto first=m.selected/8*8;
            for(std::size_t i=first;i<std::min(first+8,m.entries.size());++i) {
                const float y=265+(i-first)*46;const bool focus=i==m.selected;
                if(focus) {rounded(96,y,pageWidth-192,42,7,selected);rect(96,y+8,3,26,accent);}
                else rect(112,y+41,pageWidth-224,1,border);
                const auto& entry=m.entries[i];
                fitted(120,y+10,entry.label,entry.value.empty()?pageWidth-256:pageWidth-550,19,focus?accent:entry.enabled?textColor:muted);
                fitted(pageWidth-388,y+11,entry.value,260,17,muted);
                if(entry.enabled)hits_.push_back({{96,y,pageWidth-192,42},i});
            }
        }
        paragraph(96,646,m.notice,pageWidth-192,15,muted,2);
        rect(96,704,pageWidth-192,1,border);
        std::string shortcuts;
        for(const auto& shortcut:m.shortcuts)shortcuts+=(shortcuts.empty()?"":"   ")+shortcut;
        paragraph(96,719,shortcuts,pageWidth-192,13,muted,3);
        return true;
    }
    if(editing && !m.parent) {
        // Naming/saving is a modal task, without a fictitious settings tree.
        rect(0,72,pageWidth,728,{wellColor.r,wellColor.g,wellColor.b,.85f});
        const float left=center-368;
        card(left,188,736,394);
        fitted(left+32,215,m.title,672,30);
        paragraph(left+32,269,m.currentValue,672,16,muted,2);
        text(left+32,339,m.id=="replay-save"?tr("FILE NAME / PATH","文件名 / 路径"):tr("TEXT INPUT","输入内容"),12,accent);
        inputField(left+32,368,672);
        paragraph(left+32,425,m.hint,672,15,muted,2);
        rect(left+32,489,672,1,border);
        float y=505;
        for(const auto& shortcut:m.shortcuts)y+=paragraph(left+32,y,shortcut,672,14,muted,2)+4;
        return true;
    }
    const auto scroll=[&](const std::string& title,const std::vector<Frontend::UiEntry>& entries,
                          std::size_t selected,std::size_t rows,Rect bounds,float rowHeight) {
        std::string key=title+"/"+m.context;
        for(const auto& entry:entries)key+="/"+entry.id;
        return scroll_viewport(m,key,selected,entries.size(),rows,bounds,rowHeight);
    };
    const auto rowHit=[&](UiHit hit) {
        if(clip_) {
            const auto b=*clip_;auto& r=hit.bounds;
            const float right=std::min(r.x+r.w,b.x+b.w),bottom=std::min(r.y+r.h,b.y+b.h);
            r.x=std::max(r.x,b.x);r.y=std::max(r.y,b.y);r.w=right-r.x;r.h=bottom-r.y;
        }
        if(hit.bounds.w>0 && hit.bounds.h>0)hits_.push_back(hit);
    };
    // Each ancestor keeps its own selected row. The current selector always
    // owns the broad right-hand pane, at every depth including choice editing.
    const float rowLeft=currentX+14,rowWidth=currentWidth-28;
    const float valueWidth=std::min(243.f,currentWidth*.31f),valueX=right-36-valueWidth;
    fitted(40,94,m.path.empty()?tr("SETTINGS","设置"):m.path,pageWidth-264,13,muted);
    fitted(right-176,94,m.context,176,13,accent);
    fitted(currentX+4,132,caption(inlineText?m.parent->title:m.title),currentWidth-158,touch_controls()?34:31);
    rounded(right-126,130,126,38,7,raised);
    centered_text({right-118,130,110,38},tr("←  Back","←  返回"),15,textColor);
    hits_.push_back({{right-126,130,126,38},0,true});
    if(choices)fitted(currentX+4,175,tr("Current: ","当前值：")+m.currentValue,currentWidth-48,13,accent);

    std::vector<Frontend::UiNavigationGroup> history=m.ancestors;
    if(m.parent && !inlineText)history.push_back(*m.parent);
    if(!history.empty()) {
        text(44,148,tr("NAVIGATION PATH","选择路径"),11,muted);
        const float gap=10,height=(318-gap*(history.size()-1))/history.size();
        const bool compact=height<66;
        for(std::size_t depth=0;depth<history.size();++depth) {
            const auto& group=history[depth];
            const float top=195+depth*(height+gap);
            card(40,top,navWidth,height);
            fitted(54,top+11,std::to_string(depth+1)+"  "+caption(group.title),navWidth-32,12,muted);
            const float rowHeight=touch_controls()?(compact?30:38):(compact?20:28),offset=compact?24:32;
            const std::size_t rows=std::max(1,static_cast<int>((height-offset-6)/rowHeight));
            const auto first=scroll("ancestor/"+std::to_string(depth)+"/"+group.title,group.entries,group.selected,rows,{40,top+offset,navWidth,rows*rowHeight},rowHeight);
            clip(Rect{40,top+offset,navWidth-10,rows*rowHeight});
            for(std::size_t i=std::size_t(std::floor(first));i<std::min(std::size_t(std::ceil(first))+rows,group.entries.size());++i) {
                const auto& e=group.entries[i];const bool focus=i==group.selected;
                const float y=top+offset+(i-first)*rowHeight;
                if(e.control==Control::Separator){rule(61,y+rowHeight/2,navWidth-42);continue;}
                if(focus) {rounded(48,y,navWidth-16,rowHeight-2,5,selected);rect(48,y+4,3,rowHeight-10,accent);}
                fitted(61,y+4,caption(e.label),navWidth-52,touch_controls()?22:(compact?11:14),focus?accent:e.enabled?textColor:muted);
                if(e.enabled && e.control!=Control::Separator)rowHit({{48,y,navWidth-16,rowHeight-2},i,false,depth});
            }
            clip({});
        }
    } else {
        text(44,148,tr("SETTINGS","设置"),11,muted);
        card(40,195,navWidth,318);
        const auto* selectedEntry=m.selected<m.entries.size()?&m.entries[m.selected]:nullptr;
        fitted(58,217,selectedEntry?caption(selectedEntry->label):caption(m.title),navWidth-38,22);
        if(!touch_controls())text(58,259,tr("Enter to open or edit","Enter 进入或修改"),13,accent);
        rect(58,288,navWidth-36,1,border);
        for(std::size_t i=0;i<std::min<std::size_t>(6,m.preview.size());++i) {
            const auto& e=m.preview[i];
            if(e.control==Control::Separator){rule(58,320+i*30,navWidth-36);continue;}
            fitted(58,307+i*30,caption(e.label),e.value.empty()?navWidth-38:navWidth-146,14,muted);
            if(!e.value.empty())fitted(navWidth-80,308+i*30,e.value,102,13,accent);
        }
    }

    card(40,526,navWidth,162);
    text(56,541,tr("ABOUT THIS OPTION","选项说明"),11,accent);
    if(!m.gridPreview)paragraph(56,565,m.hint,navWidth-32,14,muted,6);
    else {
        paragraph(56,565,m.hint,navWidth-150,12,muted,7);
        const auto& grid=*m.gridPreview;
        if(grid.width>0 && grid.height>0) {
            const float unit=std::min(102.f/grid.width,106.f/grid.height);
            const float left=navWidth-80+(102-grid.width*unit)/2,top=565;
            rect(left,top,grid.width*unit,grid.height*unit,dark);
            for(int row=0;row<grid.height;++row)for(int col=0;col<grid.width;++col) {
                const auto i=static_cast<std::size_t>(row*grid.width+col);
                if(i<grid.colors.size() && grid.colors[i]>=0)
                    mino(left+col*unit,top+row*unit,unit,cell_color(grid.colors[i],grid.obscured),false,grid.squareStyle);
            }
            outline(left,top,grid.width*unit,grid.height*unit,border);
        }
    }

    card(currentX,195,currentWidth,493);
    if(editing && !inlineText) {
        text(currentX+24,219,m.kind==Kind::KeyCapture?tr("KEY BINDING","按键绑定"):tr("EDIT VALUE","编辑参数"),12,accent);
        fitted(currentX+24,264,caption(m.title),currentWidth-48,28);
        paragraph(currentX+24,317,tr("Current: ","当前值：")+m.currentValue,currentWidth-48,18,muted,3);
        inputField(currentX+24,411,currentWidth-48);
        paragraph(currentX+24,477,m.hint,currentWidth-48,17,muted,5);
        if(!touch_controls())fitted(currentX+24,652,tr("Enter  Save    Esc  Cancel","Enter 保存    Esc 取消"),currentWidth-48,14,accent);
    } else {
        const auto& entries=inlineText?m.parent->entries:m.entries;
        const auto selectedIndex=inlineText?m.parent->selected:m.selected;
        const float rowHeight=touch_controls()?62:44,rowBody=rowHeight-4;
        const std::size_t rows=touch_controls()?7:10;
        const float listHeight=rows*rowHeight,labelSize=touch_controls()?23:18,valueSize=touch_controls()?22:17;
        const float padding=touch_controls()?17:8,boxTop=touch_controls()?8:4,boxHeight=touch_controls()?42:32;
        const auto first=scroll(m.path+"/"+(inlineText?m.parent->id:m.id),entries,selectedIndex,rows,{rowLeft,211,currentWidth-14,listHeight},rowHeight);
        clip(Rect{rowLeft,211,rowWidth,listHeight});
        for(std::size_t i=std::size_t(std::floor(first));i<std::min(std::size_t(std::ceil(first))+rows,entries.size());++i) {
            const auto& e=entries[i];const bool focus=i==selectedIndex;const float y=211+(i-first)*rowHeight;
            if(e.control==Control::Separator) {rule(currentX+24,y+rowBody/2,currentWidth-48);continue;}
            if(focus) {rounded(rowLeft,y,rowWidth,rowBody,7,selected);rect(rowLeft,y+7,3,rowBody-14,accent);}
            else rule(currentX+26,y+rowBody,currentWidth-52);
            const auto fg=e.enabled?textColor:muted;
            if(choices) {
                const bool checked=e.checked.value_or(false);
                const float cy=y+(rowBody-20)/2;
                rounded(currentX+29,cy,20,20,10,checked?accent:border);
                if(!checked)rounded(currentX+31,cy+2,16,16,8,panelColor);
                else {line(currentX+34,cy+10,currentX+38,cy+14,dark,2);line(currentX+38,cy+14,currentX+45,cy+6,dark,2);}
                fitted(currentX+66,y+padding,caption(e.label),currentWidth-106,labelSize,focus?accent:fg);
            } else {
                const bool hasValue=!e.value.empty() || e.filePath || (inlineText && focus);
                fitted(currentX+31,y+padding,caption(e.label),hasValue?valueX-currentX-52:currentWidth-98,labelSize,focus?accent:fg);
                if(e.control==Control::Toggle && e.checked) {
                    const float cy=y+(rowBody-26)/2;
                    fitted(right-225,y+padding,e.value,100,touch_controls()?22:13,muted);
                    rounded(right-96,cy,58,26,13,*e.checked?accent:border);
                    rounded(right-93+(*e.checked?32:0),cy+3,20,20,10,*e.checked?dark:muted);
                } else if(hasValue) {
                    const float width=e.filePath?valueWidth-54:valueWidth;
                    if(inlineText && focus)inputField(valueX,y+(touch_controls()?4:1),width);
                    else {
                        rounded(valueX,y+boxTop,width,boxHeight,6,focus?dark:raised);
                        fitted(valueX+12,y+padding,e.value,width-24,valueSize,e.effective?accent:muted);
                    }
                    if(e.filePath) {
                        rounded(right-84,y+boxTop,48,boxHeight,5,raised);
                        centered_text({right-84,y+boxTop,48,boxHeight},tr("Open","打开"),touch_controls()?20:13,accent);
                        UiHit hit{{right-84,y+boxTop,48,boxHeight},i};hit.kind=Frontend::UiCommandKind::BrowseFile;
                        if(inlineText)hit.ancestor=m.ancestors.size();
                        if(e.enabled)rowHit(hit);
                    }
                } else if(e.control==Control::Group)arrow(right-42,y+(rowBody-10)/2,focus?accent:muted);
            }
            if(e.enabled)rowHit({{rowLeft,y,rowWidth,rowBody},i,false,inlineText?std::optional<std::size_t>{m.ancestors.size()}:std::nullopt});
        }
        clip({});
        const auto counter=std::to_string(entries.empty()?0:m.selected+1)+" / "+std::to_string(entries.size());
        fitted(currentX+24,667,touch_controls()?(inlineText?std::string{}:counter):
            inlineText?tr("Enter  Save    Esc  Back    ← →  Cursor","Enter 保存    Esc 返回    ← → 移动光标"):
            counter+tr("    ↑ ↓  Select    Enter  Apply    PgUp / PgDn","    ↑ ↓ 选择    Enter 确认    PgUp / PgDn 翻页"),currentWidth-48,12,muted);
    }
    if(!m.notice.empty()) {
        rounded(currentX,641,currentWidth,47,7,raised);
        paragraph(currentX+24,650,m.notice,currentWidth-48,13,accent,2);
    }
    footer();return true;
}
}
