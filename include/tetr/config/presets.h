#ifndef TETR_CONFIG_PRESETS_H
#define TETR_CONFIG_PRESETS_H

#include "tetr/config/settings_schema.h"
#include <filesystem>

namespace UserConfig::Presets {
struct Definition {
    PresetKind kind;
    const char* name;
    const char* description;
    PresetFields fields;
};
const std::vector<Definition>& definitions();
const Definition& definition(PresetKind kind);
std::string name(const PresetLayer& layer);
PresetValue read_value(const Settings& settings,SettingId id);
void write_value(Settings& settings,SettingId id,const PresetValue& value);
bool defined(const Settings& settings,SettingId id);
void define(Settings& settings,SettingId id);
void undefine(Settings& settings,SettingId id);
PresetFields draft_fields(const Settings& settings);
struct SavedPreset {
    int id=0;std::string name;PresetFields fields;PresetKind kind=PresetKind::Custom;
    // Absent in older/uniform presets; an empty table explicitly leaves P2 undefined.
    std::optional<PresetFields> playerTwoFields;
};
struct Collection { int nextId=1;std::vector<SavedPreset> presets; };
bool is_local_versus(const SavedPreset& preset);
std::filesystem::path default_path();
bool load(Collection& collection,const std::filesystem::path& path=default_path());
bool save(SavedPreset& preset,const std::filesystem::path& path=default_path());
std::optional<SavedPreset> find(int id,const std::filesystem::path& path=default_path());
bool remove_saved(int id,const std::filesystem::path& path=default_path());
std::string default_name(const Collection& collection);
bool apply(Settings& settings,const SavedPreset& preset,bool keepMatchMode=false,bool playerTwo=false);
bool remap_maps(const std::vector<std::pair<int,int>>& changes,const std::filesystem::path& path=default_path());
bool apply(Settings& settings,PresetKind kind,bool keepMatchMode=false);
bool remove(Settings& settings,std::uint64_t serial,bool keepMatchMode=false);
bool undo(Settings& settings,bool keepMatchMode=false);
bool clear(Settings& settings,bool keepMatchMode=false);
void commit(Settings& settings);
void record_edits(Settings& settings);
const PresetLayer* source(const Settings& settings,SettingId id);
bool modified(const PresetLayer& layer);
std::string encode(const PresetState& state);
bool decode(std::string_view text,PresetState& state);
}
#endif
