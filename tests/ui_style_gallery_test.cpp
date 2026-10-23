#include "tetr/app/application_state.h"
#include "tetr/app/start_menu.h"
#include "tetr/config/user_config.h"
#include "tetr/runtime/game_context.h"
#include "tetr/network/network_services.h"
#include "../src/app/settings_view.h"
#include "../src/app/post_game_statistics.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/map_editor.h"
#include "../src/app/ui_text.h"
#include "../src/app/single_player_challenges.h"
#include "support/terminal_screen.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace TetrTests::ui_style_gallery_test {
namespace {
struct Rig {
    std::unique_ptr<Runtime::SessionState> session=std::make_unique<Runtime::SessionState>();
    Runtime::RuntimeServices services;Runtime::GameContext context{*session,services};
    UserConfig::Settings user;Network::NetworkServices network;App::ApplicationState app;
    Rig() {context.attach_user_settings(user);context.attach_network(network);context.attach_app(app);app.primaryContext=&context;user.keyCodes=UserConfig::default_key_codes();}
};
struct Capture {
    std::ostringstream output;std::streambuf* old=std::cout.rdbuf(output.rdbuf());Test::TerminalScreen screen;
    ~Capture(){std::cout.rdbuf(old);}
    std::string feed(){const auto data=output.str();screen.feed(data);output.str("");return data;}
};
std::filesystem::path directory;
void save(const Test::TerminalScreen& screen,const std::string& name,Render::TerminalSize size) {
    assert(screen.columns()<=size.columns && screen.rows()<=size.rows);
    if(directory.empty())return;
    std::ofstream out(directory/(name+".json"));out<<"{\"columns\":"<<size.columns<<",\"rows\":"<<size.rows<<",\"cells\":[";
    bool first=true;for(int row=1;row<=size.rows;++row)for(int col=1;col<=size.columns;++col) {
        const auto cell=screen.at(row,col);if(!first)out<<',';first=false;
        out<<'['<<col-1<<','<<row-1<<','<<std::quoted(cell.glyph)<<",["<<cell.foreground[0]<<','<<cell.foreground[1]<<','<<cell.foreground[2]<<"],["<<cell.background[0]<<','<<cell.background[1]<<','<<cell.background[2]<<"]]";
    }out<<"]}";
}
App::Settings::Model::SettingNode row(std::string key,std::string title,std::string value={}) {
    App::Settings::Model::SettingNode node{key,title};node.summary=std::move(value);return node;
}
void gallery(bool chinese,Render::TerminalSize size,bool full) {
    using namespace App::Settings;using namespace App::Settings::Model;
    App::UiText::set_language(chinese?App::UiText::Language::Chinese:App::UiText::Language::English);
    Rig r;r.user.uiLanguage=chinese?0:1;
    const auto prefix=std::string(chinese?"zh-":"en-")+std::to_string(size.columns)+"x"+std::to_string(size.rows)+"-";
    auto root=make_tree(r.user,false,0);
    auto networkFound=std::find_if(root.children.begin(),root.children.end(),[](const auto& n){return n.key=="NETWORK";});assert(networkFound!=root.children.end());
    auto& networkNode=*networkFound;
    auto reset=std::move(networkNode.children.back());networkNode.children.pop_back();
    networkNode.children.push_back(row("room-users","Room users"));
    networkNode.children.push_back(row("room-view-host","View host configuration"));
    networkNode.children.push_back({"preview-reset-gap","",{},NodeKind::Separator});
    networkNode.children.push_back(std::move(reset));
    r.network.connection=Network::ConnectionState::Connected;
    r.network.connectionMessage="Connected to "+r.user.serverIp;
    const std::vector<SettingNode> menus{
        {"create","Create room","",NodeKind::Group,{},{row("name","Room name","QZTX"),row("type","Room type",App::UiText::tr("Public")),row("submit","Create room"),row("back","Back")}},
        {"join","Join by name","",NodeKind::Group,{},{row("name","Room name",chinese?"练习房间":"Practice"),row("submit","Join room"),row("back","Back")}},
        {"lobby","Room lobby","",NodeKind::Group,{},{row("one",chinese?"练习房间":"Practice","2/32"),row("two","FT7","4/8"),row("refresh","Refresh rooms"),row("back","Back")}},
        {"users","Room users","",NodeKind::Group,{},{row("manage-player:0","Alice",App::UiText::tr("Battle")+" [HOST]"),row("manage-player:1",chinese?"用户名很长的玩家":"Long-player-name",App::UiText::tr("Spectator")),row("manage-player:2","Bob",App::UiText::tr("Offline")),row("back","Back")}},
    };
    for(std::size_t i=0;i<menus.size();++i) {
        if(!full && i!=0 && menus[i].key!="users")continue;
        Capture capture;auto surface=make_settings_surface();
        TerminalSettingsView parent(r.context,false,0,false,surface,[&]{return std::optional{size};});
        const auto anchorNode=std::find_if(networkNode.children.begin(),networkNode.children.end(),[&](const auto& n){return n.title==menus[i].title;});
        assert(anchorNode!=networkNode.children.end());
        const auto anchorIndex=static_cast<std::size_t>(anchorNode-networkNode.children.begin());
        parent.set_menu_path(root,{"NETWORK"},anchorIndex);parent.print_menu(networkNode,anchorIndex,"SETTINGS > NETWORK");capture.feed();
        auto child=parent;child.set_submenu_path(parent,menus[i],{},0);child.print_menu(menus[i],0,"SETTINGS > NETWORK > "+menus[i].title);
        const auto transition=capture.feed();assert(transition.find("\x1b[2J")==std::string::npos);
        if(size.columns>=120) {
            const auto anchor=capture.screen.position_of("> "+App::UiText::tr(anchorNode->title));
            const auto entry=capture.screen.position_of("> "+App::UiText::tr(menus[i].children[0].title));
            assert(anchor.first>0 && entry.second>anchor.second);
            assert((capture.screen.at(entry.first,entry.second).background==Test::TerminalScreen::Color{56,34,16}));
        }
        save(capture.screen,prefix+menus[i].key,size);
        if(menus[i].key=="users") {
            for(const auto& kind:{std::string("user-manage"),std::string("user-offline"),std::string("user-battle-full")}) {
                const bool offline=kind=="user-offline",fullBattle=kind=="user-battle-full";
                auto users=menus[i];
                child.set_submenu_path(parent,users,{},offline?2:1);child.print_menu(users,offline?2:1,"SETTINGS > NETWORK > Room users");capture.feed();
                SettingNode options{"member",offline?"Bob":users.children[1].title};options.literalTitle=true;
                options.children={row("state:0","Battle"),row("state:1","Spectator"),row("state:2","Offline"),row("transfer","Transfer host"),row("kick","Kick player"),row("back","Back")};
                options.children[0].enabled=!offline && !fullBattle;options.children[1].enabled=!offline;
                options.children[offline?2:1].summary=App::UiText::tr("Current");
                auto management=child;management.set_submenu_path(child,options,{},0);management.print_menu(options,0,"SETTINGS > NETWORK > Room users");
                management.print_message(offline?"Offline players must change their own status.":fullBattle?"Battle player limit reached. Choose spectator or wait for a free place.":"Enter applies this status.");
                assert(capture.feed().find("\x1b[2J")==std::string::npos);
                assert(capture.screen.position_of("> "+App::UiText::tr("Battle")).first>0);
                if(size.columns>=120) {
                    const auto roster=capture.screen.position_of("> "+users.children[offline?2:1].title);
                    const auto option=capture.screen.position_of("> "+App::UiText::tr("Battle"));
                    assert(roster.first>0 && option.second>roster.second);
                }
                save(capture.screen,prefix+kind,size);
            }
        }
        if(i==0) {
            UserConfig::Settings temporary;temporary.uiLanguage=0;
            SettingItem item{SettingId::UiLanguage,"Room type",std::ref(temporary.uiLanguage)};item.owner=&temporary;
            item.choice.labels={"Public","Private"};item.choice.values={0,1};
            child.set_submenu_path(parent,menus[i],{},1);child.print_choices(item,1,"SETTINGS > NETWORK > Create room");
            assert(capture.feed().find("\x1b[2J")==std::string::npos);save(capture.screen,prefix+"room-type",size);
        }
    }
    if(full) {
        Capture capture;TerminalSettingsView view(r.context,false,0,false,{},[&]{return std::optional{size};});
        auto tree=make_tree(r.user,false,0);view.set_menu_path(tree,{},0);view.print_menu(tree,0,"SETTINGS");capture.feed();save(capture.screen,prefix+"settings-root",size);
        const auto handling=std::find_if(tree.children.begin(),tree.children.end(),[](const auto& n){return n.key=="HANDLING";});assert(handling!=tree.children.end());
        view.set_menu_path(tree,{"HANDLING"},1);view.print_scalar_input(*handling->children[1].item,"SETTINGS > HANDLING","0.120",5);capture.feed();save(capture.screen,prefix+"settings-input",size);
        view.set_menu_path(tree,{"HANDLING"},1);view.print_menu(*handling,1,"SETTINGS > HANDLING");capture.feed();save(capture.screen,prefix+"handling",size);
        view.set_menu_path(root,{"NETWORK"},3);view.print_menu(networkNode,3,"SETTINGS > NETWORK");capture.feed();save(capture.screen,prefix+"connection",size);
        assert(capture.screen.position_of("┌ "+App::UiText::tr("Connection status")).first==0);
        auto hostUser=r.user;hostUser.gameMode=4;hostUser.fieldWidth=13;
        auto hostTree=make_host_tree(hostUser,{});auto host=view;host.set_read_only(true);host.set_menu_path(hostTree,{},0);host.print_menu(hostTree,0,hostTree.title);capture.feed();save(capture.screen,prefix+"host-config",size);
        assert(capture.screen.position_of("┌ "+App::UiText::tr("NETWORK")).first==0);
        const auto game=std::find_if(hostTree.children.begin(),hostTree.children.end(),[](const auto& n){return n.key=="GAME";});assert(game!=hostTree.children.end());
        const auto field=std::find_if(game->children.begin(),game->children.end(),[](const auto& n){return n.key=="playfield";});assert(field!=game->children.end());
        host.set_menu_path(hostTree,{"GAME","playfield"},0);host.print_menu(*field,0,hostTree.title+" > GAME > Playfield");capture.feed();save(capture.screen,prefix+"host-playfield",size);
        assert(capture.screen.position_of("> "+App::UiText::tr("Width")+" = 13").first>0);
        assert(r.user.fieldWidth==10);
        for(int mode:{1,2}) {
            r.user.fieldInvisible=mode;
            auto options=make_tree(r.user,false,0);
            const auto gameOptions=std::find_if(options.children.begin(),options.children.end(),[](const auto& n){return n.key=="GAME";});
            const auto playfield=std::find_if(gameOptions->children.begin(),gameOptions->children.end(),[](const auto& n){return n.key=="playfield";});
            view.set_menu_path(options,{"GAME","playfield"},2);
            view.print_menu(*playfield,2,"SETTINGS > GAME > Playfield");capture.feed();
            assert(capture.screen.position_of(App::UiText::tr(mode==1?"Delayed invisibility":"Periodic flashing")).first>0);
            assert(capture.screen.position_of(App::UiText::tr(mode==1?"Invisibility delay (seconds)":"Flash interval (seconds)")).first>0);
            if(mode==2) assert(capture.screen.position_of(App::UiText::tr("Flash duration (seconds)")).first>0);
            save(capture.screen,prefix+"field-invisibility-"+std::to_string(mode),size);
        }
        r.user.fieldInvisible=0;
        r.user.presetDefined.emplace();auto preset=make_tree(r.user,false,0);view.set_menu_path(preset,{},0);view.print_menu(preset,0,"PRESET SETTINGS");capture.feed();save(capture.screen,prefix+"preset",size);r.user.presetDefined.reset();
    }
    {
        Capture capture;TerminalSettingsView view(r.context,false,0,false,{},[&]{return std::optional{size};});
        const auto original=r.user.serverIp;
        const std::string address="wss://a-very-long-public-server-address.example/room-gateway/ws";
        r.user.serverIp=address;
        view.set_menu_path(root,{"NETWORK"},0);view.print_menu(networkNode,0,"SETTINGS > NETWORK");capture.feed();save(capture.screen,prefix+"server-address",size);
        assert(capture.screen.text().find("...")!=std::string::npos);
        if(size.columns>=120) assert(capture.screen.position_of(address.substr(0,8)+"..."+address.substr(address.size()-8)).first>0);
        assert(r.user.serverIp==address);
        view.print_scalar_input(*networkNode.children[0].item,"SETTINGS > NETWORK",r.user.serverIp,0);capture.feed();save(capture.screen,prefix+"server-edit-start",size);
        assert(capture.screen.position_of("wss://").first>0);
        view.print_scalar_input(*networkNode.children[0].item,"SETTINGS > NETWORK",r.user.serverIp,r.user.serverIp.size());capture.feed();save(capture.screen,prefix+"server-edit-end",size);
        assert(capture.screen.position_of("gateway/ws").first>0);
        r.user.serverIp=original;
    }
    for(int selected=0;selected<1;++selected) {
        Capture capture;App::draw_start_menu(r.context,size);capture.feed();save(capture.screen,prefix+"start-"+std::to_string(selected),size);
    }
    r.network.multiplayerRoom=true;r.network.room={"Practice",true,4,32};r.network.roomPlayerIndex=0;r.network.matchPhase=Network::RoomMatchPhase::Preparing;
    r.network.roomMembers={{0,"Alice",true,Network::PlayerRole::Battle,true,true,false},{1,"Bob",false,Network::PlayerRole::Battle,false,true,false},
        {2,"Carol",false,Network::PlayerRole::Spectator,false,false,false},{3,"Dave",false,Network::PlayerRole::Offline,false,false,false}};
    {Capture capture;App::TerminalCanvas canvas;std::size_t offset=0;App::paint_room_waiting(r.context,canvas,size,1,offset);capture.feed();save(capture.screen,prefix+"waiting",size);}
    r.context.config.mode.mode=Runtime::GameMode::Multiplayer;r.context.config.garbage.mode=Runtime::GarbageMode::Multiplayer;
    auto& p=r.session->player;p.gameplay.scoring.placedBlocks=150;p.gameplay.scoring.totalAttack=300;p.gameplay.scoring.totalOutgoingAttack=240;
    p.gameplay.scoring.score=123456;
    p.gameplay.scoring.totalReceivedAttack=160;p.gameplay.scoring.roundPieceTypes={0,1,2};p.gameplay.scoring.clearStatistics[{0,4}]={2,1,0};
    p.gameplay.scoring.clearStatistics[{1,2}]={1,4,1};p.timing.completedGameTime=Runtime::Duration::from_seconds(65.125);p.timing.completedRealTime=Runtime::Duration::from_seconds(66.125);
    const auto data=App::make_post_game_statistics(r.context,p);
    {
        auto user=r.user;Challenges::find("sprint-40")->change(user);auto menu=App::make_challenge_menu(user);
        Capture capture;TerminalSettingsView view(r.context,false,0,false,{},[&]{return std::optional{size};});
        auto& sprint=menu.children.front();view.set_menu_path(menu,{sprint.key},2);view.print_menu(sprint,2,"SETTINGS > Single-player challenges > Sprint");capture.feed();
        save(capture.screen,prefix+"challenges-menu",size);
        const auto white=capture.screen.position_of("> "+App::challenge_name(*Challenges::find("sprint-40")));
        assert(white.first>0 && (capture.screen.at(white.first,white.second).foreground==Test::TerminalScreen::Color{255,255,255}));
        const auto gray=capture.screen.position_of("  "+App::challenge_name(*Challenges::find("sprint-20")));
        if(gray.first>0)assert((capture.screen.at(gray.first,gray.second+2).foreground==Test::TerminalScreen::Color{128,128,128}));
        const auto store=std::filesystem::temp_directory_path()/("termino_gallery_challenges_"+prefix+std::to_string(Challenges::utc_now_milliseconds()));
        std::filesystem::create_directories(store);user.persistencePath=(store/"player.ini").string();
        const auto replay=store/"record.txt";std::ofstream(replay)<<"test";Challenges::History history;history.nextRunId=31;
        for(int i=1;i<=30;++i)history.records.push_back({std::uint64_t(i),Challenges::utc_now_milliseconds()-(30-i)*60000,"sprint-40",{40,double(100-i)},true,replay.u8string()});
        assert(Challenges::save(history,Challenges::history_path(user)));auto recordsRoot=make_tree(user,false,0);
        auto& recordsMenu=std::find_if(recordsRoot.children.begin(),recordsRoot.children.end(),[](const auto& n){return n.key=="challenges";})->children.front().children[2].children.front();
        view.set_menu_path(recordsRoot,{"challenges","challenges/Sprint","sprint-40","records"},0);view.print_menu(recordsMenu,0,"SETTINGS > Single-player challenges > Sprint > 40 lines > Score records");capture.feed();
        save(capture.screen,prefix+"challenges-records",size);if(size.columns>=80) {
            assert(capture.screen.text().find("PB")!=std::string::npos && capture.screen.text().find("  Re")!=std::string::npos);
        }
        std::filesystem::remove_all(store);
        App::PostGameStatisticsView results(data);std::vector<Challenges::Outcome> outcomes;
        for(const auto& d:Challenges::definitions())outcomes.push_back({d.id,{d.target,d.secondary==Challenges::Secondary::Time?25.125:100},true});
        results.set_challenges(outcomes);results.next_page();results.next_page();results.next_page();results.render(r.context,"controls",size);capture.feed();
        assert(capture.screen.text().find("PB")!=std::string::npos);save(capture.screen,prefix+"challenges-results",size);
        results.move(19,0);results.render(r.context,"controls",size);capture.feed();save(capture.screen,prefix+"challenges-scrolled",size);
    }
    {Capture capture;App::PostGameStatisticsView results({data,data},{"Alice WIN","Bob LOSE"});results.render(r.context,"controls",size);capture.feed();save(capture.screen,prefix+"room-summary",size);
        results.next_page();results.render(r.context,"controls",size);capture.feed();save(capture.screen,prefix+"room-clears",size);}
    if(full) {
        Capture capture;App::PostGameStatisticsView result(data);result.next_page();result.render(r.context,"controls",size);capture.feed();save(capture.screen,prefix+"summary",size);
        assert(capture.screen.position_of(App::UiText::tr("SCORE")).first>0);
        assert(capture.screen.position_of("123456").first>0);
        result.show_replay_save_prompt("round.txt",chinese?"我的练习":"Practice",chinese?12:8);result.render(r.context,"controls",size);capture.feed();save(capture.screen,prefix+"save-replay",size);
        for(bool obscured:{false,true}) {
            r.context.config.display.obscuredColors=obscured;
            App::PostGameStatisticsView clears(data);clears.next_page();clears.next_page();
            Capture table;clears.render(r.context,"controls",size);table.feed();
            save(table.screen,prefix+(obscured?"clears-monochrome":"clears-readable"),size);
        }
        r.context.config.display.obscuredColors=false;
    }
    for(int state=0;state<(full?4:1);++state) {
        App::MapEditorState editor;editor.optionsOpen=state==0;editor.sequenceFocus=state==2;editor.exitPrompt=state==3;
        editor.map.name=chinese?"练习地图":"Practice map";editor.insert(Piece::Type{0});editor.insert(Piece::Type{1});
        App::TerminalCanvas canvas;canvas.pending=App::map_editor_frame(r.context,editor,size.columns,size.rows);canvas.columnsLimit=size.columns-1;canvas.rowsLimit=size.rows;
        Capture capture;canvas.present({},false);capture.feed();save(capture.screen,prefix+"map-"+std::to_string(state),size);
    }
}
}
int run(int argc,char** argv) {
    if(argc>1){directory=argv[1];std::filesystem::create_directories(directory);}
    for(bool chinese:{false,true}) {
        gallery(chinese,{120,40},true);
        for(const auto size:{Render::TerminalSize{80,30},{50,24},{40,16}})gallery(chinese,size,false);
    }
    App::UiText::set_language(App::UiText::Language::English);
    std::cerr<<"UI gallery: bilingual menus, preserved parent columns, incremental submenus, colors and bounded small-terminal frames passed\n";
    return 0;
}
} // namespace TetrTests::ui_style_gallery_test
