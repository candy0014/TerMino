#ifndef TETR_APP_SETTINGS_VIEW_H
#define TETR_APP_SETTINGS_VIEW_H
#include "tetr/app/settings_model.h"
#include "terminal_canvas.h"
#include "tetr/render/terminal_renderer.h"
#include <array>
#include <cstddef>
#include <memory>
#include <functional>
#include <string>
#include <utility>

namespace Runtime { class GameContext; }
namespace App::Settings {
inline constexpr std::size_t kVisibleSettingsRows = 15;
inline constexpr std::size_t kVisibleSettingsColumns = 3;
inline constexpr std::size_t first_visible_row(std::size_t selected) {
    return selected / kVisibleSettingsRows * kVisibleSettingsRows;
}
using TerminalSettingsSurface=App::TerminalCanvas;
std::shared_ptr<TerminalSettingsSurface> make_settings_surface();
class TerminalSettingsView {
public:
    using TerminalSizeQuery = std::function<std::optional<Render::TerminalSize>()>;
    TerminalSettingsView(const Runtime::GameContext& context, bool localVersus, int player, bool fromStart = false,
                        std::shared_ptr<TerminalSettingsSurface> surface = {}, TerminalSizeQuery terminalSizeQuery = {},bool mapEditor = false);
    bool terminal_size_changed() const;
    std::size_t page_rows() const { return visibleRows_; }
    void invalidate() const;
    void set_read_only(bool enabled) {readOnly_=enabled;}
    void prepare_layout(std::size_t entries, const std::string& breadcrumb) const;
    void set_menu_path(const Model::SettingNode& root, const std::vector<std::string>& path, std::size_t selected);
    void set_submenu_path(const TerminalSettingsView& parent, const Model::SettingNode& root,
                          const std::vector<std::string>& path, std::size_t selected);
    void set_text_color() const;
    void set_selection_color(bool active = true) const;
    void print_menu(const Model::SettingNode& menu, std::size_t selected, const std::string& breadcrumb,bool render = true) const;
    void print_selection(const Model::SettingNode& menu, std::size_t previous,
                         std::size_t selected, const std::string& breadcrumb) const;
    void print_edit(const Model::SettingItem& item, const std::string& breadcrumb = {},
                    const std::string& value = {}, std::size_t caret = 0) const;
    void print_scalar_input(const Model::SettingItem& item, const std::string& breadcrumb,
                             const std::string& value, std::size_t caret) const;
    void print_choices(const Model::SettingItem& item, std::size_t selected,
                       const std::string& breadcrumb) const;
    void print_reset_confirmation(bool pending) const;
    void print_message(const std::string& message) const;
private:
    struct ColumnBox { int column; int width; };
    void line(int row, const std::string& text, int atColumn = 4, int atWidth = 52) const;
    void present(std::optional<std::pair<int,int>> cursor = {}) const;
    void set_heading_color() const;
    void set_hint_color(bool highlight = false) const;
    void print_row(const Model::SettingNode& node, std::size_t index, bool selected, bool active = true) const;
    void print_hint_text(const std::string& text) const;
    void print_hint(const Model::SettingNode& node) const;
    void print_input(int row, const std::string& prompt, const std::string& value, std::size_t caret) const;
    void print_shortcuts(bool editing = false) const;
    void print_frame(const std::string& title, const std::string& breadcrumb, bool editing) const;
    void print_column(const Model::SettingNode& menu, std::size_t selected, std::size_t columnIndex) const;
    void print_column(const Model::SettingNode& menu, std::size_t selected, const ColumnBox& box, bool active) const;
    void configure_layout(const Model::SettingNode* menu, const Model::SettingItem* item, bool editing,int reserve = 0) const;
    void print_box(std::size_t index, const std::string& title) const;
    void print_box(const ColumnBox& box, const std::string& title) const;
    void print_map_preview(const Model::SettingNode& node) const;
    std::string row_text(const Model::SettingNode& node, bool selected, int availableWidth = 0,bool showSummary=true) const;
    int menu_width(const Model::SettingNode& menu) const;
    std::string value_text(const Model::SettingItem& item) const;
    const Runtime::GameContext& context_;
    int player_;
    bool localVersus_;
    bool mapEditor_;
    bool readOnly_=false;
    std::shared_ptr<TerminalSettingsSurface> surface_;
    TerminalSizeQuery terminalSizeQuery_;
    mutable std::optional<Render::TerminalSize> terminalSize_;
    mutable std::array<int,3> color_{{255,255,255}};
    mutable std::array<int,3> background_{{12,12,12}};
    struct MenuColumn { const Model::SettingNode* menu; std::size_t selected; };
    std::vector<MenuColumn> ancestors_;
    const Model::SettingNode* parent_ = nullptr;
    std::size_t parentSelected_ = 0;
    mutable std::vector<ColumnBox> boxes_;
    mutable std::optional<ColumnBox> clippedBox_;
    mutable std::size_t firstVisibleColumn_ = 0;
    mutable int firstRow_ = 5;
    mutable std::size_t visibleRows_ = kVisibleSettingsRows;
    mutable int hintRow_ = 21;
    mutable int hintLines_ = 4;
    mutable int entryColumn_ = 4;
    mutable int entryWidth_ = 52;
    mutable int frameWidth_ = 52;
};
} // namespace App::Settings
#endif
