#include "post_game_statistics.h"
#include "settings_navigation.h"
#include "ui_text.h"
#include "tetr/app/post_game_interaction.h"
#include "tetr/replay/playback_clock.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/piece.h"
#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace App {
namespace {
double nonnegative(double value) { return std::isfinite(value) ? std::max(0.,value) : 0.; }
std::pair<Runtime::Duration,Runtime::Duration> result_times(
    const Runtime::GameContext& c,const Runtime::PlayerSessionState& p) {
    if (p.timing.completedGameTime && p.timing.completedRealTime)
        return {*p.timing.completedGameTime,*p.timing.completedRealTime};
    if (c.config.mode.replayMode && c.replay_component()) {
        const auto& r=Replay::session(c);
        const auto game=r.resultGameTime.value_or(Replay::playback_game_elapsed(c,c.now())*r.recordedTimeMultiplier);
        return {game,r.resultRealTime.value_or(r.separateGameTime ? r.lastConsumedTimestamp : game)};
    }
    const auto game=Runtime::elapsed_game_time(c.session,c.now())*c.config.timeMultiplier;
    return {game,p.timing.realGameStart ? c.real_now()-*p.timing.realGameStart : game};
}
std::string decimal(double value,int precision=3) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<nonnegative(value);return out.str();
}
double rate(double value,double denominator) { return denominator>0 ? value/denominator : 0.; }
std::string time_text(Runtime::Duration time) {
    const auto millis=static_cast<unsigned long long>(std::llround(std::min(nonnegative(time.seconds()),6e13)*1000));
    const auto minutes=millis/60000;
    std::ostringstream out;out<<std::setfill('0')<<std::setw(2)<<minutes<<':'
        <<std::fixed<<std::setprecision(3)<<std::setw(6)<<(millis%60000)/1000.;return out.str();
}
void add(Runtime::ClearCounts& target,const Runtime::ClearCounts& source) {
    for (int i=0;i<3;++i) target[i]+=source[i];
}
std::string counts_text(const Runtime::ClearCounts& c) {
    return std::to_string(c[0])+";"+std::to_string(c[1])+";"+std::to_string(c[2]);
}
std::string pad(std::string text,int width) {
    text=Util::TerminalText::clip(text,std::max(0,width));
    text+=std::string(std::max(0,width-static_cast<int>(Util::TerminalText::width(text))),' ');return text;
}
}

void freeze_post_game_times(Runtime::GameContext& c,Runtime::PlayerSessionState& p) {
    const auto times=result_times(c,p);
    p.timing.completedGameTime=Runtime::Duration::from_seconds(nonnegative(times.first.seconds()));
    p.timing.completedRealTime=Runtime::Duration::from_seconds(nonnegative(times.second.seconds()));
}

