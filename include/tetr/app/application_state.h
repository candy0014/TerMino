#ifndef TETR_APP_APPLICATION_STATE_H
#define TETR_APP_APPLICATION_STATE_H

#include <array>
#include <cstdint>

#include <cstddef>
#include <atomic>
#include <mutex>
#include <vector>
#include <optional>
#include <map>
#include "tetr/config/user_config.h"
#include "tetr/challenges/challenges.h"

namespace Frontend { class TextInputSession; }
namespace Runtime { class GameContext; struct PlayerSessionState; }

namespace App {

struct StartScreenRequested {};
// Return to the remembered settings path after the host cancels a round.
struct RoomSettingsRequested {};

// Process-lifetime UI state. This is intentionally not serialized into the
// user's configuration: reopening a menu during the same program run should
// resume where the user left it, while a fresh launch starts at Player.
struct SettingsMenuState {
    // Stable node keys keep selection valid when conditional children change.
    std::map<std::string, std::string> selectedEntries;
    std::vector<std::string> path;
};

// Seeking a replay rebuilds the game from its initial state and then consumes
// recorded operations up to the requested timestamp.  This request survives
// the return from App::run() to the next initialize_game() call.
struct ReplayResumeRequest {
    bool pending = false;
    double targetSeconds = 0.0;
    double speed = 1.0;
    bool paused = false;
};
struct ReplayPreviewRequest {std::string path;bool pending=true;};

struct ApplicationState {
    // Startup option; applies to live Quick Play games throughout this run.
    bool qpContinuousCancellation = false;
    // Standalone tournament owns round progression; never persisted to settings.
    bool automaticLocalMatch = false;
    bool headlessLocalMatch = false;
    std::atomic_bool startScreenRequested{false};
    bool localVersusSettingsAvailable = false;
    int singlePlayerOwner = 0;
    std::optional<UserConfig::Settings> savedPlayerOneSettings;
    // Index in the category list, including the selected player in Local Versus.
    std::size_t settingsCategoryIndex = 0;
    SettingsMenuState settingsMenu;
    SettingsMenuState playerTwoSettingsMenu;
    ReplayResumeRequest replayResume;
    std::optional<ReplayPreviewRequest> replayPreview;
    std::optional<Challenges::Round> challengeRound;
    std::function<bool()> challengeFrame;
    std::function<void(int)> challengeSendAttack;
    std::function<bool(const Challenges::Placement&,Runtime::PlayerSessionState&)> challengePlacementObserver;
    std::optional<Challenges::Round> playerTwoChallengeRound;
    std::function<bool(const Challenges::Placement&,Runtime::PlayerSessionState&)> playerTwoChallengePlacementObserver;
    auto& challenge_round(const Runtime::GameContext* context) {return context==localPlayerTwoContext?playerTwoChallengeRound:challengeRound;}
    const auto& challenge_round(const Runtime::GameContext* context) const {return context==localPlayerTwoContext?playerTwoChallengeRound:challengeRound;}
    auto& challenge_observer(const Runtime::GameContext* context) {return context==localPlayerTwoContext?playerTwoChallengePlacementObserver:challengePlacementObserver;}
    Frontend::TextInputSession* textInputSession = nullptr;
    // Optional UI endpoints. The session owner invokes these on its own thread;
    // frontends may copy a view or unwind to their menu, never share mutable state.
    std::function<void()> openSettings;
    std::function<void()> roundFinished;
    Runtime::GameContext* primaryContext = nullptr;
    Runtime::GameContext* localPlayerTwoContext = nullptr;
    std::recursive_mutex terminalOutputMutex;
    std::optional<std::array<int,10>> gameTerminalLayout;
    std::uint64_t gameTerminalLayoutRevision = 0;
    // Observed resizes invalidate confirmation, including resizing back.
    // Restarts reuse it only while neither full-layout dimension grows.
    std::optional<std::array<int,2>> acceptedGameTerminalSize;
    std::array<int,2> acceptedGameTerminalRequirements{};
    void observe_terminal_size(int columns, int rows) {
        if (acceptedGameTerminalSize && *acceptedGameTerminalSize!=std::array<int,2>{{columns,rows}})
            acceptedGameTerminalSize.reset();
    }
};

} // namespace App

#endif
