#ifndef TETR_CONFIG_STORE_H
#define TETR_CONFIG_STORE_H
#include "../../third_party/nlohmann/json.hpp"
#include <filesystem>
#include <functional>

namespace UserConfig::Storage {
using Json=nlohmann::json;
bool read(const std::filesystem::path& file,Json& document);
bool write(const std::filesystem::path& file,const Json& document);
bool update(const std::filesystem::path& file,const std::function<bool(Json&)>& edit);
bool json_path(const std::filesystem::path& file);
Json legacy_graphics(const std::filesystem::path& file);
}
#endif
