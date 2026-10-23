#include "../src/frontend/android/mobile_ui.h"
#include "tetr/input/action.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace TetrTests::mobile_controls_test {
int run(int,char**) {
    using namespace Graphical;using namespace Graphical::Mobile;
    const auto old=std::filesystem::current_path();
    const auto dir=std::filesystem::temp_directory_path()/std::filesystem::u8path("termino-mobile-editor-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);std::filesystem::current_path(dir);
    SessionView view;Area area;Controls c;c.edit();
    assert(!c.frame(view,area).properties);
    assert(c.frame(view,area).railToggle && !c.frame(view,area).railVisible && c.frame(view,area).rail.empty());
    c.command(Command::ToggleRail);
    assert(c.frame(view,area).railVisible && c.frame(view,area).rail.size()==10);
    {
        auto before=c.frame(view,area);assert(before.railMaxOffset>0 && before.rail[0].bounds.h==76);
        c.scroll_rail(18.5f);auto moved=c.frame(view,area);
        assert(moved.railOffset==18.5f && moved.rail[0].bounds.y==before.rail[0].bounds.y-18.5f);
        assert(!rail_hit(moved,{20,moved.railViewport.y-1}));
        assert(c.frame(view,area).railOffset==18.5f);
        c.scroll_rail(100000);moved=c.frame(view,area);
        assert(moved.railOffset==moved.railMaxOffset && moved.rail.back().bounds.y+moved.rail.back().bounds.h<=moved.railViewport.y+moved.railViewport.h);
        c.scroll_rail(-100000);assert(c.frame(view,area).railOffset==0);
    }
    for(const auto a:{Area{},Area{-266,0,1732,800},Area{66,24,1066,752}}) {
        const auto field=editor_field_bounds(a);
        assert(std::abs(field.w/10-field.h/20)<.0001f);
        assert(std::abs(field.x+field.w/2-a.x-a.width/2)<.001);
        assert(std::abs(field.y+field.h/2-a.y-a.height/2)<.001);
        Button button;
        for(const double x:{.1,.49,.5,.9}) {
            button.center.x=x;const auto panel=property_bounds(a,button);
            if(x<.5)assert(panel.x>=a.x+a.width/2);
            else assert(panel.x+panel.w<=a.x+a.width/2);
            assert(panel.x>=a.x+156 && panel.x+panel.w<=a.x+a.width);
        }
    }
    c.command(Command::Properties);
    {
        Frontend::UiMetadata desktop;desktop.kind=Frontend::UiKind::Menu;desktop.id="root";desktop.page=42;
        desktop.hint="→ / Enter 编辑；留空保留当前值。";
        desktop.notice="再次按 Enter 确认重置；方向键取消。";
        desktop.shortcuts={"↑ ↓ 选择 Enter 进入", "Backspace 返回 Esc 开始界面 [R] 重开 [P] 根目录"};
        desktop.entries.push_back({"path","Replay",true,"Enter-Esc.txt"});desktop.input="Enter-Esc.txt";
        const auto mobile=touch_metadata(desktop);
        assert(mobile.shortcuts.empty() && mobile.hint=="留空保留当前值。");
        assert(mobile.notice=="再次点按确认重置；选择其他项目可取消。");
        assert(mobile.input==desktop.input && mobile.entries[0].value==desktop.entries[0].value);
        assert(Frontend::same_ui_page(desktop,mobile)); // Display adaptation must not invalidate real touch commands.
        assert(desktop.shortcuts.size()==2 && desktop.hint.find("Enter")!=std::string::npos);
        desktop.hint="Latest records. Enter twice to play an existing replay.";
        desktop.notice="Press Enter again to delete this replay file. Arrow keys cancel.";
        const auto english=touch_metadata(desktop);
        assert(english.hint=="Latest records. Tap twice to play an existing replay.");
        assert(english.notice=="Tap again to delete this replay file. Select another item to cancel.");
        desktop.hint="选择玩家1、玩家2或双方；Tab 循环切换对象并保留当前条目。";
        assert(touch_metadata(desktop).hint=="选择玩家1、玩家2或双方。");
    }
    {
        auto text=std::make_shared<Frontend::UiFrame>();text->metadata.kind=Frontend::UiKind::Text;text->overlay=true;
        SessionView prompt;prompt.status=SessionStatus::Finished;prompt.ui=text;prompt.replay=true;
        assert(content_page(prompt) && !gameplay_surface(prompt));
        Frame f;f.area=area;auto buttons=rail(f,prompt);
        assert(buttons[1].command==Command::Confirm && buttons[1].label!="Retry" && buttons[1].label!="Pause / play");
        text->metadata.kind=Frontend::UiKind::Statistics;
        assert(!content_page(prompt) && !gameplay_surface(prompt));
        buttons=rail(f,prompt);
        assert(buttons.size()==1 && buttons[0].command==Command::Back);
    }
    const auto click=[&](int row){
        const auto f=c.frame(view,area);const auto box=property_bounds(area,f.layout.buttons[f.selected]);
        c.editor_down({box.x+160,box.y+46+propertyRowHeight*row+10},area);c.editor_up();
    };
    click(4);assert(c.text_active());c.text("0.31");c.accept_field();
    assert(!c.text_active() && c.frame(view,area).layout.buttons[0].opacity==.31);
    click(2);c.text("-4");c.accept_field();assert(c.text_active());c.cancel_field();
    assert(c.frame(view,area).layout.buttons[0].width==.21);
    c.command(Command::Properties);
    c.editor_down({828,576},area);c.editor_move({888,536},area);c.editor_up();
    assert(std::abs(c.frame(view,area).layout.buttons[0].center.x-.74)<1e-9);
    c.command(Command::Duplicate);c.command(Command::Player);c.command(Command::Enabled);
    auto f=c.frame(view,area);assert(f.layout.buttons.size()==12 && f.layout.buttons.back().player==1 && !f.layout.buttons.back().enabled);
    c.command(Command::Apply);assert(!c.editing());
    Controls reopened;assert(reopened.layout().buttons.size()==12 && reopened.layout().buttons[0].opacity==.31);
    reopened.edit();reopened.command(Command::Reset);reopened.command(Command::Back);
    assert(reopened.layout().buttons.size()==12); // Cancelling never saves the draft.
    reopened.edit();for(int i=0;i<12;++i)reopened.command(Command::Remove);
    reopened.command(Command::Apply);assert(reopened.layout().buttons.empty());
    reopened.edit();reopened.command(Command::Reset);reopened.command(Command::Apply);
    assert(reopened.layout().buttons.size()==11); // Recovery works with every button removed.
    {
        Controls editor;editor.edit();
        editor.editor_down({276,444},area);editor.editor_up();editor.command(Command::Properties);
        auto selected=editor.frame(view,area);assert(selected.selected==4 && selected.properties);
        auto box=property_bounds(area,selected.layout.buttons[selected.selected]);assert(box.x>=600);
        editor.editor_down({box.x+160,box.y+46+4*propertyRowHeight+10},area);editor.editor_up();
        assert(editor.text_active());editor.text("0.37");editor.accept_field();
        assert(editor.frame(view,area).layout.buttons[4].opacity==.37);
        for(int i=0;i<4;++i)editor.command(Command::Previous);
        selected=editor.frame(view,area);assert(selected.selected==0 && selected.properties);
        box=property_bounds(area,selected.layout.buttons[0]);assert(box.x+box.w<=600);
        editor.editor_down({box.x+160,box.y+46+4*propertyRowHeight+10},area);editor.editor_up();
        assert(editor.text_active());editor.text("0.29");editor.accept_field();
        assert(editor.frame(view,area).layout.buttons[0].opacity==.29);
        editor.command(Command::Back);
    }
    {
        for(const auto a:{Area{},Area{-266,0,1732,800},Area{66,24,1066,752}}) {
            Controls editor;editor.edit();editor.command(Command::Properties);
            const auto position=[&](double x,double y){return Point{a.x+x*a.width,a.y+y*a.height};};
            editor.editor_down(position(.69,.72),a);
            editor.editor_move(position(.38,.55),a);
            auto dragging=editor.frame(view,a);assert(!dragging.properties && dragging.selected==0);
            editor.editor_up();auto moved=editor.frame(view,a);
            assert(moved.properties && moved.layout.buttons[0].center.x<.5);
            auto box=property_bounds(a,moved.layout.buttons[0]);assert(box.x>=a.x+a.width/2);
            editor.editor_down(position(.38,.55),a);editor.editor_move(position(.72,.55),a);editor.editor_up();
            moved=editor.frame(view,a);assert(moved.properties && moved.layout.buttons[0].center.x>.5);
            box=property_bounds(a,moved.layout.buttons[0]);assert(box.x+box.w<=a.x+a.width/2);
        }
    }
    {
        Controls railControls;SessionView game;game.status=SessionStatus::Playing;game.actionEnabled[0].fill(true);
        auto f=railControls.frame(game,area);assert(f.railToggle && !f.railVisible && f.rail.empty());
        assert(rail_hit(f,{30,30})==Command::ToggleRail);
        railControls.command(Command::ToggleRail);f=railControls.frame(game,area);
        assert(f.railVisible && f.rail.size()==3 && f.rail[0].command==Command::Retry && f.rail[1].command==Command::Settings && f.rail[2].command==Command::Exit);
        game.switchTarget=true;f=railControls.frame(game,area);
        assert(f.rail.front().command==Command::SwitchTarget);
        game.switchTarget=false;game.actionEnabled[0][0]=game.actionEnabled[0][11]=false;
        f=railControls.frame(game,area);
        assert(!f.layout.buttons[0].available && !f.layout.buttons.back().available && f.layout.buttons[1].available);
        game.status=SessionStatus::Finished;f=railControls.frame(game,area);
        assert(f.railToggle && !f.railVisible && f.rail.empty() && f.tools.size()==3);
        railControls.command(Command::ToggleRail);f=railControls.frame(game,area);
        assert(f.railVisible && f.rail.size()==3 && f.rail[0].command==Command::Retry && f.tools.size()==3);
        assert(rail_hit(f,{f.tools[0].bounds.x+10,f.tools[0].bounds.y+10})==Command::Tab);
        auto menu=std::make_shared<Frontend::UiFrame>();menu->metadata.kind=Frontend::UiKind::Menu;menu->metadata.entries.push_back({"item","Item"});menu->metadata.keyActions={Frontend::UiKeyAction::Retry,Frontend::UiKeyAction::Exit};game.ui=menu;
        f=railControls.frame(game,area);assert(!f.railToggle && f.railVisible && f.rail.size()==4 && f.rail[0].command==Command::Back && f.rail[1].command==Command::Confirm);
        menu->metadata.id="editor-options";menu->metadata.keyActions={Frontend::UiKeyAction::SaveMap};
        f=railControls.frame(game,area);assert(f.rail.size()==3 && f.rail.back().command==Command::MapSave);
        menu->metadata.keyActions={Frontend::UiKeyAction::SwitchTarget};
        f=railControls.frame(game,area);assert(f.rail.size()==3 && f.rail.back().command==Command::SwitchTarget);
        menu->metadata.kind=Frontend::UiKind::Statistics;
        f=railControls.frame(game,area);
        assert(std::none_of(f.rail.begin(),f.rail.end(),[](const auto& b) {
            return b.command==Command::Tab || b.command==Command::PageUp || b.command==Command::PageDown ||
                b.command==Command::Up || b.command==Command::Down || b.command==Command::Left || b.command==Command::Right;
        }));
        menu->metadata.kind=Frontend::UiKind::Menu;
        railControls.edit();f=railControls.frame(game,area);
        assert(f.editor && f.railToggle && !f.railVisible && f.rail.empty());
        railControls.command(Command::ToggleRail);assert(railControls.frame(game,area).rail.size()==10);
        railControls.command(Command::Back);f=railControls.frame(game,area);
        assert(!f.railToggle && f.railVisible && !f.rail.empty());
        game.ui.reset();f=railControls.frame(game,area);
        assert(f.railToggle && !f.railVisible && f.rail.empty());
    }
    {
        using namespace std::chrono_literals;
        UserConfig::Settings settings;settings.keyCodes=UserConfig::default_key_codes();
        settings.persistenceEnabled=false;settings.gravityInterval=100;settings.goal=0;
        LaunchOptions options;options.startScreen=false;options.touchControls=true;
        Session session(settings,settings,options);
        const auto wait=[&](auto predicate) {
            const auto until=std::chrono::steady_clock::now()+8s;
            for(;;) {
                auto v=session.view();if(predicate(v))return v;
                if(v.status==SessionStatus::Error || std::chrono::steady_clock::now()>until) {
                    std::cerr<<"Touch session timeout: "<<int(v.status)<<" "<<v.message<<'\n';assert(false);
                }
                std::this_thread::sleep_for(2ms);
            }
        };
        const auto tap=[&](int index) {
            const auto key=Input::virtual_action_key(index);
            session.input().update(key,true);std::this_thread::sleep_for(45ms);
            session.input().update(key,false);std::this_thread::sleep_for(25ms);
        };
        auto playing=wait([](const auto& v){return v.status==SessionStatus::Playing && v.board.active.has_value();});
        assert(playing.actionEnabled[0][0] && playing.actionEnabled[0][11] && playing.actionEnabled[0][12]);
        const int col=playing.board.active->pose.position.col;tap(0);
        wait([&](const auto& v){return v.board.active && v.board.active->pose.position.col<col;});
        tap(12);auto paused=wait([](const auto& v){return v.status==SessionStatus::Paused;});
        std::this_thread::sleep_for(80ms);
        assert(std::abs(session.view().board.elapsed.seconds()-paused.board.elapsed.seconds())<.02);
        tap(12);wait([](const auto& v){return v.status==SessionStatus::Playing;});
        tap(3);wait([](const auto& v){return v.board.pieces==1;});
        tap(11);wait([](const auto& v){return v.board.pieces==0;});
        tap(9);auto menu=wait([](const auto& v){return v.ui && !v.ui->overlay && v.ui->metadata.id=="root";});
        std::this_thread::sleep_for(80ms);
        assert(std::abs(session.view().board.elapsed.seconds()-menu.board.elapsed.seconds())<.02);
        session.ui_command({Frontend::UiCommandKind::Back,0,{},menu.ui->metadata.page});
        // The shared in-game Settings flow returns to Start when closed.
        wait([](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Start;});
    }
    {
        UserConfig::Settings settings;settings.persistenceEnabled=false;settings.gravityInterval=100;settings.goal=0;
        settings.disableLeft=settings.disableCounterclockwise=settings.disable180=settings.disableSoftDrop=settings.disableUndo=settings.disablePause=1;
        settings.holdEnabled=0;
        LaunchOptions options;options.startScreen=false;options.touchControls=true;
        Session session(settings,settings,options);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        SessionView playing;
        do {playing=session.view();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        while(playing.status!=SessionStatus::Playing && std::chrono::steady_clock::now()<deadline);
        assert(playing.status==SessionStatus::Playing);
        for(const auto i:{0,2,5,6,7,11,12})assert(!playing.actionEnabled[0][i]);
        for(const auto i:{1,3,4,8,9})assert(playing.actionEnabled[0][i]);
        Controls controls;const auto f=controls.frame(playing,area);
        assert(!f.layout.buttons[0].available && f.layout.buttons[1].available && !f.layout.buttons.back().available);
    }
    std::filesystem::current_path(old);std::filesystem::remove_all(dir);return 0;
}
}
