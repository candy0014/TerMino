#ifndef TETR_APP_START_SCREEN_NAVIGATION_H
#define TETR_APP_START_SCREEN_NAVIGATION_H

namespace Runtime { class GameContext; }

namespace App {
void install_start_screen_navigation(Runtime::GameContext& context);
void request_start_screen(Runtime::GameContext& context);
}

#endif