PostGameStatistics make_post_game_statistics(const Runtime::GameContext& c,
    const Runtime::PlayerSessionState& p) {
    PostGameStatistics result;const auto times=result_times(c,p);
    const double seconds=nonnegative(times.first.seconds());const auto& s=p.gameplay.scoring;
    const auto metric=[&](const std::string& name,const std::string& value) {result.metrics.emplace_back(name,value);};
    metric("TIME",time_text(times.first));
    if (c.config.simulationPps>0) {
        const bool unknown=c.config.mode.replayMode && c.replay_component() &&
            !Replay::session(c).resultRealTime && !Replay::session(c).separateGameTime;
        metric("REAL TIME",unknown ? "N/A" : time_text(times.second));
    }
    if (c.config.garbage.mode==Runtime::GarbageMode::QuickPlay) {
        metric("FLOOR",std::to_string(c.session.quickPlay.floor));
        metric("ALTITUDE",decimal(c.session.quickPlay.altitude,2));
        metric("ALTITUDE PER SECOND",decimal(rate(c.session.quickPlay.altitude,seconds)));
    }
    metric("ATTACK",std::to_string(s.totalAttack));
    metric("ATTACK PER MINUTE",decimal(rate(60.*s.totalAttack,seconds)));
    metric("ATTACK PER PIECE",decimal(rate(s.totalAttack,s.placedBlocks)));
    metric("TOTAL ATTACK",std::to_string(s.totalOutgoingAttack));
    metric("TOTAL RECEIVED",std::to_string(s.totalReceivedAttack));
    metric("MAXIMUM SPIKE",std::to_string(s.maxSpike));
    metric("PIECES",std::to_string(s.placedBlocks));
    metric("PIECES PER SECOND",decimal(rate(s.placedBlocks,seconds)));
    metric("LINES",std::to_string(s.totalLines));
    metric("LINES PER MINUTE",decimal(rate(60.*s.totalLines,seconds)));
    metric("ALLCLEAR",std::to_string(s.allClearCount));
    metric("HOLDS",std::to_string(s.totalHolds));
    // Match the live display: the first clear/difficult clear starts the
    // chain; the displayed combo/B2B counts the following extensions.
    metric("MAXIMUM COMBO",std::to_string(std::max(0,s.maxCombo-1)));
    metric("MAXIMUM BACK-TO-BACK CHAIN",std::to_string(std::max(0,s.maxB2B-1)));

    std::map<std::pair<std::string,int>,std::size_t> rows;
    std::set<std::string> usedNames;
    std::set<int> library;
    library.insert(s.roundPieceTypes.begin(),s.roundPieceTypes.end());
    for (const auto type:p.pieces.allowedPieces) if (type.valid()) library.insert(type.index());
    if (!c.config.initialMap && c.config.garbage.mode==Runtime::GarbageMode::QuickPlay &&
        (c.config.quickPlay.fool==Runtime::FoolMode::AprilFools2026 || c.config.quickPlay.fool==Runtime::FoolMode::ReverseAprilFools2026))
        for (const auto type:Piece::types_for_rotation_system(Runtime::RotationSystem::AprilFools2026,false,c.session.quickPlay.floor))
            library.insert(type.index());
    for (const auto& [key,counts]:s.clearStatistics) library.insert(key.first);
    for (const auto type:p.pieces.nextPreview) if (type.valid()) library.insert(type.index());
    for (const auto& queued:p.pieces.bag) if (queued.type.valid()) library.insert(queued.type.index());
    if (p.gameplay.hold.piece.valid()) library.insert(p.gameplay.hold.piece.index());
    std::vector<std::size_t> rowForType(Piece::catalog().size(),static_cast<std::size_t>(-1));
    std::vector<int> order;std::set<int> seen;
    for (const auto type:p.pieces.allowedPieces) if (type.valid() && seen.insert(type.index()).second) order.push_back(type.index());
    for (const int type:library) if (seen.insert(type).second) order.push_back(type);
    for (const int type:order) {
        if (type<0 || static_cast<std::size_t>(type)>=Piece::catalog().size()) continue;
        const auto& entry=Piece::catalog()[type];
        const std::string name(entry.metadata.name);
        // Merge standard rotation profiles, while keeping distinct special
        // geometries that happen to share a name (e.g. pentomino/Fool I5).
        const auto key=std::make_pair(name,Runtime::standard_rotation_system(entry.metadata.rotationSystem) ? -1 : entry.shapeIndex);
        const auto [row,inserted]=rows.emplace(key,result.pieces.size());
        if (inserted) {
            std::string label=name;
            if (!usedNames.insert(name).second) label+=entry.metadata.rotationSystem==Runtime::RotationSystem::AprilFools2026
                ? " [Fool]" : " ["+std::to_string(entry.shapes[0].size())+" cells]";
            result.pieces.push_back(label);
            result.pieceColors.push_back(entry.metadata.colorId);
        }
        rowForType[entry.type.index()]=row->second;
    }
    std::set<int> columns;
    for (const auto& [key,counts]:s.clearStatistics)
        if (key.second>=0 && (counts[0] || counts[1] || counts[2])) columns.insert(key.second);
    result.lines.assign(columns.begin(),columns.end());
    result.cells.assign(result.pieces.size(),std::vector<Runtime::ClearCounts>(result.lines.size()));
    result.rowTotals.resize(result.pieces.size());result.columnTotals.resize(result.lines.size());
    for (const auto& [key,counts]:s.clearStatistics) {
        if (key.first<0 || static_cast<std::size_t>(key.first)>=Piece::catalog().size() || key.second<0) continue;
        const auto column=std::lower_bound(result.lines.begin(),result.lines.end(),key.second);
        if (column==result.lines.end() || *column!=key.second) continue;
        const auto row=rowForType[key.first];
        const auto col=static_cast<std::size_t>(column-result.lines.begin());
        add(result.cells[row][col],counts);add(result.rowTotals[row],counts);
        add(result.columnTotals[col],counts);add(result.total,counts);
    }
    return result;
}

