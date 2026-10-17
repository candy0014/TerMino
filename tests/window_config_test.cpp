#include "window_config.h"
#include "viewport.h"
#include <cassert>
#include <chrono>
#include <fstream>
int main() {
    const auto logical=Graphical::Viewport::fit(1200,800),pixels=Graphical::Viewport::fit(2400,1600);
    assert(logical.screen_x(350)==350 && pixels.screen_x(350)==700);
    assert(logical.screen_y(200)==200 && pixels.screen_y(200)==400);
    const auto tall=Graphical::Viewport::fit(1200,1600);
    assert(tall.x==0 && tall.y==400 && tall.screen_y(200)==600);
    const auto wide=Graphical::Viewport::fit(2400,800);
    assert(wide.x==600 && wide.y==0 && wide.screen_x(350)==950);
    assert(Graphical::Viewport::fit(0,0).scale==0);
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("termino-window-中文 "+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file=folder/"graphics.ini";
    auto c=Graphical::load_window_config(file);assert(c.width==1200 && c.height==800 && c.frameLimit==240 && c.vsync);
    c.width=1920;c.height=1080;c.maximized=true;c.frameLimit=144;c.vsync=false;
    assert(Graphical::save_window_config(file,c));c=Graphical::load_window_config(file);
    assert(c.width==1920 && c.height==1080 && c.maximized && c.frameLimit==144 && !c.vsync);
    std::ofstream(file)<<"width=-5\nheight=999999\nframe_limit=0\nvsync=no\nwidth=1234junk\n";
    c=Graphical::load_window_config(file);assert(c.width==840 && c.height==16384 && c.frameLimit==30 && c.vsync);
    auto temporary=file;temporary+=".tmp";
    assert(Graphical::save_window_config(file,c));assert(!std::filesystem::exists(temporary));
    std::filesystem::remove_all(folder);
}
