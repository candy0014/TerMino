#include "tetr/frontend/graphical/session.h"
#include "tetr/config/settings_schema.h"
#include "tetr/replay/file_path.h"
#include "input.h"
#include "native_file_dialog.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
namespace TetrTests::graphical_ui_interaction_test {
namespace {
using namespace std::chrono_literals;
template<class P> Graphical::SessionView wait(Graphical::Session& s,P predicate) {
    const auto end=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<end) {
        const auto v=s.view();if(predicate(v))return v;
        assert(v.status!=Graphical::SessionStatus::Error);std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("UI interaction timed out");
}
Graphical::SessionView page(Graphical::Session& s,const std::string& id) {
    return wait(s,[&](const auto& v){return v.ui && v.ui->metadata.id==id;});
}
void click(Graphical::Session& s,const std::string& id) {
    const auto v=s.view();assert(v.ui);const auto& m=v.ui->metadata;
    const auto it=std::find_if(m.entries.begin(),m.entries.end(),[&](const auto& e){return e.id==id;});assert(it!=m.entries.end());
    s.ui_command({Frontend::UiCommandKind::Activate,static_cast<std::size_t>(it-m.entries.begin()),{},m.page});
}
void command(Graphical::Session& s,Frontend::UiCommand c) {c.page=s.view().ui->metadata.page;s.ui_command(c);}
void tap(Graphical::Session& s,SDL_Scancode code) {
    const auto key=Graphical::native_key(code);s.input().update(key,true);std::this_thread::sleep_for(25ms);
    s.input().update(key,false);std::this_thread::sleep_for(25ms);
}
UserConfig::Settings defaults() {UserConfig::Settings s;s.keyCodes=UserConfig::default_key_codes();s.persistenceEnabled=false;s.uiLanguage=1;return s;}
}
int run(int,char**) {
    namespace fs=std::filesystem;
    const auto old=fs::current_path(),dir=fs::temp_directory_path()/("termino-ui-interaction-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir/".tetr/replay/中文 目录");fs::current_path(dir);
    struct Restore {fs::path old,dir;~Restore(){fs::current_path(old);fs::remove_all(dir);}} restore{old,dir};
    const auto file=dir/".tetr/replay/中文 目录/回放 文件.txt";std::ofstream(file)<<"fixture";
    {
        auto settings=defaults();settings.gameMode=5;settings.replayPath="original.txt";
        Graphical::Session s(settings,settings,{});page(s,"start");click(s,"settings");page(s,"root");click(s,"MODEL");page(s,"MODEL");
        const auto replayId="setting-"+std::to_string(static_cast<int>(UserConfig::SettingId::ReplayPath));
        click(s,replayId);
        auto editing=wait(s,[&](const auto& v){return s.text_input_active() && v.ui && v.ui->metadata.parent;});
        const auto& m=editing.ui->metadata;const auto& owner=*m.parent;
        assert(owner.entries[owner.selected].filePath && owner.entries[owner.selected].id==replayId);
        s.text_event({Frontend::TextEditKind::Insert,"uncommitted"});
        s.ui_command({Frontend::UiCommandKind::BrowseFile,owner.selected,m.ancestors.size(),m.page});
        std::shared_ptr<Frontend::FileDialog> dialog;
        wait(s,[&](const auto&){dialog=s.take_file_dialog();return bool(dialog);});
        assert(!s.text_input_active() && fs::u8path(dialog->directory)==dir/".tetr/replay");
        assert(!s.take_file_dialog());dialog->complete({file.generic_u8string(),{}});
        const auto returned=page(s,"MODEL");
        const auto row=std::find_if(returned.ui->metadata.entries.begin(),returned.ui->metadata.entries.end(),[&](const auto& e){return e.id==replayId;});
        assert(row!=returned.ui->metadata.entries.end() && row->value.find("回放 文件.txt")!=std::string::npos);
        assert(std::none_of(returned.ui->metadata.entries.begin(),returned.ui->metadata.entries.end(),[](const auto& e){return e.id=="browse-replay";}));
        const auto saved=row->value;
        const auto shortcut=Graphical::file_browse_command(returned.ui->metadata);assert(shortcut);s.ui_command(*shortcut);
        wait(s,[&](const auto&){dialog=s.take_file_dialog();return bool(dialog);});dialog->complete();
        const auto cancelled=wait(s,[&](const auto& v){return v.ui && v.ui->metadata.id=="MODEL" && v.revision>returned.revision;});
        assert(std::find_if(cancelled.ui->metadata.entries.begin(),cancelled.ui->metadata.entries.end(),[&](const auto& e){return e.id==replayId;})->value==saved);
        click(s,replayId);wait(s,[&](const auto& v){return s.text_input_active() && v.ui->metadata.kind==Frontend::UiKind::Text;});
        s.text_event({Frontend::TextEditKind::Insert,"manual path.txt"});s.text_event({Frontend::TextEditKind::Confirm});
        const auto manual=page(s,"MODEL");
        assert(std::find_if(manual.ui->metadata.entries.begin(),manual.ui->metadata.entries.end(),[&](const auto& e){return e.id==replayId;})->value==Replay::replay_setting_path("manual path.txt"));

    }
    {
        auto settings=defaults();Graphical::Session s(settings,settings,{});
        page(s,"start");click(s,"settings");page(s,"root");click(s,"maps");page(s,"maps");click(s,"map-new");
        std::this_thread::sleep_for(30ms);tap(s,SDL_SCANCODE_RETURN);page(s,"map-editor");
        command(s,{Frontend::UiCommandKind::MapBrush,7});
        wait(s,[](const auto& v){return v.ui->metadata.editor && v.ui->metadata.editor->brush==5;});
        Frontend::UiCommand paint{Frontend::UiCommandKind::MapPaint};paint.row=19;
        command(s,paint);wait(s,[](const auto& v){return v.ui->metadata.editor->dirty;});
        paint.row=10;paint.column=9;paint.alternate=true;command(s,paint);
        wait(s,[](const auto& v){return v.ui->metadata.editor->row==10;});
        command(s,{Frontend::UiCommandKind::MapEndStroke});
        const auto ctrl=Graphical::native_key(SDL_SCANCODE_LCTRL);s.input().update(ctrl,true);tap(s,SDL_SCANCODE_Z);s.input().update(ctrl,false);
        wait(s,[](const auto& v){return !v.ui->metadata.editor->dirty;});
        command(s,{Frontend::UiCommandKind::MapTab,1});wait(s,[](const auto& v){return v.ui->metadata.editor->sequenceFocus;});
        tap(s,SDL_SCANCODE_F3);wait(s,[](const auto& v){return !v.ui->metadata.editor->generated;});
        for(std::size_t i=0;i<30;++i)command(s,{Frontend::UiCommandKind::MapCandidate,i%7});
        wait(s,[](const auto& v){return v.ui->metadata.editor->sequenceSize==30;});
        auto view=s.view();assert(view.ui->metadata.editor->sequence.size()>9 && view.ui->metadata.editor->sequence.size()<=24);
        const auto first=view.ui->metadata.editor->sequenceFirst;
        Frontend::UiCommand scroll{Frontend::UiCommandKind::MapScroll};scroll.delta=-1;command(s,scroll);
        view=wait(s,[&](const auto& v){return v.ui->metadata.editor->sequenceFirst<first;});
        assert(view.ui->metadata.editor->caret==30);
        const auto caret=view.ui->metadata.editor->sequenceFirst+1;command(s,{Frontend::UiCommandKind::MapCaret,caret});
        wait(s,[&](const auto& v){return v.ui->metadata.editor->caret==caret;});
        tap(s,SDL_SCANCODE_P);page(s,"editor-options");click(s,"editor-1");
        auto rename=wait(s,[&](const auto& v){return s.text_input_active() && v.ui->metadata.parent;});
        assert(std::none_of(rename.ui->metadata.parent->entries.begin(),rename.ui->metadata.parent->entries.end(),[](const auto& e){return e.filePath;}));
        assert(!Graphical::file_browse_command(rename.ui->metadata));
        s.text_event({Frontend::TextEditKind::Insert,"重命名地图"});s.text_event({Frontend::TextEditKind::Confirm});
        page(s,"editor-options");tap(s,SDL_SCANCODE_ESCAPE);
        wait(s,[](const auto& v){return v.ui->metadata.editor && v.ui->metadata.editor->name=="重命名地图";});

    }
    {
        auto settings=defaults();settings.autoplayMode=1;settings.autoplayPps=30;settings.goal=4;settings.pieceTarget=2;
        Graphical::LaunchOptions options;options.startScreen=false;options.touchControls=true;
        Graphical::Session s(settings,settings,options);
        wait(s,[](const auto& v){return v.status==Graphical::SessionStatus::Finished;});
        tap(s,SDL_SCANCODE_TAB);
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.statistics.has_value();});
        command(s,{Frontend::UiCommandKind::StatisticsMetric,7});
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.statistics && v.ui->metadata.statistics->selectedMetric==7;});
        command(s,{Frontend::UiCommandKind::StatisticsPage,1});
        auto table=wait(s,[](const auto& v){return v.ui && v.ui->metadata.statistics && v.ui->metadata.statistics->page==Frontend::UiStatisticsPage::Clears;});
        Frontend::UiCommand cell{Frontend::UiCommandKind::StatisticsCell};
        cell.row=int(table.ui->metadata.statistics->pieces.size());cell.column=int(table.ui->metadata.statistics->lines.size());
        command(s,cell);
        wait(s,[&](const auto& v){return v.ui && v.ui->metadata.statistics && v.ui->metadata.statistics->selectedRow==cell.row && v.ui->metadata.statistics->selectedColumn==cell.column;});
        command(s,{Frontend::UiCommandKind::StatisticsPage,0});
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.statistics && v.ui->metadata.statistics->page==Frontend::UiStatisticsPage::Summary;});
        command(s,{Frontend::UiCommandKind::Back});
        wait(s,[](const auto& v){return v.ui && v.ui->metadata.kind==Frontend::UiKind::Notice;});
    }
    return 0;
}
}
