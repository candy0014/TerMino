#include "renderer.h"
#include "board_layout.h"
#include <algorithm>
#include <cmath>

namespace Graphical {
namespace {
constexpr Color border=borderColor, raised=raisedColor, focus=focusColor;
void piece(Renderer& r,const Frontend::UiPiecePreview& p,Rect box,int style=0) {
    if(p.cells.empty())return;
    int top=999,left=999,bottom=-999,right=-999;
    for(const auto& c:p.cells){top=std::min(top,c[0]);left=std::min(left,c[1]);bottom=std::max(bottom,c[0]);right=std::max(right,c[1]);}
    const float unit=std::min({24.f,box.w/(right-left+1),box.h/(bottom-top+1)});
    const float x=box.x+(box.w-(right-left+1)*unit)/2,y=box.y+(box.h-(bottom-top+1)*unit)/2;
    for(const auto& c:p.cells)r.mino(x+(c[1]-left)*unit,y+(c[0]-top)*unit,unit,cell_color(p.color),false,style);
}
}
void Renderer::controls(const std::vector<Frontend::UiEntry>& entries,const std::string& notice,
    const std::string& title) {
    if(touch_controls())return;
    const float x=scene_left()+controlsLeft,w=controlsWidth-24;
    float size=12;
    const auto entry_height=[&](const auto& e,float font) {
        if(measure(e.label,font)+measure(e.value,font)+24<=w)return font+18;
        return (wrap(e.label,w,font).size()+wrap(e.value,w-12,font).size())*font*1.55f+20;
    };
    for(;;) {
        float height=wrap(notice,w,size).size()*size*1.55f+(notice.empty()?0:14);
        for(const auto& e:entries)height+=entry_height(e,size);
        if(height<=controlsBottom-controlsTop-52 || size<=8)break;size-=.5f;
    }
    rounded(x,controlsTop,controlsWidth,controlsBottom-controlsTop,7,panelColor);
    rect(x,controlsTop+12,2,18,accent);text(x+12,controlsTop+12,title,11,muted);
    float y=controlsTop+43;
    if(!notice.empty())y+=paragraph(x+12,y,notice,w,size,textColor,100)+14;
    for(const auto& e:entries) {
        const float keyWidth=measure(e.value,size)+12;
        if(measure(e.label,size)+keyWidth+12<=w) {
            text(x+12,y+4,e.label,size,muted);
            rounded(x+12+w-keyWidth,y,keyWidth,size+10,3,raised);
            text(x+18+w-keyWidth,y+4,e.value,size,accent);
            y+=size+18;
        } else {
            y+=paragraph(x+12,y,e.label,w,size,muted,100)+4;
            const auto lines=wrap(e.value,w-12,size);const float height=lines.size()*size*1.55f+8;
            rounded(x+12,y,w,height,3,raised);
            paragraph(x+18,y+4,e.value,w-12,size,accent,100);y+=height+8;
        }
    }
}
bool Renderer::application_surface(const Frontend::UiFrame& frame) {
    const auto& m=frame.metadata;using Kind=Frontend::UiKind;
    const float pageWidth=ui_width(),right=pageWidth-48;
    const auto tr=[&](const char* en,const char* zh){return std::string(m.chinese?zh:en);};
    const auto hit=[&](Rect bounds,Frontend::UiCommandKind kind,std::size_t index=0,int row=0,int column=0) {
        if(!touch_controls())return;
        if(clip_) {
            const float right=std::min(bounds.x+bounds.w,clip_->x+clip_->w),bottom=std::min(bounds.y+bounds.h,clip_->y+clip_->h);
            bounds.x=std::max(bounds.x,clip_->x);bounds.y=std::max(bounds.y,clip_->y);
            bounds.w=right-bounds.x;bounds.h=bottom-bounds.y;
        }
        if(bounds.w>0 && bounds.h>0) {
            UiHit target{bounds,index};target.kind=kind;target.row=row;target.column=column;hits_.push_back(target);
        }
    };
    const float stroke=std::max(1.f,1.f/viewportScale_);
    const auto card=[&](Rect box,bool active=false) {
        rounded(box.x,box.y,box.w,box.h,8,active?accent:border);
        const float s=active?stroke:1;
        rounded(box.x+s,box.y+s,box.w-2*s,box.h-2*s,7,active?focus:panelColor);
    };
    const auto footer=[&] {
        rect(48,704,pageWidth-96,1,border);float y=715;
        for(const auto& shortcut:m.shortcuts) {
            if(text_size(12)>12) {
                for(const auto& line:wrap(shortcut,pageWidth-96,12)){text(48,y,line,12,muted);y+=text_size(12)*1.2f;}
            } else y+=paragraph(48,y,shortcut,pageWidth-96,12,muted,2)+2;
        }
    };
    const auto tabs=[&](const std::vector<std::string>& labels,int selected) {
        float x=48;for(std::size_t i=0;i<labels.size();++i) {
            const float w=std::max(140.f,measure(labels[i],16)+40);
            rounded(x,174,w,34,6,int(i)==selected?focus:panelColor);
            text(x+18,181,labels[i],16,int(i)==selected?accent:muted);
            if(m.kind==Kind::MapEditor) {
                UiHit hit{{x,174,w,34},i};hit.kind=Frontend::UiCommandKind::MapTab;hits_.push_back(hit);
            } else if(m.kind==Kind::Statistics)hit({x,174,w,34},Frontend::UiCommandKind::StatisticsPage,i);
            if(int(i)==selected)rect(x+18,206,w-36,2,accent);x+=w+8;
        }
    };
    if(m.kind==Kind::Notice) {
        if(frame.overlay) {
            controls(m.entries,m.hint,tr("ROUND ENDED","本局结束"));
        } else {
            const float left=(pageWidth-744)/2;
            card({left,205,744,362});rect(left+32,239,6,28,accent);
            fitted(left+54,234,m.title,650,28);
            paragraph(left+32,306,m.hint,680,21,textColor,6);
            paragraph(left+32,475,m.notice,680,16,accent,3);footer();
        }
        return true;
    }
    if(m.kind==Kind::MapEditor && m.editor) {
        hitMetadata_=m;
        const auto& e=*m.editor;
        text(48,94,tr("Map studio","地图编辑器"),32);
        fitted(50,139,(e.name.empty()?tr("Untitled map","未命名地图"):e.name)+(e.dirty?tr("  ·  Unsaved","  ·  未保存"):""),pageWidth-480,16,e.dirty?secondary:muted);
        tabs({tr("Field","版面"),tr("Piece sequence","块序")},e.sequenceFocus?1:0);
        fitted(pageWidth-422,142,e.goal+"  ·  "+std::to_string(e.field.width)+" × "+std::to_string(e.field.height),370,15,muted);
        if(!e.sequenceFocus) {
            card({48,230,312,432});text(68,250,tr("PALETTE","调色板"),13,accent);
            const int colors[]={-1,7,0,1,2,3,4,5,6};const char* names[]={"Empty","G","I","J","L","O","S","T","Z"};
            for(int i=0;i<9;++i) {
                const float x=68+(i%3)*92,y=283+(i/3)*72;card({x,y,84,62},e.brush==colors[i]);
                UiHit hit{{x,y,84,62},static_cast<std::size_t>(i)};hit.kind=Frontend::UiCommandKind::MapBrush;hits_.push_back(hit);
                if(i)mino(x+11,y+11,24,cell_color(colors[i]),false,e.field.squareStyle);
                else {outline(x+11,y+11,24,24,muted);line(x+14,y+31,x+31,y+14,muted);}
                text(x+46,y+11,std::to_string(i),16,e.brush==colors[i]?accent:textColor);
                text(x+11,y+41,i?names[i]:tr("Empty","空"),11,muted);
            }
            text(68,521,tr("SELECTED CELL","当前方格"),12,muted);
            text(68,544,std::to_string(e.column+1)+"  /  "+std::to_string(e.field.height-e.row),30);
            paragraph(68,598,touch_controls()?tr("Tap a color, then drag on the grid to paint.","点按选色，在网格中拖动绘制。"):
                tr("Click a color, then drag to paint. Arrow and color keys also work.","单击选色，按住鼠标拖绘；也可使用方向键和颜色键。"),270,15,muted,3);
            const int rows=e.fieldRows?e.fieldRows:std::min(22,e.field.height),cols=e.fieldColumns?e.fieldColumns:std::min(26,e.field.width);
            const int firstRow=e.fieldRows?e.fieldFirstRow:std::clamp(e.row-rows/2,0,e.field.height-rows),firstCol=e.fieldColumns?e.fieldFirstColumn:std::clamp(e.column-cols/2,0,e.field.width-cols);
            const float unit=std::min({28.f,(pageWidth-496)/std::max(1,cols),414.f/std::max(1,rows)});
            const float x=394+(pageWidth-458-cols*unit)/2,y=235+(420-rows*unit)/2;
            editorField_=EditorFieldHit{{x,y,cols*unit,rows*unit},firstRow,firstCol,rows,cols};
            for(int row=0;row<rows;++row)for(int col=0;col<cols;++col) {
                const float px=x+col*unit,py=y+row*unit;
                rect(px,py,unit-1,unit-1,raised);
                const auto index=(row+firstRow)*e.field.width+col+firstCol;
                const int color=e.field.colors.at(index);
                if(color>=0)mino(px,py,unit,cell_color(color),false,e.field.squareStyle);
                if(row+firstRow==e.row && col+firstCol==e.column) {
                    outline(px,py,unit-1,unit-1,textColor,std::max(2.f,stroke));
                    rect(px+unit*.4f,py+unit*.4f,unit*.2f,unit*.2f,color<0?accent:panelColor);
                }
            }
            outline(x-4,y-4,cols*unit+7,rows*unit+7,border);
            fitted(402,671,tr("Viewport","可见范围")+"  "+std::to_string(firstCol+1)+"–"+std::to_string(firstCol+cols)+"  /  "+
                std::to_string(e.field.height-firstRow-rows+1)+"–"+std::to_string(e.field.height-firstRow),pageWidth-470,12,muted);
        } else {
            card({48,230,290,450});text(68,248,e.generated?tr("GENERATOR","生成方式"):tr("SEQUENCE","已选块序"),13,accent);
            if(e.generated) {
                paragraph(68,289,e.system,248,24,textColor,3);
                text(68,384,tr("Floor ","楼层 ")+std::to_string(e.floor),17,accent);
                paragraph(68,433,tr("Pieces are generated using this rotation system. Change the generator in Options to edit a custom sequence.","按当前旋转系统生成方块。在设置中切换生成方式后可编辑自定义块序。"),248,17,muted,6);
            } else {
                fitted(68,272,tr("Insert at ","插入位置 ")+std::to_string(e.caret+1)+" / "+std::to_string(e.sequenceSize),248,12,muted);
                constexpr auto slots=Frontend::kMapSequenceColumns*Frontend::kMapSequenceRows;
                scrollAreas_.push_back({{48,230,290,450},0});
                for(std::size_t i=0;i<slots && e.sequenceFirst+i<=e.sequenceSize;++i) {
                    const auto index=e.sequenceFirst+i;const float x=62+(i%Frontend::kMapSequenceColumns)*131,y=299+(i/Frontend::kMapSequenceColumns)*30;
                    const bool selected=index==e.caret;card({x,y,124,27},selected);
                    text(x+5,y+9,std::to_string(index+1),10,muted);
                    if(i<e.sequence.size()) {
                        piece(*this,e.sequence[i],{x+29,y+3,48,21},e.field.squareStyle);
                        fitted(x+81,y+9,e.sequence[i].name,38,10,selected?accent:textColor);
                    } else fitted(x+31,y+7,tr("+ Insert","+ 插入"),88,12,accent);
                    UiHit hit{{x,y,124,27},index};hit.kind=Frontend::UiCommandKind::MapCaret;hits_.push_back(hit);
                }
                if(e.sequenceSize+1>slots) {
                    const float height=std::max(8.f,360.f*slots/(e.sequenceSize+1));
                    const float top=299+std::min(360-height,360.f*e.sequenceFirst/(e.sequenceSize+1));
                    rounded(329,299,3,360,1,border);rounded(329,top,3,height,1,accent);
                }
            }
            const auto columns=std::max(1,e.candidateColumns),rows=std::max(1,e.candidateRows);
            const std::size_t page=columns*rows,first=e.candidateFirst;
            if(!e.generated)scrollAreas_.push_back({{358,230,pageWidth-406,362},1});
            fitted(366,231,tr("PIECE LIBRARY","方块库")+"  "+std::to_string(e.candidates.empty()?0:e.candidate+1)+" / "+std::to_string(e.candidates.size()),pageWidth-690,14,accent);
            fitted(366,257,e.query.empty()?e.system:tr("Search: ","检索：")+e.query,pageWidth-428,13,muted);
            const float cw=(pageWidth-414)/columns,ch=302.f/rows;
            if(e.candidates.empty())text(390,373,tr("No matching pieces.","没有匹配的方块。"),22,muted);
            for(std::size_t i=first;i<e.candidates.size() && i<first+page;++i) {
                const float x=366+(i-first)%columns*cw,y=286+(i-first)/columns*ch;
                card({x,y,cw-8,ch-8},i==e.candidate);
                fitted(x+12,y+10,e.candidates[i].name,cw-30,14,i==e.candidate?accent:textColor);
                piece(*this,e.candidates[i],{x+12,y+35,cw-32,ch-52},e.field.squareStyle);
                if(!e.generated){UiHit hit{{x,y,cw-8,ch-8},i};hit.kind=Frontend::UiCommandKind::MapCandidate;hits_.push_back(hit);}
            }
            card({366,602,pageWidth-422,78});
            if(e.detail) {
                piece(*this,*e.detail,{384,612,122,58},e.field.squareStyle);
                fitted(532,614,e.detail->name,pageWidth-610,22,accent);fitted(532,647,e.detail->system,pageWidth-610,14,muted);
            }
        }
        if(!m.notice.empty()) {rounded(pageWidth-608,91,552,40,8,focus);fitted(pageWidth-592,102,m.notice,520,15,accent);}
        footer();return true;
    }
    if(m.kind==Kind::Statistics && m.statistics) {
        hitMetadata_=m;
        const auto& s=*m.statistics;using Page=Frontend::UiStatisticsPage;
        text(48,94,tr("Round results","对局结果"),32);
        fitted(50,139,m.hint,pageWidth-120,14,muted);
        std::vector<std::string> pages={tr("Overview","统计总览"),tr("Line clears","消行统计")};
        if(!s.challenges.empty())pages.push_back(tr("Challenges","挑战成绩"));tabs(pages,static_cast<int>(s.page));
        const bool roster=s.players.size()>1 || s.room;
        const float x=roster?322:48,w=right-x;
        const std::string scrollKey="statistics/"+m.id+"/"+std::to_string(s.player);
        const auto visible_rows=[&](float offset,int count,int shown,const auto& draw) {
            const int first=std::max(0,int(std::floor(offset)));
            for(int i=first;i<std::min(count,int(std::ceil(offset+shown)));++i)draw(i,float(i)-offset);
        };
        if(roster) {
            text(64,235,tr("PLAYERS","玩家"),12,accent);
            const float first=scroll_viewport(m,scrollKey+"/players",s.player,s.players.size(),5,{48,264,254,390},78);
            clip(Rect{48,264,242,390});
            visible_rows(first,int(s.players.size()),5,[&](int i,float offset) {
                const auto& p=s.players[i];const float y=264+offset*78;card({48,y,242,70},i==s.player);
                fitted(63,y+9,p.name,224,17,i==s.player?accent:textColor);
                fitted(63,y+40,p.pps+" PPS  ·  "+p.apm+" APM",212,13,muted);
                hit({48,y,242,70},Frontend::UiCommandKind::StatisticsPlayer,i);
            });
            clip({});
            text(64,666,std::to_string(s.player+1)+" / "+std::to_string(s.players.size()),12,muted);
        }
        if(!s.metrics.empty() && s.metrics.front().id=="STATUS") {
            card({x,234,w,352});paragraph(x+24,269,s.metrics.front().value,w-48,25,textColor,10);
        } else if(s.page==Page::Summary) {
            std::vector<int> heroes;
            for(const char* id:{"TIME","SCORE","PIECES PER SECOND","ATTACK PER MINUTE"})
                for(std::size_t i=0;i<s.metrics.size();++i)if(s.metrics[i].id==id)heroes.push_back(int(i));
            const auto number=[&](float px,float py,const std::string& value,float width,float size,Color color) {
                size=std::min(size,size*width/std::max(1.f,measure(value,size)));fitted(px,py,value,width,size,color);
            };
            const float cw=(w-12*(std::max(1,int(heroes.size()))-1))/std::max(1,int(heroes.size()));
            for(std::size_t i=0;i<heroes.size();++i) {
                const auto& metric=s.metrics[heroes[i]];const float px=x+i*(cw+12);
                const bool active=touch_controls() && heroes[i]==s.selectedMetric;
                rounded(px,234,cw,100,7,active?focus:panelColor);rect(px,234,cw,2,active?accent:border);
                fitted(px+16,249,metric.label,cw-32,14,muted);
                number(px+16,279,metric.value,cw-32,32,textColor);
                hit({px,234,cw,100},Frontend::UiCommandKind::StatisticsMetric,heroes[i]);
            }
            // The cards above are a read-only summary. The single selectable
            // list follows the shared controller order, including those four
            // metrics, so every Down press advances one visible row.
            constexpr int count=6;constexpr float rowHeight=44;
            const float first=scroll_viewport(m,scrollKey+"/summary",s.selectedMetric,s.metrics.size(),count,
                {x,378,w,count*rowHeight},rowHeight);
            text(x,350,tr("ALL STATISTICS","全部指标"),12,muted);
            clip(Rect{x,378,w-14,count*rowHeight});
            visible_rows(first,int(s.metrics.size()),count,[&](int i,float offset) {
                const auto& metric=s.metrics[i];const bool active=i==s.selectedMetric;
                const float py=378+offset*rowHeight;
                if(active){rounded(x,py,w-14,rowHeight-2,4,focus);rect(x,py,3,rowHeight-2,accent);}
                else rect(x,py+rowHeight-3,w-14,1,border);
                text(x+14,py+11,std::to_string(i+1),12,muted);
                fitted(x+55,py+9,metric.label,w*.62f-55,16,active?accent:muted);
                const float width=w*.38f-32;
                const float size=text_size(std::min(21.f,21.f*width/std::max(1.f,measure(metric.value,21))));
                fitted(x+w-28-std::min(width,measure(metric.value,size)),py+8,metric.value,width,size,textColor);
                hit({x,py,w-14,rowHeight-2},Frontend::UiCommandKind::StatisticsMetric,i);
            });
            clip({});
            if(!s.metrics.empty()) {
                const auto& selected=s.metrics[std::clamp(s.selectedMetric,0,int(s.metrics.size())-1)];
                const bool fullValue=measure(selected.value,21)>w*.38f-32;
                fitted(x,fullValue?655:666,tr("Selected ","当前指标 ")+std::to_string(s.selectedMetric+1)+" / "+std::to_string(s.metrics.size())+
                    (fullValue?"  ·  "+selected.label:touch_controls()?std::string{}:tr("  ·  ↑ / ↓ move in order","  ·  ↑ / ↓ 逐项移动")),w,12,muted);
                if(fullValue)fitted(x,681,selected.value,w,20,textColor);
            }
        } else if(s.page==Page::Challenges) {
            const float first=scroll_viewport(m,scrollKey+"/challenges",s.selectedChallenge,s.challenges.size(),5,{x,234,w,380},76);
            clip(Rect{x,234,w-14,380});
            visible_rows(first,int(s.challenges.size()),5,[&](int i,float offset) {
                const auto& c=s.challenges[i];const float y=234+offset*76;card({x,y,w-14,70},i==s.selectedChallenge);
                fitted(x+18,y+8,c.label,w-164,19,i==s.selectedChallenge?accent:textColor);
                fitted(x+18,y+39,c.value,w-164,16,muted);
                if(!c.description.empty()){rounded(x+w-100,y+18,64,28,6,focus);fitted(x+w-90,y+23,c.description,44,14,accent);}
                hit({x,y,w-14,70},Frontend::UiCommandKind::StatisticsChallenge,i);
            });
            clip({});
            paragraph(x,631,s.challengeNotice,w,14,secondary,3);
        } else {
            const int rows=s.pieces.size(),columns=s.lines.size();
            const float label=roster?94:110,cw=162,rh=42,bodyTop=343;
            const int selectedRow=std::clamp(s.selectedRow,0,rows),selectedCol=std::clamp(s.selectedColumn,0,columns);
            const auto& selectedCounts=selectedRow==rows?(selectedCol==columns?s.total:s.columnTotals[selectedCol]):
                (selectedCol==columns?s.rowTotals[selectedRow]:s.cells[selectedRow][selectedCol]);
            const bool longCounts=std::any_of(selectedCounts.begin(),selectedCounts.end(),[&](auto value){return measure(std::to_string(value),23)>(w-24)/3-28;});
            const int visible=std::min(columns,std::max(1,int((w-label)/cw)-1)),shown=std::min(rows,longCounts?4:5);
            const float tableWidth=label+(visible+1)*cw,bodyHeight=shown*rh;
            const float firstRow=scroll_viewport(m,scrollKey+"/clear-rows",std::min(s.selectedRow,std::max(0,rows-1)),rows,shown,
                {x,bodyTop,tableWidth,bodyHeight+12},rh);
            const float firstCol=scroll_viewport(m,scrollKey+"/clear-columns",std::min(s.selectedColumn,std::max(0,columns-1)),columns,visible,
                {x+label,bodyTop,visible*cw,bodyHeight+12},cw,true);
            const char* en[]={"Normal","Spin","Mini Spin"};const char* zh[]={"普通","Spin","Mini Spin"};
            const Color colors[]={textColor,accent,secondary};
            fitted(x,232,tr("CLEAR COUNTS BY TYPE · drag to scroll","按类别统计次数 · 拖动滚动"),w,14,muted);
            rect(x,264,tableWidth,78,raised);text(x+12,291,tr("PIECE","块型"),13,muted);
            const auto column_header=[&](int col,float px) {
                const auto title=col==columns?tr("TOTAL","合计"):std::to_string(s.lines[col])+tr(" lines"," 行");
                centered_text({px+5,268,cw-10,25},title,18,accent);
                for(int k=0;k<3;++k) {
                    const float sub=px+k*cw/3;
                    if(k==2) {
                        centered_text({sub+2,297,cw/3-4,20},"Mini",18,colors[k]);
                        centered_text({sub+2,317,cw/3-4,20},"Spin",18,colors[k]);
                    } else centered_text({sub+2,306,cw/3-4,24},tr(en[k],zh[k]),18,colors[k]);
                }
                rect(px,264,1,78,border);
            };
            clip(Rect{x+label,264,visible*cw,78});
            visible_rows(firstCol,columns,visible,[&](int j,float offset){column_header(j,x+label+offset*cw);});
            clip({});column_header(columns,x+label+visible*cw);
            const float totalsTop=bodyTop+bodyHeight+14;
            const auto row_label=[&](int row,float y) {
                if(row==rows)rect(x,y,tableWidth-12,rh,raised);
                else if(row%2==0)rect(x,y,tableWidth-12,rh,panelColor);
                if(row==s.selectedRow)rect(x,y,label,rh,focus);
                if(row<rows && row<int(s.pieceColors.size()))rect(x+10,y+16,10,10,cell_color(s.pieceColors[row],s.obscured));
                fitted(x+28,y+10,row==rows?tr("TOTAL","合计"):s.pieces[row],label-38,14);
            };
            const auto cell=[&](int row,int col,float px,float y) {
                const auto& c=row==rows?(col==columns?s.total:s.columnTotals[col]):(col==columns?s.rowTotals[row]:s.cells[row][col]);
                const bool selected=row==s.selectedRow && col==s.selectedColumn;
                if(selected){rect(px+1,y+1,cw-2,rh-2,focus);outline(px+1,y+1,cw-2,rh-2,accent,stroke);}
                for(int k=0;k<3;++k) {
                    const auto value=std::to_string(c[k]);const float sub=px+k*cw/3;
                    if(measure(value,19)<=cw/3-8)centered_text({sub+4,y+5,cw/3-8,rh-10},value,19,c[k]?colors[k]:muted);
                    else fitted(sub+4,y+10,value,cw/3-8,19,c[k]?colors[k]:muted);
                }
                rect(px,y,1,rh,border);
                hit({px,y,cw,rh},Frontend::UiCommandKind::StatisticsCell,0,row,col);
            };
            clip(Rect{x,bodyTop,tableWidth-12,bodyHeight});
            visible_rows(firstRow,rows,shown,[&](int i,float offset){row_label(i,bodyTop+offset*rh);});
            clip({});row_label(rows,totalsTop);
            const auto cells=[&](int row,float y) {
                visible_rows(firstCol,columns,visible,[&](int j,float offset){cell(row,j,x+label+offset*cw,y);});
            };
            clip(Rect{x+label,bodyTop,visible*cw,bodyHeight});
            visible_rows(firstRow,rows,shown,[&](int i,float offset){cells(i,bodyTop+offset*rh);});
            clip(Rect{x+label,totalsTop,visible*cw,rh});cells(rows,totalsTop);
            clip(Rect{x+label+visible*cw,bodyTop,cw-12,bodyHeight});
            visible_rows(firstRow,rows,shown,[&](int i,float offset){cell(i,columns,x+label+visible*cw,bodyTop+offset*rh);});
            clip(Rect{x+label+visible*cw,totalsTop,cw-12,rh});cell(rows,columns,x+label+visible*cw,totalsTop);clip({});
            const int row=std::clamp(s.selectedRow,0,rows),col=std::clamp(s.selectedColumn,0,columns);
            const auto& c=row==rows?(col==columns?s.total:s.columnTotals[col]):(col==columns?s.rowTotals[row]:s.cells[row][col]);
            const auto selection=(row==rows?tr("All pieces","全部块型"):s.pieces[row])+"  /  "+(col==columns?tr("All clears","全部消行"):std::to_string(s.lines[col])+tr(" lines"," 行"));
            const float selectionY=totalsTop+rh+10,detailsY=selectionY+27;
            fitted(x,selectionY,selection,w,15,accent);
            if(longCounts) {
                rounded(x,detailsY,w,74,6,panelColor);
                for(int k=0;k<3;++k) {
                    const auto value=std::to_string(c[k]);const float valueWidth=measure(value,20),y=detailsY+3+k*24;
                    fitted(x+14,y,tr(en[k],zh[k]),w-valueWidth-44,20,muted);
                    text(x+w-valueWidth-14,y,value,20,colors[k]);
                }
            } else
            for(int k=0;k<3;++k) {
                const float px=x+k*(w+12)/3,width=(w-24)/3;
                rounded(px,detailsY,width,52,6,panelColor);fitted(px+14,detailsY+4,tr(en[k],zh[k]),width-28,12,muted);
                const auto value=std::to_string(c[k]);const float size=std::min(23.f,23.f*(width-28)/std::max(1.f,measure(value,23)));
                fitted(px+14,detailsY+27,value,width-28,size,colors[k]);
            }

        }
        if(!m.notice.empty()){rounded(pageWidth-580,93,532,40,8,focus);fitted(pageWidth-564,104,m.notice,500,14,accent);}
        footer();return true;
    }
    if(m.kind==Kind::Lobby && m.lobby) {
        const auto& l=*m.lobby;
        const float sideX=pageWidth-362,memberWidth=pageWidth-434;
        text(48,94,tr("Match lobby","对战大厅"),32);fitted(50,140,m.title,pageWidth-120,20,accent);
        paragraph(50,180,m.hint,pageWidth-120,16,muted,2);
        card({48,233,memberWidth,451});text(68,249,tr("ROOM MEMBERS","房间成员"),13,accent);
        const auto count=std::min(l.pageSize,l.members.size()-std::min(l.offset,l.members.size()));
        const float rh=std::min(49.f,376.f/std::max<std::size_t>(1,count));
        for(std::size_t i=0;i<count;++i) {
            const auto& p=l.members[l.offset+i];const float y=283+i*rh;
            if(i%2==0)rounded(60,y,memberWidth-24,rh-2,4,raised);
            const float ty=y+(rh-16)/2;rect(74,ty+4,7,7,p.ready?accent:muted);
            fitted(98,ty,p.name,sideX-472,16);
            if(p.host)text(sideX-367,ty+1,"HOST",11,secondary);
            fitted(sideX-298,ty,p.role,118,14,muted);
            fitted(sideX-170,ty,p.ready?tr("Ready","已准备"):tr("Waiting","等待中"),118,14,p.ready?accent:muted);
        }
        fitted(sideX,240,tr("READY PLAYERS","已准备玩家"),300,13,muted);
        text(sideX,267,std::to_string(l.ready)+" / "+std::to_string(l.battles),48,accent);
        rect(sideX,337,300,4,border);rect(sideX,337,300*l.ready/std::max(1,l.battles),4,accent);
        text(sideX,372,tr("ROOM CAPACITY","房间人数"),13,muted);
        text(sideX,400,std::to_string(l.players)+" / "+std::to_string(l.capacity),32);
        if(l.canStart) {
            card({sideX-4,486,306,76},l.startEnabled);
            fitted(sideX+14,500,tr("Start match now","开始对局"),266,22,l.startEnabled?accent:muted);
            text(sideX+14,537,"ENTER",12,muted);
        }
        paragraph(sideX,590,m.notice,302,16,muted,4);
        text(68,665,std::to_string(count?l.offset+1:0)+"–"+std::to_string(l.offset+count)+" / "+std::to_string(l.members.size()),12,muted);
        footer();return true;
    }
    return false;
}
}
