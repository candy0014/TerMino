#ifndef TETR_FRONTEND_UI_SURFACE_H
#define TETR_FRONTEND_UI_SURFACE_H
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace Frontend {
enum class UiKind { Canvas, Start, Menu, Choices, Text, KeyCapture, MapEditor, Statistics, Lobby, Notice };
enum class UiControl { Action, Group, Value, Choice, Toggle, Binding, Separator };
struct UiEntry {
    std::string id,label;
    bool enabled=true;
    std::string value,description;
    UiControl control=UiControl::Action;
    std::optional<bool> checked;
    bool effective=true;
    bool filePath=false;
};
struct UiNavigationGroup {
    std::string title;
    std::vector<UiEntry> entries;
    std::size_t selected=0;
    std::string id;
};
struct UiGridPreview {
    int width=0,height=0,squareStyle=0;
    bool obscured=false;
    std::vector<int> colors; // -1 is empty; remaining values are piece colors.
};
struct UiPiecePreview {
    std::string name,system;
    int color=0;
    std::vector<std::array<int,2>> cells;
};
inline constexpr std::size_t kMapSequenceColumns=2,kMapSequenceRows=12;
struct UiMapEditor {
    UiGridPreview field;
    // Logical navigation columns and page rows match the shared keyboard
    // controller. They are item counts, not terminal or pixel coordinates.
    int row=0,column=0,brush=-1,candidateColumns=1,candidateRows=1;
    bool sequenceFocus=false,generated=false,dirty=false;
    std::size_t caret=0,candidate=0,sequenceFirst=0,sequenceSize=0;
    std::size_t candidateFirst=0;
    int fieldFirstRow=0,fieldFirstColumn=0,fieldRows=0,fieldColumns=0;
    std::string name,goal,system,query;
    int floor=0;
    std::vector<UiPiecePreview> sequence,candidates;
    std::optional<UiPiecePreview> detail;
};
enum class UiStatisticsPage { Summary, Clears, Challenges };
struct UiResultPlayer {
    std::string name,pps,apm,received,sent;
};
struct UiStatistics {
    using Counts=std::array<std::uint64_t,3>; // Normal, full spin, mini spin.
    UiStatisticsPage page=UiStatisticsPage::Summary;
    std::vector<UiEntry> metrics,challenges;
    std::vector<UiResultPlayer> players;
    std::vector<std::string> pieces;
    std::vector<int> pieceColors,lines;
    std::vector<std::vector<Counts>> cells;
    std::vector<Counts> rowTotals,columnTotals;
    Counts total{};
    int player=0,selectedMetric=0,selectedRow=0,selectedColumn=0,selectedChallenge=0;
    bool room=false,obscured=false;
    std::string challengeNotice;
};
struct UiRoomMember {
    std::string name,role;
    bool host=false,ready=false;
};
struct UiLobby {
    std::vector<UiRoomMember> members;
    std::size_t offset=0,pageSize=1;
    int ready=0,battles=0,players=0,capacity=0;
    bool canStart=false,startEnabled=false;
};
// Additional keyboard commands accepted by the owning page controller.
// Directly selectable content (rows, tabs and players) keeps its own hit targets.
enum class UiKeyAction { Retry, Root, Exit, SwitchTarget, SavePreset, Undefined, SaveMap };
struct UiMetadata {
    UiKind kind=UiKind::Canvas;
    std::string id,title,path;
    std::vector<UiEntry> entries;
    std::size_t selected=0;
    // Semantic content, independent of the terminal cell layout. All strings
    // are already localized by the application; IDs remain stable.
    std::string hint,notice,context,input,currentValue;
    std::vector<std::string> shortcuts;
    std::vector<UiNavigationGroup> ancestors;
    std::vector<UiEntry> preview;
    std::size_t inputCaret=0;
    bool chinese=false;
    std::optional<UiGridPreview> gridPreview;
    // Keep the owning menu in place while a value/choice/key is edited.
    std::optional<UiNavigationGroup> parent;
    std::optional<UiMapEditor> editor;
    std::optional<UiStatistics> statistics;
    std::optional<UiLobby> lobby;
    std::vector<UiKeyAction> keyActions;
    // Changes only when the semantic page changes, not when focus moves.
    std::uint64_t page=0;
};
enum class UiCommandKind { Activate, Back, BrowseFile, MapBrush, MapPaint,
    MapEndStroke, MapTab, MapCaret, MapCandidate, MapScroll,
    StatisticsPage, StatisticsPlayer, StatisticsMetric, StatisticsCell, StatisticsChallenge };
struct UiCommand {
    UiCommandKind kind=UiCommandKind::Activate;
    std::size_t index=0;
    // An ancestor click selects a row in that menu without activating it.
    std::optional<std::size_t> ancestor;
    std::uint64_t page=0;
    int row=0,column=0,delta=0;
    bool alternate=false;
};
using UiCommandSource=std::function<std::optional<UiCommand>()>;
bool same_ui_page(const UiMetadata& first,const UiMetadata& second);
bool valid_ui_command(const UiMetadata& metadata,const UiCommand& command);
// The terminal canvas remains available to the terminal adapter. OpenGL
// consumes the semantic metadata above, never these glyph cells.
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
    UiSurfaceScope(UiSink sink,UiSize size={128,38},UiCommandSource commands={});
    ~UiSurfaceScope();
    UiSurfaceScope(const UiSurfaceScope&)=delete;
    UiSurfaceScope& operator=(const UiSurfaceScope&)=delete;
private:
    UiSink previous_;
    std::optional<UiSize> previousSize_;
    UiCommandSource previousCommands_;
    std::optional<UiMetadata> previousMetadata_;
    std::optional<UiCommand> previousPending_;
};
bool ui_surface_active() noexcept;
std::optional<UiSize> ui_surface_size() noexcept;
void present_ui(UiFrame frame);
void clear_ui();
// A controller can leave commands for a nested controller to consume.
std::optional<UiCommand> poll_ui_command(const std::function<bool(const UiCommand&)>& accept={});
}
#endif
