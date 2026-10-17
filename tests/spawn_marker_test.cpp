#include "tetr/app/application_state.h"
#include "tetr/app/settings_menu.h"
#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/engine/piece_spawn.h"
#include "tetr/engine/undo.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/replay/session_state.h"
#include "tetr/replay/playback_checkpoint.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/constants.h"
#include "tetr/runtime/player_session_state.h"
#include "tetr/runtime/construction.h"
#include "tetr/app/settings_model.h"
#include "../src/app/ui_text.h"
#include "../src/engine/garbage_rendering.h"
#include "support/terminal_screen.h"

#include <cassert>
#include <filesystem>
#include <functional>
#include <memory>
#include <iostream>
#include <sstream>

namespace {
struct Fixture {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    Replay::ReplaySessionState replay;
    UserConfig::Settings user;
    Runtime::PlayerSessionState& player=session->player;
    Test::TerminalScreen screen;
    Fixture() {
        context.attach_replay(replay);
        context.attach_user_settings(user);
        context.config.display.showSpawnPosition=true;
        context.config.field.ghost=false;
        player.presentation.margin=30;
        player.presentation.topMargin=12;
        player.gameplay.playfield.width=10;
        player.gameplay.playfield.visibleHeight=player.gameplay.playfield.activeHeight=20;
        player.gameplay.playfield.field.initialize(10,20);
        player.gameplay.playfield.nextCount=1;
        player.pieces.nextPreview={Piece::Type::from_index(0)};
        player.presentation.nextSpawnType=player.pieces.nextPreview.front();
    }
    void paint(const std::function<void()>& action) {
        std::ostringstream bytes;
        auto* old=std::cout.rdbuf(bytes.rdbuf());
        action();
        std::cout.rdbuf(old);
        screen.feed(bytes.str());
    }
    void redraw() { paint([&] { Render::redraw_player_board(context,player); }); }
    Test::TerminalScreen::Cell at(int row,int col,int offset=0) const {
        return screen.at(row+player.presentation.topMargin+1,col*2+player.presentation.margin+offset+1);
    }
    Test::TerminalScreen::Cell dot_at(int row,int col) const {
        return at(row,col,context.config.display.squareStyle==0 ? 1 : 0);
    }
    void matches_redraw(const char* stage="repaint") {
        const auto previous=screen;
        screen={}; redraw();
        const auto& field=player.gameplay.playfield;
        for (int row=-Runtime::kDisplayHiddenRows;row<field.visibleHeight;++row)
            for (int col=0;col<field.width*2;++col) {
                const int y=row+player.presentation.topMargin+1,x=col+player.presentation.margin+1;
                const auto before=previous.at(y,x),after=screen.at(y,x);
                if (before.glyph!=after.glyph || before.background!=after.background)
                    std::cerr<<stage<<": cell "<<row<<","<<col<<" was "<<before.glyph<<" now "<<after.glyph<<'\n';
                assert(before.glyph==after.glyph && before.background==after.background);
                if (after.glyph!=" ") assert(before.foreground==after.foreground);
            }
    }
};

void covered_cells_and_toggle() {
    for (int style:{0,1}) for (bool obscured:{false,true}) {
        Fixture f;
        f.context.config.display.squareStyle=style;
        f.context.config.display.obscuredColors=obscured;
        f.redraw();
        const auto cells=f.player.presentation.spawnMarkerCells;
        assert(cells.size()==4);
        const Runtime::BoardCell contents[]{Runtime::BoardCell::garbage(),Runtime::BoardCell::piece(2),
            Runtime::BoardCell::solid_garbage(),Runtime::BoardCell::timed_solid(3)};
        for (std::size_t i=0;i<cells.size();++i)
            f.player.gameplay.playfield.field.set_cell(cells[i].row,cells[i].col,contents[i]);
        f.context.config.display.showSpawnPosition=false;
        f.redraw();
        std::vector<Test::TerminalScreen::Cell> baseline;
        for (const auto& cell:cells) baseline.push_back(f.at(cell.row,cell.col));
        f.context.config.display.showSpawnPosition=true;
        f.paint([&] { Render::refresh_spawn_marker(f.context,f.player); });
        for (std::size_t i=0;i<cells.size();++i) {
            const auto& cell=cells[i];
            const auto marked=f.at(cell.row,cell.col);
            if (contents[i].is_timed_number()) {
                assert(marked.glyph==baseline[i].glyph && marked.foreground==baseline[i].foreground);
                assert(marked.background==baseline[i].background);
                if (style==0) {
                    assert(f.dot_at(cell.row,cell.col).glyph==".");
                    assert(f.dot_at(cell.row,cell.col).background==baseline[i].background);
                    const Test::TerminalScreen::Color expected=obscured
                        ? Test::TerminalScreen::Color{{64,68,74}} : Test::TerminalScreen::Color{{12,12,12}};
                    assert(f.dot_at(cell.row,cell.col).foreground==expected);
                }
            } else {
                const auto dot=f.dot_at(cell.row,cell.col);
                assert(dot.glyph=="." && dot.background==baseline[i].foreground);
                const Test::TerminalScreen::Color expected=obscured
                    ? Test::TerminalScreen::Color{{64,68,74}} : Test::TerminalScreen::Color{{12,12,12}};
                assert(dot.foreground==expected);
                if (style==0) assert(marked.glyph==" " && marked.background==baseline[i].foreground);
            }
            if (style==1) {
                assert(f.at(cell.row,cell.col,1).glyph==" ");
                assert((f.at(cell.row,cell.col,1).background==Test::TerminalScreen::Color{{12,12,12}}));
            }
            assert(f.player.gameplay.playfield.field.cell(cell.row,cell.col)==contents[i]);
        }
        f.matches_redraw("covered cells"); // Original colors survive repeated painting.
        f.context.config.display.showSpawnPosition=false;
        f.paint([&] { Render::refresh_spawn_marker(f.context,f.player); });
        for (std::size_t i=0;i<cells.size();++i)
            assert(f.at(cells[i].row,cells[i].col).foreground==baseline[i].foreground);
    }
}

void movement_and_next_change() {
    for (int style:{0,1}) for (bool obscured:{false,true}) {
        Fixture f;
        f.context.config.display.squareStyle=style;
        f.context.config.display.obscuredColors=obscured;
        f.redraw();
        auto piece=Piece::get_definition(f.player.pieces.nextPreview.front());
        const auto spawn=Piece::spawn_pose(10,piece.type());
        const auto cells=f.player.presentation.spawnMarkerCells;
        for (const auto& cell:cells) {
            assert(f.dot_at(cell.row,cell.col).glyph==".");
            assert((f.dot_at(cell.row,cell.col).foreground==Test::TerminalScreen::Color{{64,68,74}}));
        }
        f.paint([&] { Render::draw_piece(f.context,piece,f.player,spawn,f.player.gameplay.playfield.field); });
        for (const auto& cell:cells) {
            assert(f.dot_at(cell.row,cell.col).glyph==".");
            const Test::TerminalScreen::Color background=obscured
                ? Test::TerminalScreen::Color{{0,0,0}} : Test::TerminalScreen::Color{{54,235,168}};
            const Test::TerminalScreen::Color dot=obscured
                ? Test::TerminalScreen::Color{{64,68,74}} : Test::TerminalScreen::Color{{12,12,12}};
            assert(f.dot_at(cell.row,cell.col).background==background);
            assert(f.dot_at(cell.row,cell.col).foreground==dot);
        }
        const auto moved=spawn.translated(4,1);
        f.paint([&] { Render::draw_piece_transition(f.context,piece,f.player,spawn,moved,f.player.gameplay.playfield.field); });
        for (const auto& cell:cells) assert(f.dot_at(cell.row,cell.col).glyph==".");
        f.player.presentation.activePieceType=piece.type();
        f.player.presentation.activePose=moved;
        f.matches_redraw("moved piece");
        f.player.pieces.nextPreview={Piece::Type::from_index(3)};
        f.player.presentation.nextSpawnType=f.player.pieces.nextPreview.front();
        f.paint([&] { Render::refresh_spawn_marker(f.context,f.player); });
        f.matches_redraw("next changed"); // Old footprint restored while the piece stays visible.
        f.player.gameplay.playfield.nextCount=0;
        f.context.config.field.nextCount=0;
        f.paint([&] { Render::refresh_spawn_marker(f.context,f.player); });
        assert(f.player.presentation.spawnMarkerCells.empty());
        f.matches_redraw("NEXT hidden");
    }
}

void ghost_and_garbage() {
    Fixture f;
    f.context.config.field.ghost=true;
    f.redraw();
    auto piece=Piece::get_definition(f.player.pieces.nextPreview.front());
    const auto spawn=Piece::spawn_pose(10,piece.type());
    for (const auto& offset:piece.cells(spawn.rotation))
        f.player.gameplay.playfield.field.set_cell(spawn.position.row+offset[0]+1,
            spawn.position.col+offset[1],Runtime::BoardCell::garbage());
    f.redraw();
    const auto high=spawn.translated(-3,0);
    f.paint([&] { Render::draw_piece(f.context,piece,f.player,high,f.player.gameplay.playfield.field); });
    for (const auto& cell:f.player.presentation.spawnMarkerCells) {
        const auto marked=f.dot_at(cell.row,cell.col);
        assert(marked.glyph==".");
        assert((marked.background==Test::TerminalScreen::Color{{54,57,62}}));
    }
    f.player.presentation.activePieceType=piece.type(); f.player.presentation.activePose=high;
    f.matches_redraw("ghost");
    f.paint([&] {
        auto& field=f.player.gameplay.playfield.field;
        const auto before=Engine::GarbageRendering::capture_field(f.context,f.player,field,&piece,high);
        Engine::GarbageService::add_garbage(f.context,f.player,field,1,false,Runtime::BoardCell::garbage(),false);
        Engine::GarbageRendering::render_field_changes(f.context,f.player,field,before,&piece,high);
    });
    f.matches_redraw("garbage rise");
}

void clear_animation() {
    for (bool anti:{false,true}) {
        Fixture f;
        f.context.config.field.antiGravity=anti;
        f.redraw();
        const int row=f.player.presentation.spawnMarkerCells.front().row;
        for (int col=0;col<10;++col) f.player.gameplay.playfield.field.set_cell(row,col,Runtime::BoardCell::garbage());
        f.redraw();
        auto piece=Piece::get_definition(Piece::Type::from_index(0));
        Rules::PlacementResult placement;
        placement.clear.clearedLines=1; placement.clear.clearedRows={row};
        f.paint([&] {
            assert(Engine::run_locked_line_clear_phase(f.context,f.player.gameplay.playfield.field,piece,f.player,
                placement,[] { return Rules::PlayResult::ContinueFrame; })==Rules::PlayResult::ContinueFrame);
        });
        for (const auto& cell:f.player.presentation.spawnMarkerCells) assert(f.dot_at(cell.row,cell.col).glyph==".");
        f.matches_redraw("line clear");
    }
}

void actual_event_shape_and_remote() {
    for (bool placementFrame:{false,true}) for (bool extendedHold:{false,true}) {
        Fixture f;
        const auto type=Piece::april_fools_piece(8);
        f.player.presentation.nextSpawnType=type;
        f.player.pieces.nextPreview={Piece::fool_display_type(type)};
        f.player.pieces.bag.push_back({Piece::Type::from_index(0),false});
        f.context.config.quickPlay.snowball=Runtime::ModifierMode::Enabled;
        f.context.config.field.width=f.player.gameplay.playfield.width=4;
        f.player.gameplay.playfield.field.initialize(4,20);
        if (extendedHold) f.context.config.field.infiniteHold=true;
        std::optional<Network::RemotePlacementSnapshot> placement;
        if (placementFrame) placement=Network::RemotePlacementSnapshot{};
        if (placement) placement->piece=Piece::Type::from_index(0);
        const auto wire=Network::encode_board_state(f.context,f.player,Network::ActivePieceState::spawn(),placement);
        const auto snapshot=Network::decode_board_state(wire,4,20);
        assert(snapshot && snapshot->nextSpawnType==type && snapshot->nextSpawnPose);
        const auto shape=Piece::get_definition(type);
        const auto pose=Piece::fit_spawn_inside_walls(Piece::spawn_pose(4,type),shape.cells(Piece::spawn_pose(4,type).rotation),4);
        assert(snapshot->nextSpawnPose==pose);
        assert(!Network::decode_board_state(wire+";unexpected",4,20));
        f.redraw();
        if (!placementFrame) {
            Fixture remote;
            remote.context.config.field.width=remote.player.gameplay.playfield.width=4;
            remote.player.gameplay.playfield.field.initialize(4,20);
            remote.paint([&] { Engine::apply_remote_board_state(remote.context,remote.player,*snapshot); });
            assert(remote.player.presentation.nextSpawnType==type && remote.player.presentation.nextSpawnPose==pose);
            assert(remote.player.presentation.spawnMarkerCells==f.player.presentation.spawnMarkerCells);
            remote.matches_redraw("remote board");
        }
    }
}

void settings_persistence() {
    Fixture a,b;
    App::ApplicationState app;
    app.primaryContext=&a.context; app.localPlayerTwoContext=&b.context;
    a.context.attach_app(app); b.context.attach_app(app);
    assert(a.user.showSpawnPosition);
    a.user.showSpawnPosition=1;
    const auto tree=App::Settings::Model::make_tree(a.user,true,0);
    bool inAppearance=false;
    for (const auto& section:tree.children) if (section.section==UserConfig::Section::Others)
        for (const auto& child:section.children)
            if (child.item && child.item->id==UserConfig::SettingId::ShowSpawnPosition) inAppearance=true;
    assert(inAppearance);
    assert(App::UiText::tr("Show spawn position")=="显示出生位置");
    App::Settings::synchronize_shared_appearance(a.context);
    assert(b.user.showSpawnPosition==1 && a.context.config.display.showSpawnPosition && b.context.config.display.showSpawnPosition);
    const auto path=std::filesystem::temp_directory_path()/"tetr_spawn_marker_settings.ini";
    a.user.persistencePath=path.string();
    assert(UserConfig::save(a.user));
    UserConfig::Settings restored;
    UserConfig::load(restored,path.string());
    assert(restored.showSpawnPosition==1);
    UserConfig::reset_settings(restored,false);
    assert(restored.showSpawnPosition==1);
    UserConfig::reset_section(restored,UserConfig::Section::Others,false);
    assert(restored.showSpawnPosition==1);
    std::filesystem::remove(path);
}

void restored_state() {
    Fixture f;
    f.redraw();
    const auto original=f.player.presentation.spawnMarkerCells;
    f.player.clutch.currentTurnLifted=true;
    f.player.clutch.previousPieceCleared=true;
    f.paint([&] { Render::refresh_spawn_marker(f.context,f.player); });
    assert(f.player.presentation.spawnMarkerCells==original); // Clutch never moves the hint.
    Engine::begin_undo_history(f.context);
    Engine::capture_undo_snapshot(f.context);
    f.player.pieces.nextPreview={Piece::Type::from_index(3)};
    f.player.presentation.nextSpawnType=f.player.pieces.nextPreview.front();
    f.redraw();
    assert(Engine::restore_previous_undo_snapshot(f.context));
    f.redraw();
    assert(f.player.presentation.spawnMarkerCells==original);
    for (const auto& cell:original) assert(f.dot_at(cell.row,cell.col).glyph==".");

    f.context.config.mode.replayMode=true;
    f.replay.playback.started=true;
    Replay::capture_playback_checkpoint(f.context);
    f.player.pieces.nextPreview={Piece::Type::from_index(3)};
    f.player.presentation.nextSpawnType=f.player.pieces.nextPreview.front();
    f.redraw();
    Replay::schedule_playback_checkpoint_restore(f.context,Runtime::Duration{});
    assert(Replay::restore_scheduled_playback_checkpoint(f.context));
    f.redraw();
    assert(f.player.presentation.spawnMarkerCells==original);
    for (const auto& cell:original) assert(f.dot_at(cell.row,cell.col).glyph==".");
}
void construction_targets() {
    for(int style:{0,1})for(bool obscured:{false,true}) {
        Fixture f;f.context.config.display.showSpawnPosition=false;
        f.context.config.display.squareStyle=style;f.context.config.display.obscuredColors=obscured;
        f.context.config.challengeRules=Runtime::ChallengeRules::ConstructCheckerboard;f.redraw();
        for(int row=0;row<20;++row)for(int col=0;col<10;++col) {
            assert(!f.player.gameplay.playfield.field.cell(row,col).occupied());
            assert((f.dot_at(row,col).glyph==".")==((col+19-row)%2==0));
        }
        auto piece=Piece::get_definition(Piece::Type::from_index(0));auto pose=Piece::spawn_pose(10,piece.type());
        f.player.presentation.activePieceType=piece.type();f.player.presentation.activePose=pose;
        f.paint([&]{Render::draw_piece(f.context,piece,f.player,pose,f.player.gameplay.playfield.field);});
        const auto next=pose.translated(7,1);
        f.paint([&]{Render::draw_piece_transition(f.context,piece,f.player,pose,next,f.player.gameplay.playfield.field);});
        f.matches_redraw("construction moving piece");
        f.paint([&]{Render::draw_piece(f.context,piece,f.player,next,f.player.gameplay.playfield.field,Render::PieceDrawMode::Erase);});
        f.player.presentation.activePose.reset();f.player.presentation.activePieceType.reset();
        f.matches_redraw("construction erased piece");
        f.context.config.challengeRules=Runtime::ChallengeRules::ConstructSecret;f.redraw();
        assert(f.dot_at(19,0).glyph!="." && f.dot_at(19,1).glyph=="." && f.dot_at(0,0).glyph==".");
        f.context.config.challengeRules=Runtime::ChallengeRules::ConstructSymmetry;
        f.player.gameplay.playfield.field.set_cell(19,1,Runtime::BoardCell::piece(1));f.redraw();
        assert(f.dot_at(19,8).glyph=="." && !f.player.gameplay.playfield.field.cell(19,8).occupied());
        auto target=std::make_shared<Runtime::MapDefinition>();target->rows[19][0]='G';target->rows[18][0]='G';
        f.context.config.initialMap=target;f.context.config.challengeRules=Runtime::ChallengeRules::ConstructMap;
        f.player.gameplay.playfield.field.clear();f.redraw();
        assert(f.dot_at(19,0).glyph=="." && f.dot_at(18,0).glyph=="." && f.dot_at(19,1).glyph!=".");
        f.matches_redraw("construction map");
        Engine::begin_undo_history(f.context);Engine::capture_undo_snapshot(f.context);
        f.player.gameplay.playfield.field.set_cell(19,0,Runtime::BoardCell::piece(0));f.redraw();
        assert(Engine::restore_previous_undo_snapshot(f.context));f.redraw();
        assert(f.dot_at(19,0).glyph=="." && !f.player.gameplay.playfield.field.cell(19,0).occupied());
    }
}
}

int main() {
    App::UiText::set_language(App::UiText::Language::Chinese);
    covered_cells_and_toggle(); movement_and_next_change(); ghost_and_garbage();
    clear_animation(); actual_event_shape_and_remote(); settings_persistence(); restored_state();construction_targets();
}
