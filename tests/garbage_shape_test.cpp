#include "tetr/config/normalization.h"
#include "tetr/engine/garbage_service.h"
#include "tetr/replay/config_codec.h"
#include "tetr/replay/session_state.h"
#include "tetr/rules/garbage_generation.h"
#include "tetr/runtime/game_context.h"
#include "../src/app/settings_model.h"
#include "../src/app/ui_text.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
using Runtime::GameMode;
using Runtime::GarbageMode;
using Runtime::BoardCell;
struct Fixture {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Replay::ReplaySessionState replay;
    Runtime::GameContext context{*session,services};
    Fixture(const Runtime::RuntimeConfig& config,bool interoperable=false) {
        context.config=config;
        // This fixture isolates shape generation; row timing has a separate
        // live-clock/reference test, including normalized Quick Play defaults.
        context.config.garbage.riseInterval={};
        context.attach_replay(replay);
        auto& player=session->player;
        auto& field=player.gameplay.playfield;
        field.width=config.field.width;
        field.visibleHeight=field.activeHeight=config.field.height;
        field.field.initialize(field.width,field.activeHeight);
        player.presentation.margin=20;
        player.garbage.random.seed(817);
        if(interoperable) player.garbage.tetrioRandom.emplace(817);
        Engine::GarbageService::initialize(player,config.garbage.holePattern);
    }
    std::vector<BoardCell> row(int index) const {
        std::vector<BoardCell> result;
        const auto& field=session->player.gameplay.playfield;
        for(int col=0;col<field.width;++col) result.push_back(field.field.cell(index,col));
        return result;
    }
    void add(int height,bool buffered) {
        auto& player=session->player;
        auto& field=player.gameplay.playfield.field;
        if(buffered) {
            Engine::GarbageService::add_buffer(context,player,field,height,true,height,true);
            assert(Engine::GarbageService::raise_ready_buffer(context,player,field,false)==height);
        } else Engine::GarbageService::add_garbage(context,player,field,height,false,BoardCell::garbage(),false);
    }
};
void ordinary_sources() {
    for(auto game:{GameMode::Zen,GameMode::QuickPlay,GameMode::Multiplayer,GameMode::LocalVersus})
    for(int garbage=0;garbage<=5;++garbage) for(int shape:{1,2,3}) {
        UserConfig::Settings user;
        user.garbageMode=garbage;
        user.cheesePattern=shape;
        user.cheeseDensity=1;
        user.holeCount=-3;
        const auto config=Runtime::normalize_user_config(user,game);
        assert(config.garbage.cheeseModel==shape && !config.garbage.legacyShapeRules);
        if(shape==1) assert(config.garbage.holePattern==Runtime::GarbageHolePattern::contiguous(3));
        if(shape==2) assert(config.garbage.cheeseMessiness==1);
        Fixture f(config);
        const bool buffered=config.garbage.mode==GarbageMode::Multiplayer ||
            config.garbage.mode==GarbageMode::Backfire || config.garbage.mode==GarbageMode::Survival ||
            config.garbage.mode==GarbageMode::QuickPlay;
        f.add(3,buffered);
        const int bottom=config.field.height-1;
        for(int row=bottom-2;row<=bottom;++row) {
            const auto cells=f.row(row);
            const int empty=std::count(cells.begin(),cells.end(),BoardCell::empty());
            assert(empty==(shape==1 ? 3 : shape==2 ? 1 : 5));
            if(shape==1) {
                const auto first=std::find(cells.begin(),cells.end(),BoardCell::empty());
                assert(std::all_of(first,first+3,[](auto cell){return cell.empty_cell();}));
            }
            if(shape==3) for(int col=1;col<10;++col)
                assert(cells[col].occupied()!=cells[col-1].occupied());
        }
        if(shape==3) for(int col=0;col<10;++col)
            assert(f.row(bottom)[col].occupied()!=f.row(bottom-1)[col].occupied());
    }
    for(double density:{0.0,1.0}) {
        UserConfig::Settings user;
        user.cheesePattern=2; user.cheeseDensity=density;
        auto config=Runtime::normalize_user_config(user,GameMode::Multiplayer);
        Fixture f(config,true); // The interoperable RNG must not bypass custom shapes.
        f.add(2,true);
        const auto cells=f.row(19);
        assert(std::count(cells.begin(),cells.end(),BoardCell::empty())==(density==0 ? 10 : 1));
    }
}
void special_mechanisms() {
    for(int dh:{0,1,2}) for(int nh:{0,2}) {
        if(dh==0 && nh==0) continue;
        std::vector<std::vector<BoardCell>> expected;
        for(int shape:{1,2,3}) {
            UserConfig::Settings user;
            user.cheesePattern=-shape; user.cheeseDensity=0; user.holeCount=-7;
            user.qpDoubleHole=dh; user.qpNoHold=nh;
            const auto config=Runtime::normalize_user_config(user,GameMode::QuickPlay);
            assert(config.garbage.cheeseModel==-1); // Preserve the finite-layer bit.
            Fixture f(config);
            f.add(4,true);
            std::vector<std::vector<BoardCell>> actual;
            for(int row=16;row<20;++row) actual.push_back(f.row(row));
            if(expected.empty()) expected=actual;
            else assert(actual==expected);
            for(const auto& cells:actual) {
                const int holes=std::count(cells.begin(),cells.end(),BoardCell::empty());
                assert(dh==2 ? holes==6 || holes==7 : nh==2 ? holes==2 || (dh==1 && holes==3) : holes==1 || holes==2);
            }
        }
    }
    for(auto cell:{BoardCell::solid_garbage(),BoardCell::timed_solid()}) {
        std::vector<BoardCell> expected;
        for(int shape:{1,2,3}) {
            UserConfig::Settings user; user.cheesePattern=shape; user.cheeseDensity=0;
            Fixture f(Runtime::normalize_user_config(user,GameMode::Multiplayer));
            auto& player=f.session->player;
            Engine::GarbageService::add_solid_garbage(f.context,player,player.gameplay.playfield.field,1,cell);
            const auto cells=f.row(19);
            if(expected.empty()) expected=cells;
            else assert(cells==expected);
            assert(std::none_of(cells.begin(),cells.end(),[](auto c){return c.empty_cell();}));
        }
    }
}
void legacy_and_expansion() {
    Runtime::RuntimeConfig oldConfig;
    Replay::ConfigPrefixResult prefix;
    assert(Replay::decode_config_prefix("{{GarbageModel,11}{CheeseModel,3}{HoleNum,1}}",oldConfig,prefix));
    assert(oldConfig.garbage.legacyShapeRules);
    Fixture old(oldConfig);
    old.add(2,false);
    const auto oldRow=old.row(19);
    assert(std::count(oldRow.begin(),oldRow.end(),BoardCell::empty())==1);
    UserConfig::Settings user; user.cheesePattern=3;
    auto config=Runtime::normalize_user_config(user,GameMode::Multiplayer);
    Runtime::RuntimeConfig decoded;
    assert(Replay::decode_config_prefix(Replay::encode_config_prefix(config)+"}",decoded,prefix));
    assert(!decoded.garbage.legacyShapeRules && decoded.garbage.cheeseModel==3);
    Fixture original(config),replayed(decoded);
    original.add(3,false); replayed.add(3,false);
    assert(original.row(19)==replayed.row(19));
    Rules::GarbageGenerationConfig generation;
    generation.width=6; generation.garbageMode=GarbageMode::Multiplayer; generation.cheeseModel=3;
    std::mt19937 random(1);
    const std::vector<BoardCell> checker{BoardCell::garbage(),BoardCell::empty(),BoardCell::garbage(),BoardCell::empty()};
    const auto expanded=Rules::expand_garbage_row(generation,checker,1,random);
    for(int col=1;col<6;++col) assert(expanded[col].occupied()!=expanded[col-1].occupied());
}
const App::Settings::Model::SettingNode& child(const App::Settings::Model::SettingNode& node,const std::string& key) {
    const auto result=std::find_if(node.children.begin(),node.children.end(),[&](const auto& n){return n.key==key;});
    assert(result!=node.children.end()); return *result;
}
void menus() {
    using namespace App::Settings::Model;
    UserConfig::Settings user; user.garbageMode=1; user.cheesePattern=2;
    auto tree=make_tree(user,false,0);
    const auto& model=child(child(child(tree,"MODEL"),"mode-garbage"),"cheese");
    const auto& game=child(child(child(tree,"GAME"),"garbage-rules"),"cheese");
    assert(model.title=="Garbage Shape" && game.title==model.title);
    assert(model.children[0].title=="Garbage shape" && game.children[0].item->effective);
    assert(&std::get<std::reference_wrapper<int>>(model.children[0].item->value).get()==
           &std::get<std::reference_wrapper<int>>(game.children[0].item->value).get());
    App::UiText::set_language(App::UiText::Language::Chinese);
    assert(App::UiText::tr(model.title)=="垃圾行形状" && App::UiText::tr(model.children[0].title)=="垃圾行形状");
    user.gameMode=3; user.qpDoubleHole=1;
    tree=make_tree(user,false,0);
    assert(!child(child(child(tree,"GAME"),"garbage-rules"),"cheese").children[0].item->effective);
    user.gameMode=0; user.garbageMode=3;
    tree=make_tree(user,false,0);
    const auto& mode=child(child(tree,"MODEL"),"mode-garbage");
    const auto refill=std::find_if(mode.children.begin(),mode.children.end(),[](const auto& node) {
        return node.item && node.item->part==ValuePart::CheeseRefill;
    });
    assert(refill!=mode.children.end() && refill->item->effective);
}
}
int main() {
    std::ostringstream output;
    auto* original=std::cout.rdbuf(output.rdbuf());
    ordinary_sources(); special_mechanisms(); legacy_and_expansion(); menus();
    std::cout.rdbuf(original);
}
