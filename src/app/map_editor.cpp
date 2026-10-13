#include "map_editor.h"
#include "terminal_canvas.h"
#include "settings_navigation.h"
#include "settings_input.h"
#include "settings_view.h"
#include "ui_text.h"
#include "tetr/config/map_store.h"
#include "tetr/config/user_config.h"
#include "tetr/config/settings_schema.h"
#include "tetr/input/services.h"
#include "tetr/runtime/game_context.h"
#include "tetr/runtime/piece.h"
#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <linux/input.h>
#endif

namespace App {
MapEditorState::MapEditorState(Runtime::MapDefinition definition):map(std::move(definition)),row(map.height-1) {}
void MapEditorState::remember() {
    if(undoHistory.size()==128) undoHistory.erase(undoHistory.begin());
    undoHistory.push_back(map);redoHistory.clear();
}
void MapEditorState::resize(int width,int height) {
    width=std::clamp(width,4,100);height=std::clamp(height,2,100);
    if(width==map.width && height==map.height) return;
    remember();const int bottom=map.height-1-row;Runtime::resize_map(map,width,height);
    row=std::clamp(height-1-bottom,0,height-1);column=std::min(column,width-1);
}
void MapEditorState::paint(char symbol) {
    if(std::string(" GIJLOSTZ").find(symbol)==std::string::npos) return;
    brush=symbol;if(map.rows[row][column]==symbol) return;remember();map.rows[row][column]=symbol;
}
void MapEditorState::move(int rowDelta,int columnDelta,std::optional<char> heldColor) {
    if(heldColor) paint(*heldColor);
    const auto step=[&](int& position,int delta,int limit) {
        while(delta) {
            const int direction=delta>0 ? 1 : -1;
            position=std::clamp(position+direction,0,limit-1);
            if(heldColor) paint(*heldColor);
            delta-=direction;
        }
    };
    step(row,rowDelta,map.height);step(column,columnDelta,map.width);
}
void MapEditorState::insert(Piece::Type piece) {
    if(!piece.valid() || static_cast<std::size_t>(piece.index())>=Piece::catalog().size() || map.sequence.size()>=100000) return;
    remember();map.sequence.insert(map.sequence.begin()+static_cast<std::ptrdiff_t>(caret),piece);++caret;detailPiece=piece;
}
void MapEditorState::erase(bool backwards) {
    if((backwards && caret==0) || (!backwards && caret>=map.sequence.size())) return;
    remember();if(backwards) --caret;map.sequence.erase(map.sequence.begin()+static_cast<std::ptrdiff_t>(caret));
    detailPiece=map.sequence.empty() ? std::optional<Piece::Type>{} : map.sequence[std::min(caret,map.sequence.size()-1)];
}
void MapEditorState::change_generator() {
    remember();map.generator=map.generator==Runtime::MapGenerator::RotationSystem ? Runtime::MapGenerator::CustomSequence : Runtime::MapGenerator::RotationSystem;
}
void MapEditorState::change_rotation(Runtime::RotationSystem system) {if(map.rotationSystem!=system) {remember();map.rotationSystem=system;}}
void MapEditorState::change_floor(int floor) {floor=std::clamp(floor,1,10);if(map.foolFloor!=floor) {remember();map.foolFloor=floor;}}
void MapEditorState::change_goal(Runtime::MapGoal goal) {if(map.goal!=goal) {remember();map.goal=goal;}}
bool MapEditorState::undo() {
    if(undoHistory.empty()) return false;
    redoHistory.push_back(map);const auto id=map.id;const auto name=map.name;map=std::move(undoHistory.back());undoHistory.pop_back();map.id=id;map.name=name;
    row=std::min(row,map.height-1);column=std::min(column,map.width-1);caret=std::min(caret,map.sequence.size());detailPiece.reset();return true;
}
bool MapEditorState::redo() {
    if(redoHistory.empty()) return false;
    undoHistory.push_back(map);const auto id=map.id;const auto name=map.name;map=std::move(redoHistory.back());redoHistory.pop_back();map.id=id;map.name=name;
    row=std::min(row,map.height-1);column=std::min(column,map.width-1);caret=std::min(caret,map.sequence.size());detailPiece.reset();return true;
}
namespace {
enum EditorOption { SaveMap,RenameMap,FieldWidth,FieldHeight,Generator,RotationSystem,InitialFloor,MapGoal,ExitEditor,PieceLibrary };
std::string lower(const std::string& value) {
    // Catalog names use full-width letters. Accept ordinary keyboard letters
    // for both quick insertion and search without changing their display.
    std::string result;
    for(std::size_t i=0;i<value.size();++i) {
        unsigned char c=static_cast<unsigned char>(value[i]);
        if(c==0xef && i+2<value.size()) {
            const auto next=static_cast<unsigned char>(value[i+1]),last=static_cast<unsigned char>(value[i+2]);
            if((next==0xbc || next==0xbd) && (last&0xc0)==0x80) {
                const unsigned int point=((c&15)<<12)|((next&63)<<6)|(last&63);
                if(point>=0xff01 && point<=0xff5e) {c=static_cast<unsigned char>(point-0xfee0);i+=2;}
            }
        }
        result.push_back(c<128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c));
    }
    return result;
}
std::string text(const char* en,const char* cn) {return UiText::language()==UiText::Language::Chinese ? cn : en;}
std::string system_name(Runtime::RotationSystem system) {
    const auto choices=UserConfig::choice_options(UserConfig::ChoiceKind::RotationSystem);
    const auto found=std::find(choices.values.begin(),choices.values.end(),static_cast<int>(system));
    return found==choices.values.end() ? "?" : UiText::tr(choices.labels[static_cast<std::size_t>(found-choices.values.begin())]);
}
std::string goal_name(Runtime::MapGoal goal) {
    return goal==Runtime::MapGoal::ClearInitial ? text("Clear map","清空地图") : goal==Runtime::MapGoal::AllClear ? text("All clear field","全消版面") : text("OFF","关闭");
}
int letter(char c) {
#ifdef _WIN32
    return c;
#else
    switch(c) {case 'I':return KEY_I;case 'J':return KEY_J;case 'L':return KEY_L;case 'O':return KEY_O;case 'S':return KEY_S;case 'T':return KEY_T;case 'Z':return KEY_Z;case 'Y':return KEY_Y;}
    return 0;
#endif
}
int special(int index) {
#ifdef _WIN32
    static constexpr int keys[]={VK_SPACE,VK_DELETE,VK_HOME,VK_END,VK_F2,VK_F3,VK_F4,VK_F6,VK_F8,VK_OEM_2,VK_LSHIFT,VK_RSHIFT,VK_LCONTROL,VK_RCONTROL,VK_F5};
#else
    static constexpr int keys[]={KEY_SPACE,KEY_DELETE,KEY_HOME,KEY_END,KEY_F2,KEY_F3,KEY_F4,KEY_F6,KEY_F8,KEY_SLASH,KEY_LEFTSHIFT,KEY_RIGHTSHIFT,KEY_LEFTCTRL,KEY_RIGHTCTRL,KEY_F5};
#endif
    return keys[index];
}
int digit_key(int index) {
#ifdef _WIN32
    return '0'+index;
#else
    static constexpr int keys[]={KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8};return keys[index];
#endif
}
struct EditorSettings {
    UserConfig::Settings values;
    Settings::Model::SettingNode menu{"editor-options","EDITOR OPTIONS",""};
    std::vector<EditorOption> actions;
    explicit EditorSettings(const MapEditorState& state) {
        using namespace Settings;using Id=Model::SettingId;
        values.fieldWidth=state.map.width;values.fieldHeight=state.map.height;values.rotationSystem=static_cast<int>(state.map.rotationSystem);
        values.qpDefaultFloor=state.map.foolFloor;values.replayPath=state.map.name;
        const auto add=[&](EditorOption action,const std::string& label,std::optional<Model::SettingItem> item={},int indent=0) {
            Model::SettingNode node{"editor-"+std::to_string(action),std::string(indent,' ')+UiText::tr(label),"",Model::NodeKind::Setting};
            node.literalTitle=true;node.item=std::move(item);actions.push_back(action);menu.children.push_back(std::move(node));
        };
        const auto scalar=[&](Id id,int& value,const std::string& label) {return Model::SettingItem{id,label,std::ref(value),{},false,false,&values};};
        add(SaveMap,"Save map");
        Model::SettingItem name{Id::ReplayPath,"Rename",std::ref(values.replayPath),{},false,false,&values};name.hint="Enter confirms the map name. Esc cancels.";add(RenameMap,"Rename",name);
        add(FieldWidth,"Width",scalar(Id::FieldWidth,values.fieldWidth,"Width"));add(FieldHeight,"Height",scalar(Id::FieldHeight,values.fieldHeight,"Height"));
        add(Generator,"Generator");menu.children.back().summary=state.map.generator==Runtime::MapGenerator::CustomSequence ? UiText::tr("Custom sequence") : UiText::tr("Rotation System");
        auto rotation=scalar(Id::RotationSystem,values.rotationSystem,"Rotation System");const auto choices=UserConfig::choice_options(UserConfig::ChoiceKind::RotationSystem);rotation.choice={choices.labels,choices.values,false};
        if(state.map.generator==Runtime::MapGenerator::RotationSystem) {
            add(RotationSystem,"Rotation System",rotation);
            if(state.map.rotationSystem==Runtime::RotationSystem::AprilFools2026) add(InitialFloor,"Initial floor",scalar(Id::QpDefaultFloor,values.qpDefaultFloor,"Initial floor"),4);
        } else add(PieceLibrary,"Piece library",rotation);
        add(MapGoal,"Map mode");menu.children.back().summary=goal_name(state.map.goal);add(ExitEditor,"Exit editor");
    }
    std::size_t row(EditorOption action) const {return static_cast<std::size_t>(std::find(actions.begin(),actions.end(),action)-actions.begin());}
};
struct Edges {
    Runtime::GameContext& context;std::map<int,bool> down,triggered;
    explicit Edges(Runtime::GameContext& c):context(c) {
        for(const auto& [key,action]:context.input_services().bindings.entries()) down[key]=context.input_services().key_down(key);
        for(int i=0;i<15;++i) down[special(i)]=context.input_services().key_down(special(i));
        for(int i=0;i<9;++i) down[digit_key(i)]=context.input_services().key_down(digit_key(i));
        for(const char c:std::string("IJLOSTZY")) down[letter(c)]=context.input_services().key_down(letter(c));
    }
    void update() {for(auto& [key,value]:down) {const bool next=context.input_services().key_down(key);triggered[key]=next && !value;value=next;}}
    bool pressed(int key) {const bool result=triggered[key];triggered[key]=false;return result;}
    void suppress() {for(auto& [key,value]:down) value=context.input_services().key_down(key);triggered.clear();}
};
struct EditorView {
    Runtime::GameContext& context;
    TerminalCanvas canvas;
    int columns=96,rows=30;
    std::optional<Render::TerminalSize> fixedSize;
    Render::TerminalColors plain=Render::ui_colors(Render::ColorRole::Text,false);
    Render::TerminalColors selected{{{255,188,92}},{{56,34,16}}};
    explicit EditorView(Runtime::GameContext& c):context(c) {}
    void line(int y,int x,const std::string& value,int width=-1,bool active=false) {
        const auto& color=active ? selected : plain;
        canvas.write(y,x,value,width<0 ? columns-x-1 : width,color.foreground,color.background);
    }
    void begin(const std::string& title) {
        const auto size=fixedSize.value_or(Render::terminal_size().value_or(Render::TerminalSize{96,30}));
        if(size.columns!=columns || size.rows!=rows) {canvas.initialized=false;canvas.shown.clear();}
        columns=std::max(1,size.columns);rows=std::max(1,size.rows);
        // Keep the last column blank to avoid autowrap at the terminal edge.
        canvas.columnsLimit=std::max(0,columns-1);canvas.rowsLimit=rows;
        canvas.begin_frame();line(1,2,title);
    }
    void shape(Piece::Type type,int y,int x,int maxWidth,int maxHeight) {
        if(maxWidth<2 || maxHeight<=0) return;
        const auto cells=Piece::preview_cells(type);int minRow=100,minCol=100;
        for(const auto& cell:cells) {minRow=std::min(minRow,cell[0]);minCol=std::min(minCol,cell[1]);}
        const auto color=Render::cell_colors(Piece::metadata(type).colorId,false);
        for(const auto& cell:cells) {
            const int row=cell[0]-minRow,col=(cell[1]-minCol)*2;
            if(row<maxHeight && col+1<maxWidth)
                canvas.write(y+row,x+col,context.config.display.squareStyle ? "█ " : "██",2,color.foreground,color.background);
        }
    }
    int content_end() const {return std::max(0,rows-4);}
    std::optional<Piece::Type> selected_piece(const MapEditorState& state) const {
        if(state.detailPiece) return state.detailPiece;
        const auto types=map_browser_pieces(state.map.rotationSystem,state.query);
        return types.empty() ? std::optional<Piece::Type>{} : types[std::min(state.candidate,types.size()-1)];
    }
    std::vector<std::string> detail_lines(Piece::Type type,int width) const {
        const auto metadata=Piece::metadata(type);std::vector<std::string> result;
        for(const auto& caption:{text("System: ","旋转系统：")+system_name(metadata.rotationSystem),text("Name: ","名称：")+std::string(metadata.name)}) {
            const auto wrapped=Util::TerminalText::wrap(caption,std::max(1,width));result.insert(result.end(),wrapped.begin(),wrapped.end());
        }
        return result;
    }
    int detail_height(const MapEditorState& state,int width,int maximum) const {
        const auto type=selected_piece(state);if(!type || maximum<=0) return 0;
        const auto cells=Piece::preview_cells(*type);int low=100,high=-100;
        for(const auto& cell:cells) {low=std::min(low,cell[0]);high=std::max(high,cell[0]);}
        return std::min(maximum,1+static_cast<int>(detail_lines(*type,width).size())+high-low+1);
    }
    void details(const MapEditorState& state,int top,int width,int height) {
        const auto type=selected_piece(state);if(!type || height<=0) return;
        const int end=top+height;
        line(top++,2,text("Piece","方块信息"),width);
        for(const auto& caption:detail_lines(*type,width)) {if(top>=end) break;line(top++,2,caption,width);}
        shape(*type,top,2,width,std::max(0,end-top));
    }
    struct CandidatesLayout {int x,y,width,height,cardWidth,cardHeight,columns,lines;};
    CandidatesLayout candidates_layout(const MapEditorState& state) const {
        const bool sideBySide=columns>=44;
        const int split=sideBySide ? std::clamp(columns/4,12,24) : 0;
        const int x=sideBySide ? split+3 : 2,y=sideBySide ? 7 : 9+detail_height(state,columns-4,std::max(0,content_end()-10));
        const int width=std::max(0,columns-x-2),height=std::max(0,content_end()-y);
        const int cardWidth=std::max(1,std::min(16,width)),cardHeight=std::max(1,std::min(8,height));
        return {x,y,width,height,cardWidth,cardHeight,std::max(1,width/cardWidth),std::max(1,height/cardHeight)};
    }
    void palette(int& y) {
        static constexpr const char* numbers[]={"⓪","①","②","③","④","⑤","⑥","⑦","⑧"};
        int x=2;
        for(int i=0;i<9;++i) {
            const auto label=std::string(numbers[i])+" "+(i==0 ? text("Empty","空") : std::string(1," GIJLOSTZ"[i]));
            const int width=static_cast<int>(Util::TerminalText::width(label));
            if(x>2 && x+width>columns-2) {x=2;++y;}
            if(y>=content_end()) break;
            auto colors=plain;
            if(i) {
                colors.background=Render::board_cell_colors(Runtime::map_cell(" GIJLOSTZ"[i]),false).foreground;
                colors.foreground=i==3 || i==7 || i==8 || i==1 ? TerminalCanvas::Color{{255,255,255}} : TerminalCanvas::Color{{12,12,12}};
            }
            canvas.write(y,x,label,width,colors.foreground,colors.background);
            x+=width+2;
        }
        ++y;
    }
    void field(const MapEditorState& state) {
        const auto& map=state.map;
        line(4,2,text("FIELD","版面")+" "+std::to_string(map.width)+" x "+std::to_string(map.height)+"  "+text("Map mode","地图模式")+": "+goal_name(map.goal));
        int top=5;palette(top);++top;
        const int height=std::max(0,content_end()-top-1),width=std::max(0,(columns-8)/2);
        const int visibleRows=std::min(height,map.height),visibleCols=std::min(width,map.width);
        const int firstRow=std::clamp(state.row-height/2,0,std::max(0,map.height-height));
        const int firstCol=std::clamp(state.column-width/2,0,std::max(0,map.width-width));
        if(height && width) {
            line(top-1,2,(firstRow ? "↑ " : "  ")+std::string(firstCol ? "← " : "  ")+text("Cell","方格")+" "+std::to_string(state.column+1)+","+std::to_string(map.height-state.row));
            for(int y=0;y<visibleRows;++y) {
                line(top+y,2,"│",1);line(top+y,4+2*visibleCols,"│",1);
                for(int x=0;x<visibleCols;++x) {
                    const auto cell=Runtime::map_cell(map.rows[firstRow+y][firstCol+x]);
                    const bool cursor=state.row==firstRow+y && state.column==firstCol+x;
                    auto colors=Render::board_cell_colors(cell,false);
                    if(cursor) {
                        colors.background=cell.empty_cell() ? selected.background : colors.foreground;
                        colors.foreground=cell.empty_cell() ? selected.foreground : TerminalCanvas::Color{{255,255,255}};
                    }
                    const auto glyph=cursor ? "◇ " : cell.empty_cell() ? " ·" : context.config.display.squareStyle ? "█ " : "██";
                    canvas.write(top+y,4+x*2,glyph,2,colors.foreground,colors.background);
                }
            }
            std::string border="└";for(int i=0;i<2*visibleCols+1;++i) border+="─";border+="┘";
            line(top+visibleRows,2,border);
            // The border uses terminal columns, including when cells are cropped.
            if(firstRow+visibleRows<map.height) line(top+visibleRows,2,"↓",1);
            if(firstCol+visibleCols<map.width) line(top+visibleRows,4+2*visibleCols,"→",1);
        }
    }
    void sequence(const MapEditorState& state) {
        const auto& map=state.map;
        line(4,2,(map.generator==Runtime::MapGenerator::CustomSequence ? text("Custom sequence","自定义块序") : text("Fixed rotation system","固定旋转系统"))+"  "+goal_name(map.goal));
        line(5,2,system_name(map.rotationSystem)+"  "+text("Floor","楼层")+" "+std::to_string(map.foolFloor)+"  "+text("Space: options","空格：设置"));
        if(map.generator==Runtime::MapGenerator::RotationSystem) {
            line(7,2,text("Generate using this rotation system.","按所选旋转系统生成方块。"));
            const auto allowed=Piece::types_for_rotation_system(map.rotationSystem,false,map.foolFloor);
            if(!allowed.empty()) shape(allowed.front(),9,4,columns-6,content_end()-9);
            return;
        }
        const auto layout=candidates_layout(state);
        if(columns>=44) {
            const int width=layout.x-5,detailHeight=detail_height(state,width,std::max(0,(content_end()-9)/2));
            const int detailTop=content_end()-detailHeight,slots=std::max(0,detailTop-9);
            line(6,2,text("SEQUENCE","已选块序"),width);
            const auto first=state.caret>static_cast<std::size_t>(std::max(0,slots-1)) ? state.caret-std::max(0,slots-1) : 0;
            if(first) line(7,2,"↑ … "+std::to_string(first),width);
            for(int i=0;i<slots && first+i<=map.sequence.size();++i) {
                const auto index=first+i;
                const auto label=index==map.sequence.size() ? "+" : std::string(Piece::metadata(map.sequence[index]).name);
                line(8+i,2,(index==state.caret ? "› " : "  ")+std::to_string(index+1)+" "+label,width,index==state.caret);
            }
            if(first+slots<=map.sequence.size()) line(detailTop-1,2,"↓ … "+std::to_string(map.sequence.size()+1-first-slots),width);
            details(state,detailTop,width,detailHeight);
            for(int y=6;y<content_end();++y) line(y,layout.x-2,"│",1);
        } else {
            // In narrow terminals use a strip above the candidate grid. Follow
            // the insertion caret, showing the tail when appending new pieces.
            line(6,2,text("SEQUENCE","已选块序"),columns-4);
            std::string strip;std::size_t first=state.caret;
            const auto token=[&](std::size_t i) {return (i==state.caret ? "›" : "")+(i==map.sequence.size() ? std::string("+") : std::string(Piece::metadata(map.sequence[i]).name))+" ";};
            strip=token(first);
            while(first && Util::TerminalText::width("… "+token(first-1)+strip)<=static_cast<std::size_t>(std::max(0,columns-4))) {--first;strip=token(first)+strip;}
            if(first) strip="… "+strip;
            if(state.caret<map.sequence.size()) strip+=" …";
            line(7,2,strip,columns-4);
            details(state,8,columns-4,std::max(0,layout.y-9));
        }
        const auto types=map_browser_pieces(map.rotationSystem,state.query);
        const auto selected=types.empty() ? 0 : std::min(state.candidate,types.size()-1);
        line(layout.y-1,layout.x,text("CANDIDATES","候选块")+" "+std::to_string(types.empty() ? 0 : selected+1)+" / "+std::to_string(types.size()),layout.width);
        const auto pageSize=static_cast<std::size_t>(layout.columns*layout.lines),first=selected/pageSize*pageSize;
        if(types.empty()) line(layout.y,layout.x,text("No matching pieces.","没有匹配的方块。"),layout.width);
        for(std::size_t i=first;i<types.size() && i<first+pageSize;++i) {
            const int y=layout.y+static_cast<int>((i-first)/layout.columns)*layout.cardHeight;
            const int x=layout.x+static_cast<int>((i-first)%layout.columns)*layout.cardWidth;
            if(y>=content_end()) break;
            line(y,x,(i==selected ? "› " : "  ")+std::string(Piece::metadata(types[i]).name),std::min(layout.cardWidth-1,layout.width),i==selected);
            shape(types[i],y+1,x+1,std::max(0,layout.cardWidth-2),std::min(layout.cardHeight-1,content_end()-y-1));
        }
    }
    void draw(const MapEditorState& state,const Runtime::MapDefinition& baseline,const std::string& message,bool present=true) {
        if(state.exitPrompt) {
            begin(text("MAP EDITOR","地图编辑")+" / "+state.map.name+" *");
            const TerminalCanvas::Color orange{{237,109,0}};
            const int width=std::max(0,std::min(46,columns-7));
            const int top=std::min(4,std::max(0,rows-9)),height=std::max(0,std::min(5,rows-top-3));
            canvas.box(top,2,width,height,text("Unsaved changes","尚未保存的修改"),orange,plain.foreground,plain.background);
            if(height) canvas.write(top+1,4,text("Save before exiting?","退出前是否保存？"),width,orange,plain.background);
            const std::array<std::string,3> choices={text("Save and exit","保存退出"),text("Discard and exit","不保存退出"),text("Keep editing","不退出")};
            const int slots=std::max(0,height-2),first=std::clamp(state.exitChoice-slots+1,0,std::max(0,3-slots));
            for(int i=first;i<3 && i<first+slots;++i) line(top+3+i-first,4,(i==state.exitChoice ? "> " : "  ")+choices[i],width,i==state.exitChoice);
            if(!message.empty()) canvas.write(rows-2,2,message,columns-3,orange,plain.background);
            line(rows-1,2,text("Arrows Select   Enter Apply   Esc Keep editing","方向键选择  Enter 确认  Esc 不退出"));
            if(present) canvas.present({},false);
            return;
        }
        if(state.optionsOpen) {
            EditorSettings settings(state);auto surface=std::shared_ptr<TerminalCanvas>(&canvas,[](TerminalCanvas*) {});
            Settings::TerminalSettingsView view(context,false,0,false,surface,[&] {return std::optional<Render::TerminalSize>{fixedSize.value_or(Render::terminal_size().value_or(Render::TerminalSize{96,30}))};},true);
            view.set_menu_path(settings.menu,{},std::min<std::size_t>(state.option,settings.actions.size()-1));
            view.print_menu(settings.menu,std::min<std::size_t>(state.option,settings.actions.size()-1),"EDITOR OPTIONS",present);
            if(present && !message.empty()) view.print_message(message);
            const auto size=canvas.terminalSize.value_or(fixedSize.value_or(Render::terminal_size().value_or(Render::TerminalSize{96,30})));columns=size.columns;rows=size.rows;
            return;
        }
        const auto& map=state.map;
        begin(text("MAP EDITOR","地图编辑")+std::string(" / ")+(state.sequenceFocus ? text("SEQUENCE","块序") : text("FIELD","版面"))+" / "+
            (map.name.empty() ? text("New map","新建地图") : map.name)+(map.id ? " #"+std::to_string(map.id) : "")+(map!=baseline ? " *" : ""));
        // For very small terminals reserve the footer and clip all content.
        if(rows>=10) {
            line(2,2,state.sequenceFocus ? text("Tab → FIELD   Space Options","Tab → 版面编辑  空格打开设置") : text("Tab → SEQUENCE   Hold 0–8 + arrows to paint","Tab → 块序编辑  按住 0～8 + 方向键连续涂色"));
            line(3,2,std::string(std::max(0,columns-4),'-'));
            if(state.sequenceFocus) sequence(state);else field(state);
        }
        line(rows-4,2,state.sequenceFocus ? text("Insert at","插入位置")+" "+std::to_string(state.caret+1)+" / "+std::to_string(map.sequence.size())+(state.query.empty() ? "" : "  / "+state.query) :
            text("Cell","方格")+" "+std::to_string(state.column+1)+","+std::to_string(map.height-state.row)+"  "+text("Brush","颜色")+": "+(state.brush==' ' ? text("Empty","空") : std::string(1,state.brush)));
        line(rows-3,2,!message.empty() ? message : state.optionsOpen ? text("Up/Down Select   Right/Enter Open   Left/Right Change   Esc Close","上下选择  右/Enter 打开  左右调整  Esc 返回") : state.sequenceFocus ? text("Arrows Select   Shift+Left/Right Position   Enter Insert","方向键选块  Shift+左右定位  Enter 插入") : text("Arrows Move   0–8 Paint   Enter Brush","方向键移动  0～8 涂色  Enter 使用当前颜色"));
        line(rows-2,2,text("S Save   Space Options   Tab Field / Sequence","S 保存  空格设置  Tab 版面 / 块序"));
        line(rows-1,2,state.sequenceFocus ? text("Del/Backspace Remove   / Search   Esc Exit","Del/退格删除  / 检索  Esc 退出") : text("Esc Exit   S Save","Esc 退出  S 保存"));
        if(present) canvas.present({},false);
    }
};
}
std::vector<Piece::Type> map_browser_pieces(Runtime::RotationSystem system,const std::string& query) {
    if(query.empty()) return Piece::types_for_rotation_system(system);
    std::vector<std::string> words;std::istringstream search(lower(query));std::string word;while(search>>word) words.push_back(word);
    std::vector<Piece::Type> result;
    for(const auto& entry:Piece::catalog()) {
        if(Runtime::standard_rotation_system(entry.metadata.rotationSystem) && !entry.metadata.tetrioRotation) continue;
        const auto haystack=lower(system_name(entry.metadata.rotationSystem)+" "+std::string(entry.metadata.name)+" #"+std::to_string(entry.type.index()));
        if(std::all_of(words.begin(),words.end(),[&](const auto& token){
            if(token.size()==1 && std::string("ijlostz").find(token.front())!=std::string::npos) {
                const auto name=lower(std::string(entry.metadata.name));return !name.empty() && name.front()==token.front();
            }
            return haystack.find(token)!=std::string::npos;
        })) result.push_back(entry.type);
    }
    return result;
}
namespace {
std::string read_text(Runtime::GameContext& context,EditorView& view,const std::string& title,bool& cancelled) {
    bool escape=false;
    auto value=Settings::read_scalar_text(context,[] {},[&](const std::string& draft,std::size_t caret) {
        const auto caretColumns=Util::TerminalText::width(draft.substr(0,caret));
        const int width=std::max(1,view.columns-4);
        const auto scroll=caretColumns>=static_cast<std::size_t>(width) ? caretColumns-width+1 : 0;
        view.canvas.begin_update();view.line(view.rows-3,2,title);
        view.line(view.rows-2,2,Util::TerminalText::slice(draft,scroll,width),width);
        view.line(view.rows-1,2,text("Enter Confirm   Esc Cancel   Blank Use default","Enter 确认  Esc 取消  留空使用默认值"));
        view.canvas.present({{view.rows-2,2+static_cast<int>(caretColumns-scroll)}},false);
    },&escape);
    cancelled=escape || std::cin.fail() || value=="/cancel";if(std::cin.fail()) std::cin.clear();return value;
}
bool edit_editor_option(Runtime::GameContext& context,MapEditorState& state,EditorOption option,const std::function<bool()>& save={}) {
    using namespace Settings;
    EditorSettings settings(state);const auto row=settings.row(option);
    if(row>=settings.actions.size() || !settings.menu.children[row].item) return true;
    auto& item=*settings.menu.children[row].item;
    TerminalSettingsView view(context,false,0,false,{},[] {return std::optional<Render::TerminalSize>{Render::terminal_size().value_or(Render::TerminalSize{96,30})};},true);
    view.set_menu_path(settings.menu,{},row);
    if(option==RotationSystem || option==PieceLibrary) {
        auto selected=Model::selected_choice(item);view.print_choices(item,selected,"EDITOR OPTIONS");NavigationInput input(context);
        bool saveWasDown=context.input_services().key_down(letter('S'));
        for(;;) {
            const bool saveDown=context.input_services().key_down(letter('S'));
            if(save && saveDown && !saveWasDown) {
                const bool saved=save();view.print_choices(item,selected,"EDITOR OPTIONS");
                view.print_message(saved ? text("Map saved: ","地图已保存：")+state.map.name : text("Could not save the map.","地图保存失败。"));
            }
            saveWasDown=saveDown;
            if(input.update(NavigationKey::Back,false) || input.update(NavigationKey::Left,false) || input.update(NavigationKey::Backspace,false)) return true;
            const auto previous=selected;
            if(input.update(NavigationKey::Up,true)) selected=(selected+item.choice.values.size()-1)%item.choice.values.size();
            if(input.update(NavigationKey::Down,true)) selected=(selected+1)%item.choice.values.size();
            if(input.update(NavigationKey::PageUp,true)) selected=0;
            if(input.update(NavigationKey::PageDown,true)) selected=item.choice.values.size()-1;
            if(previous!=selected || view.terminal_size_changed()) view.print_choices(item,selected,"EDITOR OPTIONS");
            if(input.update(NavigationKey::Confirm,false) || input.update(NavigationKey::Right,false)) {
                state.change_rotation(Runtime::rotation_system_from_index(item.choice.values[selected]));state.query.clear();state.candidate=0;state.detailPiece.reset();return true;
            }
            context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
        }
    }
    std::string value;std::size_t caret=0;
    const auto redraw=[&](const std::string& next,std::size_t at) {value=next;caret=at;view.print_scalar_input(item,"EDITOR OPTIONS",next,at);};
    redraw({},0);bool cancelled=false;
    const auto entered=read_scalar_text(context,[&] {if(view.terminal_size_changed()) redraw(value,caret);},redraw,&cancelled);
    const bool failed=std::cin.fail();if(failed) std::cin.clear();
    if(cancelled || failed || entered.empty()) return true;
    if(option==RenameMap) {
        auto map=state.map;map.name=entered;
        if(!Runtime::valid_map(map)) return false;
        state.map.name=entered;return true;
    }
    if(!Model::assign_scalar(item,entered)) return false;
    if(option==InitialFloor) state.change_floor(settings.values.qpDefaultFloor);
    else state.resize(settings.values.fieldWidth,settings.values.fieldHeight);
    return true;
}

}
TerminalCanvas::Frame map_editor_frame(Runtime::GameContext& context,const MapEditorState& state,int columns,int rows) {
    EditorView view(context);view.fixedSize=Render::TerminalSize{columns,rows};view.draw(state,state.map,{},false);return std::move(view.canvas.pending);
}
bool run_map_editor(Runtime::GameContext& context,Runtime::MapDefinition definition) {
    using K=Settings::NavigationKey;
    Input::ControlInputScope scope(context.input_component());
    if(!definition.id && definition.name.empty()) {
        UserConfig::Maps::Collection collection;
        if(UserConfig::Maps::load(collection)) definition.name=UserConfig::Maps::default_name(collection);
        definition.foolFloor=std::clamp(context.user_settings().qpDefaultFloor,1,10);
    }
    MapEditorState state(std::move(definition));auto baseline=state.map;
    EditorView view(context);Settings::NavigationInput input(context);Edges edges(context);
    std::optional<UserConfig::Maps::SaveUndo> saveUndo;
    auto saveBaseline=baseline;
    bool saved=false;std::string message;
    const auto initialName=state.map.name;
    const auto save_draft=[&]() {
        auto map=state.map;
        if(!map.id && (map.name.empty() || map.name==initialName)) {
            UserConfig::Maps::Collection collection;if(!UserConfig::Maps::load(collection)) return false;
            map.name=UserConfig::Maps::default_name(collection);
        }
        UserConfig::Maps::SaveUndo undo;
        if(!UserConfig::Maps::save(map,undo)) return false;
        saveBaseline=baseline;state.map=std::move(map);baseline=state.map;saveUndo=std::move(undo);saved=true;return true;
    };
    int heldDigit=-1;
    const auto request_exit=[&]() {
        if(state.map==baseline) return true;
        state.exitPrompt=true;state.exitChoice=0;heldDigit=-1;message.clear();
        input.suppress_held_keys();edges.suppress();view.draw(state,baseline,message);return false;
    };
    const auto cycle_rotation=[&](int direction) {
        const auto choices=UserConfig::choice_options(UserConfig::ChoiceKind::RotationSystem);
        const auto found=std::find(choices.values.begin(),choices.values.end(),static_cast<int>(state.map.rotationSystem));
        const int current=static_cast<int>(found-choices.values.begin()),count=static_cast<int>(choices.values.size());
        state.change_rotation(Runtime::rotation_system_from_index(choices.values[(current+direction+count)%count]));
        state.query.clear();state.candidate=0;state.detailPiece.reset();
    };
    view.draw(state,baseline,message);
    for(;;) {
        edges.update();
        bool redraw=false;
        const bool up=input.update(K::Up,true),down=input.update(K::Down,true),left=input.update(K::Left,true),right=input.update(K::Right,true);
        const bool back=input.update(K::Back,false),enter=input.update(K::Confirm,false),tab=input.update(K::Tab,false);
        const bool pageUp=input.update(K::PageUp,true),pageDown=input.update(K::PageDown,true);
        const bool shift=context.input_services().key_down(special(10)) || context.input_services().key_down(special(11));
        const bool ctrl=context.input_services().key_down(special(12)) || context.input_services().key_down(special(13));
        bool savePressed=edges.pressed(letter('S'));
        if(savePressed) {
            const bool success=save_draft();message=success ? text("Map saved: ","地图已保存：")+state.map.name : text("Could not save the map.","地图保存失败。");
            state.exitPrompt=false;input.suppress_held_keys();edges.suppress();view.draw(state,baseline,message);continue;
        }
        const bool space=edges.pressed(special(0));
        if(state.exitPrompt) {
            if(back || tab || space || (enter && state.exitChoice==2)) {
                state.exitPrompt=false;message=text("Exit cancelled.","已取消退出。");input.suppress_held_keys();edges.suppress();view.draw(state,baseline,message);
            } else if(enter) {
                if(state.exitChoice==1) return saved;
                if(save_draft()) return true;
                message=text("Could not save the map. Choose another option or try again.","地图保存失败，请选择其他选项或重试。");view.draw(state,baseline,message);
            } else {
                const int delta=(down || right ? 1 : 0)-(up || left ? 1 : 0);
                if(delta) {state.exitChoice=(state.exitChoice+delta+3)%3;message.clear();}
                if(pageUp) state.exitChoice=0;
                if(pageDown) state.exitChoice=2;
                const auto size=Render::terminal_size();
                if(delta || pageUp || pageDown || (size && (size->columns!=view.columns || size->rows!=view.rows))) view.draw(state,baseline,message);
            }
            context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));continue;
        }
        int menuAction=-1;
        if(state.optionsOpen) {
            if((back && !savePressed) || space) {
                state.optionsOpen=false;message.clear();view.draw(state,baseline,message);
                context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));continue;
            }
            if(tab) state.optionsOpen=false;
            else {
                EditorSettings settings(state);
                state.option=std::clamp(state.option+(down ? 1 : 0)-(up ? 1 : 0),0,static_cast<int>(settings.actions.size()-1));
                const auto option=settings.actions[state.option];
                if((right || enter) && (option==RenameMap || option==FieldWidth || option==FieldHeight || option==RotationSystem || option==PieceLibrary || option==InitialFloor)) {
                    message=edit_editor_option(context,state,option,save_draft) ? "" : text("Invalid value; the map is unchanged.","数值无效，地图未修改。");
                    view.canvas.initialized=false;view.canvas.shown.clear();input.suppress_held_keys();edges.suppress();view.draw(state,baseline,message);continue;
                }
                const int direction=right ? 1 : left ? -1 : enter ? 1 : 0;
                if(direction) {
                    if(option==Generator) state.change_generator();
                    if(option==MapGoal) state.change_goal(static_cast<Runtime::MapGoal>((static_cast<int>(state.map.goal)+direction+3)%3));
                }
                if(enter) {
                    if(option==SaveMap || option==ExitEditor) menuAction=option;
                }
                if(menuAction<0 && !savePressed) {
                    const bool changed=up || down || left || right || enter;
                    const auto size=Render::terminal_size();
                    const bool resized=size && (size->columns!=view.columns || size->rows!=view.rows);
                    if(changed) message.clear();
                    if(changed || resized) view.draw(state,baseline,message);
                    context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));continue;
                }
                if(menuAction==ExitEditor) {
                    if(request_exit()) return saved;
                    continue;
                }
                state.optionsOpen=false;
                if(menuAction==0) savePressed=true;
            }
        } else if(space && !savePressed) {
            state.optionsOpen=true;heldDigit=-1;message.clear();view.draw(state,baseline,message);
            context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));continue;
        }
        if(back && !savePressed) {if(request_exit()) return saved;continue;}
        if(heldDigit>=0 && !context.input_services().key_down(digit_key(heldDigit))) heldDigit=-1;
        if(tab) {state.sequenceFocus=!state.sequenceFocus;heldDigit=-1;message.clear();redraw=true;}
        if(!state.sequenceFocus && !ctrl && !shift) {
            for(int i=0;i<9;++i) if(edges.pressed(digit_key(i))) {heldDigit=i;state.paint(" GIJLOSTZ"[i]);redraw=true;}
        }
        if(up || down || left || right) {
            if(state.sequenceFocus) {
                const int caretDelta=shift ? (right ? 1 : 0)-(left ? 1 : 0) : 0;
                state.caret=static_cast<std::size_t>(std::clamp(static_cast<long long>(state.caret)+caretDelta,0LL,static_cast<long long>(state.map.sequence.size())));
                const auto types=map_browser_pieces(state.map.rotationSystem,state.query);
                const auto current=types.empty() ? 0 : std::min(state.candidate,types.size()-1);
                const auto layout=view.candidates_layout(state);
                const int delta=shift ? 0 : (right ? 1 : 0)-(left ? 1 : 0)+layout.columns*((down ? 1 : 0)-(up ? 1 : 0));
                state.candidate=types.empty() ? 0 : static_cast<std::size_t>(std::clamp(static_cast<long long>(current)+delta,0LL,static_cast<long long>(types.size()-1)));
                if(shift && !state.map.sequence.empty()) state.detailPiece=state.map.sequence[std::min(state.caret,state.map.sequence.size()-1)];
                else if(!shift) state.detailPiece.reset();
            } else state.move((down ? 1 : 0)-(up ? 1 : 0),(right ? 1 : 0)-(left ? 1 : 0),heldDigit<0 ? std::optional<char>{} : std::optional<char>{" GIJLOSTZ"[heldDigit]});
            redraw=true;
        }
        if(ctrl && edges.pressed(letter('Z'))) {state.undo();redraw=true;}
        if(ctrl && edges.pressed(letter('Y'))) {state.redo();redraw=true;}
        if(savePressed) {
            message=save_draft() ? text("Map saved: ","地图已保存：")+state.map.name : text("Could not save the map.","地图保存失败。");
            input.suppress_held_keys();edges.suppress();view.draw(state,baseline,message);continue;
        }
        if(edges.pressed(special(8))) {
            if(saveUndo && UserConfig::Maps::undo_save(*saveUndo)) {
                if(saveUndo->previous) baseline=*saveUndo->previous;
                else {baseline=saveBaseline;state.map.id=0;}
                saveUndo.reset();saved=false;message=text("Last map save undone; the draft is kept.","已撤销最近一次保存，保留当前编辑内容。");
            } else message=text("No map save to undo, or this map changed elsewhere.","没有可撤销的保存，或地图已被其他操作修改。");
            redraw=true;
        }
        if(!state.sequenceFocus && (edges.pressed(special(4)) || edges.pressed(special(14)))) {
            const auto option=context.input_services().key_down(special(14)) ? FieldHeight : FieldWidth;
            if(!edit_editor_option(context,state,option,save_draft)) message=text("Invalid value; the field is unchanged.","数值无效，场地未修改。");
            view.canvas.initialized=false;view.canvas.shown.clear();
            input.suppress_held_keys();edges.suppress();redraw=true;
        }
        if(state.sequenceFocus && edges.pressed(special(5))) {state.change_generator();redraw=true;}
        if(state.sequenceFocus && edges.pressed(special(6))) {
            cycle_rotation(shift ? -1 : 1);redraw=true;
        }
        if(state.sequenceFocus && state.map.generator==Runtime::MapGenerator::RotationSystem && state.map.rotationSystem==Runtime::RotationSystem::AprilFools2026 && edges.pressed(special(7))) {
            edit_editor_option(context,state,InitialFloor,save_draft);view.canvas.initialized=false;view.canvas.shown.clear();
            input.suppress_held_keys();edges.suppress();redraw=true;
        }
        if(!state.sequenceFocus) {
            if(enter && menuAction<0) {state.paint(state.brush);redraw=true;}
        } else if(state.map.generator==Runtime::MapGenerator::CustomSequence) {
            if(pageUp || pageDown) {
                const auto types=map_browser_pieces(state.map.rotationSystem,state.query);
                const auto layout=view.candidates_layout(state);
                const int page=layout.columns*layout.lines;
                state.candidate=types.empty() ? 0 : static_cast<std::size_t>(std::clamp(static_cast<long long>(state.candidate)+(pageDown ? page : -page),0LL,static_cast<long long>(types.size()-1)));
                state.detailPiece.reset();
                redraw=true;
            }
            if(edges.pressed(special(9))) {
                bool cancelled=false;auto next=read_text(context,view,text("Search piece name or rotation system; blank clears","检索名称或旋转系统；留空清除"),cancelled);
                if(!cancelled) {state.query=std::move(next);state.candidate=0;state.detailPiece.reset();}
                input.suppress_held_keys();edges.suppress();redraw=true;
            }
            if(enter && menuAction<0) {
                const auto types=map_browser_pieces(state.map.rotationSystem,state.query);
                if(!types.empty()) state.insert(types[std::min(state.candidate,types.size()-1)]);
                redraw=true;
            }
            if(input.update(K::Backspace,true)) {state.erase(true);redraw=true;}
            if(edges.pressed(special(1))) {state.erase(false);redraw=true;}
            if(edges.pressed(special(2))) {state.caret=0;if(!state.map.sequence.empty()) state.detailPiece=state.map.sequence.front();redraw=true;}
            if(edges.pressed(special(3))) {state.caret=state.map.sequence.size();if(!state.map.sequence.empty()) state.detailPiece=state.map.sequence.back();redraw=true;}
            if(!ctrl) for(const char c:std::string("IJLOTZ")) if(edges.pressed(letter(c))) {
                const auto choices=Piece::types_for_rotation_system(state.map.rotationSystem);
                const auto found=std::find_if(choices.begin(),choices.end(),[&](const auto type){const auto name=lower(std::string(Piece::metadata(type).name));return !name.empty() && name.front()==static_cast<char>(std::tolower(static_cast<unsigned char>(c)));});
                if(found!=choices.end()) {state.insert(*found);redraw=true;}
            }
        }
        const auto size=Render::terminal_size();if(size && (size->columns!=view.columns || size->rows!=view.rows)) redraw=true;
        if(redraw) view.draw(state,baseline,message);
        context.services.clock.sleep_for(Runtime::Duration::from_seconds(.001));
    }
}
} // namespace App
