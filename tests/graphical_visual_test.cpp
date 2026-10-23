// Explicit opt-in hidden OpenGL verification. Never registered as a headless
// CTest: a working GL driver is required. All data lives in a fresh directory.
#include "scene.h"
#include "menu_pointer.h"
#include "board_layout.h"
#include "viewport.h"
#include "../src/frontend/android/mobile_ui.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/presets.h"
#include "../src/app/post_game_statistics.h"
#include "../src/app/multiplayer_battle.h"
#include "../src/app/ui_text.h"
#include "tetr/runtime/game_context.h"
#include "tetr/presentation/context_view.h"
#include "tetr/rules/quick_play.h"
#include "tetr/network/network_services.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
namespace {
template<class P> Graphical::SessionView wait(Graphical::Session& s,P p) {
    const auto deadline=std::chrono::steady_clock::now()+8s;
    while(std::chrono::steady_clock::now()<deadline) {
        const auto v=s.view();if(p(v))return v;
        if(v.status==Graphical::SessionStatus::Error)throw std::runtime_error(v.message);
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("UI transition timed out");
}
void tap(Graphical::Session& s,SDL_Scancode key) {
    s.input().update(Graphical::native_key(key),true);std::this_thread::sleep_for(30ms);
    s.input().update(Graphical::native_key(key),false);std::this_thread::sleep_for(35ms);
}
void choose(Graphical::Session& s,const std::string& id,bool activate=true) {
    for(int i=0;i<200;++i) {
        auto v=s.view();assert(v.ui);const auto& m=v.ui->metadata;
        auto it=std::find_if(m.entries.begin(),m.entries.end(),[&](const auto& e){return e.id==id;});
        if(it==m.entries.end())throw std::runtime_error("Missing entry "+id+" in "+m.id);
        if(m.selected==static_cast<std::size_t>(it-m.entries.begin())) {if(activate)tap(s,SDL_SCANCODE_RETURN);return;}
        tap(s,SDL_SCANCODE_DOWN);
    }
    throw std::runtime_error("Unreachable entry: "+id);
}
void root(Graphical::Session& s) {
    for(int i=0;i<12;++i) {auto v=s.view();if(v.ui && v.ui->metadata.id=="root")return;tap(s,SDL_SCANCODE_LEFT);}
    throw std::runtime_error("Cannot return to settings");
}
std::string setting(UserConfig::SettingId id){return "setting-"+std::to_string(static_cast<int>(id));}
}
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: graphical_visual_test FONT OUTPUT_DIR [zh|headroom] [WIDTH HEIGHT]\n";return 2;}
    try {
        const auto font=std::filesystem::absolute(std::filesystem::u8path(argv[1])).generic_u8string();
        const auto output=std::filesystem::absolute(std::filesystem::u8path(argv[2]));
        const auto data=output/("runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(data);std::filesystem::current_path(data);
        const int width=argc>4?std::stoi(argv[4]):1200,height=argc>5?std::stoi(argv[5]):800;
        assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_Window* window=SDL_CreateWindow("TerMino hidden visual check",width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if(!window)throw std::runtime_error(SDL_GetError());
        auto gl=SDL_GL_CreateContext(window);if(!gl)throw std::runtime_error(SDL_GetError());
        // A hidden WGL window may never receive a compositor vblank.
        // Deterministic captures must not wait for presentation timing.
        assert(SDL_GL_SetSwapInterval(0));
        assert(gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)));
        {
            Graphical::Renderer renderer(font);
            if(argc>3 && std::string(argv[3])=="mobile-direct") {
                using namespace Graphical;using namespace Graphical::Mobile;using C=Frontend::UiCommandKind;
                const auto viewport=Viewport::fit(width,height,true);
                const Area area{-float(width/viewport.scale-1200)/2,0,width/viewport.scale,800};
                Controls controls;SessionView view;view.status=SessionStatus::Finished;view.actionEnabled[0].fill(true);
                Frontend::UiFrame page;auto& m=page.metadata;m.chinese=true;m.page=1;m.id="GAME";m.title="游戏设置";m.kind=Frontend::UiKind::Menu;
                m.keyActions={Frontend::UiKeyAction::Retry,Frontend::UiKeyAction::Exit};
                for(int i=0;i<20;++i)m.entries.push_back({std::to_string(i),"设置选项 "+std::to_string(i+1),true,"123.45",{},Frontend::UiControl::Value});
                const auto draw=[&](const std::string& name,std::vector<MenuScroll> movement={}) {
                    view.ui=std::make_shared<Frontend::UiFrame>(page);
                    const auto frame=controls.frame(view,area);
                    renderer.begin(width,height,0,true);renderer.scroll_menu(std::move(movement));
                    Mobile::surface(renderer,page,frame);Mobile::overlay(renderer,frame,false);renderer.end();
                    renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                    return renderer.interaction();
                };
                auto interaction=draw("settings-large");assert(!interaction.menuScrollMetrics.empty());
                auto scroll=interaction.menuScrollMetrics.back();assert(std::abs(scroll.rowHeight-62*contentScale)<.001f);
                assert(scroll.maxOffset==13);
                interaction=draw("settings-fraction",{{m.page,scroll.key,.375f}});assert(interaction.menuScrollMetrics.back().offset==.375f);
                m={};m.kind=Frontend::UiKind::Statistics;m.id="statistics";m.page=2;m.chinese=true;m.statistics.emplace();
                auto& s=*m.statistics;s.players={{"玩家一","2.5","80"},{"玩家二","3.0","100"}};
                for(int i=0;i<24;++i)s.metrics.push_back({std::to_string(i),"统计指标 "+std::to_string(i+1),true,"123456.789"});
                s.challenges={{"one","挑战成绩",true,"123"}};
                const auto check=[&](const Interaction& frame,C kind,int minimum) {
                    int count=0;
                    for(const auto& hit:frame.hits)if(hit.kind==kind) {
                        ++count;assert(Frontend::valid_ui_command(m,{kind,hit.index,{},m.page,hit.row,hit.column}));
                        const auto clicked=frame.hit(hit.bounds.x+hit.bounds.w/2,hit.bounds.y+hit.bounds.h/2,m);
                        assert(clicked && clicked->kind==kind && clicked->index==hit.index && clicked->row==hit.row && clicked->column==hit.column);
                    }
                    assert(count>=minimum);
                };
                interaction=draw("summary-direct");check(interaction,C::StatisticsPage,3);check(interaction,C::StatisticsPlayer,2);check(interaction,C::StatisticsMetric,6);
                scroll=interaction.menuScrollMetrics.back();interaction=draw("summary-clipped",{{m.page,scroll.key,.375f}});
                check(interaction,C::StatisticsMetric,6);
                const auto old=interaction;s.page=Frontend::UiStatisticsPage::Clears;++m.page;
                assert(!old.hit(old.hits.front().bounds.x+2,old.hits.front().bounds.y+2,m));
                s.pieces={"I","J","L","O","S","T","Z"};s.lines={0,1,2,3,4,5,6,7};
                s.cells.resize(7,std::vector<Frontend::UiStatistics::Counts>(8,{1,2,3}));s.rowTotals.resize(7,{8,16,24});s.columnTotals.resize(8,{7,14,21});s.total={56,112,168};
                interaction=draw("clears-direct");check(interaction,C::StatisticsCell,4);
                assert(std::any_of(interaction.hits.begin(),interaction.hits.end(),[&](const auto& h){return h.kind==C::StatisticsCell && h.row==7 && h.column==8;}));
                scroll=interaction.menuScrollMetrics.back();interaction=draw("clears-clipped",{{m.page,scroll.key,.375f}});check(interaction,C::StatisticsCell,4);
                s.page=Frontend::UiStatisticsPage::Challenges;++m.page;interaction=draw("challenges-direct");check(interaction,C::StatisticsChallenge,1);
                renderer.begin(width,height,0);renderer.surface(page);renderer.end();
                const auto desktop=renderer.interaction();
                assert(std::none_of(desktop.hits.begin(),desktop.hits.end(),[](const auto& h){return h.kind==C::StatisticsPage;}));
                controls.edit();controls.frame(view,area);controls.command(Command::ToggleRail);controls.scroll_rail(10000);
                auto rail=controls.frame(view,area);assert(rail.railMaxOffset>0);
                controls.scroll_rail(18.5f);rail=controls.frame(view,area);assert(rail.railOffset>0);
                renderer.begin(width,height,0,true);Mobile::overlay(renderer,rail,false);renderer.end();
                renderer.screenshot((output/"rail-scroll.bmp").generic_u8string(),width,height);
                std::cout<<"MOBILE_DIRECT_OK "<<width<<'x'<<height<<'\n';return 0;
            }
            if(argc>3 && std::string(argv[3])=="mobile-results") {
                using namespace Graphical;using namespace Graphical::Mobile;
                const auto viewport=Viewport::fit(width,height,true);
                const Area area{-float(width/viewport.scale-1200)/2,0,width/viewport.scale,800};
                Frontend::UiFrame page;auto& m=page.metadata;
                m.kind=Frontend::UiKind::Statistics;m.id="statistics";m.page=1;m.chinese=true;
                m.statistics.emplace();auto& data=*m.statistics;
                for(const char* id:{"TIME","SCORE","PIECES PER SECOND","ATTACK PER MINUTE"})data.metrics.push_back({id,id,true,"123.45"});
                for(int i=4;i<40;++i)data.metrics.push_back({std::to_string(i),"统计指标 "+std::to_string(i+1),true,"12345.67"});
                Controls controls;SessionView view;view.status=SessionStatus::Finished;
                const auto draw=[&](const std::string& name,std::vector<MenuScroll> movements={}) {
                    view.ui=std::make_shared<Frontend::UiFrame>(page);
                    renderer.begin(width,height,0,true);renderer.scroll_menu(std::move(movements));
                    Mobile::surface(renderer,page,controls.frame(view,area));renderer.end();
                    assert(renderer.text_size(9)==minimumTextSize && renderer.text_size(24)==24);
                    renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                    return renderer.interaction();
                };
                auto before=draw("summary-before");assert(before.menuScrollMetrics.size()==1);
                auto metric=before.menuScrollMetrics.front();assert(metric.maxOffset==34);
                const float rowHeight=metric.rowHeight;
                auto after=draw("summary-fraction",{{m.page,metric.key,.25f}});
                assert(after.menuScrollMetrics.front().offset==.25f);
                assert(draw("summary-retained").menuScrollMetrics.front().offset==.25f);
                data.selectedMetric=10;after=draw("summary-keyboard");
                assert(after.menuScrollMetrics.front().offset==5);
                auto stale=draw("summary-stale",{{m.page-1,metric.key,10}});assert(stale.menuScrollMetrics.front().offset==5);
                assert(std::abs(rowHeight-44*area.height/800*contentScale)<.001f);
                m.page=2;data.page=Frontend::UiStatisticsPage::Clears;
                for(int i=0;i<12;++i){data.pieces.push_back("P"+std::to_string(i));data.pieceColors.push_back(i%7);}
                for(int i=0;i<20;++i)data.lines.push_back(i);
                data.cells.resize(12,std::vector<Frontend::UiStatistics::Counts>(20,{1,2,3}));
                data.rowTotals.resize(12,{20,40,60});data.columnTotals.resize(20,{12,24,36});data.total={240,480,720};
                before=draw("clears-before");assert(before.menuScrollMetrics.size()==2);
                const auto vertical=before.menuScrollMetrics[0],horizontal=before.menuScrollMetrics[1];
                assert(vertical.maxOffset==7 && horizontal.horizontal && horizontal.maxOffset>0);
                after=draw("clears-fraction",{{m.page,vertical.key,.25f},{m.page,horizontal.key,.5f}});
                assert(after.menuScrollMetrics[0].offset==.25f && after.menuScrollMetrics[1].offset==.5f);
                after=draw("clears-retained");assert(after.menuScrollMetrics[0].offset==.25f && after.menuScrollMetrics[1].offset==.5f);
                data.selectedRow=11;data.selectedColumn=19;after=draw("clears-keyboard");
                assert(after.menuScrollMetrics[0].offset==7 && after.menuScrollMetrics[1].offset==horizontal.maxOffset);
                data.selectedRow=12;data.selectedColumn=20;data.total={18446744073709551615ULL,18446744073709551615ULL,18446744073709551615ULL};
                after=draw("clears-long-counts");
                const bool expanded=renderer.measure("18446744073709551615",23)>((area.width-156)/contentScale-120)/3-28;
                assert(after.menuScrollMetrics[0].maxOffset==(expanded?8:7));
                UserConfig::Settings settings;settings.persistenceEnabled=false;settings.goal=0;settings.gravityInterval=100;
                Graphical::LaunchOptions launch;launch.startScreen=false;launch.touchControls=true;
                Graphical::Session game(settings,settings,launch);
                const auto live=wait(game,[](const auto& v){return v.status==SessionStatus::Playing;});
                renderer.begin(width,height,0,true);renderer.minimum_text_size(minimumTextSize);background(renderer,true,contentScale);
                renderer.scaled(contentScale,600,200,[&]{game_view(renderer,live);});
                Mobile::overlay(renderer,controls.frame(live,area),true);renderer.end();
                renderer.screenshot((output/"game-hud.bmp").generic_u8string(),width,height);
                std::cerr<<"MOBILE_RESULTS_SCROLL_OK "<<width<<'x'<<height<<'\n';return 0;
            }
            if(argc>3 && std::string(argv[3])=="mobile-overlay") {
                using namespace Graphical;using namespace Graphical::Mobile;
                const auto viewport=Viewport::fit(width,height,true);
                const Area area{-float(width/viewport.scale-1200)/2,0,width/viewport.scale,800};
                SessionView view;view.status=SessionStatus::Paused;
                auto& b=view.board;b.width=10;b.bottom=20;b.firstRow=-8;b.cells.resize(280);
                b.username="P1";b.holdEnabled=true;b.holdAvailable=true;b.spatialEffects=false;
                b.hold=Piece::Type::from_index(5);
                b.next={Piece::Type::from_index(0),Piece::Type::from_index(4),Piece::Type::from_index(2)};b.nextCount=3;
                SceneBoard peer;peer.slot=1;peer.board=b;peer.board.username="P2";view.peers.push_back(peer);
                Controls controls;
                const auto draw=[&](const std::string& name) {
                    renderer.begin(width,height,0,true);renderer.minimum_text_size(minimumTextSize);renderer.touch_controls(true);
                    background(renderer,true,contentScale);
                    renderer.scaled(contentScale,600,200,[&]{
                        const auto fit=multiplayer_geometry({{10,20},{10,20}},{},renderer.scene_left()+40,renderer.scene_right()-40);
                        assert(fit.full.front().left-26-fit.full.front().rail>=renderer.scene_left());
                        assert(fit.full.back().left+fit.full.back().width+20+fit.full.back().next<=renderer.scene_right());
                        scene_view(renderer,view);
                    });
                    Mobile::overlay(renderer,controls.frame(view,area),true);renderer.end();
                    std::vector<unsigned char> pixels(width*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                    return pixels;
                };
                const auto closed=draw("duel-rail-closed");controls.command(Command::ToggleRail);
                const auto opened=draw("duel-rail-open");
                const int first=static_cast<int>(std::ceil(156*viewport.scale))+3;
                for(int row=0;row<height;++row) {
                    const auto from=(row*width+first)*4,to=(row+1)*width*4;
                    assert(std::equal(closed.begin()+from,closed.begin()+to,opened.begin()+from));
                }
                const auto full=multiplayer_geometry({{10,20},{10,20}},{},renderer.scene_left()+40,renderer.scene_right()-40);
                const auto desktop=multiplayer_geometry({{10,20},{10,20}},{},renderer.scene_left()+gameplayLeft,renderer.scene_right()-40);
                assert(full.full.front().left<desktop.full.front().left && full.full.front().unit>=desktop.full.front().unit);
                renderer.begin(width,height,0,true);assert(!renderer.touch_controls()); // Desktop remains the default.
                std::cout<<"MOBILE_OVERLAY_OK "<<width<<'x'<<height<<'\n';return 0;
            }
            if(argc>3 && std::string(argv[3])=="mobile-layout") {
                using namespace Graphical;using namespace Graphical::Mobile;
                const auto viewport=Viewport::fit(width,height,true);
                const Area area{-float(width/viewport.scale-1200)/2,0,width/viewport.scale,800};
                Controls editor;editor.edit();SessionView view;
                const auto capture=[&](const std::string& name) {
                    renderer.begin(width,height,0,true);overlay(renderer,editor.frame(view,area),false);renderer.end();
                    GLint actual[4];glGetIntegerv(GL_VIEWPORT,actual);
                    assert(actual[0]==0 && actual[1]==0 && actual[2]==width && actual[3]==height);
                    const auto field=editor_field_bounds(area);
                    assert(std::abs(field.w/10-field.h/20)<.001f);
                    std::array<unsigned char,4> color{};
                    glReadPixels(viewport.screen_x(field.x+field.w*.55f),height-viewport.screen_y(field.y+field.h*.475f)-1,
                        1,1,GL_RGBA,GL_UNSIGNED_BYTE,color.data());
                    assert(std::abs(int(color[0])-10)<=1 && std::abs(int(color[1])-9)<=1 && std::abs(int(color[2])-8)<=1);
                    renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                };
                capture("editor-empty");editor.command(Command::ToggleRail);editor.command(Command::Properties);capture("editor-panel-left");
                for(int i=0;i<4;++i)editor.command(Command::Next);
                capture("editor-panel-right");
                Frontend::UiFrame page;page.metadata.kind=Frontend::UiKind::MapEditor;page.metadata.id="map-editor";
                page.metadata.page=1;page.metadata.editor.emplace();
                auto& field=page.metadata.editor->field;field.width=10;field.height=20;field.colors.assign(200,-1);
                const Rect pane{float(area.x+156),0,float(area.width-156),800};
                renderer.begin(width,height,0,true);Mobile::surface(renderer,page,editor.frame(view,area));renderer.end();
                const auto interaction=renderer.interaction();assert(interaction.editorField);
                const auto box=interaction.editorField->bounds;
                assert(std::abs(box.w/10-box.h/20)<.001f);
                const auto hit=interaction.hit(box.x+box.w*.95f,box.y+box.h*.975f,page.metadata);
                assert(hit && hit->kind==Frontend::UiCommandKind::MapPaint && hit->row==19 && hit->column==9);
                renderer.screenshot((output/"map-square-cells.bmp").generic_u8string(),width,height);
                page={};page.metadata.kind=Frontend::UiKind::Menu;page.metadata.id="mobile-settings";page.metadata.page=2;
                page.metadata.chinese=true;page.metadata.title="配置";
                for(const char* title:{"游戏模式","操作手感","游戏机制","机器人","虚拟按键","联机","外观","地图"})
                    page.metadata.entries.push_back({title,title,true,"默认"});
                renderer.begin(width,height,0,true);background(renderer);Mobile::surface(renderer,page,editor.frame(view,area));renderer.end();
                const auto menu=renderer.interaction();assert(!menu.hits.empty());
                for(const auto& h:menu.hits)assert(h.bounds.x>=pane.x && h.bounds.x+h.bounds.w<=pane.x+pane.w+.001f);
                renderer.screenshot((output/"menu-reflow.bmp").generic_u8string(),width,height);
                std::cout<<"MOBILE_LAYOUT_OK "<<width<<'x'<<height<<'\n';return 0;
            }
            if(argc>3 && std::string(argv[3])=="mobile-pane") {
                Frontend::UiFrame frame;auto& m=frame.metadata;
                m.kind=Frontend::UiKind::MapEditor;m.id="map-editor";m.title="Map editor";m.page=1;
                m.editor.emplace();m.editor->field.width=10;m.editor->field.height=20;
                m.editor->field.colors.assign(200,-1);
                const Graphical::Rect pane{174,20,1000,750};
                renderer.begin(width,height,0);renderer.surface_in(frame,pane);renderer.end();
                const auto interaction=renderer.interaction();assert(interaction.editorField && interaction.hits.size()>=11);
                const auto inPane=[&](Graphical::Rect r) {
                    return r.x>=pane.x && r.y>=pane.y && r.x+r.w<=pane.x+pane.w && r.y+r.h<=pane.y+pane.h;
                };
                assert(inPane(interaction.editorField->bounds));
                for(const auto& hit:interaction.hits) {
                    assert(inPane(hit.bounds));
                    const auto result=interaction.hit(hit.bounds.x+hit.bounds.w/2,hit.bounds.y+hit.bounds.h/2,m);
                    assert(result && result->kind==hit.kind && result->index==hit.index);
                }
                const auto field=interaction.editorField->bounds;
                const auto painted=interaction.hit(field.x+field.w*.05f,field.y+field.h*.025f,m);
                assert(painted && painted->kind==Frontend::UiCommandKind::MapPaint && painted->row==0 && painted->column==0);
                frame={};auto& menu=frame.metadata;menu.kind=Frontend::UiKind::Menu;menu.id="scaled-scroll";menu.page=2;
                for(int i=0;i<40;++i)menu.entries.push_back({std::to_string(i),"Option "+std::to_string(i)});
                renderer.begin(width,height,0);renderer.surface_in(frame,pane);renderer.end();
                auto scaled=renderer.interaction();assert(scaled.menuScrollMetrics.size()==1);
                auto metrics=scaled.menuScrollMetrics.front();assert(inPane(metrics.bounds) && inPane(metrics.track));
                assert(metrics.rowHeight==44*pane.h/800);
                renderer.begin(width,height,0);renderer.scroll_menu({{menu.page,metrics.key,.25f}});renderer.surface_in(frame,pane);renderer.end();
                scaled=renderer.interaction();metrics=scaled.menuScrollMetrics.front();assert(metrics.offset==.25f);
                const auto partial=scaled.hit(metrics.bounds.x+20,metrics.bounds.y+1,menu);
                assert(partial && partial->index==0 && std::abs(partial->bounds.h-29*pane.h/800)<.001f);
                std::cout<<"MOBILE_PANE_OK "<<width<<'x'<<height<<'\n';return 0;
            }
            if(argc>3 && std::string(argv[3])=="ui-interactions") {
                Frontend::UiFrame frame;auto& m=frame.metadata;
                m={Frontend::UiKind::Menu,"NETWORK","Online","Settings > Online"};m.page=1;m.chinese=true;
                m.entries={{"name","用户名",true,"方块玩家"},{"gap","",false},{"server","服务器地址",true,"wss://termino.candy0014.icu/ws"},
                    {"replay","回放路径",true,".tetr/replay/中文录像.txt"}};
                m.entries[1].control=Frontend::UiControl::Separator;m.entries[3].filePath=true;m.selected=2;
                m.ancestors.push_back({"设置",{{"online","联机"}},0});m.hint="在原条目的输入框中编辑，Enter 确认。";
                const auto draw=[&](const std::string& name,std::optional<std::array<float,2>> pointer=std::nullopt,
                    std::vector<Graphical::MenuScroll> movement=std::vector<Graphical::MenuScroll>{}) {
                    for(double time:{0.,.12}) {
                        renderer.begin(width,height,time);renderer.pointer(pointer);renderer.scroll_menu(std::move(movement));
                        Graphical::background(renderer);renderer.surface(frame);renderer.end();
                    }
                    renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);return renderer.interaction();
                };
                auto interaction=draw("ui-hover",std::array<float,2>{500,230});
                assert(interaction.hit(500,230,m) && !interaction.hit(500,275,m));
                const auto entries=m.entries;m.parent=Frontend::UiNavigationGroup{"联机设置",entries,2,"NETWORK"};
                m.entries.clear();m.kind=Frontend::UiKind::Text;m.id="server";m.title="服务器地址";m.page=2;
                m.input="wss://中文域名.example/ws";m.inputCaret=m.input.size();
                interaction=draw("ui-inline");assert(interaction.textRect && interaction.textRect->x>=881 && interaction.textRect->y<400);
                assert(std::any_of(interaction.hits.begin(),interaction.hits.end(),[](const auto& h){return h.ancestor==1 && h.bounds.x>=376;}));
                m={Frontend::UiKind::Menu,"scroll-settings","滚动设置","设置 > 滚动检查"};m.page=10;m.chinese=true;
                for(int i=0;i<40;++i)m.entries.push_back({std::to_string(i),"设置条目 "+std::to_string(i+1),true,std::to_string(i)});
                m.ancestors.push_back({"上级设置",m.entries,0});
                interaction=draw("ui-scroll-before");const auto menuKey=interaction.menu_scroll(600,400,m);assert(menuKey);
                const auto ancestorKey=interaction.menu_scroll(100,400,m);assert(ancestorKey && *ancestorKey!=*menuKey);
                interaction=draw("ui-scroll-down",{},{{m.page,*menuKey,6}});
                assert(interaction.hit(500,230,m)->index==6 && m.selected==0 && m.entries[0].value=="0");
                const auto fullRow=interaction.hit(500,230,m)->bounds;
                interaction=draw("ui-scroll-fraction",{},{{m.page,*menuKey,.25f}});
                auto partial=interaction.hit(500,215,m);assert(partial && partial->index==6);
                assert(partial->bounds.y==211 && partial->bounds.h==fullRow.h-11);
                assert(!interaction.hit(500,210,m) && !interaction.hit(500,652,m));
                const auto metrics=interaction.menu_drag(500,300,m);assert(metrics && metrics->offset==6.25f && metrics->rowHeight==44);
                interaction=draw("ui-scroll-fraction-restore",{},{{m.page,*menuKey,-.25f}});
                interaction=draw("ui-scroll-ancestor",{},{{m.page,*ancestorKey,3}});
                assert(interaction.hit(100,239,m)->index==3 && interaction.hit(500,230,m)->index==6);
                m.selected=1;interaction=draw("ui-scroll-keyboard");assert(interaction.hit(500,230,m)->index==1);
                interaction=draw("ui-scroll-stale",{},{{m.page-1,*menuKey,20}});assert(interaction.hit(500,230,m)->index==1);
                m={Frontend::UiKind::MapEditor,"map-editor","地图编辑器"};m.chinese=true;m.page=3;m.editor.emplace();
                auto& e=*m.editor;e.field.width=10;e.field.height=20;e.field.colors.assign(200,-1);e.row=19;e.brush=5;
                for(int i=180;i<200;++i)e.field.colors[i]=i%7;
                interaction=draw("ui-map-palette");
                const auto brush=interaction.hit(200,450,m);assert(brush && brush->kind==Frontend::UiCommandKind::MapBrush && brush->index==7);
                assert(interaction.editorField);const auto box=interaction.editorField->bounds;
                const auto cell=interaction.hit(box.x+box.w*.95f,box.y+box.h*.975f,m);
                assert(cell && cell->kind==Frontend::UiCommandKind::MapPaint && cell->row==19 && cell->column==9);
                e.sequenceFocus=true;e.sequenceSize=100;e.caret=3;e.sequence.resize(24);e.candidates.resize(32);
                e.candidateColumns=3;e.candidateRows=3;e.candidateFirst=e.candidate=9;m.page=4;
                for(auto* list:{&e.sequence,&e.candidates})for(std::size_t i=0;i<list->size();++i) {
                    auto& p=(*list)[i];p.name="方块 "+std::to_string(i+1);p.color=i%7;p.cells={{0,0},{0,1},{1,1},{1,2}};
                }
                interaction=draw("ui-map-sequence");
                assert(std::count_if(interaction.hits.begin(),interaction.hits.end(),[](const auto& h){return h.kind==Frontend::UiCommandKind::MapCaret;})==24);
                assert(std::count_if(interaction.hits.begin(),interaction.hits.end(),[](const auto& h){return h.kind==Frontend::UiCommandKind::MapCandidate;})==9);
                assert(interaction.scroll(100,400,m)==0 && interaction.scroll(700,400,m)==1);
                auto stale=m;stale.editor->sequenceFocus=false;assert(!interaction.hit(100,400,stale));
            } else if(argc>3 && std::string(argv[3])=="effects") {
                const auto fixture=[&](const std::string& name,Runtime::VisualEffectsConfig settings,bool duel=false,bool paused=false) {
                    renderer.reset_event_animations();Graphical::SessionView v;v.status=Graphical::SessionStatus::Playing;
                    auto& b=v.board;b.width=10;b.bottom=20;b.firstRow=-8;b.cells.resize(280);b.username="EFFECTS";
                    b.effects=settings;b.pieces=1;b.holdEnabled=true;b.hold=Piece::Type::from_index(5);b.nextCount=5;
                    for(int id:{0,1,2,3,4})b.next.push_back(Piece::Type::from_index(id));
                    for(int row=18;row<20;++row)for(int col=0;col<10;++col)if(col!=4 && col!=5)
                        b.cells[(row+8)*10+col]={Runtime::BoardCell::piece((col+row)%7),Presentation::CellAppearance::Visible};
                    b.active=Presentation::PieceView{Piece::Type::from_index(0),{{0,4},Runtime::Rotation{}}};
                    b.ghost=Presentation::PieceView{Piece::Type::from_index(0),{{17,4},Runtime::Rotation(1)}};
                    if(duel){v.room=true;v.peers.push_back({1,false,b});v.peers[0].board.username="OTHER";}
                    const auto frame=[&](double time,const std::string& suffix) {
                        renderer.begin(width,height,time);Graphical::background(renderer);
                        if(duel)Graphical::scene_view(renderer,v);else Graphical::game_view(renderer,v);renderer.end();
                        if(!suffix.empty())renderer.screenshot((output/(name+suffix+".bmp")).generic_u8string(),width,height);
                    };
                    frame(0,"");b.pieces=2;b.lines=4;b.clearLines=4;b.combo=3;b.attack=10;
                    b.elapsed=Runtime::Duration::from_seconds(.1);
                    b.placement=Presentation::PlacementView{2,{Piece::Type::from_index(0),{{17,4},Runtime::Rotation(1)}},{16,17,18,19},-2};
                    b.active=Presentation::PieceView{Piece::Type::from_index(5),Piece::spawn_pose(10,Piece::Type::from_index(5))};
                    b.ghost.reset();frame(.1,"");
                    b.elapsed=Runtime::Duration::from_seconds(.18);frame(.18,"-impact");
                    if(paused) {
                        v.status=Graphical::SessionStatus::Paused;frame(.2,"-pause");frame(5,"-pause-later");
                    } else {b.elapsed=Runtime::Duration::from_seconds(.32);frame(.32,"-particles");}
                };
                fixture("effects-default",{});
                fixture("effects-maximum",{200,200,200,200,200,200,200});
                fixture("effects-off",{0,200,200,200,200,200,200});
                fixture("effects-channels-off",{100,0,0,0,0,0,0});
                fixture("effects-duel",{},true);
                fixture("effects-frozen",{},false,true);
            } else if(argc>3 && std::string(argv[3])=="duel-effects") {
                const auto fixture=[&](const std::string& name,int peerSlot,bool spectator) {
                    renderer.reset_event_animations();Graphical::SessionView v;v.status=Graphical::SessionStatus::Playing;v.room=true;
                    Presentation::BoardView initial;initial.width=10;initial.bottom=20;initial.firstRow=-8;initial.cells.resize(280);
                    initial.holdEnabled=true;initial.hold=Piece::Type::from_index(5);initial.nextCount=3;
                    for(int id:{0,1,2})initial.next.push_back(Piece::Type::from_index(id));
                    for(int row=16;row<20;++row)for(int col=0;col<10;++col)if(col!=4)
                        initial.cells[(row+8)*10+col]={Runtime::BoardCell::piece((row+col)%7),Presentation::CellAppearance::Visible};
                    v.board=initial;v.board.username="LEFT";v.peers={{peerSlot,false,initial}};v.peers[0].board.username="RIGHT";
                    v.spectator=spectator;v.ownBoardVisible=!spectator;
                    if(spectator) {
                        v.peers.insert(v.peers.begin(),{2,false,v.board});
                        v.board.finished=true;v.board.result=Presentation::RoundResult::Defeat;
                    }
                    auto& left=spectator?v.peers[0].board:v.board;auto& right=v.peers.back().board;
                    const int leftSlot=spectator?2:-1;
                    const auto frame=[&](double time,const std::string& suffix) {
                        left.elapsed=right.elapsed=Runtime::Duration::from_seconds(time);
                        renderer.begin(width,height,time);Graphical::background(renderer);Graphical::scene_view(renderer,v);renderer.end();
                        if(!suffix.empty())renderer.screenshot((output/(name+suffix+".bmp")).generic_u8string(),width,height);
                    };
                    const auto lock=[&](Presentation::BoardView& b,int direction) {
                        b.pieces=1;b.lines=4;b.clearLines=b.placementLines=4;
                        b.placement=Presentation::PlacementView{1,{Piece::Type::from_index(0),{{17,4},Runtime::Rotation(1)}},{16,17,18,19},-2};
                        b.motion={1,1,direction,direction};
                    };
                    frame(0,"");lock(left,-1);frame(.1,"");frame(.16,"-left");
                    const auto& leftEffect=renderer.field_effects(leftSlot,left,v);
                    const auto& rightEffect=renderer.field_effects(peerSlot,right,v);
                    assert(!leftEffect.particles().empty() && rightEffect.particles().empty());
                    assert(leftEffect.transform().x<0 && rightEffect.transform().x==0 && rightEffect.transform().y==0);
                    lock(right,1);frame(.2,"");frame(.26,"-both");
                    assert(!leftEffect.particles().empty() && !rightEffect.particles().empty());
                    assert(leftEffect.pulses().back().rows.size()==4 && rightEffect.pulses().back().rows.size()==4);
                    assert(leftEffect.transform().rotation<0 && rightEffect.transform().rotation>0);
                    assert(leftEffect.transform().y>0 && rightEffect.transform().y>0);
                    assert(leftEffect.pulses().back().born!=rightEffect.pulses().back().born);
                    left.effects.intensity=0;frame(.3,"-right-only");
                    assert(leftEffect.particles().empty() && !rightEffect.particles().empty());
                    v.status=Graphical::SessionStatus::Paused;frame(.31,"");
                    const auto age=rightEffect.time();frame(3,"");assert(rightEffect.time()==age);
                };
                fixture("local-duel",-2,false);
                fixture("online-final-two",5,false);
                fixture("spectator-final-two",5,true);
            } else if(argc>3 && std::string(argv[3])=="elasticity") {
                Graphical::SessionView v;v.status=Graphical::SessionStatus::Playing;
                auto& b=v.board;b.width=10;b.bottom=20;b.firstRow=-8;b.cells.resize(280);b.username="BOUNCE";
                b.effects={100,0,0,0,0,0,100};b.holdEnabled=true;b.hold=Piece::Type::from_index(5);b.nextCount=5;
                for(int type:{0,1,2,3,4})b.next.push_back(Piece::Type::from_index(type));
                for(int row=16;row<20;++row)for(int col=0;col<10;++col)if(col!=4 && col!=5)
                    b.cells[(row+8)*10+col]={Runtime::BoardCell::piece((row+col)%7),Presentation::CellAppearance::Visible};
                const auto frame=[&](double time,const std::string& name) {
                    b.elapsed=Runtime::Duration::from_seconds(time);
                    renderer.begin(width,height,time);Graphical::background(renderer);Graphical::game_view(renderer,v);renderer.end();
                    if(!name.empty())renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                };
                frame(0,"elastic-rest");b.motion.wallSequence=1;b.motion.wallDirection=-1;frame(.01,"");frame(.06,"elastic-wall");
                frame(1,"");b.motion.rotationSequence=1;b.motion.rotationDirection=-1;frame(1.01,"");frame(1.12,"elastic-rotate");
                frame(2,"");b.pieces=1;b.placement=Presentation::PlacementView{1,{Piece::Type::from_index(0),{{16,4},Runtime::Rotation(1)}},{},-2};
                frame(2.01,"");frame(2.06,"elastic-drop");frame(3,"");b.pieces=2;b.lines=4;b.clearLines=4;
                b.placement=Presentation::PlacementView{2,{Piece::Type::from_index(0),{{16,4},Runtime::Rotation(1)}},{16,17,18,19},-2};
                frame(3.01,"");frame(3.06,"elastic-quad");frame(4,"elastic-settled");
                b.effects.bounciness=0;b.motion.wallSequence=2;b.motion.wallDirection=1;frame(4.01,"");frame(4.06,"elastic-disabled");
                // Cropped animation frames are temporary evidence, not CTest data.
                b.effects.bounciness=100;b.pieces=3;b.lines=8;b.placement->sequence=3;frame(5,"");
                for(int n=0;n<25;++n)frame(5+n/24.,"elastic-motion-"+std::to_string(n));
            } else if(argc>3 && std::string(argv[3])=="headroom") {
                for(const auto size:std::vector<Graphical::FieldSize>{{10,20},{4,4},{10,8}}) {
                    Graphical::SessionView view;view.status=Graphical::SessionStatus::Playing;
                    auto& b=view.board;b.width=size.width;b.bottom=size.height;b.firstRow=-100;
                    b.cells.resize((100+b.bottom)*b.width);b.username="HEADROOM";b.nextCount=5;
                    b.next=Piece::types_for_rotation_system(Runtime::RotationSystem::Pentomino);b.next.resize(5);
                    b.hold=Piece::april_fools_piece(51);b.holdEnabled=true;
                    const auto type=Piece::types_for_rotation_system(Runtime::RotationSystem::Pentomino)[4];
                    b.active=Presentation::PieceView{type,Piece::spawn_pose(b.width,type)};
                    const auto g=Graphical::solo_geometry(b.width,b.bottom);
                    const int first=-Graphical::displayed_above_rows(g);
                    for(int col=0;col<b.width;++col) {
                        b.cells[(100+first)*b.width+col]={Runtime::BoardCell::piece(0),Presentation::CellAppearance::Visible};
                        b.cells[(99+first)*b.width+col]={Runtime::BoardCell::piece(6),Presentation::CellAppearance::Visible};
                    }
                    renderer.begin(width,height,0);Graphical::background(renderer);Graphical::game_view(renderer,view);renderer.end();
                    renderer.screenshot((output/("headroom-"+std::to_string(b.width)+"x"+std::to_string(b.bottom)+".bmp")).generic_u8string(),width,height);
                }
            } else if(argc>3 && std::string(argv[3])=="settings") {
                UserConfig::Settings settings;settings.keyCodes=UserConfig::default_key_codes();
                settings.uiLanguage=0;settings.persistenceEnabled=false;
                Graphical::Session session(settings,settings,{});
                const auto page=[&](const std::string& id) {
                    return wait(session,[&](const auto& v){return v.ui && v.ui->metadata.id==id;});
                };
                const auto draw=[&](const std::string& name=std::string{}) {
                    const auto v=session.view();assert(v.ui);
                    renderer.begin(width,height);Graphical::background(renderer);renderer.surface(*v.ui);renderer.end();
                    if(!name.empty())renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                    return renderer.interaction();
                };
                const auto click=[&](const std::string& id,std::optional<std::size_t> ancestor=std::nullopt) {
                    const auto interaction=draw();const auto m=session.view().ui->metadata;
                    const auto& entries=!ancestor?m.entries:*ancestor<m.ancestors.size()?m.ancestors[*ancestor].entries:m.parent->entries;
                    const auto row=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.id==id;});
                    assert(row!=entries.end());const auto index=static_cast<std::size_t>(row-entries.begin());
                    const auto hit=std::find_if(interaction.hits.begin(),interaction.hits.end(),[&](const auto& h){return !h.back && h.index==index && h.ancestor==ancestor;});
                    assert(hit!=interaction.hits.end());
                    const auto resolved=interaction.hit(hit->bounds.x+hit->bounds.w/2,hit->bounds.y+hit->bounds.h/2,m);
                    assert(resolved && resolved->index==index && resolved->ancestor==ancestor);
                    if(ancestor)assert((m.kind==Frontend::UiKind::Text && *ancestor==m.ancestors.size())?hit->bounds.x>=376:hit->bounds.x<376);
                    else if(m.kind!=Frontend::UiKind::Start)assert(hit->bounds.w>600);
                    session.ui_command({Frontend::UiCommandKind::Activate,index,ancestor,m.page});
                };
                page("start");click("settings");page("root");draw("settings-root");
                click("GAME");page("GAME");click("pieces");page("pieces");
                click(setting(UserConfig::SettingId::RotationSystem));
                wait(session,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Choices;});
                const auto deep=draw("settings-ancestors");
                assert(deep.metadata->parent && deep.metadata->ancestors.size()==2);
                for(std::size_t depth=0;depth<3;++depth) {
                    const auto selected=depth<2?deep.metadata->ancestors[depth].selected:deep.metadata->parent->selected;
                    assert(std::any_of(deep.hits.begin(),deep.hits.end(),[&](const auto& h){return h.ancestor==depth && h.index==selected;}));
                }
                click("playfield",1);page("GAME");click("playfield");page("playfield");
                click(setting(UserConfig::SettingId::FieldWidth));
                wait(session,[&](const auto&){return session.text_input_active();});draw("settings-input");
                const auto owner=session.view().ui->metadata.ancestors.size();
                click(setting(UserConfig::SettingId::FieldHeight),owner);
                const auto returned=page("playfield");assert(!session.text_input_active());
                assert(returned.ui->metadata.entries[returned.ui->metadata.selected].id==setting(UserConfig::SettingId::FieldHeight));
                std::cout<<"Hidden settings layout and actual hit targets passed at "<<width<<"x"<<height<<"\n";
            } else {
            UserConfig::Settings first;first.keyCodes=UserConfig::default_key_codes();first.uiLanguage=argc>3 && std::string(argv[3])=="zh"?0:1;
            first.gravityInterval=100;first.username=first.uiLanguage?"Player One":"方块玩家";
            const auto captureSession=[&](Graphical::Session& s,const std::string& name) {
                auto v=s.view();renderer.begin(width,height);Graphical::background(renderer);
                if(!v.ui || v.ui->overlay) {
                    if(v.peers.empty() && !v.room)Graphical::game_view(renderer,v);
                    else Graphical::scene_view(renderer,v);
                }
                if(v.ui)renderer.surface(*v.ui);
                renderer.end();renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                SDL_GL_SwapWindow(window);std::cerr<<name<<std::endl;
            };
            const auto captureFrame=[&](const Frontend::UiFrame& frame,const std::string& name) {
                renderer.begin(width,height);Graphical::background(renderer);
                // The graphical renderer must work with no terminal rows.
                auto semantic=frame;semantic.rows.clear();renderer.surface(semantic);
                renderer.end();renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                SDL_GL_SwapWindow(window);std::cerr<<name<<std::endl;
            };
            {
            Graphical::Session s(first,first,{});
            const auto capture=[&](const std::string& name){captureSession(s,name);};
            wait(s,[](const auto& v){return v.ui && v.ui->metadata.id=="start";});capture("01-home");
            // Click activates its semantic target without synthetic navigation.
            s.ui_command({Frontend::UiCommandKind::Activate,1,{},s.view().ui->metadata.page});
            wait(s,[&](const auto& v){return v.ui && v.ui->metadata.id=="root";});capture("02-settings");
            choose(s,"HANDLING",false);capture("02a-handling-preview");tap(s,SDL_SCANCODE_RETURN);
            auto handling=s.view();assert(handling.ui && !handling.ui->metadata.entries[0].value.empty());
            assert(std::any_of(handling.ui->metadata.entries.begin(),handling.ui->metadata.entries.end(),[](const auto& e){return e.checked.has_value();}));
            capture("03-handling");
            for(std::size_t i=0;i<std::min<std::size_t>(10,handling.ui->metadata.entries.size());++i) {
                const auto hit=renderer.hit(400,231+i*44,handling.ui->metadata);
                assert(hit && hit->index==i);
                assert(!renderer.hit(400,253+i*44,handling.ui->metadata));
            }
            choose(s,setting(UserConfig::SettingId::Das),false);capture("03a-input-focus");tap(s,SDL_SCANCODE_RETURN);
            wait(s,[&](const auto&){return s.text_input_active();});
            assert(s.view().ui->metadata.parent && s.view().ui->metadata.parent->selected==1);
            s.text_event({Frontend::TextEditKind::Insert,"1.5f"});
            wait(s,[](const auto& v){return v.ui && v.ui->metadata.input=="1.5f";});capture("04-input");
            s.text_event({Frontend::TextEditKind::Confirm});wait(s,[&](const auto& v){return !s.text_input_active() && v.ui && v.ui->metadata.id=="HANDLING";});
            root(s);choose(s,"GAME");choose(s,"pieces");choose(s,setting(UserConfig::SettingId::RotationSystem));capture("05-choices");
            for(int i=0;i<19;++i)tap(s,SDL_SCANCODE_DOWN);capture("06-choices-scrolled");
            {const auto m=s.view().ui->metadata;const auto top=renderer.hit(400,231,m);
                assert(top && m.selected>=top->index && m.selected<top->index+10);
                for(std::size_t i=0;i<std::min<std::size_t>(10,m.entries.size()-top->index);++i) {
                    const auto hit=renderer.hit(400,231+i*44,m);assert(hit && hit->index==top->index+i);
                }}
            tap(s,SDL_SCANCODE_LEFT);root(s);choose(s,"CONTROLS");capture("07-controls");
            choose(s,setting(UserConfig::SettingId::ControlHardDrop));capture("08-key-capture");
            tap(s,SDL_SCANCODE_F7);root(s);choose(s,"NETWORK");choose(s,setting(UserConfig::SettingId::Username));
            wait(s,[&](const auto&){return s.text_input_active();});
            s.text_event({Frontend::TextEditKind::Insert,"中文姓名 / Unicode text input / a-long-value-that-scrolls-with-the-caret"});
            wait(s,[](const auto& v){return v.ui && v.ui->metadata.input.size()>60;});capture("09-unicode-input");
            s.text_event({Frontend::TextEditKind::Confirm});wait(s,[&](const auto&){return !s.text_input_active();});
            root(s);choose(s,"maps");choose(s,"map-new");tap(s,SDL_SCANCODE_RETURN);
            wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::MapEditor;});
            tap(s,SDL_SCANCODE_3);tap(s,SDL_SCANCODE_RIGHT);tap(s,SDL_SCANCODE_3);capture("18-map-field");
            tap(s,SDL_SCANCODE_ESCAPE);capture("19-map-exit");tap(s,SDL_SCANCODE_ESCAPE);
            tap(s,SDL_SCANCODE_TAB);capture("20-map-generator");tap(s,SDL_SCANCODE_F3);
            tap(s,SDL_SCANCODE_SLASH);wait(s,[&](const auto&){return s.text_input_active();});
            s.text_event({Frontend::TextEditKind::Insert,"#"});
            wait(s,[](const auto& v){return v.ui && v.ui->metadata.input=="#";});capture("21-map-search");
            s.text_event({Frontend::TextEditKind::Confirm});wait(s,[&](const auto&){return !s.text_input_active();});
            for(int i=0;i<12;++i){tap(s,SDL_SCANCODE_RIGHT);tap(s,SDL_SCANCODE_RETURN);}
            capture("22-map-sequence");tap(s,SDL_SCANCODE_PAGEDOWN);capture("23-map-candidates-scrolled");
            tap(s,SDL_SCANCODE_TAB);tap(s,SDL_SCANCODE_F2);wait(s,[&](const auto&){return s.text_input_active();});
            s.text_event({Frontend::TextEditKind::Insert,"100"});s.text_event({Frontend::TextEditKind::Confirm});wait(s,[&](const auto&){return !s.text_input_active();});
            tap(s,SDL_SCANCODE_F5);wait(s,[&](const auto&){return s.text_input_active();});
            s.text_event({Frontend::TextEditKind::Insert,"100"});s.text_event({Frontend::TextEditKind::Confirm});wait(s,[&](const auto&){return !s.text_input_active();});
            capture("24-map-large");
            tap(s,SDL_SCANCODE_F2);wait(s,[&](const auto&){return s.text_input_active();});
            s.text_event({Frontend::TextEditKind::Insert,"10"});s.text_event({Frontend::TextEditKind::Confirm});wait(s,[&](const auto&){return !s.text_input_active();});
            tap(s,SDL_SCANCODE_F5);wait(s,[&](const auto&){return s.text_input_active();});
            s.text_event({Frontend::TextEditKind::Insert,"20"});s.text_event({Frontend::TextEditKind::Confirm});wait(s,[&](const auto&){return !s.text_input_active();});
            tap(s,SDL_SCANCODE_S);tap(s,SDL_SCANCODE_ESCAPE);
            choose(s,"map-1");
            auto map=wait(s,[](const auto& v){return v.ui && v.ui->metadata.gridPreview.has_value();});
            assert(std::count_if(map.ui->metadata.gridPreview->colors.begin(),map.ui->metadata.gridPreview->colors.end(),[](int c){return c>=0;})==2);
            capture("12-map-preview");
            root(s);choose(s,"presets");choose(s,"preset-new");
            wait(s,[](const auto& v){return v.ui && v.ui->metadata.id=="root";});
            choose(s,"preset-rename",false);capture("14-preset-name-focus");
            tap(s,SDL_SCANCODE_RETURN);wait(s,[&](const auto&){return s.text_input_active();});
            s.text_event({Frontend::TextEditKind::Insert,"中文预设 / Practice preset"});
            wait(s,[](const auto& v){return v.ui && v.ui->metadata.input=="中文预设 / Practice preset";});
            capture("15-preset-name");assert(renderer.text_input_rect());
            s.text_event({Frontend::TextEditKind::Confirm});
            wait(s,[&](const auto& v){return !s.text_input_active() && v.ui && v.ui->metadata.id=="root";});
            choose(s,"preset-save");
            UserConfig::Presets::Collection presets;assert(UserConfig::Presets::load(presets));
            assert(std::any_of(presets.presets.begin(),presets.presets.end(),[](const auto& p){return p.name=="中文预设 / Practice preset";}));
            tap(s,SDL_SCANCODE_ESCAPE);wait(s,[](const auto& v){return v.ui && v.ui->metadata.id=="presets";});
            root(s);tap(s,SDL_SCANCODE_R);
            wait(s,[](const auto& v){return !v.ui && v.status==Graphical::SessionStatus::Playing;});
            for(int i=0;i<12;++i) {
                if(i%3==0)tap(s,SDL_SCANCODE_UP);
                for(int j=0;j<(i%4)+1;++j)tap(s,i%2?SDL_SCANCODE_LEFT:SDL_SCANCODE_RIGHT);
                tap(s,SDL_SCANCODE_F7);
            }
            tap(s,SDL_SCANCODE_C);capture("10-game");tap(s,SDL_SCANCODE_F2);capture("11-paused");
            }
            Graphical::LaunchOptions play;play.startScreen=false;
            {
                auto finished=first;finished.autoplayMode=1;finished.autoplayPps=20;
                finished.goal=4;finished.pieceTarget=2;
                Graphical::Session s(finished,first,play);
                for(int digit:{3,2,1}) {
                    wait(s,[&](const auto& v){return v.countdownDigit==digit && !v.ui;});
                    captureSession(s,"48-live-countdown-"+std::to_string(digit));
                }
                wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Finished && v.board.pieces==2;});
                captureSession(s,"25-finished-notice");
                tap(s,SDL_SCANCODE_S);wait(s,[&](const auto&){return s.text_input_active();});
                s.text_event({Frontend::TextEditKind::Insert,"取消保存 / Cancel"});
                wait(s,[](const auto& v){return v.ui && v.ui->metadata.input=="取消保存 / Cancel";});
                captureSession(s,"16-replay-name");assert(renderer.text_input_rect());
                s.text_event({Frontend::TextEditKind::Cancel});
                wait(s,[&](const auto& v){return !s.text_input_active() && (!v.ui || !v.ui->caret);});
                tap(s,SDL_SCANCODE_TAB);
                wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Statistics;});
                captureSession(s,"26-statistics-summary");
                for(int i=1;i<=17;++i) {
                    tap(s,SDL_SCANCODE_DOWN);
                    assert(s.view().ui->metadata.statistics->selectedMetric==i);
                    captureSession(s,"49-summary-down-"+std::to_string(i));
                }
                captureSession(s,"27-statistics-scrolled");
                tap(s,SDL_SCANCODE_TAB);captureSession(s,"28-statistics-clears");tap(s,SDL_SCANCODE_TAB);
                tap(s,SDL_SCANCODE_S);wait(s,[&](const auto&){return s.text_input_active();});
                s.text_event({Frontend::TextEditKind::Insert,"中文录像 visual check.txt"});
                wait(s,[](const auto& v){return v.ui && v.ui->metadata.input=="中文录像 visual check.txt";});
                captureSession(s,"17-statistics-save");assert(renderer.text_input_rect());
                s.text_event({Frontend::TextEditKind::Confirm});
                wait(s,[&](const auto&){return !s.text_input_active() && std::filesystem::exists(std::filesystem::u8path(".tetr/replay/中文录像 visual check.txt"));});
            }
            // Extreme-data fixtures use the shared application projection,
            // with the same controls and data model as actual result/lobby pages.
            {
                Runtime::SessionState state;Runtime::RuntimeServices services;Runtime::GameContext context(state,services);
                Network::NetworkServices network;context.attach_network(network);context.attach_user_settings(first);
                App::UiText::set_language(first.uiLanguage?App::UiText::Language::English:App::UiText::Language::Chinese);
                Frontend::UiFrame frame;Frontend::UiSurfaceScope surface([&](Frontend::UiFrame f){frame=std::move(f);});
                App::PostGameStatistics data;
                for(int i=0;i<17;++i) {data.pieces.push_back("Fool "+std::to_string(i));data.pieceColors.push_back(i%7);}
                for(int j=0;j<12;++j)data.lines.push_back(j*7);
                data.cells.assign(17,std::vector<Runtime::ClearCounts>(12));data.rowTotals.resize(17);data.columnTotals.resize(12);
                for(int i=0;i<17;++i)for(int j=0;j<12;++j)for(int k=0;k<3;++k) {
                    const std::uint64_t count=i+j+k;data.cells[i][j][k]=count;data.rowTotals[i][k]+=count;data.columnTotals[j][k]+=count;data.total[k]+=count;
                }
                data.metrics={{"SCORE","9876543210"},{"PIECES PER SECOND","4.321"},{"ATTACK PER MINUTE","256.780"},{"TOTAL RECEIVED","180"},{"TOTAL ATTACK","257"}};
                App::PostGameStatisticsView results(std::vector<App::PostGameStatistics>(9,data),{"玩家一","Player Two","Three","Four","Five","Six","Seven","Eight","Nine"});
                results.render(context,"S: Save replay",{128,38});captureFrame(frame,"47-room-summary");
                results.next_page();results.move(13,10);results.select_player(7);results.move(13,10);results.render(context,"S: Save replay",{128,38});captureFrame(frame,"29-room-clear-table");
                results.move(100,100);results.render(context,"S: Save replay",{128,38});captureFrame(frame,"30-clear-totals");
                std::vector<Challenges::Outcome> outcomes;for(const auto& c:Challenges::definitions()) {outcomes.push_back({c.id,{100,42.195,0},outcomes.size()%2==0,0});if(outcomes.size()==8)break;}
                results.set_challenges(outcomes);results.next_page();results.move(6,0);results.render(context,"S: Save replay",{128,38});captureFrame(frame,"31-challenge-results");
                network.room.name="TerMino / 中文公开房间";network.room.capacity=32;network.room.players=23;
                network.matchPhase=Network::RoomMatchPhase::Preparing;network.roomPlayerIndex=0;
                for(int i=0;i<23;++i){Network::RoomMember member;member.slot=i;member.username=i==4?"很长的中文玩家名称 / Long player name":(i%2?"Player ":"玩家 ")+std::to_string(i+1);member.host=i==0;member.ready=i%3!=0;member.role=i%5==0?Network::PlayerRole::Spectator:Network::PlayerRole::Battle;network.roomMembers.push_back(member);}
                std::size_t offset=8;App::TerminalCanvas canvas;
                App::paint_room_waiting(context,canvas,{128,38},0,offset);
                assert(!frame.metadata.lobby->members[5].ready);
                assert(std::count_if(frame.metadata.lobby->members.begin(),frame.metadata.lobby->members.end(),[](const auto& member){return member.ready;})==frame.metadata.lobby->ready);
                captureFrame(frame,"32-room-lobby");
                for(auto& member:network.roomMembers)member.ready=false;offset=0;
                App::paint_room_waiting(context,canvas,{128,38},0,offset);captureFrame(frame,"34-room-not-ready");
                Frontend::UiFrame connection;connection.metadata={Frontend::UiKind::Notice,"network-waiting",first.uiLanguage?"Network":"网络连接"};
                connection.metadata.hint=first.uiLanguage?"Joining current match… / Practice room":"正在加入当前对局… / 练习房间";
                connection.metadata.shortcuts={"P: Settings    R: Reconnect    Esc: Start"};captureFrame(connection,"33-network-notice");
            }
            first.gameMode=6;auto second=first;second.keyCodes[3]=Graphical::native_key(SDL_SCANCODE_KP_ENTER);
            second.username=first.uiLanguage?"Player Two":"玩家二";
            Graphical::Session duo(first,second,play);
            wait(duo,[](const auto& v){return v.status==Graphical::SessionStatus::Playing && v.peers.size()==1;});
            tap(duo,SDL_SCANCODE_SPACE);tap(duo,SDL_SCANCODE_KP_ENTER);
            const auto pair=duo.view();renderer.begin(width,height);Graphical::background(renderer);Graphical::scene_view(renderer,pair);
            renderer.end();renderer.screenshot((output/"13-local-versus.bmp").generic_u8string(),width,height);
            auto dense=pair;dense.peers.clear();dense.ui.reset();auto& b=dense.board;
            b.quickPlay=true;b.altitude=9876.5;b.floor=10;b.booster=8;b.experience=18;b.boosterProgress=18./32.;b.garbage={3,4,2};b.garbageReadyIn={0,.15,1};
            b.spinPiece=Piece::types_for_rotation_system(Runtime::RotationSystem::SrsPlus).at(5);b.mini=true;b.clearLines=4;
            b.perfectClear=true;b.clutch=3;b.spike=24;b.windup=10;b.blighted=true;b.population=32;b.rank=4;
            b.stages=3;b.stageTask="A long stage objective with a piece sequence to complete";
            dense.challengeNotice="Challenge progress / 挑战进度更新";
            renderer.begin(width,height);Graphical::background(renderer);Graphical::game_view(renderer,dense);
            renderer.end();renderer.screenshot((output/"35-dense-left-hud.bmp").generic_u8string(),width,height);
            dense.board=pair.board;dense.room=true;dense.peers.clear();
            for(int i=0;i<7;++i){auto peer=pair.board;peer.username="Player "+std::to_string(i+2);dense.peers.push_back({i+1,true,std::move(peer)});}
            renderer.begin(width,height);Graphical::background(renderer);Graphical::scene_view(renderer,dense);
            renderer.end();renderer.screenshot((output/"36-room-boards.bmp").generic_u8string(),width,height);
            const auto captureView=[&](const Graphical::SessionView& view,const std::string& name) {
                renderer.reset_event_animations();renderer.begin(width,height,0);Graphical::background(renderer);
                if(view.room || !view.peers.empty())Graphical::scene_view(renderer,view);else Graphical::game_view(renderer,view);
                renderer.end();renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
            };
            auto standard=pair;standard.peers.clear();standard.board.elapsed=Runtime::Duration::from_seconds(83.417);
            standard.board.pieces=198;standard.board.attack=142;standard.board.lines=76;standard.board.score=123456;
            standard.board.clearLines=2;standard.board.b2b=7;standard.board.combo=4;
            standard.board.spinPiece=Piece::Type::from_index(5);standard.board.garbage={3,4,2};standard.board.garbageReadyIn={0,.15,1};
            captureView(standard,"37-gameplay-layout");
            for(const int remaining:{5,2,1,0}) {
                auto finite=standard;finite.board.nextCount=5;finite.board.next.resize(remaining);
                finite.board.hold=Piece::Type::from_index(5);
                captureView(finite,"140-finite-next-"+std::to_string(remaining));
            }
            auto clock=standard;clock.board.goal={Runtime::GameGoal::Time,120,36,84,0,"s"};
            clock.board.elapsed=Runtime::Duration::from_seconds(36.583);
            captureView(clock,"141-time-minutes");
            {
                Runtime::RuntimeConfig config;config.field.invisible=true;config.field.invisibilityDelay=Runtime::Duration::from_seconds(2);
                auto player=std::make_unique<Runtime::PlayerSessionState>();auto& f=player->gameplay.playfield;
                f.width=10;f.visibleHeight=f.activeHeight=20;f.field.initialize(10,20);
                for(int row=14;row<19;++row)for(int col=0;col<10;++col)f.field.set_cell(row,col,Runtime::BoardCell::piece(col%7));
                for(int col=0;col<10;++col)if(col!=5)f.field.set_cell(19,col,Runtime::BoardCell::garbage());
                int index=0;
                for(double time:{0.,1.,1.25,1.5,1.75,2.,3.}) {
                    auto frame=standard;frame.board=Presentation::capture_board_view(config,*player,f.field,Runtime::Duration::from_seconds(time));
                    captureView(frame,"89-delayed-opacity-"+std::to_string(index++));
                }
                config.field.periodicInvisible=true;config.field.invisibilityDuration=Runtime::Duration::from_seconds(3);index=0;
                for(double time:{4.9,5.,5.5,6.,6.5,7.,7.5,8.}) {
                    auto frame=standard;frame.board=Presentation::capture_board_view(config,*player,f.field,Runtime::Duration::from_seconds(time));
                    captureView(frame,"90-periodic-opacity-"+std::to_string(index++));
                }
                config.field.invisible=false;
                player->presentation.remoteFieldVisibility=Runtime::FieldVisibility{true,true,{},Runtime::Duration::from_seconds(5),Runtime::Duration::from_seconds(3)};
                auto frame=standard;frame.room=true;frame.peers={{1,false,Presentation::capture_board_view(config,*player,f.field,Runtime::Duration::from_seconds(5.5))}};
                captureView(frame,"91-independent-peer-visibility");
                for(int fieldWidth:{20,40,80}) {
                    f.width=fieldWidth;f.field.initialize(fieldWidth,20);
                    for(int row=10;row<20;++row)for(int col=0;col<fieldWidth;++col)
                        if(col!=fieldWidth/2)f.field.set_cell(row,col,Runtime::BoardCell::garbage());
                    const auto source=Network::capture_mini_board(*player,16,14,1);
                    const auto mini=Network::project_mini_board(source,8,7);
                    Graphical::SceneBoard peer;peer.slot=1;peer.miniature=true;peer.logicalWidth=fieldWidth;peer.logicalHeight=20;
                    peer.sampleRows=2.*fieldWidth/mini.columns;peer.board.width=mini.columns;peer.board.bottom=mini.rows;peer.board.firstRow=0;peer.board.username="WIDE FIELD";
                    for(const auto& cell:mini.cells)peer.board.cells.push_back({cell,cell.occupied()?Presentation::CellAppearance::Visible:Presentation::CellAppearance::Empty});
                    auto gallery=standard;gallery.room=true;gallery.peers={peer};captureView(gallery,"92-wide-garbage-hole-"+std::to_string(fieldWidth));
                }
            }
            auto headroom=standard;headroom.board.firstRow=-100;headroom.board.cells.assign(120*10,{});
            headroom.board.active.reset();headroom.board.ghost.reset();headroom.board.perfectClear=false;
            captureView(headroom,"130-main-six-empty");
            for(int col=0;col<10;++col) {
                headroom.board.cells[94*10+col]={Runtime::BoardCell::piece(0),Presentation::CellAppearance::Visible};
                headroom.board.cells[93*10+col]={Runtime::BoardCell::piece(6),Presentation::CellAppearance::Visible};
            }
            captureView(headroom,"130-main-six-rows");
            for(int columns:{4,10,20,40,100}) {
                auto preview=headroom;auto& b=preview.board;b.width=columns;b.cells.assign(120*columns,{});
                const auto g=Graphical::solo_geometry(columns,20);
                const auto region=Graphical::top_preview_geometry(g,columns);
                for(int col=0;col<columns;++col) {
                    b.cells[(100-region.rows)*columns+col]={Runtime::BoardCell::piece(0),Presentation::CellAppearance::Visible};
                    b.cells[99*columns+col]={Runtime::BoardCell::piece(2),Presentation::CellAppearance::Visible,true};
                    if(region.rows<100)b.cells[(99-region.rows)*columns+col]={Runtime::BoardCell::piece(6),Presentation::CellAppearance::Visible};
                }
                captureView(preview,"131-preview-"+std::to_string(columns));
            }
            const auto fourPreview=[&](Graphical::SessionView v,const std::string& name) {
                auto& b=v.board;b.width=4;b.cells.assign((100+b.bottom)*4,{});
                const auto g=Graphical::solo_geometry(4,b.bottom);
                const std::size_t cues=(b.goal.target>0?1:0)+(b.growth.nextSize>0?1:0);
                const auto region=Graphical::top_preview_geometry(g,4,Graphical::next_rail_top(g,Graphical::next_slot_count(b),cues));
                for(int col=0;col<4;++col) {
                    b.cells[(100-region.rows)*4+col]={Runtime::BoardCell::piece(0),Presentation::CellAppearance::Visible};
                    b.cells[99*4+col]={Runtime::BoardCell::piece(2),Presentation::CellAppearance::Visible,true};
                }
                captureView(v,name);
            };
            for(int height:{0,4,20})for(bool goal:{false,true}) {
                auto four=headroom;four.board.bottom=height;
                if(goal)four.board.goal={Runtime::GameGoal::Lines,5,0,5,0,"L"};
                fourPreview(four,"139-four-"+std::to_string(height)+(goal?"-goal":"-none"));
            }
            auto twoCues=headroom;twoCues.board.bottom=4;twoCues.board.quickPlay=true;
            twoCues.board.goal={Runtime::GameGoal::Lines,5,0,5,0,"L"};twoCues.board.growth={5,1,.95};
            fourPreview(twoCues,"139-four-two-goals");
            auto manyNext=headroom;manyNext.board.bottom=0;manyNext.board.next.assign(100,Piece::Type::from_index(0));
            fourPreview(manyNext,"139-four-many-next");
            auto initial=headroom;initial.board.cells.assign(initial.board.cells.size(),{});
            initial.board.elapsed=Runtime::Duration{};initial.board.goal={Runtime::GameGoal::Lines,5,0,5,0,"L"};
            captureView(initial,"134-playing-initial-goal");
            initial.status=Graphical::SessionStatus::Countdown;initial.countdownDigit=3;
            captureView(initial,"133-countdown-goal");
            initial.board.goal={Runtime::GameGoal::Time,5,0,5,0,"s"};captureView(initial,"135-countdown-time");
            initial.board.goal={Runtime::GameGoal::Lines,21,0,21,0,"L"};captureView(initial,"136-countdown-far-goal");
            initial.board.goal={};captureView(initial,"137-countdown-no-goal");
            auto overflow=standard;overflow.board.active->pose.position.row=-8;
            overflow.board.score=999999999999LL;overflow.board.cells[0].appearance=Presentation::CellAppearance::Dimmed;
            overflow.board.cells[0].value=Runtime::BoardCell::piece(3);
            captureView(overflow,"38-stable-overflow");
            auto top=standard;top.board.firstRow=-16;top.board.cells.assign(36*10,{});
            top.board.active.reset();top.board.ghost.reset();top.board.perfectClear=false;
            captureView(top,"120-top-empty");
            auto boundaries=top;
            for(const auto cell:{Runtime::CellPosition{-11,0},{-10,1},{-1,8},{0,9}})
                boundaries.board.cells[(cell.row+16)*10+cell.col]={Runtime::BoardCell::piece(0),Presentation::CellAppearance::Visible};
            captureView(boundaries,"121-top-boundaries");
            auto marked=top;marked.board.active=Presentation::PieceView{Piece::Type::from_index(5),Piece::Pose{{-6,3},Runtime::Rotation{}}};
            marked.board.ghost=Presentation::PieceView{Piece::Type::from_index(1),Piece::Pose{{-4,3},Runtime::Rotation{}}};
            for(int row=-10;row<0;++row)for(int col=0;col<10;++col)
                marked.board.cells[(row+16)*10+col]={Runtime::BoardCell::piece(2),Presentation::CellAppearance::Visible};
            // Spawn hints on settled, ghost and active cells must remain dots
            // at the final layer, including the two crop boundaries.
            for(const auto cell:{Runtime::CellPosition{-10,0},{-1,9}})marked.board.cells[(cell.row+16)*10+cell.col].hint=true;
            for(const auto& piece:{marked.board.active,marked.board.ghost}) {
                const auto definition=Piece::get_definition(piece->type);
                for(const auto& c:definition.cells(piece->pose.rotation))
                    marked.board.cells[(piece->pose.position.row+c[0]+16)*10+piece->pose.position.col+c[1]].hint=true;
            }
            captureView(marked,"122-top-marked");
            auto unmarked=marked;for(auto& cell:unmarked.board.cells)cell.hint=false;
            captureView(unmarked,"123-top-unmarked");
            marked.board.obscuredColors=true;captureView(marked,"124-top-obscured");
            top.board.firstRow=-3;top.board.cells.assign(23*10,{});captureView(top,"125-top-short-snapshot");
            top.board.next.clear();top.board.nextCount=0;captureView(top,"126-top-no-next");
            top=standard;top.board.firstRow=-16;top.board.cells.assign(36*10,{});top.board.active.reset();top.board.ghost.reset();top.board.perfectClear=false;
            for(int row=-10;row<0;++row)for(int col=0;col<10;++col)
                top.board.cells[(row+16)*10+col]={Runtime::BoardCell::piece(0),Presentation::CellAppearance::Empty};
            captureView(top,"127-top-concealed");
            for(int level:{1,4,8,12}) {
                auto qp=standard;qp.board.quickPlay=true;qp.board.booster=level;qp.board.experience=2*level;
                qp.board.boosterProgress=.5;qp.board.floor=8;qp.board.altitude=1108.9;
                captureView(qp,"39-booster-"+std::to_string(level));
            }
            for(int count:{1,16,31}) {
                auto room=standard;room.room=true;room.peers.clear();
                for(int i=0;i<count;++i) {
                    Graphical::SceneBoard peer{i+1,true,pair.board};peer.board.username="Player "+std::to_string(i+2);
                    peer.board.active.reset();peer.board.ghost.reset();
                    for(int row=13-i%8;row<20;++row)for(int col=0;col<10;++col)if(col!=i%10)
                        peer.board.cells[(row-peer.board.firstRow)*10+col]={Runtime::BoardCell::piece((i+row)%7),Presentation::CellAppearance::Visible};
                    room.peers.push_back(std::move(peer));
                }
                captureView(room,"40-gallery-"+std::to_string(count));
            }
            auto duel=standard;duel.room=true;duel.peers={{1,false,pair.board}};captureView(duel,"41-final-duel");
            auto initialDuel=duel;initialDuel.status=Graphical::SessionStatus::Countdown;initialDuel.countdownDigit=3;
            initialDuel.board.goal={Runtime::GameGoal::Lines,5,0,5,0,"L"};
            initialDuel.peers[0].board.goal=initialDuel.board.goal;
            captureView(initialDuel,"138-duel-countdown-goal");
            auto sampled=standard;sampled.room=true;
            Graphical::SceneBoard mini;mini.miniature=true;mini.logicalWidth=10;mini.logicalHeight=20;mini.sampleRows=2;
            mini.board.width=10;mini.board.bottom=14;mini.board.username="SAMPLED";mini.board.cells.resize(140);
            for(int i=85;i<140;++i)if(i%10!=4)mini.board.cells[i]={Runtime::BoardCell::piece(4),Presentation::CellAppearance::Visible};
            mini.board.cells[22]={Runtime::BoardCell::piece(5),Presentation::CellAppearance::Visible};
            sampled.peers.push_back(mini);captureView(sampled,"45-sampled-miniature");
            auto spectating=duel;spectating.spectator=true;spectating.ownBoardVisible=false;
            spectating.peers[0].board.username="FOCUSED PLAYER";captureView(spectating,"42-spectator");
            auto mixed=duel;mixed.peers[0].board.width=32;mixed.peers[0].board.bottom=40;mixed.peers[0].board.firstRow=-8;mixed.peers[0].board.cells.resize(32*48);
            captureView(mixed,"46-room-large-peer");
            auto wide=standard;wide.board.width=16;wide.board.bottom=24;wide.board.firstRow=-8;wide.board.cells.resize(16*32);
            wide.board.perfectClear=true;wide.board.clutch=4;wide.challengeNotice="Clear the target while keeping the board stable";
            captureView(wide,"43-wide-field");
            auto shortField=standard;shortField.board.bottom=8;shortField.board.cells.resize(10*(8-shortField.board.firstRow));
            captureView(shortField,"44-short-field");
            for(int digit:{3,2,1}) {
                auto ready=duel;ready.countdownDigit=digit;ready.status=Graphical::SessionStatus::Countdown;
                captureView(ready,"50-duel-countdown-"+std::to_string(digit));
                ready.spectator=true;ready.ownBoardVisible=false;
                captureView(ready,"51-spectator-countdown-"+std::to_string(digit));
            }
            auto allspin=standard;allspin.board.active.reset();allspin.board.ghost.reset();
            allspin.board.quickPlay=true;allspin.board.booster=4;allspin.board.boosterProgress=.65;allspin.board.spike=25;
            allspin.board.altitude=532.8;allspin.board.floor=5;allspin.board.combo=6;allspin.board.b2b=3;
            for(int row=16;row<20;++row)for(int col=0;col<10;++col) {
                using Cell=Runtime::BoardCell;
                const auto value=row==19?(col==4?Cell::timed_release():Cell::timed_solid()):
                    row==18?Cell::timed_solid(col%5+1):row==17?Cell::solid_garbage():Cell::garbage();
                allspin.board.cells[(row-allspin.board.firstRow)*10+col]={value,Presentation::CellAppearance::Visible};
            }
            captureView(allspin,"52-allspin-garbage");
            allspin.board.squareStyle=1;captureView(allspin,"53-allspin-narrow");
            allspin.board.obscuredColors=true;captureView(allspin,"54-allspin-obscured");
            auto spike=standard;spike.board.spike=32;captureView(spike,"55-old-side-spike");
            spike.board.b2b=1;spike.board.combo=1;spike.board.spike=0;spike.board.spinPiece=Piece::Type::none();spike.board.clearLines=0;
            captureView(spike,"56-inactive-events");
            spike=standard;spike.board.mini=true;spike.board.spike=15;captureView(spike,"58-mini-spin");
            spike.board.combo=123;spike.board.b2b=123;spike.board.spike=1234;captureView(spike,"59-large-event-counts");
            for(const auto goal:{Runtime::GameGoal::Time,Runtime::GameGoal::Lines,Runtime::GameGoal::Garbage,
                Runtime::GameGoal::Pieces,Runtime::GameGoal::Attack,Runtime::GameGoal::AllClear,Runtime::GameGoal::InitialCells}) {
                Runtime::RuntimeConfig config;config.mode.goal=goal;
                config.mode.blitzTime=Runtime::Duration::from_seconds(22);
                config.mode.racingDistance=config.mode.garbageTarget=config.mode.pieceTarget=config.mode.attackTarget=config.mode.allClearTarget=22;
                auto player=std::make_unique<Runtime::PlayerSessionState>();player->gameplay.playfield.activeHeight=20;
                auto& score=player->gameplay.scoring;score.totalLines=score.totalGarbageLines=score.placedBlocks=score.totalAttack=score.allClearCount=17;
                auto map=std::make_shared<Runtime::MapDefinition>();map->rows[19][0]='G';config.initialMap=map;
                auto& f=player->gameplay.playfield.field;f.initialize(10,20);f.set_cell(19,0,Runtime::BoardCell::garbage());f.mark_initial_cell(19,0);
                auto goalFrame=standard;goalFrame.board.goal=Presentation::capture_goal_view(config,*player,Runtime::Duration::from_seconds(17.2));
                if(goal==Runtime::GameGoal::AllClear || goal==Runtime::GameGoal::InitialCells)
                    assert(goalFrame.board.goal.markerRows==0);
                else assert(goalFrame.board.goal.markerRows>0);
                captureView(goalFrame,"60-goal-"+std::to_string(static_cast<int>(goal)));
            }
            for(int height:{0,1,2,3,4,8})for(int width:{4,10}) {
                auto shortBoard=standard;auto& b=shortBoard.board;
                b.width=width;b.bottom=height;b.cells.assign((height-b.firstRow)*width,{});
                b.active=Presentation::PieceView{Piece::Type::from_index(5),Piece::spawn_pose(width,Piece::Type::from_index(5))};
                b.ghost.reset();b.spinPiece=Piece::Type::none();b.clearLines=0;b.perfectClear=false;
                captureView(shortBoard,"110-field-"+std::to_string(width)+"x"+std::to_string(height));
            }
            for(int height:{0,1,3,4,20})for(int remaining:{0,1,3,5,20,21}) {
                auto target=standard;auto& b=target.board;
                b.bottom=height;b.cells.assign((height-b.firstRow)*b.width,{});
                b.active.reset();b.ghost.reset();b.perfectClear=false;
                b.spinPiece=Piece::Type::none();b.clearLines=0;b.growth={};
                b.goal={Runtime::GameGoal::Lines,40,40-remaining,remaining,0,"L"};
                captureView(target,"114-target-"+std::to_string(height)+"-"+std::to_string(remaining));
                if(height==20 && remaining==5) {
                    for(int col=0;col<8;++col)b.cells[(17-b.firstRow)*b.width+col]=
                        {Runtime::BoardCell::piece(col),Presentation::CellAppearance::Visible};
                    captureView(target,"115-target-blocks");
                    b.growth={6,2,.9};captureView(target,"116-target-and-growth");
                    b.obscuredColors=true;captureView(target,"117-target-obscured");
                    target.status=Graphical::SessionStatus::Paused;captureView(target,"118-target-paused");
                }
            }
            for(int lines:{0,19,20,39,300,319,320}) {
                auto state=std::make_unique<Runtime::SessionState>();Runtime::RuntimeServices services;
                Runtime::GameContext context(*state,services);context.config.garbage.mode=Runtime::GarbageMode::QuickPlay;
                context.config.quickPlay.snowball=Runtime::ModifierMode::Enabled;
                auto& p=state->player;const int size=Rules::qp_snowball_size(lines);
                p.gameplay.playfield.width=size;p.gameplay.playfield.visibleHeight=p.gameplay.playfield.activeHeight=size;
                p.gameplay.playfield.field.initialize(size,size);p.gameplay.playfield.holdEnabled=true;
                p.gameplay.hold= {};p.gameplay.hold.piece=standard.board.hold;p.pieces.nextPreview=standard.board.next;
                p.gameplay.scoring.totalLines=lines;p.presentation.username="SNOWBALL";
                auto snowball=standard;snowball.board=Presentation::capture_board_view(context,p);
                captureView(snowball,"111-snowball-"+std::to_string(lines));
                if(lines==19) {
                    snowball.board.goal={Runtime::GameGoal::Lines,40,19,21,0,"L"};
                    captureView(snowball,"112-snowball-with-goal");
                    snowball.board.obscuredColors=true;captureView(snowball,"113-snowball-obscured");
                }
            }
            for(int millis:{0,100,200,500,900}) {
                auto timer=standard;auto& b=timer.board;
                b.cells.assign(b.cells.size(),{});b.active.reset();b.ghost.reset();b.perfectClear=false;
                b.spinPiece=Piece::Type::none();b.clearLines=0;b.growth={};
                b.elapsed=Runtime::Duration::from_seconds(35+millis/1000.);
                b.goal={Runtime::GameGoal::Time,40,35,5,0,"s"};
                captureView(timer,"119-time-continuous-"+std::to_string(millis));
            }
            for(const auto title:{"GAME OVER","FINISH","YOU WIN"}) {
                auto end=standard;end.status=Graphical::SessionStatus::Finished;end.board.headline=title;
                captureView(end,std::string("61-centered-")+title);
            }
            auto endedDuel=duel;endedDuel.status=Graphical::SessionStatus::Finished;
            endedDuel.board.headline="YOU WIN";endedDuel.peers[0].board.headline="GAME OVER";
            captureView(endedDuel,"62-centered-duel");
            auto result=duel;result.status=Graphical::SessionStatus::Finished;
            result.board.finished=true;result.board.result=Presentation::RoundResult::Win;
            result.peers[0].board.finished=true;result.peers[0].board.result=Presentation::RoundResult::Defeat;
            captureView(result,"84-battle-results");
            auto observed=duel;observed.status=Graphical::SessionStatus::Finished;observed.ownBoardVisible=false;
            observed.board.finished=true;observed.board.result=Presentation::RoundResult::Defeat;
            observed.peers[0].board.finished=false;observed.peers[0].board.result=Presentation::RoundResult::None;
            captureView(observed,"83-surviving-opponent");
            observed.status=Graphical::SessionStatus::Playing;captureView(observed,"83-surviving-opponent-control");
            auto roster=standard;roster.room=true;roster.status=Graphical::SessionStatus::Countdown;roster.countdownDigit=3;
            for(int i=0;i<3;++i){Graphical::SceneBoard peer{i+1,true,standard.board};peer.board.username="READY "+std::to_string(i+1);roster.peers.push_back(std::move(peer));}
            captureView(roster,"82-roster-countdown");
            auto attack=standard;attack.board.garbagePhases={Presentation::GarbagePhase::Ready,Presentation::GarbagePhase::Incoming,Presentation::GarbagePhase::Early};
            captureView(attack,"85-garbage-phases");
            auto remoteSpike=standard;remoteSpike.room=true;
            Graphical::SceneBoard spikePeer{1,true,standard.board};spikePeer.board.username="REMOTE";spikePeer.board.spike=28;spikePeer.board.wins=3;
            remoteSpike.peers.push_back(spikePeer);captureView(remoteSpike,"87-remote-spike");
            auto miniResult=remoteSpike;miniResult.peers[0].board.result=Presentation::RoundResult::Defeat;
            captureView(miniResult,"88-mini-defeat");
            auto cleared=standard;cleared.board.perfectClear=true;cleared.board.halfClear=false;
            cleared.board.active.reset();cleared.board.ghost.reset();cleared.board.spinPiece=Piece::Type::none();
            for(auto& c:cleared.board.cells)c={Runtime::BoardCell::empty(),Presentation::CellAppearance::Empty};
            captureView(cleared,"63-all-clear");
            auto half=standard;half.board.halfClear=true;half.board.perfectClear=false;
            captureView(half,"64-half-clear");
            auto clearedEnd=cleared;clearedEnd.status=Graphical::SessionStatus::Finished;clearedEnd.board.headline="FINISH";
            captureView(clearedEnd,"65-finished-all-clear");
            auto clearsDuel=duel;clearsDuel.board=cleared.board;clearsDuel.peers[0].board=half.board;
            captureView(clearsDuel,"66-duel-clears");
            for(int count:{1,3,4,6,8,12,24,30,60,77,100,150,250,500}) {
                auto chain=standard;chain.board.b2b=count+1;chain.board.combo=1;chain.board.spike=0;
                chain.board.spinPiece=Piece::Type::none();chain.board.clearLines=0;
                captureView(chain,"67-b2b-"+std::to_string(count));
                if(count==77) {chain.board.obscuredColors=true;captureView(chain,"68-b2b-obscured");}
            }
            auto marker=standard;marker.board.spinPiece=Piece::Type::none();marker.board.clearLines=0;
            marker.board.active.reset();marker.board.ghost.reset();
            for(int row:{-1,0,1})for(int col=3;col<7;++col)
                marker.board.cells[(row-marker.board.firstRow)*10+col]={Runtime::BoardCell::piece(col),Presentation::CellAppearance::Visible,true};
            captureView(marker,"69-spawn-settled");
            marker.board.active=Presentation::PieceView{Piece::Type::from_index(5),Piece::Pose{{0,3},Runtime::Rotation{}},Presentation::CellAppearance::Visible};
            captureView(marker,"70-spawn-active");
            marker.board.active.reset();marker.board.ghost=Presentation::PieceView{Piece::Type::from_index(5),Piece::Pose{{0,3},Runtime::Rotation{}},Presentation::CellAppearance::Dimmed};
            captureView(marker,"71-spawn-ghost");
            marker.board.ghost.reset();
            for(int col=3;col<7;++col)marker.board.cells[(1-marker.board.firstRow)*10+col]={Runtime::BoardCell::timed_solid(col),Presentation::CellAppearance::Visible,true};
            captureView(marker,"72-spawn-numbered");
            auto previews=standard;previews.board.hold=Piece::Type::from_index(5);
            previews.board.holdAvailable=true;previews.board.next={Piece::Type::from_index(0),Piece::Type::from_index(5),Piece::Type::from_index(3)};
            captureView(previews,"73-preview-size");
            // A deterministic render clock samples complete effects without
            // sleeping or advancing any live gameplay/replay state.
            const auto motionFrame=[&](const Graphical::SessionView& v,const std::string& name,double now) {
                renderer.begin(width,height,now);Graphical::background(renderer);
                if(v.room || !v.peers.empty())Graphical::scene_view(renderer,v);else Graphical::game_view(renderer,v);
                renderer.end();renderer.screenshot((output/(name+".bmp")).generic_u8string(),width,height);
                SDL_GL_SwapWindow(window);
            };
            auto animated=standard;animated.board.spike=32;animated.board.perfectClear=true;
            for(auto& c:animated.board.cells)c={Runtime::BoardCell::empty(),Presentation::CellAppearance::Empty};
            const double baseTime=animated.board.elapsed.seconds();
            renderer.reset_event_animations();
            for(int millis:{0,740,1490,1500,2000}) {
                auto spike=animated;spike.board.perfectClear=false;
                spike.board.elapsed=Runtime::Duration::from_seconds(baseTime+millis/1000.);
                motionFrame(spike,"128-spike-duration-"+std::to_string(millis),millis/1000.);
            }
            renderer.reset_event_animations();
            bool movie=width==1200 && height==800 && !first.uiLanguage;
