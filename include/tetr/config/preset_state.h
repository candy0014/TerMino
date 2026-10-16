#ifndef TETR_CONFIG_PRESET_STATE_H
#define TETR_CONFIG_PRESET_STATE_H

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace UserConfig {
struct Settings;
enum class SettingId;
enum class PresetKind { Classic, Delayed, Marathon, Custom };
using PresetValue=std::variant<int,double,std::string,std::vector<int>>;
using PresetFields=std::map<SettingId,PresetValue>;
struct PresetLayer {
    std::uint64_t serial=0;
    PresetKind kind=PresetKind::Classic;
    PresetFields reference;
    PresetFields values;
    std::string name;
};
struct PresetState {
    PresetFields base;
    std::vector<PresetLayer> layers;
    std::uint64_t nextSerial=1;
    // Only used while authoring a sparse preset; ordinary profiles have none.
    std::set<SettingId> undefinedBase;
};
// One volatile challenge layer, separate from persistent preset layers.
struct ChallengeAdjustment {
    std::string challengeId;
    PresetFields before;
    bool explicitlyStarted=false;
    // Full pre-selection snapshot, shared only as immutable rollback data.
    std::shared_ptr<const Settings> snapshot;
    bool paired=false;
};
struct ChallengeReplayRestore {
    int gameMode=0;
    std::string replayPath;
    bool pending=true;
    std::string challengeId;
};
}
#endif