PostGameStatisticsView::PostGameStatisticsView(PostGameStatistics first,std::optional<PostGameStatistics> second) {
    players_.push_back(std::move(first));if (second) players_.push_back(std::move(*second));
}
void PostGameStatisticsView::next_page() {
    page_=static_cast<PostGamePage>((static_cast<int>(page_)+1)%3);invalidate();
}
void PostGameStatisticsView::select_player(int delta) {
    if (players_.size()<2 || !delta) return;
    player_=(player_+static_cast<int>(players_.size())+delta%static_cast<int>(players_.size()))%players_.size();
    selectedRow_=selectedColumn_=rowStart_=columnStart_=summaryRow_=summaryScroll_=0;invalidate();
}
void PostGameStatisticsView::move(int rows,int columns) {
    const auto& s=players_[player_];
    if (page_==PostGamePage::Clears) {
        selectedRow_=std::clamp(selectedRow_+rows,0,static_cast<int>(s.pieces.size()));
        selectedColumn_=std::clamp(selectedColumn_+columns,0,static_cast<int>(s.lines.size()));
    } else if (page_==PostGamePage::Summary)
        summaryRow_=std::clamp(summaryRow_+rows,0,std::max(0,static_cast<int>(s.metrics.size())-1));
    dirty_=true;
}
bool PostGameStatisticsView::poll(Settings::NavigationInput& first,Settings::NavigationInput* second) {
    using K=Settings::NavigationKey;
    const auto pressed=[&](K key,bool repeat) {
        const bool a=first.update(key,repeat);const bool b=second && second->update(key,repeat);return a||b;
    };
    bool changed=pressed(K::Tab,false);
    if (changed) next_page();
    const bool up=pressed(K::Up,true),down=pressed(K::Down,true),left=pressed(K::Left,true),right=pressed(K::Right,true);
    const bool prev=pressed(K::PageUp,true),next=pressed(K::PageDown,true);
    if (page_!=PostGamePage::Board) {
        if (up || down || left || right) {move(int(down)-int(up),int(right)-int(left));changed=true;}
        if (prev || next) {
            if (players_.size()>1) select_player(int(next)-int(prev));
            else move(10*(int(next)-int(prev)),0);
            changed=true;
        }
    }
    return changed;
}
bool PostGameStatisticsView::reserves(int key) const {
    using N=Settings::NavigationInput;using K=Settings::NavigationKey;
    if (key==N::key_code(K::Tab)) return true;
    if (page_==PostGamePage::Board) return false;
    for (const auto k:{K::Up,K::Down,K::Left,K::Right,K::PageUp,K::PageDown})
        if (key==N::key_code(k)) return true;
    return false;
}

void PostGameStatisticsView::show_replay_save_prompt(const std::string& defaultName,const std::string& value,std::size_t caret) {
    savePrompt_=SavePrompt{defaultName,value,std::min(caret,value.size())};notice_.clear();dirty_=true;
}
void PostGameStatisticsView::finish_replay_save_prompt(const std::string& message) {
    savePrompt_.reset();notice_=message;dirty_=true;
}

