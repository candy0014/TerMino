#include "tetr/config/multiplayer_config.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/normalization.h"
#include "tetr/config/map_store.h"
#include "tetr/util/text_codec.h"
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
namespace UserConfig {
namespace {
bool handling(const SettingDescriptor& d) {
    return d.section==Section::Handling && d.id!=SettingId::GravityInterval;
}
bool synchronized(const SettingDescriptor& d) {
    if (d.keyBinding || d.section==Section::Network || d.section==Section::Others ||
        d.id==SettingId::Ghost || d.id==SettingId::MapId || d.id==SettingId::ReplayPath ||
        d.id==SettingId::ReplayTimeMultiplier) return false;
    return std::holds_alternative<int Settings::*>(d.member) || std::holds_alternative<double Settings::*>(d.member);
}
bool view_setting(const SettingDescriptor& d) {
    return d.section==Section::Others || d.section==Section::Network || d.keyBinding || d.id==SettingId::Ghost ||
        d.id==SettingId::ReplayPath || d.id==SettingId::ReplayTimeMultiplier;
}
std::string key(const SettingDescriptor& d) {return std::string(section_name(d.section))+"/"+(d.keyBinding?std::to_string(static_cast<int>(d.id)):std::string(d.diskName));}
}
std::string encode_multiplayer_config(const Settings& settings,const Runtime::RuntimeConfig& config,bool includePersonal) {
    std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(17)<<"TETR_ROOM_CONFIG 1\n";
    for (const auto& d:setting_schema()) if (synchronized(d) || (includePersonal && view_setting(d))) {
        out<<key(d)<<'=';
        if(d.keyBinding) out<<control_keys_text(settings,d.controlIndex);
        else if (const auto member=std::get_if<int Settings::*>(&d.member)) out<<settings.**member;
        else if(const auto member=std::get_if<std::string Settings::*>(&d.member)) out<<settings.**member;
        else out<<settings.*std::get<double Settings::*>(d.member);
        out<<'\n';
    }
    out<<"continuousCancellation="<<(config.quickPlay.continuousCancellation?1:0)<<"\nMAP\n";
    if (config.initialMap) out<<Runtime::encode_map(*config.initialMap);
    return out.str();
}
std::optional<MultiplayerConfig> decode_multiplayer_config(std::string_view wire) {
    constexpr std::string_view header="TETR_ROOM_CONFIG 1\n";
    if (wire.size()>3100000 || wire.substr(0,header.size())!=header) return std::nullopt;
    const auto mapAt=wire.find("\nMAP\n",header.size());
    if (mapAt==std::string_view::npos || mapAt>20000) return std::nullopt;
    MultiplayerConfig result;
    std::set<std::string> seen;
    std::size_t at=header.size();
    while (at<mapAt) {
        const auto end=wire.find('\n',at), equal=wire.find('=',at);
        if (end==std::string_view::npos || equal>=end) return std::nullopt;
        const auto name=wire.substr(at,equal-at), value=wire.substr(equal+1,end-equal-1);
        if (!seen.insert(std::string(name)).second) return std::nullopt;
        if (name=="continuousCancellation") {
            if (value!="0" && value!="1") return std::nullopt;
            result.continuousCancellation=value=="1";
        } else {
            bool found=false;
            for (const auto& d:setting_schema()) if ((synchronized(d) || view_setting(d)) && key(d)==name) {
                if(d.keyBinding) {
                    const auto keys=parse_control_keys(value);if(!keys) return std::nullopt;set_control_keys(result.settings,d.controlIndex,*keys);
                } else if (const auto member=std::get_if<int Settings::*>(&d.member)) {
                    const auto number=Util::Text::parse_integer<int>(value); if (!number) return std::nullopt;
                    result.settings.**member=*number;
                } else if(const auto member=std::get_if<std::string Settings::*>(&d.member)) {
                    if(value.size()>4096 || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<32 || c==127;})) return std::nullopt;
                    result.settings.**member=std::string(value);
                } else {
                    const auto number=Util::Text::parse_finite_real(value); if (!number) return std::nullopt;
                    result.settings.*std::get<double Settings::*>(d.member)=*number;
                }
                found=true; break;
            }
            if (!found) return std::nullopt;
        }
        at=end+1;
    }
    if (!seen.count("continuousCancellation")) return std::nullopt;
    for (const auto& d:setting_schema()) if (synchronized(d) && d.id!=SettingId::AttackMultiplier && !seen.count(key(d))) return std::nullopt;
    const auto mapWire=wire.substr(mapAt+5);
    if (!mapWire.empty()) {
        Runtime::MapDefinition map;
        if (!Runtime::decode_map(mapWire,map)) return std::nullopt;
        result.map=std::make_shared<const Runtime::MapDefinition>(std::move(map));
    }
    return result;
}
Runtime::RuntimeConfig apply_multiplayer_config(const Settings& local,const Runtime::RuntimeConfig& current,
    const MultiplayerConfig& host,bool forceRules) {
    auto effective=local;
    for (const auto& d:setting_schema()) if (synchronized(d) && !handling(d) && d.section!=Section::Autoplay && forceRules) {
        if (const auto member=std::get_if<int Settings::*>(&d.member)) effective.**member=host.settings.**member;
        else effective.*std::get<double Settings::*>(d.member)=host.settings.*std::get<double Settings::*>(d.member);
    }
    auto result=Runtime::normalize_user_config(effective,Runtime::selected_game_mode(effective));
    result.initialMap=forceRules?host.map:current.initialMap;
    if (result.initialMap) Runtime::apply_map_config(result,*result.initialMap);
    result.quickPlay.continuousCancellation=forceRules?host.continuousCancellation:current.quickPlay.continuousCancellation;
    // Network identity/transport are independent of the selected play mode.
    result.garbage.mode=Runtime::GarbageMode::Multiplayer;
    return result;
}
bool copy_multiplayer_config(Settings& local,const MultiplayerConfig& host) {
    auto candidate=local;
    for(const auto& d:setting_schema()) if(synchronized(d) && !handling(d) && d.section!=Section::Autoplay) {
        if(const auto member=std::get_if<int Settings::*>(&d.member)) candidate.**member=host.settings.**member;
        else candidate.*std::get<double Settings::*>(d.member)=host.settings.*std::get<double Settings::*>(d.member);
    }
    candidate.mapId=0;
    if(host.map) {
        Maps::Collection collection;if(!Maps::load(collection)) return false;
        auto map=*host.map;
        for(const auto& existing:collection.maps) {
            map.id=existing.id;
            if(map==existing) {candidate.mapId=existing.id;break;}
        }
        if(!candidate.mapId) {
            map.id=0;Maps::SaveUndo undo;if(!Maps::save(map,undo)) return false;
            candidate.mapId=map.id;
        }
    }
    local=std::move(candidate);
    return true;
}
Settings multiplayer_config_view_settings(const Settings& local,const MultiplayerConfig& host) {
    auto result=host.settings;
    for(const auto& d:setting_schema()) if(d.section==Section::Autoplay) {
        if(const auto member=std::get_if<int Settings::*>(&d.member))result.**member=local.**member;
        else if(const auto member=std::get_if<double Settings::*>(&d.member))result.**member=local.**member;
    }
    return result;
}
}
