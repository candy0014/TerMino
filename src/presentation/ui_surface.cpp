#include "tetr/frontend/ui_surface.h"
namespace Frontend {
namespace {
thread_local UiSink sink;
thread_local std::optional<UiSize> size;
thread_local UiCommandSource commands;
thread_local std::optional<UiMetadata> metadata;
thread_local std::optional<UiCommand> pending;
thread_local std::uint64_t nextPage=0;
bool same_entries(const std::vector<UiEntry>& a,const std::vector<UiEntry>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].id!=b[i].id || a[i].enabled!=b[i].enabled)return false;
    return true;
}
bool same_group(const UiNavigationGroup& a,const UiNavigationGroup& b) {
    return a.title==b.title && same_entries(a.entries,b.entries);
}
}
bool same_ui_page(const UiMetadata& a,const UiMetadata& b) {
    if(a.kind!=b.kind || a.id!=b.id || (a.kind!=UiKind::MapEditor && a.title!=b.title) || a.path!=b.path || a.context!=b.context ||
       a.ancestors.size()!=b.ancestors.size() || bool(a.parent)!=bool(b.parent) || !same_entries(a.entries,b.entries))return false;
    for(std::size_t i=0;i<a.ancestors.size();++i)if(!same_group(a.ancestors[i],b.ancestors[i]))return false;
    if(a.editor && b.editor && (a.editor->sequenceFocus!=b.editor->sequenceFocus ||
        a.editor->field.width!=b.editor->field.width || a.editor->field.height!=b.editor->field.height ||
        a.editor->fieldFirstRow!=b.editor->fieldFirstRow || a.editor->fieldFirstColumn!=b.editor->fieldFirstColumn))return false;
    if(bool(a.statistics)!=bool(b.statistics) || a.keyActions!=b.keyActions)return false;
    if(a.statistics && (a.statistics->page!=b.statistics->page || a.statistics->player!=b.statistics->player ||
        a.statistics->players.size()!=b.statistics->players.size() || a.statistics->pieces!=b.statistics->pieces ||
        a.statistics->lines!=b.statistics->lines || !same_entries(a.statistics->metrics,b.statistics->metrics) ||
        !same_entries(a.statistics->challenges,b.statistics->challenges)))return false;
    return !a.parent || same_group(*a.parent,*b.parent);
}
bool valid_ui_command(const UiMetadata& m,const UiCommand& c) {
    if(c.kind==UiCommandKind::Back)return true;
    if(m.kind==UiKind::Statistics && m.statistics) {
        const auto& s=*m.statistics;
        switch(c.kind) {
        case UiCommandKind::StatisticsPage:return c.index<(s.challenges.empty()?2u:3u);
        case UiCommandKind::StatisticsPlayer:return c.index<s.players.size();
        case UiCommandKind::StatisticsMetric:return s.page==UiStatisticsPage::Summary && c.index<s.metrics.size();
        case UiCommandKind::StatisticsCell:return s.page==UiStatisticsPage::Clears && c.row>=0 && c.column>=0 &&
            std::size_t(c.row)<=s.pieces.size() && std::size_t(c.column)<=s.lines.size();
        case UiCommandKind::StatisticsChallenge:return s.page==UiStatisticsPage::Challenges && c.index<s.challenges.size();
        default:return false;
        }
    }
    if(c.kind==UiCommandKind::Activate || c.kind==UiCommandKind::BrowseFile) {
        const auto* entries=&m.entries;
        bool valid=m.kind==UiKind::Menu || m.kind==UiKind::Choices || m.kind==UiKind::Start;
        if(c.ancestor) {
            valid=true;
            if(*c.ancestor<m.ancestors.size())entries=&m.ancestors[*c.ancestor].entries;
            else if(*c.ancestor==m.ancestors.size() && m.parent)entries=&m.parent->entries;
            else valid=false;
        }
        if(!valid || c.index>=entries->size() || !entries->at(c.index).enabled ||
            entries->at(c.index).control==UiControl::Separator)return false;
        return c.kind!=UiCommandKind::BrowseFile || entries->at(c.index).filePath;
    }
    if(m.kind!=UiKind::MapEditor || !m.editor)return false;
    const auto& e=*m.editor;
    switch(c.kind) {
    case UiCommandKind::MapBrush:return !e.sequenceFocus && c.index<9;
    case UiCommandKind::MapPaint:return !e.sequenceFocus && c.row>=0 && c.column>=0 && c.row<e.field.height && c.column<e.field.width;
    case UiCommandKind::MapEndStroke:return true;
    case UiCommandKind::MapTab:return c.index<2;
    case UiCommandKind::MapCaret:return e.sequenceFocus && !e.generated && c.index<=e.sequenceSize;
    case UiCommandKind::MapCandidate:return e.sequenceFocus && !e.generated && c.index<e.candidates.size();
    case UiCommandKind::MapScroll:return e.sequenceFocus && !e.generated && c.index<2 && c.delta>=-16 && c.delta<=16;
    default:return false;
    }
}
UiSurfaceScope::UiSurfaceScope(UiSink target,UiSize extent,UiCommandSource source)
    :previous_(std::move(sink)),previousSize_(size),previousCommands_(std::move(commands)),
     previousMetadata_(std::move(metadata)),previousPending_(std::move(pending)) {
    sink=std::move(target);size=extent;commands=std::move(source);metadata.reset();pending.reset();
}
UiSurfaceScope::~UiSurfaceScope() {
    sink=std::move(previous_);size=previousSize_;commands=std::move(previousCommands_);
    metadata=std::move(previousMetadata_);pending=std::move(previousPending_);
}
bool ui_surface_active() noexcept {return static_cast<bool>(sink);}
std::optional<UiSize> ui_surface_size() noexcept {return size;}
void present_ui(UiFrame frame) {
    if(size)frame.extent=*size;
    frame.metadata.page=metadata && same_ui_page(*metadata,frame.metadata)?metadata->page:++nextPage;
    metadata=frame.metadata;
    if(sink)sink(std::move(frame));
}
void clear_ui() {present_ui({});}
std::optional<UiCommand> poll_ui_command(const std::function<bool(const UiCommand&)>& accept) {
    if(!commands || !metadata)return {};
    for(;;) {
        if(!pending)pending=commands();
        if(!pending)return {};
        if(pending->page!=metadata->page) {pending.reset();continue;}
        if(!valid_ui_command(*metadata,*pending)){pending.reset();continue;}
        if(accept && !accept(*pending))return {};
        auto result=pending;pending.reset();return result;
    }
}
}