#ifdef _WIN32
            movie=false;
#endif
            for(int i=0;i<=62;++i) {
                const bool key=i==0 || i==1 || i==4 || i==8 || i==12 || i==25 || i==38 || i==48 || i==55 || i==62;
                if(!movie && !key)continue;
                const double age=i/10.;animated.board.elapsed=Runtime::Duration::from_seconds(baseTime+age);
                motionFrame(animated,"74-motion-"+std::to_string(i),age);
            }
            renderer.reset_event_animations();animated.board.elapsed=Runtime::Duration::from_seconds(baseTime);
            motionFrame(animated,"75-motion-start",0);
            animated.board.elapsed=Runtime::Duration::from_seconds(baseTime+.4);motionFrame(animated,"75-motion-before-pause",.4);
            animated.status=Graphical::SessionStatus::Paused;motionFrame(animated,"75-motion-paused",5);
            animated.status=Graphical::SessionStatus::Playing;motionFrame(animated,"76-motion-resumed",5.1);
            ++animated.board.pieces;animated.board.elapsed=Runtime::Duration::from_seconds(baseTime+.5);
            motionFrame(animated,"77-motion-retriggered",5.2);
            animated.board.elapsed=Runtime::Duration::from_seconds(baseTime+1);motionFrame(animated,"78-motion-identical",5.7);
            ++animated.board.pieces;animated.board.perfectClear=false;animated.board.halfClear=true;
            animated.board.elapsed=Runtime::Duration::from_seconds(baseTime+1.1);motionFrame(animated,"79-motion-halfclear",5.8);
            renderer.reset_event_animations();auto independent=clearsDuel;
            motionFrame(independent,"80-motion-duel-start",0);
            independent.board.elapsed+=Runtime::Duration::from_seconds(3);
            independent.peers[0].board.elapsed+=Runtime::Duration::from_seconds(3);++independent.board.pieces;
            motionFrame(independent,"80-motion-duel-independent",3);
            renderer.reset_event_animations();auto complete=clearedEnd;
            motionFrame(complete,"81-motion-finished-start",0);motionFrame(complete,"81-motion-finished-expired",6.3);
            auto tiny=standard;tiny.board.bottom=2;tiny.countdownDigit=3;tiny.status=Graphical::SessionStatus::Countdown;
            tiny.board.active.reset();tiny.board.ghost.reset();tiny.board.cells.resize(10*(2-tiny.board.firstRow));
            captureView(tiny,"57-two-row-countdown");
            }
        }
        SDL_GL_DestroyContext(gl);SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"HIDDEN_VISUAL_CHECK_OK "<<width<<'x'<<height<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
}
