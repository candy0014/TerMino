#include "tetr/config/settings_schema.h"

#include <cassert>
#include <cmath>
#include <set>
#include <string>
#include <utility>

int main() {
    const auto& schema = UserConfig::setting_schema();
    assert(!schema.empty());

    std::set<UserConfig::SettingId> ids;
    std::set<std::pair<int, std::string>> diskKeys;
    std::size_t controls = 0;
    for (const auto& descriptor : schema) {
        assert(ids.insert(descriptor.id).second);
        if (!descriptor.diskName.empty()) {
            assert(diskKeys.insert({static_cast<int>(descriptor.section), std::string(descriptor.diskName)}).second);
        }
        if (descriptor.keyBinding) {
            ++controls;
            assert(descriptor.section == UserConfig::Section::Controls);
            assert(descriptor.controlIndex < UserConfig::Settings::kControlCount);
        }
    }
    assert(controls == UserConfig::Settings::kControlCount);

    UserConfig::Settings settings;
    assert(settings.continueInputUnfocused==0);
    const auto& background=UserConfig::setting_descriptor(UserConfig::SettingId::ContinueInputUnfocused);
    assert(background.section==UserConfig::Section::Others && background.choice==UserConfig::ChoiceKind::Boolean);
    assert(UserConfig::assign_setting_text(settings,background,"1"));
    UserConfig::reset_settings(settings);
    assert(settings.continueInputUnfocused==1); // Root reset preserves appearance.
    UserConfig::reset_section(settings,UserConfig::Section::Others);
    assert(settings.continueInputUnfocused==0);
    assert(UserConfig::clamped_int(settings, UserConfig::SettingId::FieldWidth) == 10);
    settings.fieldWidth = 1000;
    assert(UserConfig::clamped_int(settings, UserConfig::SettingId::FieldWidth) == 100);
    settings.qpDefaultFloor = 0;
    assert(UserConfig::clamped_int(settings, UserConfig::SettingId::QpDefaultFloor) == 1);


    UserConfig::Settings parsed;
    assert(UserConfig::assign_setting_text(parsed,UserConfig::SettingId::HitMultiplier,"0.123456789"));
    assert(UserConfig::assign_setting_text(parsed,UserConfig::SettingId::CancellationMultiplier,"1.75"));
    assert(parsed.hitMultiplier==.123456789 && parsed.cancelMultiplier==1.75);
    assert(!UserConfig::assign_setting_text(parsed,UserConfig::SettingId::HitMultiplier,"nan"));
    assert(!UserConfig::assign_setting_text(parsed,UserConfig::SettingId::CancellationMultiplier,"inf"));
    assert(UserConfig::assign_setting_text(parsed, UserConfig::SettingId::FieldWidth, "abc-0012x"));
    assert(parsed.fieldWidth == -12);
    assert(UserConfig::assign_setting_text(parsed, UserConfig::SettingId::GravityInterval, "--.1.2"));
    assert(std::fabs(parsed.gravityInterval + 0.12) < 1e-12);
    assert(UserConfig::assign_setting_text(parsed, UserConfig::SettingId::ReplayPath, "replay/demo.txt"));
    assert(parsed.replayPath == "replay/demo.txt");

    const auto allSpin = UserConfig::choice_options(UserConfig::ChoiceKind::AllSpin);
    // Extended all-spin intentionally remains manual-config only.
    assert(allSpin.labels.size() == 3);
    assert(allSpin.values.size() == 3);
    const auto styles = UserConfig::choice_options(UserConfig::ChoiceKind::SquareStyle);
    assert(styles.labels == std::vector<std::string>({"Filled", "Spaced"}));
    assert(styles.values == std::vector<int>({0, 1}));

    const auto autoplay = UserConfig::choice_options(UserConfig::ChoiceKind::Autoplay);
    assert(autoplay.labels.size() == 11);
    assert(autoplay.values == std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

    const auto gameModes = UserConfig::choice_options(UserConfig::ChoiceKind::GameMode);
    assert(gameModes.labels.size() == 7);
    assert(gameModes.labels[gameModes.labels.size() - 2] == "Local Versus");
    assert(gameModes.values[gameModes.values.size() - 2] == 6);
    assert(gameModes.labels.back() == "Replay");
    assert(gameModes.values.back() == 5);

    const auto* quickInvisible = UserConfig::find_setting(UserConfig::Section::QuickPlay, "Invisible");
    const auto* gameInvisible = UserConfig::find_setting(UserConfig::Section::Game, "Invisible");
    assert(quickInvisible && quickInvisible->id == UserConfig::SettingId::QpInvisible);
    assert(gameInvisible && gameInvisible->id == UserConfig::SettingId::FieldInvisible);

    const auto* garbageInTime = UserConfig::find_setting(UserConfig::Section::Garbage, "GarbageInTime");
    assert(garbageInTime && garbageInTime->id == UserConfig::SettingId::GarbageInTime);
    assert(UserConfig::clamped_real(parsed, UserConfig::SettingId::GarbageInTime) == 0.3);
    assert(UserConfig::assign_setting_text(parsed, UserConfig::SettingId::GarbageInTime, "2.5"));
    assert(UserConfig::clamped_real(parsed, UserConfig::SettingId::GarbageInTime) == 2.5);
    parsed.garbageInTime = -2.0;
    assert(UserConfig::clamped_real(parsed, UserConfig::SettingId::GarbageInTime) == 0.0);

    return 0;
}
