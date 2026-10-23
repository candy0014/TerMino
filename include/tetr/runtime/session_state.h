#ifndef TETR_RUNTIME_SESSION_STATE_H
#define TETR_RUNTIME_SESSION_STATE_H

#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/quick_play_progress.h"
#include "tetr/runtime/gravity.h"
#include "tetr/runtime/time.h"

#include <optional>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Runtime {

struct QuickPlaySessionState {
    double speed = 1.0; // Booster level; retained name at the replay boundary.
    double altitude = 0.0;
    int floor = 1;
    double garbageMultiple = 1.0;
    Duration garbageInTime = Duration::from_seconds(1.0);
    QuickPlayProgress progress;
};

struct DynamicRuleState {
    std::optional<GravityInterval> gravityInterval;
    std::optional<Duration> epld;
    std::optional<double> survivalApm;
    std::optional<Duration> incomingGarbageDelay;

    void clear() {
        gravityInterval.reset();
        epld.reset();
        survivalApm.reset();
        incomingGarbageDelay.reset();
    }
};

// Mutable state belonging to one active game/session. PlayerSessionState is
// the actual per-player runtime model. The historical compatibility facade no longer
// participates in normal session ownership and is isolated to the BOT bridge.
struct SessionState {
    PlayerSessionState player;
    PlayerSessionState opponent;
    struct RoomScene {
        bool enabled=false, spectator=false, selfAlive=true, finished=false, localForfeit=false;
        bool aborted=false;
        bool started=false;
        int winnerSlot=-1;
        std::uint64_t roomInstance=0;
        std::vector<int> participants;
        std::map<int,bool> alive;
        int ownSlot=-1, focusSlot=-1;
        std::uint64_t epoch=0, revision=0;
        std::map<int,PlayerSessionState> players;
        // Screen state is separate from the received model: checkpoints and
        // motion can arrive several times before the next terminal update.
        std::map<int,PlayerSessionState> paintedPlayers;
        std::map<int,std::string> usernames;
        std::map<int,std::string> paintedNames;
        std::map<int,std::string> paintedResults;
        std::map<int,int> killers;
        std::vector<int> survivors;
        std::vector<int> finalists;
        std::vector<int> finalistLayout;
        std::vector<int> miniSlots;
        std::vector<int> paintedFullSlots;
        std::map<int,std::uint64_t> paintedMiniSequences;
        std::map<int,std::vector<BoardCell>> paintedMiniCells;
        std::map<int,std::uint64_t> appliedMiniClocks;
        std::size_t miniPaintCursor=0;
        struct BattleAnchor {int column,row,columns,rows,width,height;};
        std::optional<BattleAnchor> finalBattleAnchor;
        struct MiniLayout {int left=0,top=0,width=0,height=0,ceiling=0,logicalWidth=0,logicalHeight=0;};
        std::map<int,MiniLayout> miniLayouts;
        TimePoint lastPaint{};
        TimePoint lastMiniPaint{};
        int galleryColumn=0;
        int galleryTop=0,miniColumns=0,miniCellWidth=0,miniCellHeight=0;
        int paintedColumns=0,paintedRows=0;
    } roomScene;
    std::optional<TimePoint> gameStartTime;
    // Presentation of the shared start sequence; zero outside its 3/2/1 phases.
    // This is not a second timer and is not part of recording/network formats.
    int countdownDigit = 0;
    QuickPlaySessionState quickPlay;
    DynamicRuleState dynamicRules;
    TimePoint simulationTime{};
    std::uint64_t completedSimulationPieces = 0;
    Duration turnWaitDuration{};
    std::optional<TimePoint> turnWaitStarted;
    // Real time removed by undo; game timers remain on their saved timeline.
    Duration clockOffset{};
};

inline Duration elapsed_game_time(const SessionState& session, TimePoint now) noexcept {
    return session.gameStartTime ? now - *session.gameStartTime : Duration{};
}

} // namespace Runtime

#endif
