#include "text_events.h"
#include "input.h"
#include "menu_pointer.h"
#include "menu_viewport.h"
#include "scroll_gesture.h"
#include "native_file_dialog.h"
#include "frame_worker.h"
#include "board_layout.h"
#include "tetr/config/user_config.h"
#include <chrono>
#include <cassert>
#include <atomic>
#include <future>
#include <vector>
namespace TetrTests::graphical_input_test {
namespace {
SDL_DialogFileCallback dialogCallback=nullptr;void* dialogUserdata=nullptr;
void SDLCALL fake_dialog(SDL_DialogFileCallback callback,void* userdata,SDL_Window* window,
    const SDL_DialogFileFilter* filters,int count,const char* location,bool many) {
    assert(!window && !filters && count==0 && !many);
    assert(std::string(location)=="/tmp/中文 目录/.tetr/replay");
    dialogCallback=callback;dialogUserdata=userdata;
}
void file_dialog_callbacks() {
    const auto request=std::make_shared<Frontend::FileDialog>("/tmp/中文 目录/.tetr/replay");
    Graphical::show_file_dialog(nullptr,request,fake_dialog);assert(!request->result());
    std::string path="/tmp/中文 目录/.tetr/replay/回放 文件.txt";const char* files[]={path.c_str(),nullptr};
    std::thread([&]{dialogCallback(dialogUserdata,files,0);}).join();path="invalidated";
    assert(request->result()->path=="/tmp/中文 目录/.tetr/replay/回放 文件.txt");
    request->complete();assert(!request->result()->path.empty());
    const auto cancelled=std::make_shared<Frontend::FileDialog>(request->directory);
    Graphical::show_file_dialog(nullptr,cancelled,fake_dialog);const char* empty[]={nullptr};
    dialogCallback(dialogUserdata,empty,-1);assert(cancelled->result() && cancelled->result()->path.empty());
    const auto error=std::make_shared<Frontend::FileDialog>(request->directory);
    Graphical::show_file_dialog(nullptr,error,fake_dialog);SDL_SetError("fixture dialog error");
    dialogCallback(dialogUserdata,nullptr,-1);assert(error->result()->error=="fixture dialog error");
}
void menu_scroll_delivery() {
    Graphical::MenuViewport viewport;
    assert(viewport.show(0,40,10,1)==0);
    assert(viewport.show(0,40,10,1,3)==3);
    assert(viewport.show(0,40,10,1)==3); // Repainting keeps the scrolled viewport.
    assert(viewport.show(0,40,10,1,.25f)==3.25f);
    assert(viewport.show(0,40,10,1,.75f)==4);
    assert(viewport.show(1,40,10,1)==1); // Keyboard selection follows focus again.
    assert(viewport.show(1,40,10,1,100)==30);
    assert(viewport.show(1,40,10,2)==1); // Opening an editor reveals its field.
    assert(viewport.show(0,3,10,3,100)==0);
    assert(viewport.show(12,40,10,4)==3);
    assert(viewport.show(12,40,5,4)==8); // Resizing keeps keyboard focus visible.
    Graphical::Interaction interaction;Frontend::UiMetadata m;m.page=1;interaction.metadata=m;
    interaction.menuScrollMetrics.push_back({{0,0,200,440},"settings",44,0,30,{190,0,10,440},{190,0,10,110}});
    Graphical::ScrollGesture touch(interaction,m,50,200);
    assert(!touch.move(50,198) && !touch.dragged());
    auto movement=touch.move(50,189);assert(movement && movement->rows==.25f && touch.dragged());
    movement=touch.move(50,178);assert(movement && movement->rows==.25f);
    Graphical::ScrollGesture thumb(interaction,m,195,40);
    assert(thumb.scrollbar());movement=thumb.move(195,51);assert(movement && movement->rows==1);
    interaction.menuScrollMetrics.push_back({{0,0,200,440},"clear-columns",100,0,6,{0,430,200,10},{0,430,50,10},true});
    Graphical::ScrollGesture horizontal(interaction,m,100,200);
    movement=horizontal.move(75,202);assert(movement && movement->key=="clear-columns" && movement->rows==.25f);
    movement=horizontal.move(50,220);assert(movement && movement->key=="clear-columns" && movement->rows==.25f);
    Graphical::ScrollGesture vertical(interaction,m,100,200);
    movement=vertical.move(99,189);assert(movement && movement->key=="settings" && movement->rows==.25f);
    Graphical::ScrollGesture horizontalThumb(interaction,m,20,435);
    assert(horizontalThumb.scrollbar());movement=horizontalThumb.move(45,435);
    assert(movement && movement->key=="clear-columns" && movement->rows==1);
    m.page=2;Graphical::ScrollGesture stale(interaction,m,50,200);assert(!stale.move(50,150));
    std::promise<void> entered,release;auto started=entered.get_future();auto proceed=release.get_future().share();
    std::atomic<int> frames=0;float rows=0;
    Graphical::FrameWorker worker({{}, {}, [&](const Graphical::FrameRequest& request,bool) {
        if(frames++==0){entered.set_value();proceed.wait();}
        for(const auto& movement:request.menuScroll)rows+=movement.rows;
        return Graphical::Interaction{};
    }},{800,600},1000);
    assert(started.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    for(int i=0;i<20;++i){worker.scroll({1,"settings",.5f});worker.submit({800,600});}
    release.set_value();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(frames<6 && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    worker.stop();assert(frames>=6 && rows==10); // No lost or repeated wheel input.
}
void sdl_mapping_and_samples() {
    using namespace Graphical;
    using namespace std::chrono;
    const SDL_Scancode defaults[]={SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_DOWN,SDL_SCANCODE_SPACE,
        SDL_SCANCODE_UP,SDL_SCANCODE_Z,SDL_SCANCODE_A,SDL_SCANCODE_C,SDL_SCANCODE_R,SDL_SCANCODE_P,
        SDL_SCANCODE_S,SDL_SCANCODE_U,SDL_SCANCODE_F2};
    const auto& native=UserConfig::default_key_codes();
    for(std::size_t i=0;i<native.size();++i) assert(native_key(defaults[i])==native[i]);
    assert(native_key(SDL_SCANCODE_UNKNOWN)==0);
    assert(native_key(SDL_SCANCODE_LSHIFT)!=native_key(SDL_SCANCODE_RSHIFT));
    assert(native_key(SDL_SCANCODE_MINUS)>0 && native_key(SDL_SCANCODE_EQUALS)>0);
    assert(native_key(SDL_SCANCODE_VOLUMEUP)>0 && native_key(SDL_SCANCODE_MEDIA_NEXT_TRACK)>0);
    assert(native_key(SDL_SCANCODE_LANG1)>0 && native_key(SDL_SCANCODE_INTERNATIONAL4)>0);
    const auto origin=steady_clock::now();EventClock clock(1000000,origin);
    assert(clock.at(1012345)-origin==nanoseconds(12345));
    assert(clock.at(987655)-origin==nanoseconds(-12345));
    Input::FocusState input;
    SDL_KeyboardEvent event{};event.scancode=SDL_SCANCODE_LEFT;event.timestamp=1010000;event.down=true;
    deliver_key(input,event,clock);
    assert(input.key_down(native[0]));
    // An SDL batch becomes visible to gameplay only after the polling loop
    // marks it complete. Its progress uses the same clock as keyboard events.
    Input::FocusState sampled;
    complete_input_sample(sampled,clock,1000000);
    event.repeat=false;event.down=true;event.timestamp=1010000;
    deliver_key(sampled,event,clock);
    std::uint64_t sampledCursor=0;
    Input::KeyEventBuffer::SampleTime through;
    assert(sampled.events().read(sampledCursor,&through).empty() && through==origin);
    complete_input_sample(sampled,clock,1020000);
    const auto batch=sampled.events().read(sampledCursor,&through);
    assert(batch.size()==1 && batch.front().down && batch.front().time==origin+nanoseconds(10000));
    assert(through==origin+nanoseconds(20000));
    // Events received during pumping remain immediately available. A delayed
    // handler or an OS repeat cannot claim newer physical input coverage.
    InputSample sample(1025000);
    event.down=false;event.timestamp=1030000;sample.deliver(sampled,event,clock);
    event.repeat=true;event.timestamp=2000000;sample.deliver(sampled,event,clock);
    sample.complete(sampled,clock);
    const auto released=sampled.events().read(sampledCursor,&through);
    assert(released.size()==1 && !released.front().down);
    assert(through==origin+nanoseconds(30000));
    event.repeat=true;event.timestamp=1020000;deliver_key(input,event,clock);
    event.repeat=false;event.down=false;event.timestamp=1030000;deliver_key(input,event,clock);
    std::uint64_t cursor=0;
    const auto events=input.events().read(cursor);
    assert(events.size()==2 && events[0].time==origin+nanoseconds(10000));
    assert(events[1].time==origin+nanoseconds(30000));
    const auto before=steady_clock::now();EventClock realClock;
    const auto converted=realClock.at(SDL_GetTicksNS());
    assert(converted>=before-milliseconds(1) && converted<=steady_clock::now()+milliseconds(1));
}
void focus_and_pointer_order_are_preserved() {
    Input::FocusState source;Graphical::EventClock clock;
    const auto down=Graphical::native_key(SDL_SCANCODE_DOWN);
    source.update(down,true); // A mouse shortcut is holding the same key.
    const auto push=[](bool down) {
        SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
        event.key.scancode=SDL_SCANCODE_DOWN;event.key.down=down;event.key.timestamp=SDL_GetTicksNS();
        assert(SDL_PushEvent(&event));
    };
    push(true);
    bool cancelled=false;
    auto events=Graphical::poll_input_events(source,clock,[&] {source.update(down,false);cancelled=true;});
    assert(cancelled && source.key_down(down));
    push(false);Graphical::poll_input_events(source,clock);
    SDL_Event event{};event.type=SDL_EVENT_WINDOW_FOCUS_LOST;assert(SDL_PushEvent(&event));
    std::string unfocused="unfocused",focused="focused";
    event.type=SDL_EVENT_TEXT_INPUT;event.text.text=unfocused.c_str();assert(SDL_PushEvent(&event));
    event.type=SDL_EVENT_WINDOW_FOCUS_GAINED;assert(SDL_PushEvent(&event));
    event.type=SDL_EVENT_TEXT_INPUT;event.text.text=focused.c_str();assert(SDL_PushEvent(&event));
    events=Graphical::poll_input_events(source,clock);
    unfocused.assign(200,'x');focused.assign(200,'y');
    Graphical::poll_input_events(source,clock);
    int text=0;
    for(const auto& routed:events)if(routed.event.type==SDL_EVENT_TEXT_INPUT) {
        assert(routed.acceptingInput==(text==1));
        assert(std::string(routed.event.text.text)==(text==0?"unfocused":"focused"));++text;
    }
    assert(text==2 && source.accepting_input());
    // Android adds finger transitions after draining SDL. Neither keyboard
    // nor sampled progress may become visible before that whole batch commits.
    Input::FocusState combined;std::uint64_t cursor=0;
    Graphical::complete_input_sample(combined,clock,SDL_GetTicksNS());
    Input::KeyEventBuffer::SampleTime before,after;
    assert(combined.events().read(cursor,&before).empty());
    push(true);Graphical::poll_input_events(combined,clock,{},false);
    assert(combined.events().read(cursor,&after).empty() && after==before);
    combined.update(0x10003,true,clock.at(SDL_GetTicksNS()));
    assert(combined.events().read(cursor,&after).empty() && after==before);
    Graphical::complete_input_sample(combined,clock,SDL_GetTicksNS());
    const auto changes=combined.events().read(cursor,&after);
    assert(changes.size()==2 && changes[0].key==down && changes[1].key==0x10003 && after>=before);
}
void local_editor_owns_keyboard() {
    Input::FocusState source;Graphical::EventClock clock;
    const auto enter=Graphical::native_key(SDL_SCANCODE_RETURN);
    const auto push=[](SDL_EventType type,bool down=false) {
        SDL_Event event{};event.type=type;event.key.scancode=SDL_SCANCODE_RETURN;
        event.key.down=down;event.key.timestamp=SDL_GetTicksNS();assert(SDL_PushEvent(&event));
    };
    source.update(enter,true);source.set_focused(false);source.set_focused(true);
    push(SDL_EVENT_KEY_UP);
    push(SDL_EVENT_KEY_DOWN,true);
    const auto events=Graphical::poll_input_events(source,clock,{},true,false);
    assert(events.size()==2 && events.back().event.key.scancode==SDL_SCANCODE_RETURN);
    assert(!source.key_down(enter)); // The editor still receives Return; its underlying menu does not.
    push(SDL_EVENT_WINDOW_FOCUS_LOST);push(SDL_EVENT_WINDOW_FOCUS_GAINED);
    push(SDL_EVENT_KEY_DOWN,true);
    Graphical::poll_input_events(source,clock,{},true,false);
    assert(source.accepting_input() && !source.key_down(enter));
    push(SDL_EVENT_KEY_UP);Graphical::poll_input_events(source,clock,{},true,false);
    push(SDL_EVENT_KEY_DOWN,true);Graphical::poll_input_events(source,clock);
    assert(source.key_down(enter)); // Normal keyboard navigation resumes after the editor closes.
    push(SDL_EVENT_KEY_UP);Graphical::poll_input_events(source,clock);
}
}
int run(int argc,char** argv) {
    assert(SDL_Init(SDL_INIT_EVENTS));
    sdl_mapping_and_samples();focus_and_pointer_order_are_preserved();local_editor_owns_keyboard();file_dialog_callbacks();menu_scroll_delivery();
    using K=Frontend::TextEditKind;
    Graphical::TextEvents router;Frontend::TextField field("initial");
    std::vector<K> edits;
    const auto send=[&](Frontend::TextEditEvent event){edits.push_back(event.kind);field.apply(event);};
    SDL_Event event{};event.type=SDL_EVENT_TEXT_EDITING;event.edit.text="中文组合";router.consume(event,send);
    for(auto key:{SDL_SCANCODE_LEFT,SDL_SCANCODE_DELETE,SDL_SCANCODE_BACKSPACE,SDL_SCANCODE_RETURN,SDL_SCANCODE_ESCAPE}) {
        event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=key;router.consume(event,send);
    }
    assert(edits.empty() && field.value=="initial" && !field.done);
    event.type=SDL_EVENT_TEXT_INPUT;event.text.text="中文";router.consume(event,send);
    assert(field.value=="中文" && router.composition.empty());
    event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=SDL_SCANCODE_LEFT;router.consume(event,send);
    event.key.scancode=SDL_SCANCODE_DELETE;router.consume(event,send);assert(field.value=="中");
    event.key.scancode=SDL_SCANCODE_A;event.key.mod=SDL_KMOD_CTRL;router.consume(event,send);
    event.key.scancode=SDL_SCANCODE_V;router.consume(event,send,[]{return "路径 with spaces";});assert(field.value=="路径 with spaces");
    event.key.mod=0;event.key.scancode=SDL_SCANCODE_RETURN;router.consume(event,send);assert(field.done && !field.cancelled);
    // Loss of focus suppresses held actions until release, and clearing the
    // IME composition cannot synthesize a confirm/cancel event.
    Input::FocusState focus;Graphical::EventClock clock;
    event.key.scancode=SDL_SCANCODE_LEFT;event.key.down=true;Graphical::deliver_key(focus,event.key,clock);
    focus.set_focused(false);router.composition="未提交";router.reset();focus.set_focused(true);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_LEFT)) && router.composition.empty());
    event.key.down=false;Graphical::deliver_key(focus,event.key,clock);event.key.down=true;Graphical::deliver_key(focus,event.key,clock);
    assert(focus.key_down(Graphical::native_key(SDL_SCANCODE_LEFT)));
    focus.update(Graphical::native_key(SDL_SCANCODE_LEFT),false);
    Graphical::MenuPointer pointer;
    Frontend::UiMetadata menu{Frontend::UiKind::Menu,"handling","Handling",{},{{"das","DAS"},{"arr","ARR"}},0};
    menu.page=1;
    pointer.key(focus,menu,SDL_SCANCODE_DOWN,100);pointer.update(focus,&menu,110);
    assert(focus.key_down(Graphical::native_key(SDL_SCANCODE_DOWN)));
    pointer.update(focus,&menu,140);assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_DOWN)));
    pointer.key(focus,menu,SDL_SCANCODE_UP,200);
    auto changed=menu;changed.page=2;
    pointer.update(focus,&changed,210);assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_UP)));
    pointer.key(focus,menu,SDL_SCANCODE_DOWN,500);pointer.cancel(focus);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_DOWN)));
    // A horizontal-only wheel must not change the selected menu option.
    pointer.wheel(focus,menu,0,510);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_DOWN)));
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_UP)));
    pointer.wheel(focus,menu,.5f,520);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_UP)));
    pointer.update(focus,&menu,560);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_UP)));
    pointer.wheel(focus,menu,-.5f,570);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_DOWN)));
    menu.path="play-rooms";pointer.wheel(focus,menu,-1,580);
    assert(focus.key_down(Graphical::native_key(SDL_SCANCODE_DOWN)));menu.path.clear();
    pointer.cancel(focus);
    menu.kind=Frontend::UiKind::KeyCapture;pointer.key(focus,menu,SDL_SCANCODE_RETURN,600);
    assert(!focus.key_down(Graphical::native_key(SDL_SCANCODE_RETURN)));
    menu.kind=Frontend::UiKind::MapEditor;menu.editor.emplace();menu.editor->sequenceFocus=true;
    assert(!pointer.editor_wheel(menu,0,.25f));assert(!pointer.editor_wheel(menu,0,.25f));
    assert(!pointer.editor_wheel(menu,1,.5f)); // Each list keeps its own fractional scroll.
    const auto sequenceWheel=pointer.editor_wheel(menu,0,.5f);assert(sequenceWheel && sequenceWheel->delta==-1 && sequenceWheel->index==0);
    const auto candidateWheel=pointer.editor_wheel(menu,1,-1.5f);assert(candidateWheel && candidateWheel->delta==1 && candidateWheel->index==1);
    ++menu.page;assert(!pointer.editor_wheel(menu,0,.5f));
    Presentation::BoardView board;board.width=10;board.firstRow=-8;board.bottom=20;board.cells.resize(280);
    board.active=Presentation::PieceView{Piece::Type::from_index(5),Piece::spawn_pose(10,Piece::Type::from_index(5))};
    const auto geometry=Graphical::solo_geometry(board.width,board.bottom);
    assert(Graphical::first_display_row(board,geometry)==-5);
    board.cells[12].hint=true;assert(Graphical::first_display_row(board,geometry)==-5);
    board.cells[12].hint=false;board.cells[2].appearance=Presentation::CellAppearance::Dimmed;
    assert(Graphical::first_display_row(board,geometry)==-5);
    board.cells[2].appearance=Presentation::CellAppearance::Empty;
    assert(Graphical::first_display_row(board,geometry)==-5);
    assert(std::abs(geometry.top-(252-geometry.unit))<.01f && std::abs(geometry.bottom()-(714-geometry.unit))<.01f && geometry.unit<25);
    assert(std::abs(geometry.top-geometry.ceiling-5*geometry.unit)<.01f);
    const auto topPreview=Graphical::top_preview_geometry(geometry,board.width);
    assert(topPreview.rows==10 && topPreview.left==geometry.left+geometry.width+20);
    assert(topPreview.width==geometry.next && topPreview.top+topPreview.height<geometry.top);
    assert(std::abs(topPreview.unit*board.width-topPreview.width)<.01f);
    assert(Graphical::target_line_y(geometry,5)==geometry.bottom()-5*geometry.unit);
    assert(Graphical::target_line_y(geometry,4.8)==geometry.bottom()-4.8f*geometry.unit);
    assert(Graphical::target_line_y(geometry,4.75)>Graphical::target_line_y(geometry,4.8));
    assert(std::abs(*Graphical::target_line_y(geometry,20)-geometry.top)<.01f);
    assert(!Graphical::target_line_y(geometry,21));
    assert(!Graphical::target_line_y(geometry,0));
    assert(!Graphical::target_line_y(geometry,-1));
    assert(!Graphical::target_line_y(Graphical::solo_geometry(10,0),1));
    const auto shallow=Graphical::solo_geometry(4,3);
    assert(Graphical::target_line_y(shallow,1)==shallow.bottom()-shallow.unit);
    assert(!Graphical::target_line_y(shallow,4));
    board.active->pose.position.row=-8;board.score=999999999999LL;
    board.cells[0].appearance=Presentation::CellAppearance::Visible;
    const auto changedGeometry=Graphical::solo_geometry(board.width,board.bottom);
    assert(geometry.unit==changedGeometry.unit && geometry.left==changedGeometry.left && geometry.top==changedGeometry.top);
    for(int width:{4,6,10,20,100})for(int height:{0,1,2,3,4,5,7,8,9,11,12,20,100}) {
        const auto g=Graphical::solo_geometry(width,height);
        const auto p=Graphical::top_preview_geometry(g,width);
        const float available=g.top-Graphical::topPreviewGap-Graphical::topPreviewCeiling;
        assert(p.top>=Graphical::topPreviewCeiling-.01f && p.top+p.height<g.top && p.unit>0);
        assert(std::abs(width*p.unit-p.width)<.01f);
        assert(p.rows>=1 && p.rows*p.unit<=available+.01f);
        assert((p.rows+1)*p.unit>available || p.rows==Runtime::kHiddenRowCapacity);
        for(std::size_t count:{0,5,20,100})for(std::size_t cues:{0,1,2}) {
            const float nextTop=Graphical::next_rail_top(g,count,cues);
            const auto actual=Graphical::top_preview_geometry(g,width,nextTop);
            assert(nextTop<=g.top && nextTop>=Graphical::railTop && actual.rows<=p.rows);
            if(count)assert((Graphical::railTop+Graphical::railHeight-nextTop-54*cues-40)/count>0);
        }
        assert(g.unit>0 && g.width<=360.01f && g.height<=462.01f && g.top>=g.ceiling && g.bottom()<=714.01f);
        assert(Graphical::displayed_above_rows(g)==Graphical::reserved_above_rows(height) && g.ceiling>=Graphical::fieldCeiling-.01f);
        assert(g.left-g.rail-26>=40 && g.left+g.width+20+g.next<=1160);
        const auto scene=Graphical::multiplayer_geometry({{width,height},{10,20}},{});
        for(const auto& f:scene.full) {
            assert(f.unit>0 && f.top>=Graphical::fieldTop-.01f && f.bottom()<=Graphical::fieldFloor+.01f);
            assert(f.ceiling>=Graphical::fieldCeiling-.01f && Graphical::displayed_above_rows(f)>=5);
        }
    }
    assert(Graphical::solo_geometry(4,4).unit<80 && Graphical::solo_geometry(4,4).unit>geometry.unit);
    const auto four=Graphical::solo_geometry(4,4);
    assert(Graphical::top_preview_geometry(four,4,Graphical::next_rail_top(four,5,0)).rows==9);
    assert(Graphical::top_preview_geometry(four,4,Graphical::next_rail_top(four,5,1)).rows==9);
    assert(Graphical::solo_geometry(10,8).unit>geometry.unit);
    assert(Graphical::displayed_above_rows(four)==7);
    assert(std::abs((four.top+four.bottom())/2-(483-Graphical::fieldVerticalOffset))<.01f && four.top<473);
    assert(Graphical::first_display_row(board,four)==-7);
    assert(Graphical::multiplayer_geometry({{4,4}},{}).full[0].unit>geometry.unit);
    const auto one=Graphical::multiplayer_geometry({{10,20}},{{10,20}});
    const auto duel=Graphical::multiplayer_geometry({{10,20},{10,20}},{});
    assert(one.full[0].left==duel.full[0].left && one.full[0].unit==duel.full[0].unit);
    const auto mixed=Graphical::multiplayer_geometry({{10,20},{32,40}},{});
    assert(mixed.full[0].left==one.full[0].left && mixed.full[0].unit==one.full[0].unit);
    assert(one.full[0].left+one.full[0].width/2<600);
    assert(one.mini[0].bottom()==one.full[0].bottom());
    assert(one.mini[0].ceiling>=Graphical::fieldCeiling);
    assert(one.mini[0].unit>=20); // One opponent fills the available gallery.
    const auto wideScene=Graphical::multiplayer_geometry({{10,20}},std::vector<Graphical::FieldSize>(7,{10,20}),-160,1360);
    assert(wideScene.mini.size()==7 && std::any_of(wideScene.mini.begin(),wideScene.mini.end(),[](const auto& g){return g.left+g.width>1160;}));
    for(int count:{7,16,31}) {
        std::vector<Graphical::FieldSize> fields(count,{10,20});fields.back()={16,24};
        const auto scene=Graphical::multiplayer_geometry({{10,20}},fields);
        assert(scene.mini.size()==fields.size());
        assert(scene.full[0].left==one.full[0].left && scene.full[0].unit==one.full[0].unit);
        for(std::size_t i=0;i<scene.mini.size();++i) {
            const auto& a=scene.mini[i];assert(a.unit>0 && a.left>=scene.full[0].left+scene.full[0].width && a.left+a.width<=1160.01f);
            assert(a.ceiling>=Graphical::fieldCeiling && a.bottom()+17<=750);
            for(std::size_t j=0;j<i;++j) {
                const auto& b=scene.mini[j];
                assert(a.left+a.width<=b.left || b.left+b.width<=a.left || a.bottom()+17<=b.ceiling || b.bottom()+17<=a.ceiling);
            }
        }
    }
    SDL_Quit();
    return 0;
}
} // namespace TetrTests::graphical_input_test
