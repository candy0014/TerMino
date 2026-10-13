#ifndef TETR_RUNTIME_SPIN_BONUSES_H
#define TETR_RUNTIME_SPIN_BONUSES_H

#include <array>
#include <string_view>

namespace Runtime {
enum class SpinBonuses : int {
    TSpins = 0, TSpinsPlus, AllSpin, AllSpinPlus, AllMini, AllMiniPlus,
    MiniOnly, Handheld, Stupid, None
};
struct SpinBonusesInfo {
    SpinBonuses value;
    std::string_view label;
    std::string_view tetrioName;
    std::string_view hint;
};
inline constexpr std::array<SpinBonusesInfo,10> kSpinBonuses = {{
    {SpinBonuses::TSpins,"T-SPINS","T-spins","Receive bonuses for spinning T-pieces."},
    {SpinBonuses::TSpinsPlus,"T-SPINS+","T-spins+","Receive bonuses for spinning T-pieces (immobile allowed)."},
    {SpinBonuses::AllSpin,"ALL-SPIN","all","Receive bonuses for spinning all pieces."},
    {SpinBonuses::AllSpinPlus,"ALL-SPIN+","all+","Receive bonuses for spinning all pieces (immobile allowed)."},
    {SpinBonuses::AllMini,"ALL-MINI","all-mini","Receive bonuses for spinning T-pieces and receive Back-to-Back for spinning all other pieces."},
    {SpinBonuses::AllMiniPlus,"ALL-MINI+","all-mini+","Receive bonuses for spinning T-pieces (immobile allowed) and receive Back-to-Back for spinning all other pieces."},
    {SpinBonuses::MiniOnly,"MINI-ONLY","mini-only","Receive Back-to-Back for spinning all pieces."},
    {SpinBonuses::Handheld,"HANDHELD","handheld","Receive bonuses for spinning all pieces but attacks are halved except for T-Spins."},
    {SpinBonuses::Stupid,"STUPID","stupid","Everything is a spin because YEAH WHY NOT (O-spin SUPPORTED!)."},
    {SpinBonuses::None,"NONE","none","Receive no spin bonuses."}
}};
constexpr SpinBonuses spin_bonuses_from_index(int value) noexcept {
    return value>=0 && value<static_cast<int>(kSpinBonuses.size())
        ? static_cast<SpinBonuses>(value) : SpinBonuses::AllMiniPlus;
}
constexpr const SpinBonusesInfo& spin_bonuses_info(SpinBonuses value) noexcept {
    return kSpinBonuses[static_cast<int>(spin_bonuses_from_index(static_cast<int>(value)))];
}
} // namespace Runtime
#endif
