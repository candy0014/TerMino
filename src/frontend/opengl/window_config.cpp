#include "window_config.h"
#include <algorithm>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
namespace Graphical {
namespace {
WindowConfig normalized(WindowConfig c) {
    c.width=std::clamp(c.width,840,16384);c.height=std::clamp(c.height,560,16384);
    c.frameLimit=std::clamp(c.frameLimit,30,1000);return c;
}
}
WindowConfig load_window_config(const std::filesystem::path& file) {
    WindowConfig c;std::ifstream in(file);std::string line;
    while(std::getline(in,line)) {
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        const auto key=line.substr(0,eq);int value=0;
        try {std::size_t used=0;const auto text=line.substr(eq+1);value=std::stoi(text,&used);if(text.find_first_not_of(" \t\r",used)!=std::string::npos)continue;}
        catch(const std::exception&) {continue;}
        if(key=="width")c.width=value;else if(key=="height")c.height=value;
        else if(key=="frame_limit")c.frameLimit=value;
        else if(key=="vsync")c.vsync=value!=0;else if(key=="maximized")c.maximized=value!=0;
    }
    return normalized(c);
}
bool save_window_config(const std::filesystem::path& file,WindowConfig config) {
    config=normalized(config);std::error_code error;
    if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path(),error);
    if(error)return false;
    auto temporary=file;temporary+=".tmp";
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out<<"version=1\nwidth="<<config.width<<"\nheight="<<config.height<<"\nframe_limit="<<config.frameLimit
        <<"\nvsync="<<config.vsync<<"\nmaximized="<<config.maximized<<'\n';out.close();if(!out)return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,file,error);return !error;
#endif
}
}
