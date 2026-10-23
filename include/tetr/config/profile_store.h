#ifndef TETR_CONFIG_PROFILE_STORE_H
#define TETR_CONFIG_PROFILE_STORE_H
namespace UserConfig {
struct Settings;
// A shared document can exist before Player 2 has initialized a profile.
bool has_saved_profile(const Settings& settings);
}
#endif
