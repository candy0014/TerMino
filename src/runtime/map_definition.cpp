#include "tetr/runtime/map_definition.h"
#include "tetr/runtime/piece.h"
#include "tetr/runtime/runtime_config.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <locale>

namespace Runtime {
bool MapDefinition::operator==(const MapDefinition& o) const {
    return id==o.id && name==o.name && width==o.width && height==o.height && rows==o.rows &&
        generator==o.generator && rotationSystem==o.rotationSystem && foolFloor==o.foolFloor && goal==o.goal && sequence==o.sequence;
}
bool valid_map(const MapDefinition& map) {
    if(map.id<0 || map.name.size()>256 || std::any_of(map.name.begin(),map.name.end(),[](unsigned char c){return c<32 || c==127;}) ||
       map.width<4 || map.width>100 || map.height<2 || map.height>100 ||
       map.rows.size()!=static_cast<std::size_t>(map.height) || map.foolFloor<1 || map.foolFloor>10 ||
       map.sequence.size()>100000 || static_cast<int>(map.goal)<0 || static_cast<int>(map.goal)>2 ||
       (map.generator!=MapGenerator::RotationSystem && map.generator!=MapGenerator::CustomSequence)) return false;
    const int rotation=static_cast<int>(map.rotationSystem);
    if(rotation<0 || rotation>11) return false;
    for(const auto& row:map.rows)
        if(row.size()!=static_cast<std::size_t>(map.width) || row.find_first_not_of(" .GIJLOSTZ")!=std::string::npos) return false;
    for(const auto type:map.sequence)
        if(!type.valid() || static_cast<std::size_t>(type.index())>=Piece::catalog().size()) return false;
    return true;
}
BoardCell map_cell(char symbol) {
    if(symbol=='G') return BoardCell::garbage();
    const std::string colors="IJLOSTZ";
    const auto color=colors.find(symbol);
    return color==std::string::npos ? BoardCell::empty() : BoardCell::piece(static_cast<int>(color));
}
char map_symbol(BoardCell cell) {
    if(cell.empty_cell()) return ' ';
    if(cell.is_piece() && cell.color_id()>=0 && cell.color_id()<7) return "IJLOSTZ"[cell.color_id()];
    return 'G';
}
void resize_map(MapDefinition& map,int width,int height) {
    width=std::clamp(width,4,100);height=std::clamp(height,2,100);
    std::vector<std::string> rows(height,std::string(width,' '));
    for(int y=0;y<std::min(height,map.height);++y)
        for(int x=0;x<std::min(width,map.width);++x) rows[height-1-y][x]=map.rows[map.height-1-y][x];
    map.width=width;map.height=height;map.rows=std::move(rows);
}
std::string encode_map(const MapDefinition& map) {
    if(!valid_map(map)) return {};
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<"[map "<<map.id<<"]\nName = "<<std::quoted(map.name)<<"\nSize = "<<map.width<<' '<<map.height
       <<"\nGenerator = "<<(map.generator==MapGenerator::CustomSequence ? "sequence" : "rotation")
       <<"\nRotation = "<<static_cast<int>(map.rotationSystem)<<"\nFloor = "<<map.foolFloor<<"\nGoal = "<<static_cast<int>(map.goal)<<"\nSequence =";
    for(const auto type:map.sequence) out<<' '<<type.index();
    out<<"\n{{pfstart}}\n";
    for(const auto& row:map.rows) {
        out<<"{{pfrow";
        for(const auto symbol:row) out<<'|'<<(symbol=='.' ? ' ' : symbol);
        out<<"}}\n";
    }
    out<<"{{pfend}}\n[/map]\n";return out.str();
}
bool decode_map(std::string_view text,MapDefinition& map) {
    if(text.size()>3000000) return false;
    std::istringstream input{std::string(text)};input.imbue(std::locale::classic());
    MapDefinition result;result.rows.clear();
    bool started=false,ended=false,field=false,fieldEnded=false,name=false,size=false,generator=false,rotation=false,floor=false,goal=false,sequence=false;
    std::string line;
    while(std::getline(input,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.empty()) continue;
        if(ended) return false;
        if(!started) {
            if(line.rfind("[map ",0)!=0 || line.back()!=']') return false;
            std::istringstream id(line.substr(5,line.size()-6));char extra;
            if(!(id>>result.id) || (id>>extra)) return false;
            started=true;continue;
        }
        if(line=="{{pfstart}}") { if(field || fieldEnded) return false;field=true;continue; }
        if(line=="{{pfend}}") { if(!field) return false;field=false;fieldEnded=true;continue; }
        if(line=="[/map]") { if(field || !fieldEnded) return false;ended=true;continue; }
        if(field) {
            if(line.rfind("{{pfrow|",0)!=0 || line.size()<10 || line.substr(line.size()-2)!="}}") return false;
            std::string row;
            const auto cells=line.substr(8,line.size()-10);
            std::size_t begin=0;
            for(;;) {
                const auto end=cells.find('|',begin);const auto value=cells.substr(begin,end==std::string::npos ? end : end-begin);
                if(value.size()>1) return false;
                row.push_back(value.empty() ? ' ' : value.front());
                if(end==std::string::npos) break;
                begin=end+1;
            }
            std::replace(row.begin(),row.end(),'.',' ');
            result.rows.push_back(std::move(row));if(result.rows.size()>100) return false;continue;
        }
        const auto equals=line.find('=');if(equals==std::string::npos) return false;
        auto key=line.substr(0,equals);while(!key.empty() && key.back()==' ') key.pop_back();
        std::istringstream value(line.substr(equals+1));value.imbue(std::locale::classic());char extra;
        if(key=="Name") {if(name || !(value>>std::quoted(result.name))) return false;name=true;}
        else if(key=="Size") {if(size || !(value>>result.width>>result.height)) return false;size=true;}
        else if(key=="Generator") {
            std::string kind;if(generator || !(value>>kind) || (kind!="rotation" && kind!="sequence")) return false;
            result.generator=kind=="sequence" ? MapGenerator::CustomSequence : MapGenerator::RotationSystem;generator=true;
        } else if(key=="Rotation") {int system;if(rotation || !(value>>system) || system<0 || system>11) return false;result.rotationSystem=rotation_system_from_index(system);rotation=true;}
        else if(key=="Floor") {if(floor || !(value>>result.foolFloor)) return false;floor=true;}
        else if(key=="Goal") {int mode;if(goal || !(value>>mode) || mode<0 || mode>2) return false;result.goal=static_cast<MapGoal>(mode);goal=true;}
        else if(key=="Sequence") {
            if(sequence) return false;sequence=true;
            int type;while(value>>type) {result.sequence.push_back(Piece::Type::from_index(type));if(result.sequence.size()>100000) return false;}
            if(!value.eof()) return false;continue;
        } else return false;
        if(value>>extra) return false;
    }
    if(!started || !ended || !name || !size || !generator || !rotation || !floor || !sequence || !valid_map(result)) return false;
    map=std::move(result);return true;
}
std::string encode_map_hex(const MapDefinition& map) {
    static constexpr char hex[]="0123456789abcdef";
    const auto text=encode_map(map);std::string result;result.reserve(text.size()*2);
    for(const unsigned char c:text) {result+=hex[c>>4];result+=hex[c&15];}return result;
}
bool decode_map_hex(std::string_view text,MapDefinition& map) {
    if(text.size()%2 || text.size()>6000000) return false;
    const auto digit=[](char c){return c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : -1;};
    std::string result;result.reserve(text.size()/2);
    for(std::size_t i=0;i<text.size();i+=2) {const int a=digit(text[i]),b=digit(text[i+1]);if(a<0 || b<0) return false;result+=static_cast<char>(a*16+b);}
    return decode_map(result,map);
}
MapDefinition construction_map_target(const MapDefinition& map) {
    MapDefinition target;target.id=map.id;target.name=map.name;target.width=map.width;target.height=map.height;target.rows=map.rows;
    for(auto& row:target.rows)for(auto& cell:row)cell=map_cell(cell).occupied()?'G':' ';
    return target;
}
void apply_map_config(RuntimeConfig& config,const MapDefinition& map) {
    if(config.challengeRules==ChallengeRules::ConstructMap) {
        config.initialMap=std::make_shared<const MapDefinition>(construction_map_target(map));
        config.field.width=map.width;config.field.height=map.height;
        return;
    }
    auto stored=std::make_shared<MapDefinition>(map);
    if(!config.legacyEventGeometry)
        for(auto& type:stored->sequence)type=Piece::canonical_event_piece(type);
    if(stored->generator==MapGenerator::CustomSequence && config.mapRotationSystem>=0) {
        const auto system=rotation_system_from_index(config.mapRotationSystem);
        const auto candidates=Piece::types_for_rotation_system(system);
        // Replace only equivalent named shapes. Special pieces without an
        // equivalent in the requested system keep their original definition.
        const auto normalized=[](Piece::Shape cells) {
            int row=100,col=100;
            for(const auto& cell:cells) {row=std::min(row,cell[0]);col=std::min(col,cell[1]);}
            for(auto& cell:cells) {cell[0]-=row;cell[1]-=col;}
            std::sort(cells.begin(),cells.end());return cells;
        };
        std::vector<Piece::Type> replacements(Piece::catalog().size(),Piece::Type::none());
        for(auto& type:stored->sequence) {
            auto& replacement=replacements[type.index()];
            if(replacement.valid()) {type=replacement;continue;}
            replacement=type;
            const auto& source=Piece::catalog_entry(type);
            for(const auto candidate:candidates) {
                const auto& target=Piece::catalog_entry(candidate);
                if(source.metadata.name!=target.metadata.name) continue;
                const auto shape=normalized(source.shapes[0]);
                if(std::any_of(target.shapes.begin(),target.shapes.end(),[&](const auto& cells){return shape==normalized(cells);})) {replacement=candidate;break;}
            }
            type=replacement;
        }
    }
    config.initialMap=stored;
    config.field.width=stored->width;config.field.height=stored->height;
    config.quickPlay.fool=FoolMode::Disabled;
    config.quickPlay.snowball=ModifierMode::Disabled;
    config.quickPlay.defaultFloor=stored->foolFloor;
    if(stored->generator==MapGenerator::RotationSystem) {
        config.rotationSystem=stored->rotationSystem;config.legacyRotationRules=false;config.legacyFoolPieceSet=false;
    } else if(config.mapRotationSystem>=0) {
        config.rotationSystem=rotation_system_from_index(config.mapRotationSystem);config.legacyRotationRules=false;
    } else if(!stored->sequence.empty()) {
        const auto first=Piece::metadata(stored->sequence.front());
        config.rotationSystem=first.rotationSystem;config.legacyRotationRules=!first.tetrioRotation;
    }
}
} // namespace Runtime
