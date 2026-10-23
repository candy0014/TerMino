#ifndef TETR_OPENGL_WINDOW_CONFIG_H
#define TETR_OPENGL_WINDOW_CONFIG_H
#include <filesystem>
#include "tetr/config/client_config.h"
namespace Graphical {
using WindowConfig=UserConfig::GraphicsSettings;
WindowConfig load_window_config(const std::filesystem::path& file);
bool save_window_config(const std::filesystem::path& file,WindowConfig config);
}
#endif
