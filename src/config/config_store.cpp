#include "config_store.h"
#include "tetr/config/client_config.h"
#include "tetr/util/runtime_directory_lock.h"
#include <algorithm>
#include <climits>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace UserConfig::Storage {
bool json_path(const std::filesystem::path& file) {return file.extension()==".json";}
bool read(const std::filesystem::path& file,Json& document) {
    std::error_code error;
    if(!std::filesystem::exists(file,error)) {
        if(error)return false;
        document={{"version",1},{"players",Json::object()}};return true;
    }
    if(std::filesystem::file_size(file,error)>4000000 || error)return false;
    std::ifstream input(file,std::ios::binary);if(!input)return false;
    auto parsed=Json::parse(input,nullptr,false);if(input.bad() || !parsed.is_object())return false;
    if(!parsed.contains("version") || !parsed["version"].is_number_integer() || parsed["version"]!=1)return false;
    if(parsed.contains("players") && !parsed["players"].is_object())return false;
    if(parsed.contains("graphics") && !parsed["graphics"].is_object())return false;
    document=std::move(parsed);return true;
}
bool write(const std::filesystem::path& file,const Json& document) {
    std::error_code error;
    if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path(),error);
    if(error)return false;
    auto temporary=file;temporary+=".tmp";
    std::string data;
    try {data=document.dump(2)+"\n";}catch(const Json::exception&) {return false;}
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);if(!output)return false;
    output<<data;output.close();
    if(!output){std::filesystem::remove(temporary,error);return false;}
#ifdef _WIN32
    if(MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
#else
    std::filesystem::rename(temporary,file,error);if(!error)return true;
#endif
    std::filesystem::remove(temporary,error);return false;
}
bool update(const std::filesystem::path& file,const std::function<bool(Json&)>& edit) {
    Util::FileWriteLock lock(file);if(!lock.acquired())return false;
    Json document;if(!read(file,document))return false;
    try {return edit(document) && write(file,document);}catch(const Json::exception&) {return false;}
}
Json legacy_graphics(const std::filesystem::path& file) {
    Json result=Json::object();std::ifstream input(file);std::string line;
    while(std::getline(input,line)) {
        const auto equal=line.find('=');if(equal==std::string::npos)continue;
        const auto key=line.substr(0,equal),text=line.substr(equal+1);
        try {
            std::size_t used=0;const int value=std::stoi(text,&used);
            if(text.find_first_not_of(" \t\r",used)!=std::string::npos)continue;
            if(key=="vsync" || key=="maximized")result[key]=value!=0;
            else if(key=="width" || key=="height" || key=="frame_limit")result[key]=value;
        }catch(const std::exception&) {}
    }
    return result;
}
}
namespace UserConfig {
namespace {
GraphicsSettings normalized(GraphicsSettings c) {
    c.width=c.width>0?std::min(c.width,16384):GraphicsSettings{}.width;
    c.height=c.height>0?std::min(c.height,16384):GraphicsSettings{}.height;
    c.frameLimit=std::clamp(c.frameLimit,30,1000);return c;
}
}
GraphicsSettings load_graphics_settings(const std::filesystem::path& file) {
    Storage::Json graphics;
    if(Storage::json_path(file)) {
        Storage::Json document;if(!Storage::read(file,document))return {};
        graphics=document.value("graphics",Storage::legacy_graphics(file.parent_path()/"graphics.ini"));
    } else graphics=Storage::legacy_graphics(file);
    GraphicsSettings c;
    const auto integer=[&](const char* key,int& target) {
        if(graphics.contains(key) && graphics[key].is_number_integer()) {
            const auto& value=graphics[key];
            if(value>=INT_MIN && value<=INT_MAX)target=value.get<int>();
        }
    };
    integer("width",c.width);integer("height",c.height);integer("frame_limit",c.frameLimit);
    if(graphics.contains("vsync") && graphics["vsync"].is_boolean())c.vsync=graphics["vsync"].get<bool>();
    if(graphics.contains("maximized") && graphics["maximized"].is_boolean())c.maximized=graphics["maximized"].get<bool>();
    return normalized(c);
}
bool save_graphics_settings(const std::filesystem::path& file,GraphicsSettings settings) {
    settings=normalized(settings);
    return Storage::update(file,[&](auto& document) {
        document["graphics"]={{"width",settings.width},{"height",settings.height},{"frame_limit",settings.frameLimit},
            {"vsync",settings.vsync},{"maximized",settings.maximized}};return true;
    });
}
int load_active_player(const std::filesystem::path& file) {
    Storage::Json document;
    if(Storage::read(file,document) && document.contains("active_player"))return document["active_player"]==1?1:0;
    int owner=0;std::ifstream(file.parent_path()/"active_player")>>owner;return owner==1?1:0;
}
bool save_active_player(const std::filesystem::path& file,int player) {
    if(!Storage::json_path(file)) {std::ofstream out(file.parent_path()/"active_player");out<<(player==1?1:0);return bool(out);}
    return Storage::update(file,[&](auto& document){document["active_player"]=player==1?1:0;return true;});
}
}
