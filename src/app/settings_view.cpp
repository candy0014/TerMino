#include "settings_view.h"
#include "settings_navigation.h"
#include "tetr/config/presets.h"
#include "ui_text.h"
#include "tetr/autoplay/availability.h"
#include "tetr/config/normalization.h"
#include "tetr/config/map_store.h"
#include "tetr/util/terminal_text.h"
#include "tetr/render/terminal_renderer.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/rotation_systems.h"
#include "tetr/app/application_state.h"
#include "tetr/network/network_services.h"

#include <algorithm>
#include <iostream>

namespace App::Settings {

std::shared_ptr<TerminalSettingsSurface> make_settings_surface() {
    return std::make_shared<TerminalSettingsSurface>();
}

namespace {
constexpr int column = 4;
constexpr int width = 52;
constexpr int columnGap = 2;
constexpr int previousColumnSlice = 6;
constexpr int rightMargin = 8;
constexpr int leftMargin = 2;
constexpr int hintRows = 4;
using UiText::tr;
using UiText::key_label;

bool english_mod_name(const Model::SettingItem& item) {
    return UserConfig::setting_descriptor(item.id).section == UserConfig::Section::QuickPlay &&
        item.id != Model::SettingId::QpDefaultFloor;
}
std::string preset_hint(const Model::SettingItem& item,const std::string& hint) {
    const auto label=[&](const UserConfig::Settings& user) {
        const auto* layer=UserConfig::Presets::source(user,item.id);
        return layer ? tr(UserConfig::Presets::name(*layer))+
            (UserConfig::Presets::modified(*layer) ? tr(" (adjusted)") : "") : tr("Personal settings");
    };
    if (!UserConfig::Presets::source(*item.owner,item.id) &&
        (!item.secondaryOwner || !UserConfig::Presets::source(*item.secondaryOwner,item.id))) return hint;
    auto text=label(*item.owner);
    if(item.secondaryOwner) {
        const auto other=label(*item.secondaryOwner);
        if(text!=other) text+=" | "+other;
    }
    return tr("Source: ")+text+". "+hint;
}
std::string item_label(const Model::SettingItem& item) {
    return english_mod_name(item) ? item.label : tr(item.label);
}
std::string node_title(const Model::SettingNode& node) {
    return node.literalTitle || (node.item && english_mod_name(*node.item)) ? node.title : tr(node.title);
}
Autoplay::Availability bot_availability(const Model::SettingItem& item, int value) {
    if (item.id != Model::SettingId::AutoplayMode) return {};
    auto config = Runtime::normalize_user_config(*item.owner, Runtime::selected_game_mode(*item.owner));
    if(const auto map=UserConfig::Maps::find(item.owner->mapId)) Runtime::apply_map_config(config,*map);
    auto available=Autoplay::availability(config, Runtime::autoplay_mode_from_index(value));
    if (item.secondaryOwner) {
        if (!available) available.reason=tr("Player 1")+": "+tr(available.reason);
        else {
            auto other=Runtime::normalize_user_config(*item.secondaryOwner,Runtime::selected_game_mode(*item.secondaryOwner));
            if(const auto map=UserConfig::Maps::find(item.secondaryOwner->mapId)) Runtime::apply_map_config(other,*map);
            available=Autoplay::availability(other,Runtime::autoplay_mode_from_index(value));
            if (!available) available.reason=tr("Player 2")+": "+tr(available.reason);
        }
    }
    return available;
}

std::string item_hint(const Model::SettingItem& item) {
    if (item.bothReadOnly) return tr("Choose Player 1 or Player 2 to edit this item.");
    if (item.secondaryOwner) {
        auto first=Model::rebind_item(item,*item.owner);
        first.effective=true;
        if (Model::mixed_value(item)) first.choice.cycleImmediately=false;
        auto hint=item_hint(first);
        if (!item.effective || !item.secondaryEffective) {
            hint=tr("Currently active: ")+
                (item.effective ? tr("Player 1") : item.secondaryEffective ? tr("Player 2") : tr("Neither player"))+". "+hint;
        }
        if (Model::mixed_value(item) && !item.choice.labels.empty())
            hint=tr("Different values: choose one value for both players.")+" "+hint;
        return hint;
    }
    if (item.id == Model::SettingId::RandomPieces) {
        const auto hint = tr(Runtime::bag_randomizer_info(
            Runtime::bag_randomizer_from_index(Model::choice_value(item))).hint);
        return item.effective ? hint :
            tr("Inactive with the current rules; edits are saved for other configurations.") + " " + hint;
    }
    if (item.id == Model::SettingId::RotationSystem) {
        if(Model::choice_value(item)==-1) return tr("Use each map piece's original rotation system. Other systems replace matching pieces; unmatched special pieces keep their original rotation.");
        if (const auto* info = Runtime::rotation_system_info(
            Runtime::rotation_system_from_index(Model::choice_value(item)))) {
            const auto hint = tr(info->hint);
            return item.effective ? hint :
                tr("Inactive with the current rules; edits are saved for other configurations.") + " " + hint;
        }
    }
    if (item.id == Model::SettingId::SpinBonuses) {
        const auto hint = tr(Runtime::spin_bonuses_info(
            Runtime::spin_bonuses_from_index(Model::choice_value(item))).hint);
        return item.effective ? hint :
            tr("Inactive with the current rules; edits are saved for other configurations.") + " " + hint;
    }
    if(item.owner->presetDefined) return tr("U restores this parameter to undefined.")+" "+(item.choice.labels.empty() ? tr("Right / Enter edits; blank input keeps the value.") : tr("Right / Enter opens the available values."));
    if (!item.effective) return tr("Inactive with the current rules; edits are saved for other configurations.") +
        (item.hint.empty() ? "" : " " + tr(item.hint));
    if (!item.hint.empty() && !item.choice.cycleImmediately) return tr(item.hint);
    if (item.keyBinding) return tr("Press a key to add, transfer, or remove its binding.")+" "+
        tr(item.id==Model::SettingId::ControlRetry || item.id==Model::SettingId::ControlOpenSettings
            ? "Restart / Settings cannot use Enter, Esc, arrows or Tab."
            : "Enter, Esc, arrows and Tab can be bound to this action.");
    if (item.choice.cycleImmediately) return tr("Right / Enter cycles the value.") +
        (item.hint.empty() ? "" : " " + tr(item.hint));
    if (!item.choice.labels.empty()) return tr("Right / Enter opens the available values.");
    if (item.id == Model::SettingId::ReplayPath) return tr("Replay filename or path inside replay/.");
    if (std::holds_alternative<std::reference_wrapper<std::string>>(item.value))
        return tr("Text input supports Chinese; Enter saves.");
    return tr("Right / Enter edits; blank input keeps the value.");
}
} // namespace

TerminalSettingsView::TerminalSettingsView(const Runtime::GameContext& context, bool localVersus,
    int player, bool fromStart, std::shared_ptr<TerminalSettingsSurface> surface, TerminalSizeQuery terminalSizeQuery,bool mapEditor)
    : context_(context), player_(player), localVersus_(localVersus), mapEditor_(mapEditor),surface_(surface ? std::move(surface) : make_settings_surface()),
      terminalSizeQuery_(terminalSizeQuery ? std::move(terminalSizeQuery) : TerminalSizeQuery{Render::terminal_size}) {
    (void)fromStart;
}
bool TerminalSettingsView::terminal_size_changed() const {
    const auto size=terminalSizeQuery_();
    if (auto* app=context_.app_component(); app && size) app->observe_terminal_size(size->columns,size->rows);
    return size!=surface_->terminalSize;
}
void TerminalSettingsView::line(int row, const std::string& text, int atColumn, int atWidth) const {
    surface_->write(row,atColumn,text,atWidth,color_,background_);
}
void TerminalSettingsView::present(std::optional<std::pair<int,int>> cursor) const {
    surface_->present(cursor,context_.config.display.obscuredColors);
    surface_->terminalSize=terminalSize_;
}
void TerminalSettingsView::invalidate() const {
    surface_->shown.clear();
    surface_->pending.clear();
    surface_->initialized = false;
}
void TerminalSettingsView::set_menu_path(const Model::SettingNode& root,
    const std::vector<std::string>& path, std::size_t selected) {
    ancestors_.clear();
    const auto* menu = &root;
    for (const auto& key : path) {
        const auto found = std::find_if(menu->children.begin(), menu->children.end(),
            [&](const auto& child) { return child.key == key; });
        if (found == menu->children.end()) break;
        ancestors_.push_back({menu, static_cast<std::size_t>(found - menu->children.begin())});
        menu = &*found;
    }
    parent_ = menu;
    parentSelected_ = selected;
    configure_layout(parent_, nullptr, false);
}
void TerminalSettingsView::set_text_color() const {
    background_ = {{12,12,12}};
    color_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{0,0,0}} :
        TerminalSettingsSurface::Color{{255,255,255}};
}
void TerminalSettingsView::set_selection_color(bool active) const {
    if (active) {
        color_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{54,57,62}} :
            TerminalSettingsSurface::Color{{255,188,92}};
        background_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{0,0,0}} :
            TerminalSettingsSurface::Color{{56,34,16}};
    } else {
        background_ = {{12,12,12}};
        color_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{54,57,62}} :
            TerminalSettingsSurface::Color{{193,168,65}};
    }
}
void TerminalSettingsView::set_heading_color() const {
    background_ = {{12,12,12}};
    color_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{54,57,62}} :
        TerminalSettingsSurface::Color{{237,109,0}};
}
void TerminalSettingsView::set_hint_color(bool highlight) const {
    background_ = {{12,12,12}};
    color_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{54,57,62}} :
        highlight ? TerminalSettingsSurface::Color{{193,168,65}} : TerminalSettingsSurface::Color{{255,255,255}};
}
std::string TerminalSettingsView::value_text(const Model::SettingItem& item) const {
    if (item.secondaryOwner) {
        const auto first=Model::rebind_item(item,*item.owner),second=Model::rebind_item(item,*item.secondaryOwner);
        const auto value=value_text(first);
        return Model::mixed_value(item) ? value+" | "+value_text(second) : value;
    }
    if(item.owner && !UserConfig::Presets::defined(*item.owner,Model::item_fields(item).front())) return tr("Undefined");
    if (item.keyBinding) {
        std::string result;
        for (const int key : UserConfig::control_keys(*item.owner, UserConfig::setting_descriptor(item.id).controlIndex)) {
            if (!result.empty()) result += " / ";
            result += key_label(key);
        }
        return result.empty() ? tr("Unbound") : result;
    }
    if (!item.choice.labels.empty()) {
        const auto selected = Model::selected_choice(item);
        if (selected >= item.choice.labels.size()) return tr("Custom");
        if (item.squarePreview) return tr(selected == 0 ? "Filled" : "Spaced");
        return tr(item.choice.labels[selected]);
    }
    return Model::scalar_value(item);
}
std::string TerminalSettingsView::row_text(const Model::SettingNode& node, bool selected) const {
    std::string text = (selected ? "> " : "  ") + node_title(node);
    if (node.item) text += " = " + value_text(*node.item);
    else if (!node.summary.empty()) text += " = " + node.summary;
    else if (node.key == "player") text += " = " + (player_==2 ? tr("Both players") : tr("Player")+" "+std::to_string(player_+1));
    else if (node.kind == Model::NodeKind::Player && node.player == player_) text += tr(" [current]");
    if (node.kind == Model::NodeKind::Group) text += "  >";
    return text;
}
int TerminalSettingsView::menu_width(const Model::SettingNode& menu) const {
    std::size_t columns = Util::TerminalText::width(tr(menu.title));
    for (const auto& node : menu.children) columns = std::max(columns, Util::TerminalText::width(row_text(node, true)));
    return static_cast<int>(std::clamp(columns, std::size_t{18}, std::size_t{width}));
}
void TerminalSettingsView::configure_layout(const Model::SettingNode* menu,
    const Model::SettingItem* item, bool editing,int reserve) const {
    boxes_.clear();
    clippedBox_.reset();
    terminalSize_=terminalSizeQuery_();
    const std::size_t totalColumns = ancestors_.size() + (menu ? 1 : 0) + (editing ? 1 : 0);
    firstVisibleColumn_ = !terminalSize_ && totalColumns > kVisibleSettingsColumns
        ? totalColumns - kVisibleSettingsColumns : 0;
    int next = terminalSize_ ? leftMargin : firstVisibleColumn_>0 ? previousColumnSlice + columnGap : column;
    std::size_t logicalColumn = 0;
    auto append = [&](int columns) {
        if (logicalColumn++ < firstVisibleColumn_) return;
        boxes_.push_back({next, columns});
        next += columns + 4 + columnGap; // Border, padding, then two empty columns.
    };
    for (const auto& ancestor : ancestors_) append(menu_width(*ancestor.menu));
    if (menu) append(menu_width(*menu));
    if (editing || boxes_.empty()) {
        int columns = width;
        if (item && item->keyBinding) {
            const auto needed = std::max({Util::TerminalText::width(item_label(*item)) + 2,
                Util::TerminalText::width(tr("Current: ") + value_text(*item)),
                Util::TerminalText::width(tr("Key: "))});
            columns = static_cast<int>(std::clamp(needed, std::size_t{18}, std::size_t{width}));
        } else if (item && !item->choice.labels.empty()) {
            std::size_t needed = Util::TerminalText::width(item_label(*item));
            for (const auto& label : item->choice.labels)
                needed = std::max(needed, Util::TerminalText::width(tr(label)) + 2);
            columns = static_cast<int>(std::clamp(needed, std::size_t{18}, std::size_t{width}));
        }
        append(columns);
    }
    if (terminalSize_) {
        auto& last=boxes_.back();
        last.width=std::min(last.width,std::max(1,terminalSize_->columns-leftMargin-rightMargin-reserve-4));
        const int overflow=std::max(0,last.column+last.width+4-(terminalSize_->columns-rightMargin-reserve));
        for (auto& box:boxes_) box.column-=overflow;
        while (firstVisibleColumn_+1<boxes_.size() && boxes_[firstVisibleColumn_].column<leftMargin)
            ++firstVisibleColumn_;
        if (firstVisibleColumn_>0) clippedBox_=boxes_[firstVisibleColumn_-1];
        boxes_.erase(boxes_.begin(),boxes_.begin()+firstVisibleColumn_);
    } else if (firstVisibleColumn_>0) {
        const int previousWidth=menu_width(*ancestors_.at(firstVisibleColumn_-1).menu);
        clippedBox_=ColumnBox{previousColumnSlice-previousWidth-4,previousWidth};
    }
    const auto& current = boxes_.back();
    entryColumn_ = current.column + 2;
    entryWidth_ = current.width;
    frameWidth_ = terminalSize_ ? std::max(1,terminalSize_->columns-column-rightMargin) :
        std::max(width, current.column + current.width + 4 - column);
}
void TerminalSettingsView::print_box(std::size_t index, const std::string& title) const {
    print_box(boxes_.at(index),title);
}
void TerminalSettingsView::print_box(const ColumnBox& box, const std::string& title) const {
    set_heading_color();
    const auto heading=color_;
    set_text_color();
    surface_->box(firstRow_-1,box.column,box.width,static_cast<int>(visibleRows_),title,heading,color_,background_);
}
void TerminalSettingsView::print_map_preview(const Model::SettingNode& node) const {
    if(!terminalSize_ || !node.mapPreview || boxes_.empty()) return;
    const auto& map=*node.mapPreview;
    const auto& last=boxes_.back();
    const int left=last.column+last.width+4+columnGap;
    const int available=terminalSize_->columns-rightMargin-left;
    const int width=map.width*2;
    if(available<width+4 || map.height>terminalSize_->rows-firstRow_-12) return;
    hintRow_=std::max(hintRow_,firstRow_+map.height+2);
    surface_->hintRow=hintRow_;
    set_heading_color();const auto heading=color_;set_text_color();
    surface_->box(firstRow_-1,left,width,map.height,tr("Map preview"),heading,color_,background_);
    for(int y=0;y<map.height;++y) for(int x=0;x<map.width;++x) {
        const auto symbol=map.rows[y][x];
        const auto colors=Render::board_cell_colors(Runtime::map_cell(symbol),context_.config.display.obscuredColors);
        surface_->write(firstRow_+y,left+2+2*x,symbol==' ' ? " ·" : context_.config.display.squareStyle ? "█ " : "██",2,colors.foreground,colors.background);
    }
}
void TerminalSettingsView::print_row(const Model::SettingNode& node, std::size_t index, bool selected, bool active) const {
    set_text_color();
    if (selected && active) set_selection_color();
    else if (!node.enabled || (node.item && node.item->id == Model::SettingId::AutoplayMode &&
        !bot_availability(*node.item, Model::choice_value(*node.item))))
        color_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{0,0,0}} :
            TerminalSettingsSurface::Color{{128,128,128}};
    else if (selected) set_selection_color(false); else set_text_color();
    line(firstRow_ + static_cast<int>(index % visibleRows_), row_text(node, selected), entryColumn_, entryWidth_);
    set_text_color();
}
void TerminalSettingsView::print_hint_text(const std::string& text) const {
    const auto rows = Util::TerminalText::wrap(text,frameWidth_);
    for (int i = 0; i < hintRows; ++i)
        line(hintRow_ + i, i < static_cast<int>(rows.size()) ? rows[i] : "", column, frameWidth_);
}
void TerminalSettingsView::print_hint(const Model::SettingNode& node) const {
    set_hint_color();
    if (node.item && node.item->id == Model::SettingId::AutoplayMode) {
        const auto available = bot_availability(*node.item, Model::choice_value(*node.item));
        if (!available) {
            print_hint_text(tr("Unavailable: ") + tr(available.reason));
            return;
        }
    }
    if(node.mapPreview) {
        const auto& map=*node.mapPreview;
        const auto goal=map.goal==Runtime::MapGoal::ClearInitial ? tr("Clear map") : map.goal==Runtime::MapGoal::AllClear ? tr("All clear field") : tr("OFF");
        const auto generator=map.generator==Runtime::MapGenerator::CustomSequence ? tr("Custom sequence")+" ("+std::to_string(map.sequence.size())+" "+tr("pieces")+")" :
            tr("Fixed rotation system")+" / "+tr(UserConfig::choice_options(UserConfig::ChoiceKind::RotationSystem).labels.at(static_cast<std::size_t>(map.rotationSystem)));
        const auto rows=Util::TerminalText::wrap_words(tr("Field size")+": "+std::to_string(map.width)+" x "+std::to_string(map.height)+"\n"+
            tr("Generator")+": "+generator+"\n"+tr("Final goal")+": "+goal,frameWidth_);
        for(int i=0;i<hintRows;++i) line(hintRow_+i,i<static_cast<int>(rows.size()) ? rows[i] : "",column,frameWidth_);
    } else print_hint_text(node.item ? preset_hint(*node.item,item_hint(*node.item)) : tr(node.description)+
        (node.descriptionSuffix.empty() ? "" : " "+tr(node.descriptionSuffix)));
}
void TerminalSettingsView::prepare_layout(std::size_t entries, const std::string& breadcrumb) const {
    (void)entries;
    const auto rows = UiText::breadcrumb_rows(breadcrumb, frameWidth_);
    firstRow_ = std::max(5, 4 + static_cast<int>(rows.size()));
    visibleRows_=mapEditor_ && terminalSize_ ? static_cast<std::size_t>(std::clamp(terminalSize_->rows-firstRow_-11,1,static_cast<int>(kVisibleSettingsRows))) : kVisibleSettingsRows;
    // Reserve all fifteen rows on every page, then one blank row.
    hintRow_ = firstRow_ + static_cast<int>(visibleRows_) + 2;
}
void TerminalSettingsView::print_column(const Model::SettingNode& menu, std::size_t selected,
    std::size_t columnIndex) const {
    print_column(menu,selected,boxes_.at(columnIndex),columnIndex == boxes_.size() - 1);
}
void TerminalSettingsView::print_column(const Model::SettingNode& menu, std::size_t selected,
    const ColumnBox& box, bool active) const {
    print_box(box, tr(menu.title));
    entryColumn_ = box.column + 2;
    entryWidth_ = box.width;
    const auto first = selected/visibleRows_*visibleRows_;
    const auto end = std::min(menu.children.size(), first + visibleRows_);
    for (auto i = first; i < end; ++i)
        print_row(menu.children[i], i, i == selected, active);
    if (menu.children.size() > visibleRows_) {
        set_hint_color();
        line(firstRow_ + static_cast<int>(visibleRows_) + 1,
            std::to_string(first + 1) + "-" + std::to_string(end) + " / " + std::to_string(menu.children.size()),
            box.column, box.width + 4);
    }
}
void TerminalSettingsView::print_frame(const std::string& title, const std::string& breadcrumb, bool editing) const {
    if (surface_->initialized && terminalSize_!=surface_->terminalSize) invalidate();
    surface_->leftLimit=terminalSize_ ? std::min(leftMargin,terminalSize_->columns-1) : 0;
    surface_->columnsLimit=terminalSize_ ?
        std::optional<int>{std::max(surface_->leftLimit+1,terminalSize_->columns-rightMargin)} : std::nullopt;
    surface_->rowsLimit=mapEditor_ && terminalSize_ ? std::optional<int>{terminalSize_->rows} : std::nullopt;
    prepare_layout(kVisibleSettingsRows, breadcrumb);
    surface_->hintRow=hintRow_;
    surface_->begin_frame();
    set_heading_color();
    const auto target=player_==2 ? tr("Both players")+" ("+tr("Player 1")+" | "+tr("Player 2")+")" :
        tr("Player")+" "+std::to_string(player_+1);
    line(1, (mapEditor_ ? tr("MAP EDITOR") : context_.user_settings().presetDefined ? tr("PRESET SETTINGS")+(localVersus_ ? " / "+tr("Edit target: ")+target : "") : (localVersus_ ? tr("Edit target: ") : "")+target)+" / "+title, column, frameWidth_);
    set_text_color();
    const auto rows = UiText::breadcrumb_rows(breadcrumb, frameWidth_);
    for (std::size_t i = 0; i < rows.size(); ++i) line(3 + static_cast<int>(i), rows[i], column, frameWidth_);
    if (clippedBox_) {
        if (terminalSize_) {
            if (firstVisibleColumn_<=ancestors_.size()) {
                const auto& previous=ancestors_.at(firstVisibleColumn_-1);
                print_column(*previous.menu,previous.selected,*clippedBox_,false);
            } else if (parent_) print_column(*parent_,parentSelected_,*clippedBox_,false);
        } else print_box(*clippedBox_,"");
    }
    for (std::size_t i = firstVisibleColumn_; i < ancestors_.size(); ++i)
        print_column(*ancestors_[i].menu, ancestors_[i].selected, i - firstVisibleColumn_);
    if (editing && parent_ && firstVisibleColumn_ <= ancestors_.size())
        print_column(*parent_, parentSelected_, ancestors_.size() - firstVisibleColumn_);
    entryColumn_ = boxes_.back().column + 2;
    entryWidth_ = boxes_.back().width;
}
void TerminalSettingsView::print_shortcuts(bool editing) const {
    set_hint_color();
    int row = hintRow_ + hintRows + 2;
    // Wrap long multi-key captions so every configured key remains visible.
    const auto write = [&](const std::string& text) {
        std::string part;
        for (std::size_t i = 0; i < text.size();) {
            const unsigned char first = static_cast<unsigned char>(text[i]);
            const std::size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            const auto glyph = text.substr(i, bytes);
            if (Util::TerminalText::width(part + glyph) > static_cast<std::size_t>(frameWidth_)) {
                line(row++, part, column, frameWidth_);
                part.clear();
            }
            part += glyph;
            i += bytes;
        }
        line(row++, part, column, frameWidth_);
    };
    write(tr(editing ? "Up/Down Select   Right/Enter Apply" : "Up/Down Select   Right/Enter Open"));
    write(tr(mapEditor_ ? "Left/Backspace/Esc Back" : context_.user_settings().presetDefined ? "Left/Backspace Back   Esc Exit" : "Left/Backspace Back   Esc Start screen"));
    if(mapEditor_) {write(tr(editing ? "S Save map" : "S Save map   Space Back   Tab Field / Sequence"));return;}
    const auto binding=[&](std::size_t control) {
        if (!localVersus_) return UiText::binding_label(context_.user_settings(),control);
        std::string text;
        for (const int key:UserConfig::control_keys(context_.user_settings(),control)) {
            if (key==NavigationInput::key_code(NavigationKey::Tab)) continue;
            if (!text.empty()) text+=" / ";
            text+=key_label(key);
        }
        return text.empty() ? std::string{} : "["+text+"]";
    };
    if(context_.user_settings().presetDefined) {
        write(tr("[S] Save preset   [U] Undefined   Esc Exit"));
        return;
    }
    const auto restart=binding(8),root=binding(9);
    std::string actions;
    if (!restart.empty()) actions=restart+" "+tr("Restart");
    if (!root.empty()) actions+=(actions.empty() ? "" : "   ")+root+" "+tr("Root");
    if (!actions.empty()) write(actions);
    if (localVersus_ && !editing) write(tr("[Tab] Player 1 / Player 2 / Both players"));
}
void TerminalSettingsView::print_menu(const Model::SettingNode& menu, std::size_t selected,
                                    const std::string& breadcrumb,bool render) const {
    configure_layout(&menu, nullptr, false);
    if(terminalSize_ && !menu.children.empty() && menu.children[selected].mapPreview) {
        const auto& map=*menu.children[selected].mapPreview;
        prepare_layout(menu.children.size(),breadcrumb);
        const int reserve=map.width*2+4+columnGap;
        if(map.height<=terminalSize_->rows-firstRow_-12 && menu_width(menu)+4+leftMargin+rightMargin+reserve<=terminalSize_->columns)
            configure_layout(&menu,nullptr,false,reserve);
    }
    print_frame(tr(menu.title), breadcrumb, false);
    print_column(menu, selected, boxes_.size() - 1);
    if(!menu.children.empty()) print_map_preview(menu.children[selected]);
    const auto first = selected/visibleRows_*visibleRows_;
    const auto end = std::min(menu.children.size(), first + visibleRows_);
    set_hint_color();
    if (menu.children.size() > visibleRows_)
        line(hintRow_ + hintRows, std::to_string(first + 1) + "-" + std::to_string(end) + " / " +
            std::to_string(menu.children.size()) + tr("   PgUp/PgDn Page"), column, frameWidth_);
    if (menu.section == UserConfig::Section::Network) {
        const auto* app = context_.app_component();
        const auto& primary = app && app->primaryContext ? *app->primaryContext : context_;
        line(hintRow_ + hintRows, UiText::connection_status(primary.network_component() ? primary.network_component()->connectionMessage : "Disconnected"), column, frameWidth_);
    }
    if (!menu.children.empty()) print_hint(menu.children[selected]);
    print_shortcuts();
    if(render) present();
}
void TerminalSettingsView::print_selection(const Model::SettingNode& menu, std::size_t previous,
    std::size_t selected, const std::string& breadcrumb) const {
    (void)previous;
    print_menu(menu,selected,breadcrumb);
}
void TerminalSettingsView::print_connection(std::size_t selected,const std::string& breadcrumb) const {
    configure_layout(parent_,nullptr,true);
    print_frame(tr("Connection status"),breadcrumb+" > Connection status",true);
    print_box(boxes_.size()-1,tr("Connection status"));
    const auto* app=context_.app_component();
    const auto& primary=app && app->primaryContext ? *app->primaryContext : context_;
    const auto* network=primary.network_component();
    const auto status=UiText::connection_status(network ? network->connectionMessage : "Disconnected");
    set_text_color();
    const auto lines=Util::TerminalText::wrap(status,entryWidth_);
    for (std::size_t i=0;i<std::min(lines.size(),std::size_t{5});++i)
        line(firstRow_+static_cast<int>(i),lines[i],entryColumn_,entryWidth_);
    const std::array<std::string,4> actions{{"Rooms","CONNECT / RECONNECT","DISCONNECT","Back to settings"}};
    for (std::size_t i=0;i<actions.size();++i) {
        set_text_color();
        if (selected==i) set_selection_color();
        line(firstRow_+6+static_cast<int>(i),(selected==i ? "> " : "  ")+tr(actions[i]),entryColumn_,entryWidth_);
    }
    set_hint_color();
    print_hint_text(tr("Connection status updates here. Choose Rooms to create or join a room."));
    print_shortcuts(true);
    present();
}
void TerminalSettingsView::print_edit(const Model::SettingItem& item, const std::string& breadcrumb,
    const std::string& value, std::size_t caret) const {
    if (Model::uses_inline_input(item) && parent_) {
        print_scalar_input(item, breadcrumb, value, caret);
        return;
    }
    configure_layout(parent_, &item, true);
    print_frame(tr("Edit ") + item_label(item), breadcrumb + " > " + item.label, true);
    print_box(boxes_.size() - 1, item_label(item));
    set_text_color();
    line(firstRow_, tr("Current: ") + value_text(item), entryColumn_, entryWidth_);
    set_hint_color();
    print_hint_text(preset_hint(item,item_hint(item)));
    if (item.keyBinding) {
        const bool menuShortcut=item.id==Model::SettingId::ControlRetry || item.id==Model::SettingId::ControlOpenSettings;
        line(hintRow_ + hintRows, tr("Press and release a key; existing bindings toggle."), column, frameWidth_);
        if (menuShortcut) line(hintRow_ + hintRows + 1, tr(context_.user_settings().presetDefined ? "Esc Cancel" : "Esc Start screen"), column, frameWidth_);
    } else {
        line(hintRow_ + hintRows, tr(context_.user_settings().presetDefined ? "Enter Confirm   Esc Cancel   Blank Keep value" : "Enter Save   Esc Save and start screen   Blank Keep value"), column, frameWidth_);
        line(hintRow_ + hintRows + 1, tr("Left/Right Cursor   Backspace/Delete Edit"), column, frameWidth_);
    }
    const auto prompt = tr(item.keyBinding ? "Key: " : "New value: ");
    print_input(firstRow_ + 4,prompt,value,caret);
}
void TerminalSettingsView::print_input(int row, const std::string& prompt,
    const std::string& value, std::size_t caret) const {
    set_selection_color();
    const int caretColumns=static_cast<int>(Util::TerminalText::width(
        prompt + value.substr(0,std::min(caret,value.size()))));
    const int scroll=std::max(0,caretColumns-entryWidth_+1);
    surface_->write(row,entryColumn_-scroll,prompt+value,entryWidth_+scroll,color_,background_,entryColumn_,"");
    present(std::pair{row,entryColumn_+caretColumns-scroll});
}
void TerminalSettingsView::print_scalar_input(const Model::SettingItem& item, const std::string& breadcrumb,
                                              const std::string& value, std::size_t caret) const {
    configure_layout(parent_, nullptr, false);
    const auto prompt = "> " + item_label(item) + " = ";
    auto& box = boxes_.back();
    // Expand the right border with the draft, preserving the column's left
    // edge. Scroll only after the box reaches the terminal's usable right edge.
    box.width = std::max(box.width, static_cast<int>(Util::TerminalText::width(prompt + value)) + 1);
    if (terminalSize_) {
        box.width = std::min(box.width,
            std::max(1, terminalSize_->columns - rightMargin - box.column - 4));
    } else {
        frameWidth_ = std::max(width, box.column + box.width + 4 - column);
    }
    print_frame(tr("Edit ") + item_label(item), breadcrumb + " > " + item.label, false);
    if (parent_) print_column(*parent_, parentSelected_, boxes_.size() - 1);
    else print_box(boxes_.size() - 1, item_label(item));
    entryColumn_ = box.column + 2;
    entryWidth_ = box.width;
    const int row = firstRow_ + static_cast<int>(parentSelected_ % visibleRows_);
    set_hint_color();
    print_hint_text(preset_hint(item,item_hint(item)));
    line(hintRow_ + hintRows, tr(item.allowEmptyText ? "Enter Confirm   Blank Auto name" : mapEditor_ || context_.user_settings().presetDefined ? "Enter Confirm   Esc Cancel   Blank Keep value" : "Enter Save   Esc Save and start screen   Blank Keep value"), column, frameWidth_);
    line(hintRow_ + hintRows + 1, tr("Left/Right Cursor   Backspace/Delete Edit"), column, frameWidth_);
    print_input(row,prompt,value,caret);
}
void TerminalSettingsView::print_choices(const Model::SettingItem& item, std::size_t selected,
                                       const std::string& breadcrumb) const {
    configure_layout(parent_, &item, true);
    print_frame(tr("Select ") + item_label(item), breadcrumb + " > " + item.label, true);
    print_box(boxes_.size() - 1, item_label(item));
    const auto first = selected/visibleRows_*visibleRows_;
    const auto end = std::min(item.choice.labels.size(), first + visibleRows_);
    for (auto i = first; i < end; ++i) {
        set_text_color();
        if (i == selected) set_selection_color();
        else if (!bot_availability(item, item.choice.values[i]))
            color_ = context_.config.display.obscuredColors ? TerminalSettingsSurface::Color{{0,0,0}} :
                TerminalSettingsSurface::Color{{128,128,128}};
        else set_text_color();
        line(firstRow_ + static_cast<int>(i - first), (i == selected ? "> " : "  ") + tr(item.choice.labels[i]), entryColumn_, entryWidth_);
    }
    set_hint_color();
    if (item.choice.labels.size() > visibleRows_)
        line(hintRow_ + hintRows, std::to_string(first + 1) + "-" + std::to_string(end) + " / " +
            std::to_string(item.choice.labels.size()) + tr("   PgUp/PgDn Page"), column, frameWidth_);
    const auto available = bot_availability(item, item.choice.values[selected]);
    const auto* rotationInfo = item.id == Model::SettingId::RotationSystem && item.choice.values[selected]>=0
        ? Runtime::rotation_system_info(Runtime::rotation_system_from_index(item.choice.values[selected])) : nullptr;
    if (item.id == Model::SettingId::RandomPieces) {
        print_hint_text(tr(Runtime::bag_randomizer_info(
            Runtime::bag_randomizer_from_index(item.choice.values[selected])).hint));
    } else if (rotationInfo) {
        print_hint_text(tr(rotationInfo->hint));
    } else if(item.id==Model::SettingId::RotationSystem && item.choice.values[selected]==-1) {
        print_hint_text(tr("Use each map piece's original rotation system. Other systems replace matching pieces; unmatched special pieces keep their original rotation."));
    } else if (item.id == Model::SettingId::SpinBonuses) {
        print_hint_text(tr(Runtime::spin_bonuses_info(
            Runtime::spin_bonuses_from_index(item.choice.values[selected])).hint));
    } else {
        print_hint_text(available ? tr("Changes apply after confirmation; Left / Backspace goes back.")
            : tr("Unavailable: ") + tr(available.reason));
    }
    print_shortcuts(true);
    present();
}
void TerminalSettingsView::print_reset_confirmation(bool pending) const {
    if (pending) print_message(player_==2 ? "Press Enter again to reset both players. Arrow keys cancel." :
        "Press Enter again to confirm RESET. Arrow keys cancel.");
    else { hintRow_=surface_->hintRow.value_or(hintRow_);surface_->begin_update(); print_hint_text(""); present(); }
}
void TerminalSettingsView::print_message(const std::string& message) const {
    hintRow_=surface_->hintRow.value_or(hintRow_);
    surface_->begin_update();
    set_hint_color(true);
    print_hint_text(tr(message));
    set_text_color();
    present();
}
} // namespace App::Settings
