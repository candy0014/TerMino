#ifndef TETR_ANDROID_MOBILE_UI_H
#define TETR_ANDROID_MOBILE_UI_H
#include "../opengl/renderer.h"
#include "tetr/config/virtual_controls.h"
#include "tetr/frontend/graphical/session.h"

namespace Graphical::Mobile {
using namespace Frontend::Touch;
inline constexpr float contentScale=1.15f,propertyRowHeight=49;
inline constexpr float minimumTextSize=20;
inline bool content_page(const SessionView& view) {
    if(!view.ui)return false;
    using K=Frontend::UiKind;const auto k=view.ui->metadata.kind;
    return k==K::Start || k==K::Menu || k==K::Choices || k==K::Text || k==K::KeyCapture || k==K::MapEditor || k==K::Lobby;
}
inline bool gameplay_surface(const SessionView& view) {
    return !view.ui || (view.ui->overlay && !content_page(view) && view.ui->metadata.kind!=Frontend::UiKind::Statistics);
}
enum class Command {Back,Confirm,Settings,Up,Down,Left,Right,Tab,PageUp,PageDown,
    Save,Undo,Slower,Faster,OpenFile,Export,Layout,Apply,Properties,Previous,Next,Duplicate,Reset,Remove,Player,Enabled,EndMatch,
    MapOptions,MapSave,MapUndo,MapRedo,MapUndoSave,Delete,Backspace,Search,Home,End,Paste,SelectAll,SavePreset,Undefined,Exit,Retry,ToggleRail,Root,SwitchTarget};
struct RailButton {Command command;std::string label;Rect bounds;};
struct Frame {
    Layout layout;
    Area area;
    Rect railBounds{0,0,156,800};
    Rect railViewport{};
    float railOffset=0,railMaxOffset=0;
    bool editor=false,properties=false,chinese=true,railVisible=true,railToggle=false;
    std::size_t selected=0;
    std::uint64_t page=0;
    int field=-1;
    std::string input,message;
    std::vector<RailButton> rail,tools;
};
class Controls {
public:
    Controls();
    Frame frame(const SessionView& view,Area area,std::optional<double> windowLeft={});
    void edit();
    bool editing() const {return editing_;}
    bool text_active() const {return field_>=0;}
    bool numeric_text() const {return field_>=0 && field_!=12;}
    void command(Command command);
    void scroll_rail(float pixels) {railOffset_=std::clamp(railOffset_+pixels,0.f,railMaxOffset_);}
    void editor_down(Point point,const Area& area);
    void editor_move(Point point,const Area& area);
    void editor_up() {drag_=0;vertex_=-1;}
    void text(const std::string& text) {if(field_>=0){if(replaceInput_){input_.clear();replaceInput_=false;}input_+=text;}}
    void backspace();
    void cancel_field() {field_=-1;input_.clear();}
    void accept_field();
    const Layout& layout() const {return active_layout(config_);}
private:
    Layout& draft_layout();
    Button* selected();
    Configuration config_,draft_;
    bool editing_=false,properties_=false,chinese_=true,replaceInput_=false,railOpen_=false;
    int railContext_=-1;
    std::size_t selected_=0;
    std::uint64_t page_=0;
    int field_=-1,drag_=0,vertex_=-1;
    float railOffset_=0,railMaxOffset_=0;
    std::vector<Command> railCommands_;
    Point dragStart_,buttonStart_;
    std::string input_,message_;
};
std::vector<RailButton> rail(const Frame& frame,const SessionView& view);
std::optional<Command> rail_hit(const Frame& frame,Point point);
bool surface(Renderer& renderer,const Frontend::UiFrame& ui,const Frame& frame);
void overlay(Renderer& renderer,const Frame& frame,bool gameplay);
int action_index(const std::string& action);
Rect editor_field_bounds(const Area& area);
Rect property_bounds(const Area& area,const Button& selected);
// Adapt display instructions only; IDs, values and keyboard commands stay shared.
Frontend::UiMetadata touch_metadata(Frontend::UiMetadata metadata);
}
#endif
