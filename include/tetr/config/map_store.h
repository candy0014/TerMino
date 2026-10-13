#ifndef TETR_CONFIG_MAP_STORE_H
#define TETR_CONFIG_MAP_STORE_H
#include "tetr/runtime/map_definition.h"
#include <filesystem>
#include <optional>
#include <vector>
#include <utility>

namespace UserConfig { struct Settings; }

namespace UserConfig::Maps {
struct Collection { int nextId=1;std::vector<Runtime::MapDefinition> maps; };
struct SaveUndo { Runtime::MapDefinition saved;std::optional<Runtime::MapDefinition> previous; };
std::filesystem::path default_path();
bool load(Collection& collection,const std::filesystem::path& path=default_path());
std::string default_name(const Collection& collection);
std::optional<Runtime::MapDefinition> find(int id,const std::filesystem::path& path=default_path());
bool save(Runtime::MapDefinition& map,SaveUndo& undo,const std::filesystem::path& path=default_path());
bool undo_save(const SaveUndo& undo,const std::filesystem::path& path=default_path());
using IdChanges=std::vector<std::pair<int,int>>;
int renumbered_id(int id,const IdChanges& changes);
void remap_selection(Settings& settings,const IdChanges& changes);
bool remove(int id,const std::filesystem::path& path=default_path(),IdChanges* changes=nullptr);
} // namespace UserConfig::Maps
#endif
