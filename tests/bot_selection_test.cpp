#include "tetr/compat/legacy_encoding.h"
#include "tetr/config/user_config.h"
#include "tetr/runtime/game_modes.h"
#include "tetr/app/settings_model.h"

#include <array>
#include <cassert>
#include <string>
#include <vector>

int main() {
    using Runtime::AutoplayMode;

    static_assert(static_cast<int>(AutoplayMode::Disabled) == 0);
    static_assert(static_cast<int>(AutoplayMode::While1) == 1);
    static_assert(static_cast<int>(AutoplayMode::Candy0014) == 2);
    static_assert(static_cast<int>(AutoplayMode::Mech) == 3);
    static_assert(static_cast<int>(AutoplayMode::ZzztojBotris) == 4);
    static_assert(static_cast<int>(AutoplayMode::ZzztojToj) == 5);
    static_assert(static_cast<int>(AutoplayMode::ZzztojDig) == 6);
    static_assert(static_cast<int>(AutoplayMode::ZzztojQq) == 7);
    static_assert(static_cast<int>(AutoplayMode::ZzztojC2) == 8);
    static_assert(static_cast<int>(AutoplayMode::ChatGPT) == 9);
    static_assert(static_cast<int>(AutoplayMode::ColdClear) == 10);

    const std::array<std::string, 11> expectedNames = {
        "Disabled", "bot_while1", "bot_candy0014", "bot_mech",
        "zzztoj_botris", "zzztoj_toj", "zzztoj_dig", "zzztoj_qq", "zzztoj_c2", "bot_chatgpt", "bot_coldclear"
    };
    for (std::size_t i = 0; i < Runtime::kAutoplayModes.size(); ++i) {
        assert(static_cast<int>(Runtime::kAutoplayModes[i].mode) == static_cast<int>(i));
        assert(Runtime::kAutoplayModes[i].name == expectedNames[i]);
        assert(Runtime::autoplay_mode_from_index(static_cast<int>(i)) == Runtime::kAutoplayModes[i].mode);
    }

    // Replay compatibility keeps the historical IDs of the surviving BOT
    // implementations even though Runtime's internal selection IDs are compact and contiguous.
    assert(Compat::to_legacy(AutoplayMode::Disabled) == 0);
    assert(Compat::to_legacy(AutoplayMode::While1) == -1);
    assert(Compat::to_legacy(AutoplayMode::Candy0014) == 2);
    assert(Compat::to_legacy(AutoplayMode::Mech) == 4);
    assert(Compat::to_legacy(AutoplayMode::ZzztojBotris) == 7);
    assert(Compat::to_legacy(AutoplayMode::ZzztojToj) == 8);
    assert(Compat::to_legacy(AutoplayMode::ZzztojDig) == 9);
    assert(Compat::to_legacy(AutoplayMode::ZzztojQq) == 10);
    assert(Compat::to_legacy(AutoplayMode::ZzztojC2) == 11);
    assert(Compat::to_legacy(AutoplayMode::ChatGPT) == 12);
    assert(Compat::to_legacy(AutoplayMode::ColdClear) == 13);
    assert(Compat::autoplay_mode_from_legacy(-1) == AutoplayMode::While1);
    assert(Compat::autoplay_mode_from_legacy(4) == AutoplayMode::Mech);
    assert(Compat::autoplay_mode_from_legacy(5) == AutoplayMode::Disabled);
    assert(Compat::autoplay_mode_from_legacy(6) == AutoplayMode::Disabled);
    assert(Compat::autoplay_mode_from_legacy(7) == AutoplayMode::ZzztojBotris);
    assert(Compat::autoplay_mode_from_legacy(8) == AutoplayMode::ZzztojToj);
    assert(Compat::autoplay_mode_from_legacy(9) == AutoplayMode::ZzztojDig);
    assert(Compat::autoplay_mode_from_legacy(10) == AutoplayMode::ZzztojQq);
    assert(Compat::autoplay_mode_from_legacy(11) == AutoplayMode::ZzztojC2);
    assert(Compat::autoplay_mode_from_legacy(12) == AutoplayMode::ChatGPT);
    assert(Compat::autoplay_mode_from_legacy(13) == AutoplayMode::ColdClear);

    UserConfig::Settings user;
    user.garbageMode = 1; // Backfire uses incoming delay.
    const auto tree = App::Settings::Model::make_tree(user, false, 0);
    const App::Settings::Model::SettingNode* autoplay = nullptr;
    const App::Settings::Model::SettingNode* garbage = nullptr;
    for (const auto& node : tree.children) {
        if (node.key == "AUTOPLAY") autoplay = &node;
        if (node.key == "GAME") for (const auto& child:node.children)
            if (child.key=="garbage-rules") garbage=&child;
    }
    assert(garbage != nullptr && autoplay != nullptr);
    bool foundGarbageInTime = false;
    for (const auto& node : garbage->children) {
        if (node.item && node.item->id == App::Settings::Model::SettingId::GarbageInTime) {
            assert(&std::get<std::reference_wrapper<double>>(node.item->value).get()==&user.garbageInTime);
            assert(std::get<std::reference_wrapper<double>>(node.item->value).get() == 0.3);
            foundGarbageInTime = true;
        }
    }
    assert(foundGarbageInTime);
    const auto& bot = *autoplay->children.front().item;
    assert(bot.id == App::Settings::Model::SettingId::AutoplayMode);
    assert(bot.label == "Bot");
    assert(bot.choice.labels == std::vector<std::string>(expectedNames.begin(), expectedNames.end()));
    assert(bot.choice.values == std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

    return 0;
}
