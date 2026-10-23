#ifndef TETR_OPENGL_SESSION_SCENE_H
#define TETR_OPENGL_SESSION_SCENE_H
#include "tetr/frontend/graphical/session.h"
namespace Runtime {class GameContext;struct PlayerSessionState;}
namespace Graphical {
struct BoardGeometry;
struct RoomBoards {
    Presentation::BoardView ownBoard;
    std::vector<SceneBoard> peers;
    bool ownBoardVisible=true;
};
Presentation::BoardView capture_graphical_board(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player,const BoardGeometry& geometry);
// Capture the roster even before the first sampled network board arrives.
RoomBoards capture_room_boards(Runtime::GameContext& context);
}
#endif
