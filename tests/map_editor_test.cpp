#include "tetr/config/map_store.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/app/init.h"
#include "tetr/autoplay/availability.h"
#include "tetr/engine/board_lifecycle.h"
#include "tetr/engine/game_loop_adapter.h"
#include "tetr/engine/piece_queue.h"
#include "tetr/engine/locked_piece_resolution.h"
#include "tetr/engine/locked_line_clear_phase.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/engine/undo.h"
#include "tetr/rules/game_completion.h"
#include "tetr/input/services.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/rotation_systems.h"
#include "../src/app/map_editor.h"
#include "../src/app/init_player_setup.h"
#include "../src/app/settings_maps.h"
#include "../src/app/ui_text.h"
#include "tetr/render/board_renderer.h"
#include "support/terminal_screen.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using Runtime::MapDefinition;using Runtime::MapGenerator;
struct MapFile {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/"termino_map_editor_test";
    std::filesystem::path file=directory/"maps.txt";
    std::string previous;
    MapFile() {
        if(const auto* value=std::getenv("TETR_MAP_FILE")) previous=value;
        std::filesystem::remove_all(directory);
#ifdef _WIN32
        _putenv_s("TETR_MAP_FILE",file.string().c_str());
#else
        setenv("TETR_MAP_FILE",file.string().c_str(),1);
#endif
    }
    ~MapFile() {
#ifdef _WIN32
        _putenv_s("TETR_MAP_FILE",previous.c_str());
#else
        if(previous.empty()) unsetenv("TETR_MAP_FILE");else setenv("TETR_MAP_FILE",previous.c_str(),1);
#endif
        std::filesystem::remove_all(directory);
    }
};
std::string contents(const std::filesystem::path& path) {std::ifstream in(path);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void codec_and_storage(MapFile& file) {
    MapDefinition map;map.name="地图1";map.rows.back()=" GIJLOSTZ ";
    map.generator=MapGenerator::CustomSequence;
    map.sequence={Piece::Type::from_index(0),Piece::april_fools_piece(69),Piece::types_for_rotation_system(Runtime::RotationSystem::Ars).back()};
    const auto encoded=Runtime::encode_map(map);
    assert(encoded.find("{{pfstart}}\n")!=std::string::npos);
    assert(encoded.find("{{pfrow| |G|I|J|L|O|S|T|Z| }}")!=std::string::npos);
    MapDefinition decoded;assert(Runtime::decode_map(encoded,decoded) && decoded==map);
    assert(Runtime::decode_map_hex(Runtime::encode_map_hex(map),decoded) && decoded==map);
    map.goal=Runtime::MapGoal::AllClear;
    assert(Runtime::decode_map(Runtime::encode_map(map),decoded) && decoded==map);
    auto oldFormat=encoded;const auto goalLine=oldFormat.find("Goal = 0\n");assert(goalLine!=std::string::npos);oldFormat.erase(goalLine,9);
    assert(Runtime::decode_map(oldFormat,decoded) && decoded.goal==Runtime::MapGoal::Disabled);
    map.goal=Runtime::MapGoal::Disabled;
    for(const auto symbol:std::string(" GIJLOSTZ")) assert(Runtime::map_symbol(Runtime::map_cell(symbol))==symbol);
    assert(!Runtime::decode_map_hex("ffffx",decoded) && decoded==map);
    auto broken=encoded;broken.replace(broken.find("{{pfrow"),7,"{{xxrow");
    assert(!Runtime::decode_map(broken,decoded) && decoded==map);
    UserConfig::Maps::Collection collection;assert(UserConfig::Maps::load(collection) && collection.maps.empty());
    assert(UserConfig::Maps::default_name(collection)=="地图1");
    UserConfig::Maps::SaveUndo undo;
    assert(UserConfig::Maps::save(map,undo) && map.id==1);
    auto same=map;same.id=0;assert(UserConfig::Maps::save(same,undo) && same.id!=map.id);
    auto third=map;third.id=0;third.name="地图3";assert(UserConfig::Maps::save(third,undo));
    assert(UserConfig::Maps::load(collection) && collection.maps.size()==3);
    assert(UserConfig::Maps::default_name(collection)=="地图2");
    const auto original=map;map.name="同名 \"地图\"";map.rows.back()[0]='T';
    assert(UserConfig::Maps::save(map,undo));
    assert(UserConfig::Maps::find(map.id)==std::optional<MapDefinition>{map});
    assert(UserConfig::Maps::undo_save(undo));
    assert(UserConfig::Maps::find(original.id)==std::optional<MapDefinition>{original});
    assert(UserConfig::Maps::find(same.id)==std::optional<MapDefinition>{same});
    auto created=original;created.id=0;created.name="地图2";assert(UserConfig::Maps::save(created,undo));
    assert(UserConfig::Maps::load(collection) && UserConfig::Maps::default_name(collection)=="地图4");
    assert(UserConfig::Maps::undo_save(undo) && !UserConfig::Maps::find(created.id));
    UserConfig::Maps::IdChanges changes;
    assert(UserConfig::Maps::remove(original.id,file.file,&changes));
    assert(UserConfig::Maps::renumbered_id(original.id,changes)==0);
    same.id=UserConfig::Maps::renumbered_id(same.id,changes);third.id=UserConfig::Maps::renumbered_id(third.id,changes);
    assert(same.id==1 && third.id==2 && UserConfig::Maps::find(same.id)==same);
    assert(UserConfig::Maps::load(collection) && collection.nextId==3);
    assert(UserConfig::Maps::load(collection) && UserConfig::Maps::default_name(collection)=="地图2");
    const auto valid=contents(file.file);
    {std::ofstream out(file.file);out<<"TETR_MAPS 1 99\n[map corrupt]\n";}
    const auto corrupt=contents(file.file);auto another=original;another.id=0;
    assert(!UserConfig::Maps::save(another,undo) && contents(file.file)==corrupt);
    {std::ofstream out(file.file);out<<valid;}
    assert(UserConfig::Maps::load(collection));
    UserConfig::Settings user;user.mapId=same.id;
    user.persistencePath=(file.directory/"player.ini").string();assert(UserConfig::save(user));
    UserConfig::Settings loaded;UserConfig::load(loaded,user.persistencePath);assert(loaded.mapId==same.id);
    const auto menu=App::Settings::make_map_menu(user);
    assert(menu.key=="maps" && menu.children.front().kind==App::Settings::Model::NodeKind::MapNew);
    assert(menu.children[1].kind==App::Settings::Model::NodeKind::MapDisable);
    for(const auto& option:menu.children) {
        assert(option.confirmTwice==(option.kind!=App::Settings::Model::NodeKind::Group));
        if(option.kind==App::Settings::Model::NodeKind::Group) {
            assert(option.children.size()==3 && option.literalTitle && option.summary.empty());
            assert(option.title=="#"+std::to_string(option.mapId)+": "+option.mapPreview->name);
            for(const auto& action:option.children) assert(action.confirmTwice && action.mapId==option.mapId);
        }
    }
}
void editor_operations() {
    App::MapEditorState editor;editor.paint('T');editor.column=1;editor.paint('G');
    editor.resize(4,8);assert(editor.map.rows.back()=="TG  " && editor.row==7);
    editor.resize(12,25);assert(editor.map.rows.back()=="TG          " && editor.row==24);
    assert(editor.undo() && editor.map.width==4 && editor.map.height==8);
    assert(editor.redo() && editor.map.width==12 && editor.map.height==25);
    editor.change_generator();assert(editor.map.generator==MapGenerator::CustomSequence);
    const auto i=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus).front();
    const auto t=Piece::types_for_rotation_system(Runtime::RotationSystem::Ars)[5];
    const auto event=Piece::april_fools_piece(48);
    editor.insert(i);editor.insert(t);editor.caret=1;editor.insert(event);
    assert(editor.map.sequence==std::vector<Piece::Type>({i,event,t}) && editor.caret==2);
    editor.erase(true);assert(editor.map.sequence==std::vector<Piece::Type>({i,t}) && editor.caret==1);
    editor.erase(false);assert(editor.map.sequence==std::vector<Piece::Type>({i}));
    assert(editor.undo() && editor.map.sequence==std::vector<Piece::Type>({i,t}));
    for(int system=0;system<12;++system) assert(!App::map_browser_pieces(Runtime::rotation_system_from_index(system)).empty());
    const auto pentomino=App::map_browser_pieces(Runtime::RotationSystem::SrsPlus,"PENTOMINO I");
    assert(!pentomino.empty());for(const auto type:pentomino) assert(Piece::metadata(type).rotationSystem==Runtime::RotationSystem::Pentomino);
    assert(App::map_browser_pieces(Runtime::RotationSystem::SrsPlus,"no_such_piece_123").empty());
    App::MapEditorState drawing;drawing.resize(4,4);
    drawing.move(-2,2,'T');
    assert(drawing.map.rows[3][0]=='T' && drawing.map.rows[2][0]=='T');
    assert(drawing.map.rows[1].substr(0,3)=="TTT");
    drawing.move(1,0);assert(drawing.map.rows[2][2]==' ');
    drawing.move(-1,-2,' ');assert(drawing.map.rows[1]=="    ");
    drawing.move(-20,-20,'G');assert(drawing.row==0 && drawing.column==0);
    assert(drawing.undo() && drawing.redo());
    drawing.change_goal(Runtime::MapGoal::ClearInitial);assert(drawing.map.goal==Runtime::MapGoal::ClearInitial);
    assert(drawing.undo() && drawing.map.goal==Runtime::MapGoal::Disabled);
    assert(drawing.redo() && drawing.map.goal==Runtime::MapGoal::ClearInitial);
}
struct Fixture {
    std::unique_ptr<Runtime::SessionState> owned=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*owned,services};
    Input::Services input;Replay::ReplaySessionState replay;UserConfig::Settings user;
    Fixture() {context.attach_input(input);context.attach_replay(replay);context.attach_user_settings(user);Input::refresh_bindings(input,UserConfig::default_key_codes());input.keyStateProvider=[](int){return false;};}
    void initialize(const MapDefinition& map) {
        context.config=Runtime::normalize_user_config(user,Runtime::GameMode::Zen);Runtime::apply_map_config(context.config,map);
        App::InitDetail::reset_player_board(owned->player,context.config);Engine::initialize_board(context,owned->player);
    }
};
void map_racing_goals() {
    using Runtime::MapGoal;
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    const auto domino=Piece::types_for_rotation_system(Runtime::RotationSystem::M123)[1];
    for(const auto goal:{MapGoal::Disabled,MapGoal::ClearInitial,MapGoal::AllClear}) {
        Fixture f;MapDefinition map;Runtime::resize_map(map,4,6);map.rows.back()="GG  ";
        map.generator=MapGenerator::CustomSequence;map.goal=MapGoal::AllClear;
        map.sequence={types[3],domino,types[0],types[5]};
        f.user.mapGoal=static_cast<int>(goal);f.initialize(map);
        auto& player=f.owned->player;
        assert(f.context.config.mode.mapGoal==goal); // Editor default must not replace the player's current choice.
        f.context.config.mode.goal=goal==MapGoal::Disabled ? Runtime::GameGoal::Lines :
            goal==MapGoal::ClearInitial ? Runtime::GameGoal::InitialCells : Runtime::GameGoal::AllClear;
        f.context.config.mode.racingDistance=goal==MapGoal::Disabled ? 1 : 0;
        f.context.config.attackRules.perfectClearEnabled=false;
        int played=0;std::ostringstream progress;auto* output=std::cout.rdbuf(progress.rdbuf());
        Render::print_message(f.context,player);
        const auto outcome=Engine::run_piece_loop(f.context,player,[&](Runtime::Playfield& field,Piece::Definition piece,Runtime::PlayerSessionState& p,bool) {
            ++played;assert(played<=2);
            const auto& cells=piece.cells(Runtime::Rotation::from_index(0));int maxRow=-100,minCol=100;
            for(const auto& cell:cells) {maxRow=std::max(maxRow,cell[0]);minCol=std::min(minCol,cell[1]);}
            Piece::Pose pose{{field.height()-1-maxRow,(played==1 ? 2 : 0)-minCol},Runtime::Rotation::from_index(0)};
            const auto result=Engine::resolve_locked_piece(f.context,field,piece,p,pose,{});
            assert(result.clear.clearedLines==1);
            assert(Engine::run_locked_line_clear_phase(f.context,field,piece,p,result,[]{return Rules::PlayResult::ContinueFrame;})==Rules::PlayResult::ContinueFrame);
            ++p.gameplay.scoring.placedBlocks;p.gameplay.scoring.totalLines+=result.clear.clearedLines;
            if(result.allClear) ++p.gameplay.scoring.allClearCount;
            return Rules::PlayResult::Locked;
        });
        assert(outcome==Engine::GameLoopExit::GoalReached && played==(goal==MapGoal::AllClear ? 2 : 1));
        std::cout.rdbuf(output);
        if(goal==MapGoal::ClearInitial) {
            Test::TerminalScreen screen;screen.feed(progress.str());assert(screen.position_of("2/2 MAP CELLS").first!=0);
        }
        assert(player.pieces.bag.size()==map.sequence.size()-played);
        assert(player.gameplay.playfield.field.remaining_initial_cells()==0);
        assert(player.gameplay.scoring.mapAllClearReached==(goal==MapGoal::AllClear));
        if(goal!=MapGoal::AllClear) assert(player.gameplay.playfield.field.cell(5,2).occupied());
        const auto encoded=Replay::encode_config_prefix(f.context.config)+"}";
        Runtime::RuntimeConfig decoded;Replay::ConfigPrefixResult prefix;
        assert(Replay::decode_config_prefix(encoded,decoded,prefix) && decoded.mode.mapGoal==goal);
        assert(decoded.initialMap->goal==MapGoal::AllClear);
    }
    Fixture f;MapDefinition map;Runtime::resize_map(map,4,6);map.rows.back()="GI  ";
    map.generator=MapGenerator::CustomSequence;map.sequence={types[0]};f.user.mapGoal=1;f.initialize(map);
    auto& field=f.owned->player.gameplay.playfield.field;
    assert(field.remaining_initial_cells()==2);
    Engine::GarbageService::initialize(f.owned->player,{});
    Engine::GarbageService::add_garbage(f.context,f.owned->player,field,1,false,Runtime::BoardCell::garbage(),false);
    assert(field.remaining_initial_cells()==2 && field.initial_cell(4,0) && !field.initial_cell(5,0));
    field.expand(6,8,1);assert(field.remaining_initial_cells()==2 && field.initial_cell(6,1));
    auto copy=field;field=copy;assert(field.remaining_initial_cells()==2);
    Engine::begin_undo_history(f.context);
    field.set_cell(6,1,Runtime::BoardCell::empty());f.owned->player.gameplay.scoring.placedBlocks=1;
    Engine::capture_undo_snapshot(f.context);
    assert(Engine::restore_previous_undo_snapshot(f.context) && field.remaining_initial_cells()==2);
    f.context.config.mode.mode=Runtime::GameMode::Racing;
    const auto failed=Engine::run_piece_loop(f.context,f.owned->player,[](Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState&,bool){return Rules::PlayResult::Locked;});
    assert(failed==Engine::GameLoopExit::Dead); // An exhausted sequence did not clear its map target.
    f.user.mapGoal=0;f.initialize(map);f.context.config.mode.mode=Runtime::GameMode::Racing;
    assert(Engine::run_piece_loop(f.context,f.owned->player,[](Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState&,bool){return Rules::PlayResult::Locked;})==Engine::GameLoopExit::Dead);
    f.user.mapGoal=1;
    map.rows=std::vector<std::string>(6,std::string(4,' '));f.initialize(map);f.context.config.mode.mode=Runtime::GameMode::Racing;
    assert(Engine::run_piece_loop(f.context,f.owned->player,[](Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState&,bool){assert(false);return Rules::PlayResult::Locked;})==Engine::GameLoopExit::GoalReached);
}
const App::Settings::Model::SettingItem& rotation_item(const App::Settings::Model::SettingNode& tree) {
    if(tree.item && tree.item->id==UserConfig::SettingId::RotationSystem) return *tree.item;
    for(const auto& child:tree.children) {
        try {return rotation_item(child);} catch(const std::out_of_range&) {}
    }
    throw std::out_of_range("rotation item");
}
void custom_rotation(MapFile& file) {
    Fixture fixture;MapDefinition map;map.name="旋转测试";map.generator=MapGenerator::CustomSequence;
    const auto srs=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    const auto ars=Piece::types_for_rotation_system(Runtime::RotationSystem::Ars);
    const auto special=Piece::april_fools_piece(69);
    map.sequence={srs[0],ars[5],special};
    UserConfig::Maps::SaveUndo undo;assert(UserConfig::Maps::save(map,undo));fixture.user.mapId=map.id;
    auto tree=App::Settings::Model::make_tree(fixture.user,false,0);
    auto item=rotation_item(tree);
    assert(item.effective && item.choice.labels.back()=="Custom" && item.choice.values.back()==-1);
    assert(App::Settings::Model::choice_value(item)==-1);
    fixture.initialize(map);assert(fixture.context.config.initialMap->sequence==map.sequence);
    App::Settings::Model::apply_choice(item,static_cast<int>(Runtime::RotationSystem::Ars));
    assert(fixture.user.rotationSystem==0 && fixture.user.mapRotationSystem==static_cast<int>(Runtime::RotationSystem::Ars));
    fixture.initialize(map);
    assert(fixture.context.config.initialMap->sequence==std::vector<Piece::Type>({ars[0],ars[5],special}));
    assert(fixture.owned->player.pieces.bag.front().type==ars[0]);
    assert(UserConfig::Maps::find(map.id)->sequence==map.sequence);
    fixture.user.persistencePath=(file.directory/"rotation.ini").string();assert(UserConfig::save(fixture.user));
    UserConfig::Settings loaded;UserConfig::load(loaded,fixture.user.persistencePath);
    assert(loaded.mapRotationSystem==fixture.user.mapRotationSystem);
    const auto recorded=Replay::encode_config_prefix(fixture.context.config)+"}";
    Runtime::RuntimeConfig replay;Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(recorded,replay,prefix));
    assert(replay.mapRotationSystem==fixture.user.mapRotationSystem && replay.initialMap->sequence==fixture.context.config.initialMap->sequence);
    for(const auto& system:Runtime::kRotationSystems) {
        App::Settings::Model::apply_choice(item,static_cast<int>(system.value));fixture.initialize(map);
        const auto target=Piece::types_for_rotation_system(system.value);
        assert(fixture.context.config.initialMap->sequence==std::vector<Piece::Type>({target[0],target[5],special}));
    }
    App::Settings::Model::apply_choice(item,-1);fixture.initialize(map);
    assert(fixture.context.config.initialMap->sequence==map.sequence);
    UserConfig::Settings ordinary;
    auto both=App::Settings::Model::make_both_tree(ordinary,fixture.user);
    auto joint=rotation_item(both);assert(joint.choice.values.back()==-1);
    App::Settings::Model::apply_choice(joint,static_cast<int>(Runtime::RotationSystem::Nrs));
    assert(ordinary.rotationSystem==static_cast<int>(Runtime::RotationSystem::Nrs));
    assert(fixture.user.mapRotationSystem==static_cast<int>(Runtime::RotationSystem::Nrs));
    fixture.user.mapId=0;tree=App::Settings::Model::make_tree(fixture.user,false,0);
    assert(rotation_item(tree).choice.values.size()==12 && App::Settings::Model::choice_value(rotation_item(tree))==0);
    assert(UserConfig::Maps::remove(map.id));
}
void editor_layout() {
    Fixture fixture;App::MapEditorState state;
    for(const auto language:{App::UiText::Language::English,App::UiText::Language::Chinese}) {
        App::UiText::set_language(language);
        for(int style:{0,1}) for(const auto size:std::vector<Render::TerminalSize>{{96,30},{44,20},{43,20},{24,14},{16,10},{8,6},{1,1}}) {
            fixture.context.config.display.squareStyle=style;
            for(int screen:{0,1,2}) for(bool sequence:{false,true}) {
                state.optionsOpen=screen==1;state.exitPrompt=screen==2;
                state.sequenceFocus=sequence;state.map.generator=MapGenerator::CustomSequence;
                state.map.rotationSystem=Runtime::RotationSystem::AprilFools2026;
                state.map.sequence=std::vector<Piece::Type>(300,Piece::april_fools_piece(69));state.caret=300;state.candidate=76;
                const auto frame=App::map_editor_frame(fixture.context,state,size.columns,size.rows);
                assert(frame.size()<=static_cast<std::size_t>(size.rows));
                std::string text;
                for(const auto& row:frame) {assert(row.size()<=static_cast<std::size_t>(size.columns-1));for(const auto& cell:row) text+=cell.glyph;text+='\n';}
                assert(text.find("[]")==std::string::npos);
                assert(text.find("Undo edit")==std::string::npos && text.find("Redo edit")==std::string::npos && text.find("Undo last save")==std::string::npos);
                if(screen==0 && size.columns>=24 && size.rows>=14) {
                    assert(text.find(sequence ? "⓪" : "CANDIDATES")==std::string::npos);
                    if(sequence) assert(text.find("…")!=std::string::npos);
                }
            }
        }
    }
    App::UiText::set_language(App::UiText::Language::English);state.sequenceFocus=false;state.optionsOpen=false;state.exitPrompt=false;
    auto frame=App::map_editor_frame(fixture.context,state,96,30);
    std::string hints;
    for(const auto& row:frame) for(const auto& cell:row) hints+=cell.glyph;
    assert(hints.find("P Options")!=std::string::npos && hints.find("Space Options")==std::string::npos);
    const auto locate=[&](const std::string& glyph) {
        for(const auto& row:frame) for(const auto& cell:row) if(cell.glyph==glyph) return cell;
        throw std::out_of_range(glyph);
    };
    assert(locate("⓪").background==Render::ui_colors(Render::ColorRole::Text,false).background);
    assert(locate("①").background==Render::board_cell_colors(Runtime::map_cell('G'),false).foreground);
    assert(locate("⑦").background==Render::board_cell_colors(Runtime::map_cell('T'),false).foreground);
    assert(locate("◇").background==App::TerminalCanvas::Color({56,34,16}));
    state.sequenceFocus=true;state.map.rotationSystem=Runtime::RotationSystem::SrsPlus;state.candidate=0;
    state.detailPiece=Piece::types_for_rotation_system(Runtime::RotationSystem::Ars).front();
    frame=App::map_editor_frame(fixture.context,state,96,30);
    std::string left;int detailRow=-1;
    for(std::size_t row=0;row<frame.size();++row) {
        std::string line;for(std::size_t col=0;col<std::min<std::size_t>(24,frame[row].size());++col) line+=frame[row][col].glyph;
        if(line.find("Piece")!=std::string::npos) detailRow=static_cast<int>(row);
        left+=line+'\n';
    }
    assert(detailRow>8 && left.find("System: ARS")!=std::string::npos && left.find("Name: "+std::string(Piece::metadata(*state.detailPiece).name))!=std::string::npos);
    int shapeCells=0;for(std::size_t row=detailRow+1;row<26;++row) for(std::size_t col=0;col<std::min<std::size_t>(24,frame[row].size());++col) if(frame[row][col].glyph=="█") ++shapeCells;
    assert(shapeCells>0);
    state.optionsOpen=true;state.option=0;
    const auto menu_text=[&] {
        const auto menu=App::map_editor_frame(fixture.context,state,96,30);std::string text;
        for(const auto& row:menu) {for(const auto& cell:row) text+=cell.glyph;text+='\n';}return text;
    };
    state.map.generator=MapGenerator::RotationSystem;state.map.rotationSystem=Runtime::RotationSystem::SrsPlus;
    auto text=menu_text();assert(text.find("MAP EDITOR / EDITOR OPTIONS")!=std::string::npos && text.find("Rename =")!=std::string::npos);
    assert(text.find("  Rotation System =")!=std::string::npos && text.find("Initial floor")==std::string::npos);
    frame=App::map_editor_frame(fixture.context,state,96,30);
    bool aligned=false;
    for(const auto& row:frame) if(row.size()>8 && row[6].glyph=="R" && row[7].glyph=="o" && row[8].glyph=="t") {aligned=true;break;}
    assert(aligned);
    state.map.rotationSystem=Runtime::RotationSystem::AprilFools2026;
    text=menu_text();assert(text.find("    Initial floor =")!=std::string::npos);
    state.map.generator=MapGenerator::CustomSequence;
    text=menu_text();assert(text.find("Piece library =")!=std::string::npos && text.find("Initial floor")==std::string::npos);
    state.exitPrompt=true;state.exitChoice=0;frame=App::map_editor_frame(fixture.context,state,96,30);
    assert(frame.size()>4 && std::any_of(frame[4].begin(),frame[4].end(),[](const auto& cell){return cell.glyph=="U";}));
    assert(locate("?").color==App::TerminalCanvas::Color({237,109,0}));
    text=menu_text();assert(text.find("Save before exiting?")!=std::string::npos);
    assert(text.find("> Save and exit")!=std::string::npos && text.find("Discard and exit")!=std::string::npos && text.find("Keep editing")!=std::string::npos);
}
void finite_sequences() {
    const auto types=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus);
    for(int holds:{0,1,2}) {
        Fixture fixture;MapDefinition map;map.generator=MapGenerator::CustomSequence;map.sequence={types[0],types[5],types[3]};
        fixture.initialize(map);auto& player=fixture.owned->player;
        fixture.context.config.mode.mode=Runtime::GameMode::Zen;
        assert(player.pieces.nextPreview.size()==3);
        std::vector<Piece::Type> played;int holdCount=0;
        const auto outcome=Engine::run_piece_loop(fixture.context,player,[&](Runtime::Playfield&,Piece::Definition piece,Runtime::PlayerSessionState& p,bool) {
            played.push_back(piece.type());assert(played.size()<8);
            assert(p.pieces.nextPreview.size()<3);
            if(holdCount<holds) {assert(Engine::hold_replacement_available(fixture.context,p));++holdCount;return Rules::PlayResult::Hold;}
            if(p.pieces.bag.size()==1 && !p.gameplay.hold.piece.valid()) assert(!Engine::hold_replacement_available(fixture.context,p));
            ++p.gameplay.scoring.placedBlocks;return Rules::PlayResult::Locked;
        });
        assert(outcome==Engine::GameLoopExit::GoalReached && player.pieces.bag.empty() && !player.gameplay.hold.piece.valid());
        assert(player.gameplay.scoring.placedBlocks==3);
        assert(played.back()==(holds==0 ? types[3] : holds==1 ? types[0] : types[5]));
    }
    Fixture empty;MapDefinition map;map.generator=MapGenerator::CustomSequence;empty.initialize(map);
    assert(Engine::run_piece_loop(empty.context,empty.owned->player,[](Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState&,bool){assert(false);return Rules::PlayResult::Locked;})==Engine::GameLoopExit::GoalReached);
    Fixture last;map.sequence={types.front()};last.initialize(map);
    Runtime::PlayerSessionState remote;remote.gameplay.playfield.width=10;remote.gameplay.playfield.visibleHeight=20;
    remote.gameplay.playfield.nextCount=1;remote.pieces.bag={{types.front(),false}};
    Engine::initialize_board(last.context,remote,false);
    assert(remote.pieces.nextPreview.size()==1);
    remote.gameplay.playfield.nextCount=2;bool rejected=false;
    try {Engine::initialize_board(last.context,remote,false);}catch(const std::logic_error&) {rejected=true;}
    assert(rejected);
    assert(Engine::run_piece_loop(last.context,last.owned->player,[](Runtime::Playfield&,Piece::Definition,Runtime::PlayerSessionState& player,bool){++player.gameplay.scoring.placedBlocks;return Rules::PlayResult::Locked;})==Engine::GameLoopExit::GoalReached);
}
void startup_and_replay(MapFile&) {
    Fixture fixture;MapDefinition map;map.name="初始地图";Runtime::resize_map(map,4,8);map.rows.back()="G IT";
    map.generator=MapGenerator::CustomSequence;map.sequence={Piece::Type::from_index(0),Piece::april_fools_piece(55)};
    UserConfig::Maps::SaveUndo undo;assert(UserConfig::Maps::save(map,undo));
    fixture.user.mapId=map.id;fixture.user.fieldWidth=17;fixture.user.fieldHeight=23;fixture.user.gameMode=3;
    fixture.user.qpMessy=2;fixture.user.qpDoubleHole=2;fixture.user.qpFool=3;fixture.user.qpSnowball=1;
    App::initialize_game(fixture.context);
    const auto& player=fixture.owned->player;assert(player.gameplay.playfield.width==4 && player.gameplay.playfield.visibleHeight==8);
    assert(player.pieces.bag.size()==2 && player.pieces.bag[0].type==map.sequence[0] && player.pieces.bag[1].type==map.sequence[1]);
    Engine::initialize_quick_play(fixture.context,fixture.owned->player);
    assert(player.gameplay.playfield.field.cell(7,0)==Runtime::BoardCell::garbage());
    assert(player.gameplay.playfield.field.cell(7,2)==Runtime::BoardCell::piece(0));
    for(int row=0;row<7;++row) for(int col=0;col<4;++col) assert(player.gameplay.playfield.field.cell(row,col).empty_cell());
    const auto recorded=Replay::encode_config_prefix(fixture.context.config)+"}";
    assert(UserConfig::Maps::remove(map.id));
    bool unavailable=false;
    try {App::initialize_game(fixture.context);} catch(const App::MapUnavailable& error) {unavailable=&error.context==&fixture.context;}
    assert(unavailable);
    Runtime::RuntimeConfig decoded;Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix(recorded,decoded,prefix) && decoded.initialMap && *decoded.initialMap==map);
    fixture.context.config=decoded;App::InitDetail::reset_player_board(fixture.owned->player,decoded);Engine::initialize_board(fixture.context,fixture.owned->player);
    assert(fixture.owned->player.pieces.bag.size()==2 && fixture.owned->player.gameplay.playfield.field.cell(7,3)==Runtime::BoardCell::piece(5));
    assert(!Autoplay::availability(decoded,Runtime::AutoplayMode::ChatGPT));
    assert(!Replay::decode_config_prefix("{{Map,broken}}",decoded,prefix));
    MapDefinition fixed;fixed.rotationSystem=Runtime::RotationSystem::AprilFools2026;fixed.foolFloor=3;
    fixture.initialize(fixed);assert(fixture.owned->player.pieces.allowedPieces.size()==28);
    for(const auto type:fixture.owned->player.pieces.allowedPieces) if(Piece::metadata(type).rotationSystem==Runtime::RotationSystem::AprilFools2026) assert(Piece::metadata(type).familyIndex/7+1<=3);
}
}
int main() {
    App::UiText::set_language(App::UiText::Language::English);
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    MapFile file;codec_and_storage(file);editor_operations();map_racing_goals();custom_rotation(file);editor_layout();finite_sequences();startup_and_replay(file);
    std::cout.rdbuf(previous);std::cerr<<"map_editor_test: OK\n";
}
