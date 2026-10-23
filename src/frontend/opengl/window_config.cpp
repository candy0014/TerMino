#include "window_config.h"
namespace Graphical {
WindowConfig load_window_config(const std::filesystem::path& file) {return UserConfig::load_graphics_settings(file);}
bool save_window_config(const std::filesystem::path& file,WindowConfig settings) {return UserConfig::save_graphics_settings(file,settings);}
}
