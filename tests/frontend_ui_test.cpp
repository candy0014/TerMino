#include "tetr/frontend/ui_surface.h"
#include "tetr/frontend/text_input.h"
#include <cassert>
#include <deque>
#include <thread>

namespace TetrTests::frontend_ui_test {
int run(int argc,char** argv) {
    using namespace Frontend;
    assert(!ui_surface_active());
    UiFrame captured;
    {
        UiSurfaceScope outer([&](UiFrame frame){captured=std::move(frame);},{120,40});
        UiFrame source;source.rows={{{"中"},{"",{0,0,0},{12,12,12},true}}};
        source.caret=std::pair{1,2};source.metadata.id="editor";
        present_ui(source);source.rows[0][0].glyph="changed";
        assert(captured.rows[0][0].glyph=="中" && captured.rows[0][1].continuation);
        assert(captured.extent==UiSize(120,40) && captured.caret==std::pair(1,2));
        std::thread isolated([]{assert(!ui_surface_active() && !ui_surface_size());});isolated.join();
        bool innerCalled=false;
        {UiSurfaceScope inner([&](UiFrame){innerCalled=true;},{80,24});clear_ui();}
        assert(innerCalled && ui_surface_size()==UiSize(120,40));
        clear_ui();assert(captured.rows.empty());
    }
    assert(!ui_surface_active() && !ui_surface_size());
    {
        std::deque<UiCommand> commands;
        UiSurfaceScope scope([&](UiFrame frame){captured=std::move(frame);},{120,40},[&]() -> std::optional<UiCommand> {
            if(commands.empty())return {};
            const auto command=commands.front();commands.pop_front();return command;
        });
        UiFrame frame;frame.metadata={UiKind::Menu,"menu","Menu",{},{{"a","A"},{"b","B"}}};
        present_ui(frame);const auto originalPage=captured.metadata.page;
        commands.push_back({UiCommandKind::Activate,1,{},originalPage});
        frame.metadata.selected=1;present_ui(frame);
        assert(captured.metadata.page==originalPage);
        assert(!poll_ui_command([](const auto& c){return c.ancestor.has_value();}));
        assert(poll_ui_command()->index==1); // One command, independent of distance from focus.
        commands.push_back({UiCommandKind::Activate,0,{},originalPage});
        frame.metadata.path="new menu";present_ui(frame);
        assert(captured.metadata.page!=originalPage && !poll_ui_command());
        frame.metadata.ancestors.push_back({"Parent",{{"one","One"},{"two","Two"}},0});
        present_ui(frame);
        commands.push_back({UiCommandKind::Activate,1,0,captured.metadata.page});
        const auto ancestor=poll_ui_command();assert(ancestor && ancestor->ancestor==0 && ancestor->index==1);
        commands.push_back({UiCommandKind::Activate,9,{},captured.metadata.page});
        commands.push_back({UiCommandKind::Back,0,{},captured.metadata.page});
        assert(poll_ui_command()->kind==UiCommandKind::Back); // Invalid rows are discarded.
        assert(!poll_ui_command());
    }
    {
        UiMetadata editor;editor.kind=UiKind::MapEditor;editor.editor.emplace();
        auto& e=*editor.editor;e.field.width=10;e.field.height=20;
        UiCommand paint{UiCommandKind::MapPaint};paint.row=19;paint.column=9;
        assert(valid_ui_command(editor,paint));paint.column=10;assert(!valid_ui_command(editor,paint));
        e.sequenceFocus=true;assert(!valid_ui_command(editor,paint));
        e.sequenceSize=100;e.candidates.resize(7);
        assert(valid_ui_command(editor,{UiCommandKind::MapCaret,100}));
        assert(!valid_ui_command(editor,{UiCommandKind::MapCaret,101}));
        assert(valid_ui_command(editor,{UiCommandKind::MapCandidate,6}));
        e.generated=true;assert(!valid_ui_command(editor,{UiCommandKind::MapCandidate,6}));
        const auto shown=editor;editor.title="Map editor *";assert(same_ui_page(shown,editor));
        e.field.width=20;assert(!same_ui_page(shown,editor));
        UiMetadata menu;menu.kind=UiKind::Menu;menu.entries={{"replay","Replay"}};
        assert(!valid_ui_command(menu,{UiCommandKind::BrowseFile}));
        menu.entries[0].filePath=true;assert(valid_ui_command(menu,{UiCommandKind::BrowseFile}));
    }
    {
        UiMetadata stats;stats.kind=UiKind::Statistics;stats.statistics.emplace();auto& s=*stats.statistics;
        s.metrics={{"time","Time"},{"score","Score"}};s.players.resize(2);s.pieces={"I","T"};s.lines={1,2,4};
        assert(valid_ui_command(stats,{UiCommandKind::StatisticsMetric,1}));
        assert(!valid_ui_command(stats,{UiCommandKind::StatisticsMetric,2}));
        assert(valid_ui_command(stats,{UiCommandKind::StatisticsPlayer,1}));
        assert(!valid_ui_command(stats,{UiCommandKind::StatisticsPlayer,2}));
        assert(valid_ui_command(stats,{UiCommandKind::StatisticsPage,1}));
        assert(!valid_ui_command(stats,{UiCommandKind::StatisticsPage,2}));
        const auto summary=stats;s.selectedMetric=1;assert(same_ui_page(summary,stats));
        s.page=UiStatisticsPage::Clears;assert(!same_ui_page(summary,stats));
        UiCommand total{UiCommandKind::StatisticsCell};total.row=2;total.column=3;
        assert(valid_ui_command(stats,total));total.row=3;assert(!valid_ui_command(stats,total));
        total.row=-1;assert(!valid_ui_command(stats,total));
        assert(!valid_ui_command(stats,{UiCommandKind::StatisticsMetric,0}));
        const auto firstPlayer=stats;s.player=1;assert(!same_ui_page(firstPlayer,stats));
        s.challenges={{"challenge","Challenge"}};s.page=UiStatisticsPage::Challenges;
        assert(valid_ui_command(stats,{UiCommandKind::StatisticsPage,2}));
        assert(valid_ui_command(stats,{UiCommandKind::StatisticsChallenge,0}));
        assert(!valid_ui_command(stats,{UiCommandKind::StatisticsChallenge,1}));
    }
    TextField text("previous");text.apply({TextEditKind::Insert,"中文🙂"});
    assert(text.value=="中文🙂" && text.caret==text.value.size());
    text.apply({TextEditKind::Left});text.apply({TextEditKind::Backspace});
    assert(text.value=="中🙂" && text.caret==3);
    text.apply({TextEditKind::Delete});assert(text.value=="中");
    text.apply({TextEditKind::Home});text.apply({TextEditKind::Insert,"A"});
    text.apply({TextEditKind::End});text.apply({TextEditKind::Insert,"B"});
    assert(text.value=="A中B");
    text.apply({TextEditKind::SelectAll});text.apply({TextEditKind::Backspace});
    assert(text.value.empty() && text.caret==0);
    text.apply({TextEditKind::Insert,std::string(16385,'x')});assert(text.value.empty());
    text.apply({TextEditKind::Confirm});assert(text.done && !text.cancelled);
    TextField cancelled;cancelled.apply({TextEditKind::Cancel});assert(cancelled.done && cancelled.cancelled);
    return 0;
}
} // namespace TetrTests::frontend_ui_test