void PostGameStatisticsView::render(Runtime::GameContext& context,const std::string& footer,Render::TerminalSize size) {
    if (page_==PostGamePage::Board) return;
    size.columns=std::max(1,size.columns);size.rows=std::max(1,size.rows);
    if (canvas_.terminalSize!=size) {invalidate();canvas_.terminalSize=size;}
    if (shownFooter_!=footer) {dirty_=true;shownFooter_=footer;}
    if (!dirty_) return;
    Render::OutputGuard output(context);dirty_=false;
    canvas_.columnsLimit=size.columns-1;canvas_.rowsLimit=size.rows;canvas_.begin_frame();
    const bool obscured=context.config.display.obscuredColors;
    const auto normal=Render::ui_colors(Render::ColorRole::Text,obscured);
    const auto heading=Render::ui_colors(Render::ColorRole::Accent,obscured);
    const auto background=normal.background;
    const TerminalCanvas::Color boxHeading=obscured ? TerminalCanvas::Color{{54,57,62}} : TerminalCanvas::Color{{237,109,0}};
    const TerminalCanvas::Color selectedBackground=obscured ? TerminalCanvas::Color{{150,150,150}} : heading.foreground;
    const TerminalCanvas::Color selectedText{{12,12,12}};
    const int width=std::max(0,size.columns-2),left=size.columns>2 ? 1 : 0;
    const auto line=[&](int row,const std::string& text,bool accent=false) {
        canvas_.write(row,left,text,width,accent ? heading.foreground : normal.foreground,background);
    };
    const auto& s=players_[player_];
    if (size.rows<10 || size.columns<28) {
        line(size.rows/2,UiText::tr("Enlarge the window to view statistics."));
    } else if (page_==PostGamePage::Summary) {
        // Metrics are grouped in model order. Keyboard navigation follows the
        // same top-to-bottom order in every window size.
        const int panelWidth=std::min(width,56),x=left+(width-panelWidth)/2;
        const int span=panelWidth<48 ? 2 : 1;
        TerminalCanvas panels;panels.columnsLimit=size.columns-1;
        std::array<std::vector<int>,4> groups;
        for (int i=0;i<static_cast<int>(s.metrics.size());++i) {
            const auto& label=s.metrics[i].first;
            const int group=label=="TIME" || label=="REAL TIME" || label=="FLOOR" || label.find("ALTITUDE")==0 ? 0
                : label.find("ATTACK")==0 || label.find("TOTAL ")==0 || label=="MAXIMUM SPIKE" ? 1
                : label.find("PIECES")==0 || label.find("LINES")==0 || label=="ALLCLEAR" ? 2 : 3;
            groups[group].push_back(i);
        }
        const std::array<std::string,4> names{{"TIME & ALTITUDE","ATTACK","PIECES & LINES","CHAINS & HOLD"}};
        int top=0,selectedAt=0;
        for (int group=0;group<4;++group) {
            if (groups[group].empty()) continue;
            const int height=static_cast<int>(groups[group].size())*span+2;
            panels.box(top,x,panelWidth-4,height-2,UiText::tr(names[group]),boxHeading,normal.foreground,background);
            for (int i=0;i<static_cast<int>(groups[group].size());++i) {
                const int index=groups[group][i],row=top+1+i*span;
                const auto& [label,value]=s.metrics[index];const bool selected=index==summaryRow_;
                if (selected) selectedAt=row;
                const auto fg=selected ? selectedText : normal.foreground;
                const auto bg=selected ? selectedBackground : background;
                const auto shortLabel=label=="MAXIMUM BACK-TO-BACK CHAIN" ? "MAXIMUM B2B CHAIN" : label;
                const int w=panelWidth-4,valueWidth=std::min(w/2,static_cast<int>(Util::TerminalText::width(value)));
                if (span==2) {
                    panels.write(row,x+2,shortLabel,w,fg,bg);
                    panels.write(row+1,x+2,"  "+value,w,selected ? heading.foreground : normal.foreground,background);
                } else panels.write(row,x+2,pad(shortLabel,w-valueWidth-1)+" "+value,w,fg,bg);
            }
            top+=height+1;
        }
        const int visible=size.rows;
        if (selectedAt<summaryScroll_) summaryScroll_=selectedAt;
        if (selectedAt+span>summaryScroll_+visible) summaryScroll_=selectedAt+span-visible;
        summaryScroll_=std::clamp(summaryScroll_,0,std::max(0,top-1-visible));
        for (int i=0;i<visible && i+summaryScroll_<static_cast<int>(panels.pending.size());++i) {
            if (canvas_.pending.size()<=static_cast<std::size_t>(i)) canvas_.pending.resize(i+1);
            canvas_.pending[i]=std::move(panels.pending[i+summaryScroll_]);
        }
    } else {
        const int top=0;
        const int rows=static_cast<int>(s.pieces.size()),columns=static_cast<int>(s.lines.size());
        const int bodyRows=std::max(1,size.rows-top-6);
        int nameWidth=5,cellWidth=5;
        for (const auto& name:s.pieces) nameWidth=std::max(nameWidth,static_cast<int>(Util::TerminalText::width(name)));
        nameWidth=std::min({nameWidth,12,std::max(5,width-19)});
        cellWidth=std::max(cellWidth,static_cast<int>(counts_text(s.total).size()));
        cellWidth=std::min(cellWidth,std::max(5,(width-nameWidth-10)/2));
        const int visible=columns ? std::min(columns,std::max(1,(width-nameWidth-4)/(cellWidth+3)-1)) : 0;
        rowStart_=std::clamp(rowStart_,0,std::max(0,rows-bodyRows));
        if (selectedRow_<rows) {
            if (selectedRow_<rowStart_) rowStart_=selectedRow_;
            if (selectedRow_>=rowStart_+bodyRows) rowStart_=selectedRow_-bodyRows+1;
        }
        columnStart_=std::clamp(columnStart_,0,std::max(0,columns-visible));
        if (selectedColumn_<columns) {
            if (selectedColumn_<columnStart_) columnStart_=selectedColumn_;
            if (selectedColumn_>=columnStart_+visible) columnStart_=selectedColumn_-visible+1;
        }
        std::vector<int> shown;
        for (int col=columnStart_;col<columns && col<columnStart_+visible;++col) shown.push_back(col);
        shown.push_back(columns);
        const int tableWidth=nameWidth+3+static_cast<int>(shown.size())*(cellWidth+3)+1;
        const int x=left+std::max(0,(width-tableWidth)/2);
        const auto rule=[&](int y,const std::string& l,const std::string& joint,const std::string& r) {
            const auto bar=[](int n) {std::string s;while(n-->0) s+="─";return s;};
            std::string text=l+bar(nameWidth+2);
            for (std::size_t i=0;i<shown.size();++i) text+=joint+bar(cellWidth+2);
            text+=r;canvas_.write(y,x,text,tableWidth,heading.foreground,background);
        };
        rule(top,"┌","┬","┐");
        const auto row=[&](int y,int index,bool header=false) {
            canvas_.write(y,x,"│ "+pad(header ? "PIECE" : index==rows ? "SUM" : s.pieces[index],nameWidth)+" │",nameWidth+4,
                header || index==rows ? heading.foreground : Render::cell_colors(s.pieceColors[index],obscured).foreground,background);
            int at=x+nameWidth+4;
            for (const int col:shown) {
                const Runtime::ClearCounts counts=header ? Runtime::ClearCounts{} : index==rows
                    ? (col==columns ? s.total : s.columnTotals[col]) : (col==columns ? s.rowTotals[index] : s.cells[index][col]);
                const bool selected=!header && index==selectedRow_ && col==selectedColumn_;
                const auto text=header ? (col==columns ? "SUM" : std::to_string(s.lines[col])) : counts_text(counts);
                canvas_.write(y,at," "+pad(text,cellWidth)+" ",cellWidth+2,
                    selected ? selectedText : header || col==columns || index==rows ? heading.foreground
                        : counts==Runtime::ClearCounts{} ? Render::ui_colors(Render::ColorRole::Ghost,obscured).foreground : normal.foreground,
                    selected ? selectedBackground : background);
                at+=cellWidth+2;canvas_.write(y,at++,"│",1,heading.foreground,background);
            }
        };
        row(top+1,0,true);rule(top+2,"├","┼","┤");
        const int count=std::min(bodyRows,rows);
        for (int i=0;i<count;++i) row(top+3+i,rowStart_+i);
        rule(top+3+count,"├","┼","┤");row(top+4+count,rows);
        rule(top+5+count,"└","┴","┘");
    }
    if (!notice_.empty()) {
        const int columns=std::min(width,static_cast<int>(Util::TerminalText::width(notice_)));
        canvas_.write(size.rows-1,left+(width-columns)/2,notice_,columns,normal.foreground,background);
    }
    std::optional<std::pair<int,int>> cursor;
    if (savePrompt_ && size.columns>=6 && size.rows>=6) {
        const int w=std::min(width,72),x=left+(width-w)/2,top=std::max(0,size.rows-6);
        canvas_.box(top,x,w-4,4,UiText::tr("Save Replay"),boxHeading,normal.foreground,background);
        const int content=std::max(1,w-4);
        canvas_.write(top+1,x+2,UiText::tr("Replay file [replay/")+savePrompt_->defaultName+"]",content,normal.foreground,background);
        canvas_.write(top+2,x+2,UiText::tr("Enter: default name   Esc / /cancel: cancel"),content,normal.foreground,background);
        const auto text=UiText::tr("Custom replay name: ")+savePrompt_->value;
        const int caret=static_cast<int>(Util::TerminalText::width(UiText::tr("Custom replay name: ")+savePrompt_->value.substr(0,savePrompt_->caret)));
        const int scroll=std::max(0,caret-content+1);
        canvas_.write(top+3,x+2,Util::TerminalText::slice(text,scroll,content),content,heading.foreground,background);
        cursor=std::pair{top+3,x+2+caret-scroll};
    }
    canvas_.present(cursor,obscured);
}
} // namespace App
