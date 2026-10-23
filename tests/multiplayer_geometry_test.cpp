#include "tetr/engine/multiplayer_scene.h"
#include "tetr/engine/game_terminal_layout.h"
#include "../src/engine/placement_presentation.h"
#include "tetr/rules/placement_resolution.h"
#include "tetr/config/normalization.h"
#include "tetr/config/user_config.h"
#include "tetr/network/network_services.h"
#include "tetr/network/transport.h"
#include "tetr/render/board_renderer.h"
#include "tetr/render/piece_renderer.h"
#include "tetr/runtime/piece.h"
#include "tetr/util/terminal_text.h"
#include "tetr/runtime/game_context.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace TetrTests::multiplayer_geometry_test {
namespace {
struct Scene {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;
    Runtime::GameContext context{*session,services};
    Network::NetworkServices network;
    Scene(int count) {
        context.attach_network(network);
        context.config=Runtime::normalize_user_config(UserConfig::Settings{},Runtime::GameMode::Multiplayer);
        context.pause.started=services.clock.now();context.pause.paused=true;
        network.multiplayerRoom=true;network.roomPlayerIndex=0;network.matchEpoch=1;
        network.matchPhase=Network::RoomMatchPhase::Playing;
        for(int slot=0;slot<count;++slot) {
            network.roomMembers.push_back({slot,"PLAYER-"+std::to_string(slot+1),slot==0,Network::PlayerRole::Battle,true,true,true});
            Network::LobbyPlayerSetup setup;setup.width=10;setup.visibleHeight=20;setup.nextCount=3;
            setup.holdEnabled=true;setup.blockMaxWidth=3;setup.blockMaxHeight=2;
            network.playerSetups[slot]=setup;
        }
        Engine::initialize_multiplayer_scene(context);
        for(int slot=0;slot<count;++slot) resize(slot,10,20);
    }
    Runtime::PlayerSessionState& player(int slot) {
        return slot==0?session->player:session->roomScene.players.at(slot);
    }
    void resize(int slot,int width,int height) {
        auto& p=player(slot);p.gameplay.playfield.width=width;p.gameplay.playfield.visibleHeight=height;
        p.gameplay.playfield.activeHeight=height;p.gameplay.playfield.nextCount=3;p.gameplay.playfield.holdEnabled=true;
        p.presentation.blockMaxWidth=3;p.presentation.blockMaxHeight=2;
        p.gameplay.playfield.field.initialize(width,height);
        p.pieces.nextPreview={Piece::Type{0},Piece::Type{1},Piece::Type{2}};
        for(int row=std::max(0,height-6);row<height;++row)for(int col=0;col<width;++col)
            if(col!=width/2)p.gameplay.playfield.field.set_cell(row,col,Runtime::BoardCell::piece((slot+row+col)%7));
    }
    Test::TerminalScreen paint(Render::TerminalSize size,bool force=true,Test::TerminalScreen screen={},bool drain=true) {
        std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());
        const int rounds=force || !drain?1:std::max(1,int(session->roomScene.miniSlots.size()));
        for(int i=0;i<rounds;++i) {
            session->roomScene.lastPaint=context.real_now()-Runtime::Duration::from_seconds(1);
            session->roomScene.lastMiniPaint=context.real_now()-Runtime::Duration::from_seconds(1);
            Engine::draw_multiplayer_scene(context,force,size);
        }
        std::cout.rdbuf(old);screen.feed(output.str());return screen;
    }
};
void same(const Test::TerminalScreen& a,const Test::TerminalScreen& b,Render::TerminalSize size) {
    for(int row=1;row<=size.rows;++row)for(int col=1;col<=size.columns;++col) {
        const auto x=a.at(row,col),y=b.at(row,col);
        assert(x.glyph==y.glyph && x.background==y.background);
        if(x.glyph!=" " && !x.glyph.empty())assert(x.foreground==y.foreground);
    }
}
void save(const Test::TerminalScreen& screen,const std::filesystem::path& path,Render::TerminalSize size) {
    std::ofstream output(path);output<<"{\"columns\":"<<size.columns<<",\"rows\":"<<size.rows<<",\"cells\":[";
    for(int row=1;row<=size.rows;++row)for(int col=1;col<=size.columns;++col) {
        if(row>1 || col>1)output<<',';const auto cell=screen.at(row,col);
        output<<'['<<col-1<<','<<row-1<<','<<std::quoted(cell.glyph)<<",["<<cell.foreground[0]<<','<<cell.foreground[1]<<','<<cell.foreground[2]
            <<"],["<<cell.background[0]<<','<<cell.background[1]<<','<<cell.background[2]<<"]]";
    }
    output<<"]}";
}
void bounds(Scene& scene,const Test::TerminalScreen& screen,Render::TerminalSize size) {
    assert(screen.rows()<=size.rows && screen.columns()<=size.columns);
    const auto& layout=scene.session->roomScene;
    for(const auto& [slot,g]:layout.miniLayouts) {
        assert(g.left>=0 && g.left+g.width+2<size.columns && g.top+g.height+2<size.rows);
        // Empty top rows have no horizontal cap. Hidden cells, if present,
        // occupy this same row instead of being cut at the visible height.
        for(int x=0;x<g.width+2;++x)assert(screen.at(g.top+1,g.left+x+1).glyph!="-");
        const auto label=Engine::compact_player_name(layout.usernames.at(slot),layout.miniCellWidth-1);
        const auto name=screen.position_of(label);
        assert(name.first==g.top+g.height+3);
        assert(screen.at(name.first,name.second).foreground==Render::ui_colors(Render::ColorRole::Accent,false).foreground);
        assert(std::abs(2*(name.second-1-g.left)+int(Util::TerminalText::width(label))-g.width-2)<=1);
        if(layout.miniSlots.size()<=std::size_t(layout.miniColumns))
            assert(g.top+g.height+1==scene.session->player.presentation.topMargin+scene.session->player.gameplay.playfield.visibleHeight);
    }
}
}
int run(int argc,char** argv) {
    const std::filesystem::path folder=argc>1?argv[1]:"";
    if(!folder.empty())std::filesystem::create_directories(folder);
    const auto capture=[&](Scene& scene,Render::TerminalSize size,const char* name) {
        auto screen=scene.paint(size);bounds(scene,screen,size);
        if(!folder.empty())save(screen,folder/(std::string(name)+".json"),size);
        return screen;
    };
    {
        Scene scene(16);const Render::TerminalSize size{140,45};
        scene.paint(size);auto& state=scene.session->roomScene;
        assert(state.miniSlots.size()>4);
        for(const int slot:state.miniSlots) {
            const auto& geometry=state.miniLayouts.at(slot);
            scene.network.peerMiniBoards[slot]=Network::capture_mini_board(scene.player(slot),geometry.width,geometry.height,1);
        }
        auto screen=scene.paint(size);
        const int slot=state.miniSlots.back();auto& frame=scene.network.peerMiniBoards.at(slot);
        frame.sequence=2;frame.cells.back()=Runtime::BoardCell::piece(6);
        scene.network.miniUpdateBudget=60;state.miniPaintCursor=0;
        state.lastMiniPaint=scene.context.real_now(); // No legacy refresh is due.
        state.lastPaint=scene.context.real_now()-Runtime::Duration::from_seconds(.02);
        std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());
        Engine::draw_multiplayer_scene(scene.context,false,size);std::cout.rdbuf(old);
        assert(state.paintedMiniSequences.at(slot)==2); // Skip unchanged peers and paint the fresh tail now.
        screen.feed(output.str());same(screen,scene.paint(size),size);
    }
    for(int height:{4,20,40,100})for(int holdHeight:{5,11}) {
        Scene scene(1);scene.resize(0,10,height);auto& p=scene.session->player;
        p.presentation.margin=38;p.presentation.topMargin=4;p.presentation.blockMaxHeight=holdHeight-3;
        scene.context.config.quickPlay.allSpin=Runtime::AllSpinMode::Extended;
        for(int index=0;index<12;++index)p.gameplay.quickPlay.operations.push_back({index,0});
        Rules::PlacementResult placement;placement.clear.clearedLines=1;
        std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());
        Engine::present_extended_all_spin_history(scene.context,p,placement);std::cout.rdbuf(old);
        Test::TerminalScreen screen;screen.feed(output.str());
        for(int row=0;row<3;++row) {
            const auto text=row==0?"I0 J0 L0 O0":row==1?"S0 T0 Z0 -0":"I1 J1 L1 O1";
            assert(screen.position_of(text).first==p.presentation.topMargin+Render::board_info_top_row(p)+4+row);
        }
    }
    if(!folder.empty()) {
        const auto drawInformation=[](Runtime::GameContext& context,Runtime::PlayerSessionState& p) {
            p.gameplay.scoring.placedBlocks=80;p.gameplay.scoring.totalAttack=50;
            p.timing.completedGameTime=Runtime::Duration::from_seconds(4);
            p.presentation.spike={8,Runtime::TimePoint::from_seconds(1)};
            Render::redraw_player_board(context,p);
            Render::print_spin(context,p,Piece::Type{0},false);Render::print_clear(context,p,1);
            Render::print_b2b(context,p,3);Render::print_combo(context,p,4);
            Render::print_clutch(context,p,1);Render::print_all_clear(context,p,true);
        };
        for(int height:{4,20,40,100}) {
            Scene scene(1);scene.resize(0,10,height);scene.session->roomScene.enabled=false;
            scene.context.config.mode.mode=Runtime::GameMode::Zen;
            scene.context.config.garbage.mode=Runtime::GarbageMode::None;
            const Render::TerminalSize size{120,height==100?120:60};
            Engine::center_game_boards(scene.context,size);
            std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());
            drawInformation(scene.context,scene.session->player);std::cout.rdbuf(old);
            Test::TerminalScreen screen;screen.feed(output.str());
            save(screen,folder/("information-area-height-"+std::to_string(height)+".json"),size);
        }
        for(const int goal:{0,1,2,3,4,5,6,7}) {
            Scene scene(1);scene.resize(0,10,20);scene.session->roomScene.enabled=false;
            scene.context.config.mode.mode=Runtime::GameMode::Zen;
            scene.context.config.mode.goal=static_cast<Runtime::GameGoal>(goal);
            scene.context.config.garbage.mode=Runtime::GarbageMode::None;
            if(goal==7) scene.context.config.initialMap=std::make_shared<Runtime::MapDefinition>();
            const Render::TerminalSize size{100,40};Engine::center_game_boards(scene.context,size);
            std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());
            drawInformation(scene.context,scene.session->player);std::cout.rdbuf(old);
            Test::TerminalScreen screen;screen.feed(output.str());
            save(screen,folder/("freeplay-goal-"+std::to_string(goal)+".json"),size);
        }
        Scene scene(2);scene.resize(0,10,40);scene.resize(1,10,20);
        scene.session->roomScene.enabled=false;scene.session->opponent=scene.player(1);
        scene.context.config.mode.mode=Runtime::GameMode::LocalVersus;
        const Render::TerminalSize size{180,80};Engine::center_game_boards(scene.context,size);
        std::ostringstream output;auto* old=std::cout.rdbuf(output.rdbuf());
        drawInformation(scene.context,scene.session->player);drawInformation(scene.context,scene.session->opponent);
        std::cout.rdbuf(old);Test::TerminalScreen screen;screen.feed(output.str());
        save(screen,folder/"local-versus-information-aligned-different-heights.json",size);
    }
    {
        Scene scene(8);const Render::TerminalSize size{160,50};
        Engine::position_multiplayer_scene(scene.context,size);const auto fixed=scene.session->player.presentation;
        scene.session->roomScene.survivors={0,1,2};Engine::position_multiplayer_scene(scene.context,size);
        assert(scene.session->player.presentation.margin==fixed.margin && scene.session->player.presentation.topMargin==fixed.topMargin);
        scene.session->roomScene.survivors={0,1};Engine::position_multiplayer_scene(scene.context,size);
        const auto final=scene.session->player.presentation;Engine::position_multiplayer_scene(scene.context,size);
        assert(scene.session->player.presentation.margin==final.margin && scene.session->player.presentation.topMargin==final.topMargin);
        scene.session->roomScene.survivors={0,1,2};scene.resize(1,6,12);scene.resize(2,12,28);
        auto screen=capture(scene,size,"3-mixed-heights-bottom-aligned");
        assert(scene.session->roomScene.miniColumns==2);
        const auto g=scene.session->roomScene.miniLayouts.at(1);
        scene.player(1).gameplay.playfield.field.set_cell(-1,0,Runtime::BoardCell::piece(0));
        screen=scene.paint(size,false,screen);
        assert(screen.at(g.top+1,g.left+2).glyph=="█");
        if(!folder.empty())save(screen,folder/"3-open-top-hidden-overflow.json",size);
        scene.player(1).gameplay.playfield.field.set_cell(-1,0,Runtime::BoardCell{});
        screen=scene.paint(size,false,screen);assert(screen.at(g.top+1,g.left+2).glyph==" ");
        same(screen,scene.paint(size),size);
        // An extreme peer compresses horizontally and clips vertically; the
        // main board stays at the same anchor and old cells are erased.
        scene.session->roomScene.survivors={0,1};scene.resize(1,100,100);
        screen=scene.paint(size,false,screen);
        assert(Engine::full_multiplayer_slots(scene.context,size).size()==1);
        assert(scene.session->player.presentation.margin==final.margin);
        same(screen,scene.paint(size),size);capture(scene,size,"2-extreme-opponent-100x100");
    }
    {
        Scene scene(2);scene.resize(1,30,28);
        for(const auto size:{Render::TerminalSize{80,40},{100,40},{120,40}}) {
            auto screen=capture(scene,size,("2-narrow-right-thumbnail-"+std::to_string(size.columns)).c_str());
            assert(Engine::full_multiplayer_slots(scene.context,size).size()==1);
            assert(scene.session->roomScene.miniSlots==std::vector<int>{1});
            const auto& mini=scene.session->roomScene.miniLayouts.at(1);
            assert(mini.width<scene.player(1).gameplay.playfield.width*2 && mini.width>0);
            assert(mini.left>scene.session->player.presentation.margin+20);
        }
        capture(scene,{320,80},"2-resized-restores-full-right");
        assert(Engine::full_multiplayer_slots(scene.context,Render::TerminalSize{320,80}).size()==2 && scene.session->roomScene.miniSlots.empty());
    }
    {
        for(const auto size:{Render::TerminalSize{120,40},{180,60},{240,80}}) {
            Scene scene(2);capture(scene,size,size.columns==120?"2-balanced-standard-duel":"2-balanced-spacious-duel");
            const auto& a=scene.session->player;const auto& b=scene.player(1);
            const double fieldsCenter=(a.presentation.margin+a.gameplay.playfield.width+b.presentation.margin+b.gameplay.playfield.width)/2.;
            assert(std::abs(fieldsCenter-size.columns/2.)<=2);
        }
    }
    {
        Scene scene(4);capture(scene,{120,40},"4-horizontal-thumbnails");
        assert(scene.session->roomScene.miniColumns==3 && scene.session->roomScene.miniSlots.size()==3);
        Scene wide(8);capture(wide,{240,60},"8-wide-horizontal-thumbnails");
        assert(wide.session->roomScene.miniColumns==7 && wide.session->roomScene.miniSlots.size()==7);
    }
    {
        Scene scene(2);const Render::TerminalSize size{180,80};scene.resize(0,10,40);scene.resize(1,10,20);
        capture(scene,size,"2-information-aligned-different-heights");
        const auto fixed=scene.session->player.presentation;
        const auto& a=scene.session->player;const auto& b=scene.player(1);
        assert(a.presentation.topMargin+Render::board_info_top_row(a)==b.presentation.topMargin+Render::board_info_top_row(b));
        scene.resize(1,10,60);capture(scene,size,"2-tall-opponent-information-aligned");
        assert(a.presentation.margin==fixed.margin && a.presentation.topMargin==fixed.topMargin);
        assert(a.presentation.topMargin+Render::board_info_top_row(a)==b.presentation.topMargin+Render::board_info_top_row(b));
        assert(b.presentation.topMargin>=0 && b.presentation.topMargin+61<size.rows);
        scene.resize(1,10,100);capture(scene,size,"2-oversized-opponent-only-clipped");
        assert(a.presentation.margin==fixed.margin && a.presentation.topMargin==fixed.topMargin);
        assert(b.presentation.topMargin<=0);
    }
    {
        Scene scene(8);const Render::TerminalSize size{120,40};auto before=capture(scene,size,"8-standard-open-top");
        assert(scene.session->roomScene.miniSlots.size()>std::size_t(scene.session->roomScene.miniColumns));
        const auto lower=scene.session->roomScene.miniSlots.at(scene.session->roomScene.miniColumns);
        const auto upper=scene.session->roomScene.miniSlots.front();
        const auto above=scene.session->roomScene.miniLayouts.at(upper),below=scene.session->roomScene.miniLayouts.at(lower);
        auto& field=scene.player(lower).gameplay.playfield.field;
        for(int row=-Runtime::kDisplayHiddenRows;row<0;++row)for(int col=0;col<10;++col)field.set_cell(row,col,Runtime::BoardCell::garbage());
        auto after=scene.paint(size,false,before);
        for(int row=above.top+1;row<below.ceiling+1;++row)for(int col=above.left+1;col<above.left+scene.session->roomScene.miniCellWidth;++col)
            assert(before.at(row,col).glyph==after.at(row,col).glyph);
        same(after,scene.paint(size),size);
        if(!folder.empty())save(after,folder/"8-overflow-clipped-by-upper-player.json",size);
    }
    {
        Scene scene(6);scene.resize(1,1,1);scene.resize(2,100,2);scene.resize(3,2,100);scene.resize(4,100,100);scene.resize(5,20,40);
        capture(scene,{160,50},"6-extreme-mixed-map-sizes");
        for(const auto& [slot,g]:scene.session->roomScene.miniLayouts) {
            const auto& p=scene.player(slot);const double scale=std::max(1.,double(p.gameplay.playfield.width*2)/(scene.session->roomScene.miniCellWidth-3));
            assert(g.width==std::max(1,int(std::floor(p.gameplay.playfield.width*2/scale))));
            assert(g.height==std::max(1,std::min(scene.session->roomScene.miniCellHeight-3,int(std::ceil(p.gameplay.playfield.visibleHeight/scale)))));
        }
        scene.resize(0,40,60);capture(scene,{320,100},"6-large-main-mixed-opponents");
    }
    {
        Scene scene(4);scene.resize(0,100,100);scene.resize(1,2,100);scene.resize(2,100,2);scene.resize(3,20,40);
        const Render::TerminalSize size{320,140};auto screen=capture(scene,size,"4-large-main-original-size");
        const auto& main=scene.session->player;
        const auto mainBounds=Render::board_horizontal_extent(scene.context,main,false);
        assert(mainBounds.left>=0 && mainBounds.right<scene.session->roomScene.galleryColumn);
        assert(main.presentation.margin+main.gameplay.playfield.width*2<size.columns);
        assert(main.presentation.topMargin>=0 && main.presentation.topMargin+100<size.rows);
        scene.session->roomScene.survivors={0,1};capture(scene,size,"2-large-main-original-size");
        const auto anchor=main.presentation;Engine::position_multiplayer_scene(scene.context,size);
        assert(main.presentation.margin==anchor.margin && main.presentation.topMargin==anchor.topMargin);
        scene.session->roomScene.survivors={0,1,2,3};capture(scene,{120,40},"4-oversized-main-clipped-no-miniatures");
        assert(scene.session->roomScene.miniSlots.empty());
        assert(main.gameplay.playfield.width==100 && main.gameplay.playfield.visibleHeight==100);
    }
    {
        Scene scene(8);const Render::TerminalSize size{180,60};auto& s=scene.session->roomScene;
        s.spectator=true;s.selfAlive=false;s.focusSlot=0;
        capture(scene,size,"8-spectator-centered");const int many=scene.session->player.presentation.margin;
        s.survivors={0,1,2};capture(scene,size,"3-spectator-centered");
        assert(scene.session->player.presentation.margin!=many);
    }
    {
        Scene scene(3);const Render::TerminalSize size{180,60};scene.resize(1,6,12);
        auto& s=scene.session->roomScene;capture(scene,size,"3-before-finalist-position-adjustment");
        const int many=scene.session->player.presentation.margin;
        s.survivors={0,1};capture(scene,size,"2-finalists-single-position-adjustment");
        const auto final=scene.session->player.presentation;assert(final.margin!=many);
        scene.resize(1,8,25);Engine::position_multiplayer_scene(scene.context,size);
        assert(scene.session->player.presentation.margin==final.margin && scene.session->player.presentation.topMargin==final.topMargin);
        s.finalists={0,1};s.finalistLayout={0,1};s.survivors={0};Engine::position_multiplayer_scene(scene.context,size);
        assert(scene.session->player.presentation.margin==final.margin && scene.session->player.presentation.topMargin==final.topMargin);
    }
    {
        Scene scene(3);auto& frame=scene.network.peerMiniBoards[1];
        frame.clock=Network::RemoteClockSample{12.345,0.};frame.receivedRevision=1;
        Engine::update_multiplayer_scene(scene.context);
        auto& timing=scene.player(1).timing;const auto received=*timing.remoteTimeUpdated;
        assert(timing.remoteGameTime->seconds()==12.345 && timing.remoteTimeRate==0.);
        assert(timing.remote_elapsed(received+Runtime::Duration::from_seconds(10)).seconds()==12.345);
        Engine::update_multiplayer_scene(scene.context);assert(timing.remoteTimeUpdated==received);
        frame.clock=Network::RemoteClockSample{15.,2.};++frame.receivedRevision;
        Engine::update_multiplayer_scene(scene.context);const auto next=*timing.remoteTimeUpdated;
        assert(timing.remote_elapsed(next+Runtime::Duration::from_seconds(3)).seconds()==21.);
        // A clock-only heartbeat restarts the baseline even at the same
        // visual sequence, including a completed turn-based clock.
        timing.completedGameTime=Runtime::Duration::from_seconds(99);
        frame.clock=Network::RemoteClockSample{20.,0.};++frame.receivedRevision;
        Engine::update_multiplayer_scene(scene.context);
        assert(timing.completedGameTime->seconds()==20. && timing.remoteTimeRate==0.);
    }
    {
        Scene scene(32);auto& n=scene.network;
        n.localTransport=std::make_shared<Network::LocalDuplexTransport>();n.localEndpoint=0;
        n.connection=Network::ConnectionState::Connected;
        Network::NetworkServices relay;relay.localTransport=n.localTransport;relay.localEndpoint=1;
        const auto latest_interest=[&] {
            std::optional<std::string> latest;
            while(auto wire=Network::try_receive(relay))latest=std::move(*wire);
            return latest;
        };
        scene.paint({120,40});auto wire=latest_interest();assert(wire);
        auto interest=Network::decode_board_views(*wire);assert(interest && !interest->players.empty() && interest->players.size()<31);
        scene.paint({400,100});wire=latest_interest();assert(wire);
        interest=Network::decode_board_views(*wire);assert(interest && interest->players.size()==31);
        // Changing the spectator focus and viewport must replace the server's
        // subscription, including removing every now-hidden miniature.
        scene.session->roomScene.spectator=true;scene.session->roomScene.selfAlive=false;scene.session->roomScene.focusSlot=1;
        scene.resize(1,100,100);scene.paint({120,40});wire=latest_interest();assert(wire);
        interest=Network::decode_board_views(*wire);
        assert(interest && interest->players.size()==1 && interest->players.front().slot==1 && interest->players.front().full);
        assert(!Network::try_receive(relay));
    }
    {
        Scene scene(8);scene.resize(0,40,60);const Render::TerminalSize size{120,80};
        auto screen=capture(scene,size,"8-wide-main-hides-opponents");
        assert(scene.session->roomScene.miniSlots.empty());
        assert(scene.session->player.presentation.margin+80<size.columns);
        assert(scene.session->player.presentation.topMargin>=0);
        assert(screen.position_of("PLAYER-1").first>0);
    }
    std::cout<<"multiplayer geometry passed\n";
    return 0;
}
} // namespace TetrTests::multiplayer_geometry_test
