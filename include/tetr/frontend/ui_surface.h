#ifndef TETR_FRONTEND_UI_SURFACE_H
#define TETR_FRONTEND_UI_SURFACE_H
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace Frontend {
enum class UiKind { Canvas, Start, Menu, Choices, Text, KeyCapture, MapEditor, Statistics, Lobby };
struct UiEntry {std::string id,label;bool enabled=true;};
struct UiMetadata {
    UiKind kind=UiKind::Canvas;
    std::string id,title,path;
    std::vector<UiEntry> entries;
    std::size_t selected=0;
};
// Shared application canvas in logical layout cells, without escape sequences,
// platform handles, renderer objects or references to mutable menu state.
struct UiCell {
    std::string glyph=" ";
    std::array<int,3> color{{0,0,0}},background{{12,12,12}};
    bool continuation=false;
    bool operator==(const UiCell& other) const {
        return glyph==other.glyph && continuation==other.continuation && background==other.background &&
            (glyph==" " || glyph.empty() || color==other.color);
    }
};
struct UiFrame {
    using Rows=std::vector<std::vector<UiCell>>;
    Rows rows;
    std::optional<std::pair<int,int>> caret;
    bool obscured=false,overlay=false;
    std::pair<int,int> extent{128,38};
    UiMetadata metadata;
};
using UiSize=std::pair<int,int>;
using UiSink=std::function<void(UiFrame)>;
class UiSurfaceScope {
public:
    UiSurfaceScope(UiSink sink,UiSize size={128,38});
    ~UiSurfaceScope();
    UiSurfaceScope(const UiSurfaceScope&)=delete;
    UiSurfaceScope& operator=(const UiSurfaceScope&)=delete;
private:
    UiSink previous_;
    std::optional<UiSize> previousSize_;
};
bool ui_surface_active() noexcept;
std::optional<UiSize> ui_surface_size() noexcept;
void present_ui(UiFrame frame);
void clear_ui();
}
#endif
