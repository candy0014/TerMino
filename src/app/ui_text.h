#ifndef TETR_APP_UI_TEXT_H
#define TETR_APP_UI_TEXT_H
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "tetr/input/action.h"

namespace UserConfig { struct Settings; }

namespace App::UiText {
enum class Language { Chinese, English };
void set_language(Language language);
Language language();
void initialize_language(int savedLanguage = 0); // Saved preference; optional environment override.
std::string tr(std::string_view english);
std::string key_label(int keyCode);
std::string key_label(int keyCode, Input::KeyPlatform platform);
std::string binding_label(const UserConfig::Settings& settings, std::size_t control);
std::string breadcrumb(std::string_view path);
std::vector<std::string> breadcrumb_rows(std::string_view path, std::size_t columns);
std::string connection_status(std::string_view status);
}
#endif
