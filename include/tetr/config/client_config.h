#ifndef TETR_CONFIG_CLIENT_CONFIG_H
#define TETR_CONFIG_CLIENT_CONFIG_H
#include <filesystem>

namespace UserConfig {
inline constexpr const char* kClientConfigPath=".tetr/config.json";
#ifdef _WIN32
inline constexpr const char* kConfigPlatform="windows";
#elif defined(__ANDROID__)
inline constexpr const char* kConfigPlatform="android";
#else
inline constexpr const char* kConfigPlatform="linux";
#endif
struct GraphicsSettings {
    int width=1200,height=800,frameLimit=240;
    bool vsync=true,maximized=false;
};
GraphicsSettings load_graphics_settings(const std::filesystem::path& file=kClientConfigPath);
bool save_graphics_settings(const std::filesystem::path& file,GraphicsSettings settings);
int load_active_player(const std::filesystem::path& file=kClientConfigPath);
bool save_active_player(const std::filesystem::path& file,int player);
}
#endif
