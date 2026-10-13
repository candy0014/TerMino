#ifndef TETR_APP_MAP_EDITOR_H
#define TETR_APP_MAP_EDITOR_H
#include "tetr/runtime/map_definition.h"
#include "terminal_canvas.h"
#include <cstddef>
#include <optional>
#include <vector>
namespace Runtime { class GameContext; }
namespace App {
struct MapEditorState {
    Runtime::MapDefinition map;
    int row=19,column=0;
    std::size_t caret=0;
    char brush='G';
    bool sequenceFocus=false;
    bool optionsOpen=false;
    bool exitPrompt=false;
    int exitChoice=0;
    int option=0;
    std::size_t candidate=0;
    std::optional<Piece::Type> detailPiece;
    std::string query;
    std::vector<Runtime::MapDefinition> undoHistory,redoHistory;
    explicit MapEditorState(Runtime::MapDefinition definition={});
    void resize(int width,int height);
    void paint(char symbol);
    void move(int rowDelta,int columnDelta,std::optional<char> heldColor={});
    void insert(Piece::Type piece);
    void erase(bool backwards);
    void change_generator();
    void change_rotation(Runtime::RotationSystem system);
    void change_floor(int floor);
    void change_goal(Runtime::MapGoal goal);
    bool undo();bool redo();
private:
    void remember();
};
std::vector<Piece::Type> map_browser_pieces(Runtime::RotationSystem system,const std::string& query={});
// Render a bounded frame independently of terminal output.
TerminalCanvas::Frame map_editor_frame(Runtime::GameContext& context,const MapEditorState& state,int columns,int rows);
// Unsaved changes require choosing save, discard, or keep editing before exit.
bool run_map_editor(Runtime::GameContext& context,Runtime::MapDefinition map={});
}
#endif
