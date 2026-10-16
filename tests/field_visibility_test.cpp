#include "tetr/config/normalization.h"
#include "tetr/config/settings_schema.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/frame_coordinator.h"
#include "tetr/engine/locked_piece_resolution.h"
#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/engine/placement_coordinator.h"
#include "tetr/engine/remote_board_state_adapter.h"
#include "tetr/engine/undo.h"
#include "tetr/input/services.h"
#include "tetr/network/board_state_codec.h"
#include "tetr/network/network_services.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/render/board_renderer.h"
#include "tetr/runtime/field_visibility.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/settings_model.h"
#include "support/terminal_screen.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using namespace Runtime;
using Id=UserConfig::SettingId;
namespace Model=App::Settings::Model;
Duration seconds(double value) {return Duration::from_seconds(value);}
const Model::SettingItem* find(const Model::SettingNode& tree,Id id) {
    if(tree.item && tree.item->id==id) return &*tree.item;
    for(const auto& child:tree.children) if(const auto* item=find(child,id)) return item;
    return nullptr;
}
struct Capture {
    std::ostringstream text;
    std::streambuf* old=std::cout.rdbuf(text.rdbuf());
    ~Capture() {std::cout.rdbuf(old);}
};
struct Fixture {
    std::unique_ptr<SessionState> session=std::make_unique<SessionState>();
    RuntimeServices services;
    GameContext context{*session,services};
    Replay::ReplaySessionState replay;
    Input::Services input;
    Network::NetworkServices network;
    PlayerSessionState& player=session->player;
    Playfield& field=player.gameplay.playfield.field;
    Fixture() {
        context.attach_replay(replay);context.attach_input(input);context.attach_network(network);
        input.keyStateProvider=[](int){return false;};
        UserConfig::Settings user;user.fieldInvisible=1;user.invisibilityDelay=2;user.nextCount=0;user.simulationPps=1;
        context.config=normalize_user_config(user,GameMode::Zen);
        player.pieces.allowedPieces=Piece::types_for_rotation_system(context.config.rotationSystem);
        player.gameplay.playfield.width=10;player.gameplay.playfield.visibleHeight=20;
        player.presentation.margin=30;
        Engine::initialize_board(context,player);
        session->gameStartTime=TimePoint{};
    }
    void at(double elapsed) {session->simulationTime=TimePoint::from_seconds(elapsed);}
    bool visible(int row,int col) {return Render::board_cell_visible(context,player,row,col,field.cell(row,col));}
};

void configuration() {
    UserConfig::Settings user;
    assert(user.fieldInvisible==0 && user.invisibilityDelay==0 && user.invisibilityInterval==5 && user.invisibilityDuration==1);
    for(int mode=0;mode<=2;++mode) {
        user.fieldInvisible=mode;
        const auto tree=Model::make_tree(user,false,0);
        assert(find(tree,Id::FieldInvisible)->choice.values==std::vector<int>({0,1,2}));
        assert((find(tree,Id::InvisibilityDelay)!=nullptr)==(mode==1));
        assert((find(tree,Id::InvisibilityInterval)!=nullptr)==(mode==2));
        assert((find(tree,Id::InvisibilityDuration)!=nullptr)==(mode==2));
        user.invisibilityDelay=.123456789012345;user.invisibilityInterval=2.75;user.invisibilityDuration=.6;
        const auto config=normalize_user_config(user,GameMode::Zen);
        assert(config.field.invisible==(mode!=0) && config.field.periodicInvisible==(mode==2));
        RuntimeConfig decoded;Replay::ConfigPrefixResult prefix;
        assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,prefix));
        assert(decoded.field.invisible==config.field.invisible && decoded.field.periodicInvisible==config.field.periodicInvisible);
        assert(decoded.field.invisibilityDelay==config.field.invisibilityDelay);
        assert(decoded.field.invisibilityInterval==config.field.invisibilityInterval && decoded.field.invisibilityDuration==config.field.invisibilityDuration);
    }
    const auto path=std::filesystem::temp_directory_path()/"termino_field_visibility_test.ini";
    user.persistencePath=path.string();assert(UserConfig::save(user));
    UserConfig::Settings loaded;UserConfig::load(loaded,path.string());std::filesystem::remove(path);
    assert(loaded.fieldInvisible==2 && loaded.invisibilityDelay==user.invisibilityDelay);
    assert(loaded.invisibilityInterval==2.75 && loaded.invisibilityDuration==.6);
    RuntimeConfig decoded;decoded.field.invisibilityDelay=seconds(9);decoded.field.periodicInvisible=true;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix("{{Invisible,1}{QP_mod_invisible,0}}",decoded,prefix));
    assert(decoded.field.invisible && !decoded.field.periodicInvisible && decoded.field.invisibilityDelay==Duration{});
    assert(Replay::decode_config_prefix("{{Model,3}{GarbageModel,10}{Invisible,1}{QP_mod_invisible,1}}",decoded,prefix));
    assert(decoded.field.periodicInvisible && decoded.field.invisibilityInterval==seconds(5) && decoded.field.invisibilityDuration==seconds(1));
    user.qpInvisible=1;
    const auto qp=normalize_user_config(user,GameMode::QuickPlay);
    assert(qp.field.invisible && qp.field.periodicInvisible && qp.field.invisibilityDelay==Duration{});
    assert(qp.field.invisibilityInterval==seconds(5) && qp.field.invisibilityDuration==seconds(1));
    user.qpInvisible=2;
    assert(!normalize_user_config(user,GameMode::QuickPlay).field.periodicInvisible);
}

