#ifndef TETR_CONFIG_VIRTUAL_CONTROLS_H
#define TETR_CONFIG_VIRTUAL_CONTROLS_H
#include "tetr/frontend/virtual_controls.h"
#include "tetr/config/client_config.h"
namespace UserConfig {
Frontend::Touch::Configuration load_virtual_controls(const std::filesystem::path& file=kClientConfigPath);
bool save_virtual_controls(const Frontend::Touch::Configuration& value,const std::filesystem::path& file=kClientConfigPath);
}
#endif
