#include "tetr/frontend/ui_surface.h"
#include "tetr/frontend/text_input.h"
#include <cassert>
#include <thread>

int main() {
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
}