void independent_piece_timers() {
    Fixture f;
    auto piece=Piece::get_definition(Piece::Type{0});
    Piece::Pose left{{0,2},{}},right{{0,7},{}};
    while(piece.can_place(left.translated(1,0),f.field)) ++left.position.row;
    while(piece.can_place(right.translated(1,0),f.field)) ++right.position.row;
    f.at(5);Engine::resolve_locked_piece(f.context,f.field,piece,f.player,left,{},seconds(5));
    f.at(6);Engine::resolve_locked_piece(f.context,f.field,piece,f.player,right,{},seconds(6));
    const auto cell=piece.cells({}).front();
    const int row=left.position.row+cell[0],col=left.position.col+cell[1],other=right.position.col+cell[1];
    assert(f.field.cell_lock_time(row,col)==seconds(5));
    assert(f.field.cell_lock_time(row,other)==seconds(6));
    assert(f.visible(row,col) && f.visible(row,other));
    Engine::synchronize_field_visibility(f.context,f.player);
    f.at(7);assert(Engine::synchronize_field_visibility(f.context,f.player));
    assert(!f.visible(row,col) && f.visible(row,other));
    assert(!Engine::synchronize_field_visibility(f.context,f.player));
    {Capture output;Render::redraw_player_board(f.context,f.player);Test::TerminalScreen screen;screen.feed(output.text.str());
        assert(screen.at(row+9,31+col*2).glyph==" ");assert(screen.at(row+9,31+other*2).glyph=="█");}
    f.at(8);assert(Engine::synchronize_field_visibility(f.context,f.player));assert(!f.visible(row,other));
    f.context.config.mode.replayMode=true;f.replay.separateGameTime=true;f.replay.gameClockPaused=true;f.replay.lastGameTimestamp=seconds(7);
    assert(Render::board_cell_appearance(f.context,f.player,row,col,f.field.cell(row,col))==Render::BoardCellAppearance::Dimmed);
    assert(Render::board_cell_appearance(f.context,f.player,row,other,f.field.cell(row,other))==Render::BoardCellAppearance::Visible);

    Fixture live;live.at(3);
    Engine::MovementState movement;movement.pose=left;
    Capture output;
    assert(Engine::finalize_locked_piece(live.context,live.field,piece,live.player,movement,0,
        [] {return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::Locked);
    Test::TerminalScreen screen;screen.feed(output.text.str());
    assert(live.field.cell_lock_time(row,col)==seconds(3));
    assert(screen.at(row+9,31+col*2).glyph=="█");
}

void periodic_visibility() {
    Fixture f;f.context.config.field.periodicInvisible=true;
    f.field.set_cell(19,0,BoardCell::piece(0));f.field.set_cell(19,1,BoardCell::garbage());
    for(double time:{0.,4.999,5.,5.999,6.,9.999,10.,11.}) {
        f.at(time);Engine::synchronize_field_visibility(f.context,f.player);
        assert(f.visible(19,0)==(time==5 || time==5.999 || time==10));
        assert(f.visible(19,1));
    }
    f.context.config.field.invisibilityInterval=seconds(2.5);f.context.config.field.invisibilityDuration=seconds(.75);
    for(double time:{0.,2.499,2.5,3.249,3.25,5.,5.75}) {
        f.at(time);Engine::synchronize_field_visibility(f.context,f.player);
        assert(f.visible(19,0)==(time==2.5 || time==3.249 || time==5));
    }
    assert(!periodic_invisible_flash(seconds(10),seconds(5),{}));
    assert(periodic_invisible_flash({}, {},seconds(1)));
    assert(!periodic_invisible_flash({}, {},{}));
}

void row_moves_and_undo() {
    Fixture f;f.at(7);
    f.field.set_cell(18,0,BoardCell::piece(0));f.field.mark_cell_lock_time(18,0,seconds(6));
    for(int col=0;col<10;++col) f.field.set_cell(19,col,BoardCell::piece(0));
    f.field.mark_cell_lock_time(19,0,seconds(1));
    Rules::PlacementResult placement;placement.clear.clearedLines=1;placement.clear.clearedRows={19};
    auto piece=Piece::get_definition(Piece::Type{0});
    {Capture output;Render::redraw_player_board(f.context,f.player);Test::TerminalScreen screen;screen.feed(output.text.str());
        output.text.str("");
        Engine::run_locked_line_clear_phase(f.context,f.field,piece,f.player,placement,[]{return Rules::PlayResult::ContinueFrame;});
        screen.feed(output.text.str());assert(f.field.cell_lock_time(19,0)==seconds(6));
        assert(screen.at(28,31).glyph=="█");}
    f.field.copy_row(18,19);f.field.mark_cell_lock_time(18,0,seconds(2));
    assert(f.field.cell_lock_time(19,0)==seconds(6));
    Playfield copy=f.field;f.field.set_cell(19,0,{});
    assert(copy.cell_lock_time(19,0)==seconds(6) && f.field.cell_lock_time(19,0)==Duration{});
    f.field=copy;
    Engine::begin_undo_history(f.context);f.field.clear();f.at(12);++f.player.gameplay.scoring.placedBlocks;
    assert(Engine::restore_previous_undo_snapshot(f.context));
    assert(f.field.cell_lock_time(19,0)==seconds(6) && f.visible(19,0));
    f.field.expand(12,22,1);assert(f.field.cell_lock_time(21,1)==seconds(6));
}

void network_timing() {
    Fixture f;f.at(7);f.field.set_cell(19,0,BoardCell::piece(0));f.field.mark_cell_lock_time(19,0,seconds(6));
    const auto wire=Network::encode_board_state(f.context,f.player);
    const auto snapshot=Network::decode_board_state(wire,10,20);
    assert(snapshot && snapshot->field.cell_lock_time(19,0)==seconds(6));
    Fixture remote;
    {Capture output;Engine::apply_remote_board_state(remote.context,remote.player,*snapshot,true);
        Test::TerminalScreen screen;screen.feed(output.text.str());assert(screen.at(28,31).glyph=="█");}
    assert(remote.field.cell_lock_time(19,0)==seconds(6));
    auto old=wire;const auto start=old.find(";visibility-v1;"),end=old.find(";clock-v1;",start);
    old.erase(start,end-start);assert(Network::decode_board_state(old,10,20));
    auto malformed=wire;malformed.insert(end, "0:6,");assert(!Network::decode_board_state(malformed,10,20));
    malformed=wire;malformed.replace(start+15,end-start-15,"999999:6,");assert(!Network::decode_board_state(malformed,10,20));
}
}
int main() {configuration();independent_piece_timers();periodic_visibility();row_moves_and_undo();network_timing();}
