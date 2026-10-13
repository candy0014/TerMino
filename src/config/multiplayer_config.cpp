#include "tetr/config/multiplayer_config.h"
#include "tetr/config/settings_schema.h"
#include "tetr/config/normalization.h"
#include "tetr/util/text_codec.h"
#include <cmath>
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
std::string key(const SettingDescriptor& d) {return std::string(section_name(d.section))+"/"+std::string(d.diskName);}
}
std::string encode_multiplayer_config(const Settings& settings,const Runtime::RuntimeConfig& config) {
    std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(17)<<"TETR_ROOM_CONFIG 1\n";
    for (const auto& d:setting_schema()) if (synchronized(d)) {
        out<<key(d)<<'=';
        if (const auto member=std::get_if<int Settings::*>(&d.member)) out<<settings.**member;
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
            for (const auto& d:setting_schema()) if (synchronized(d) && key(d)==name) {
                if (const auto member=std::get_if<int Settings::*>(&d.member)) {
                    const auto number=Util::Text::parse_integer<int>(value); if (!number) return std::nullopt;
                    result.settings.**member=*number;
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
    for (const auto& d:setting_schema()) if (synchronized(d) && !seen.count(key(d))) return std::nullopt;
    const auto mapWire=wire.substr(mapAt+5);
    if (!mapWire.empty()) {
        Runtime::MapDefinition map;
        if (!Runtime::decode_map(mapWire,map)) return std::nullopt;
        result.map=std::make_shared<const Runtime::MapDefinition>(std::move(map));
    }
    return result;
}
Runtime::RuntimeConfig apply_multiplayer_config(const Settings& local,const Runtime::RuntimeConfig& current,
    const MultiplayerConfig& host,bool forceRules,bool forceHandling) {
    auto effective=local;
    for (const auto& d:setting_schema()) if (synchronized(d) && (handling(d)?forceHandling:forceRules)) {
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
}
