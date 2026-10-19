// The finale (docs/FINAL_BATTLE.md): the first win of the First Ranker's
// contest opens story/progression.json clear_exit, the epilogue on the
// finale's stage (original scene 10), and that leads to the ending movie
// (scene 11) and the staff roll.

#include "desktop_game_screens.hpp"
#include "desktop_intro.hpp"
#include "desktop_screens.hpp"

#include <exception>

namespace phantomcrash {

std::unique_ptr<Screen> ScreenRunner::make_finale() {
  try {
    finale_tuning_ = parse_finale_tuning(roots_.document("story/finale.json"));
    const auto tables = converted_table(roots_, "tables/finale.json");
    if (!tables) {
      return make(finale_tuning_.next, name_);
    }
    finale_tables_ = parse_finale_tables(*tables);
  } catch (const std::exception &error) {
    SDL_Log("Finale: %s; playing the ending", error.what());
    return make(finale_tuning_.next, name_);
  }
  // The riding robot alone (0x045270 sets up contest 0, which has no team).
  battle_training_ = test_drive_training(training_, test_drive_, quest_.garage,
                                         quest_.garage.riding, game_->part_tables());
  battle_training_.stage = finale_tuning_.stage;
  battle_training_.battle.drones = 0;
  return make_training(battle_training_, test_drive_.lesson, {}, -1, false, {}, nullptr, true);
}

std::unique_ptr<Screen> ScreenRunner::make_ending() {
  const json::JsonValue document = roots_.document("story/ending.json");
  auto screen = std::make_unique<IntroScreen>(textures_, roots_, parse_intro(document), true, 0);
  // The movie's subtitles (0x01c4fa), when the converted table is there.
  if (const json::JsonValue *style = document.find("subtitles")) {
    try {
      MovieSubtitles subtitles = parse_movie_subtitles(*style);
      if (const auto table = converted_table(roots_, subtitles.table.c_str())) {
        add_subtitle_lines(subtitles, *table);
        screen->show_subtitles(font_, std::move(subtitles));
      }
    } catch (const std::exception &error) {
      SDL_Log("Ending subtitles: %s", error.what());
    }
  }
  return screen;
}

} // namespace phantomcrash
