#pragma once

#include "desktop_screens.hpp"
#include "phantomcrash/settings.hpp"

#include <SDL3/SDL.h>

#include <filesystem>

namespace phantomcrash {

// The copy icon and F9 (docs/SCENE_DUMP.md): writes the frame drawn so far, a settings file
// holding the running game's save, and a JSON description an agent can act
// on, next to `preferences` in scene_dumps/; the JSON also goes to the
// clipboard. `live` is the settings as the menu has them now. Failures are
// logged, never thrown: a dump never stops the game. Nothing is shown;
// true once copied.
bool dump_scene(SDL_Renderer *renderer, const ScreenRunner &screens, Settings live,
                const std::filesystem::path &preferences);

// Saves what has been drawn so far this frame (before it is presented); throws on failure.
void save_frame(SDL_Renderer *renderer, const std::filesystem::path &path);

} // namespace phantomcrash
