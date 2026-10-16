#ifndef TETR_APP_POST_GAME_STATISTICS_H
#define TETR_APP_POST_GAME_STATISTICS_H

#include "tetr/runtime/player_state.h"
#include "tetr/challenges/challenges.h"
#include "terminal_canvas.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Runtime { class GameContext; struct PlayerSessionState; }
namespace App::Settings { class NavigationInput; }
namespace App {

struct PostGameStatistics {
    std::vector<std::pair<std::string,std::string>> metrics;
    std::vector<std::string> pieces;
    std::vector<int> pieceColors;
    std::vector<int> lines;
    std::vector<std::vector<Runtime::ClearCounts>> cells;
    std::vector<Runtime::ClearCounts> rowTotals,columnTotals;
    Runtime::ClearCounts total{};
};
PostGameStatistics make_post_game_statistics(const Runtime::GameContext& context,
    const Runtime::PlayerSessionState& player);

enum class PostGamePage { Board, Summary, Clears, Challenges };
class PostGameStatisticsView {
public:
    explicit PostGameStatisticsView(PostGameStatistics first,
        std::optional<PostGameStatistics> second=std::nullopt);
    PostGameStatisticsView(std::vector<PostGameStatistics> players,std::vector<std::string> names);
    PostGamePage page() const { return page_; }
    void next_page();
    void set_challenges(std::vector<Challenges::Outcome> outcomes,const std::string& notice={});
    void close();
    void select_player(int delta);
    void move(int rows,int columns);
    bool poll(Settings::NavigationInput& first,Settings::NavigationInput* second=nullptr);
    bool reserves(int key) const;
    void invalidate() { dirty_=true; canvas_.initialized=false; canvas_.shown.clear(); }
    void render(Runtime::GameContext& context,const std::string& footer,
        Render::TerminalSize size);
    void show_replay_save_prompt(const std::string& defaultName,const std::string& value,std::size_t caret);
    void finish_replay_save_prompt(const std::string& message);
    int selected_row() const { return selectedRow_; }
    int selected_column() const { return selectedColumn_; }
    int player_index() const { return player_; }
    int selected_metric() const { return summaryRow_; }

private:
    std::vector<PostGameStatistics> players_;
    std::vector<std::string> playerNames_;
    bool roomResults_=false;
    PostGamePage page_=PostGamePage::Board;
    int player_=0,selectedRow_=0,selectedColumn_=0,rowStart_=0,columnStart_=0,summaryRow_=0,summaryScroll_=0;
    bool dirty_=true;
    TerminalCanvas canvas_;
    std::string shownFooter_;
    struct SavePrompt {std::string defaultName,value;std::size_t caret;};
    std::optional<SavePrompt> savePrompt_;
    std::string notice_;
    std::vector<Challenges::Outcome> challenges_;
    std::string challengeNotice_;
    int challengeRow_=0,challengeScroll_=0;
};
} // namespace App
#endif
