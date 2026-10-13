#ifndef TETR_RUNTIME_ROTATION_SYSTEMS_H
#define TETR_RUNTIME_ROTATION_SYSTEMS_H

#include "tetr/runtime/game_modes.h"

namespace Runtime {
struct RotationSystemInfo {
    RotationSystem value;
    std::string_view label;
    std::string_view tetrioName;
    std::string_view hint;
};
inline constexpr std::array<RotationSystemInfo,8> kRotationSystems = {{
    {RotationSystem::SrsPlus,"SRS+","SRS+","The default natural rotation system with symmetric I-piece rotation."},
    {RotationSystem::Srs,"SRS","SRS","The standard natural rotation system."},
    {RotationSystem::SrsX,"SRS-X","SRS-X","SRS with more powerful 180 spins."},
    {RotationSystem::TetraX,"TETRA-X","TETRA-X","Novel rotation system by Dr Ocelot."},
    {RotationSystem::Nrs,"NRS","NRS","The classic rotation system."},
    {RotationSystem::Ars,"ARS","ARS","Rotation system used in arcade games."},
    {RotationSystem::Asc,"ASC","ASC","Permissive rotation system by Winternebs."},
    {RotationSystem::None,"NONE","none","No kicks possible."}
}};
constexpr const RotationSystemInfo* rotation_system_info(RotationSystem system) noexcept {
    for (const auto& info:kRotationSystems) if(info.value==system) return &info;
    return nullptr;
}
} // namespace Runtime
#endif
