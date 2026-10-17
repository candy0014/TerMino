#ifndef TETR_OPENGL_WINDOW_CONFIG_H
#define TETR_OPENGL_WINDOW_CONFIG_H
#include <filesystem>
namespace Graphical {
struct WindowConfig {
    int width=1200,height=800,frameLimit=240;
    bool vsync=true,maximized=false;
};
WindowConfig load_window_config(const std::filesystem::path& file);
bool save_window_config(const std::filesystem::path& file,WindowConfig config);
}
#endif
