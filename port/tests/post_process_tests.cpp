#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/post_process.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/training_ground.hpp"

#include <cmath>

using namespace phantomcrash;

TEST_CASE("the original's screen effects default on and each selection survives settings") {
  CHECK(settings_from_json(json::parse("{}")).post_process == PostProcessOptions{});
  for (int mask = 0; mask < 8; ++mask) {
    Settings settings;
    settings.post_process = {(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
    settings.quest.player_name = "Mr. USA";
    settings.quest.day = 7;
    settings.quest.money = 19127;
    settings.save_slots[1] = settings.quest;
    const auto loaded = settings_from_json(json::parse(json::write(settings_to_json(settings))));
    CHECK(loaded == settings);
  }
  const auto partial = settings_from_json(json::parse(R"({"post_process":{"depth_blur":false}})"));
  CHECK((partial.post_process == PostProcessOptions{false, true, true}));
}

TEST_CASE("volumetric light rides on dynamic lighting, on unless switched off") {
  CHECK(PostProcessOptions{}.volumetric_light);
  CHECK(settings_from_json(json::parse("{}")).post_process.volumetric_light);
  Settings settings;
  settings.post_process.dynamic_lighting = true;
  settings.post_process.volumetric_light = false;
  const auto loaded = settings_from_json(json::parse(json::write(settings_to_json(settings))));
  CHECK(loaded.post_process.dynamic_lighting);
  CHECK(!loaded.post_process.volumetric_light);
}

TEST_CASE("blur tint and stage distortion can be enabled independently") {
  ScreenPass stage;
  stage.depth_of_field = stage.distortion = true;
  stage.tint = {0.8F, 0.7F, 0.6F};
  CHECK(!selected_screen(stage, {}).active());
  const auto wave = selected_screen(stage, {false, true, false});
  CHECK(wave.distortion && !wave.depth_of_field && wave.tint.x == 1.0F);
  const auto blur = selected_screen(stage, {true, false, false});
  CHECK(blur.depth_of_field && !blur.distortion && blur.tint.y == 1.0F);
  const auto tint = selected_screen(stage, {false, false, true});
  CHECK(!tint.depth_of_field && !tint.distortion && tint.tint.x == 0.8F);
  CHECK(!selected_screen(ScreenPass{}, {true, true, true}).active());
}

TEST_CASE("original softness is opt-in and mixes by its strength") {
  PostProcessOptions options;
  CHECK(!selected_screen(ScreenPass{}, options).active());
  options.original_softness = true;
  const auto soft = selected_screen(ScreenPass{}, options);
  CHECK(soft.active());
  CHECK(soft.softness == 1.0F);
  options.softness_strength = 2.0F;
  CHECK(selected_screen(ScreenPass{}, options).softness == 1.0F);
  options.softness_strength = 0.0F;
  CHECK(!selected_screen(ScreenPass{}, options).active());
}

TEST_CASE("battle focus follows the stored day through the ten day cycle") {
  constexpr float focus[] = {80, 78, 76, 74, 72, 70, 72, 74, 76, 78};
  for (int day = 0; day < 30; ++day) {
    const auto screen = screen_for_day({}, day);
    CHECK(std::abs(screen.focus - focus[day % 10]) < 1e-5F);
  }
  CHECK(screen_for_day({}, 5).day_strength == 1.0F);
  CHECK(screen_for_day({}, 10).day_strength == 0.0F);
}

TEST_CASE("blur targets keep the original sample grids as output resolution increases") {
  const auto original = screen_blur_sizes(640, 480);
  CHECK((original[0] == std::array<int, 2>{320, 240}));
  CHECK((original[1] == std::array<int, 2>{160, 120}));
  CHECK(screen_blur_sizes(1600, 1200) == original);
  const auto wide = screen_blur_sizes(1920, 1080);
  CHECK((wide[0] == std::array<int, 2>{427, 240}));
  CHECK((wide[1] == std::array<int, 2>{213, 120}));
  CHECK(screen_blur_sizes(3840, 2160) == wide);
}

TEST_CASE("offset map advances on source ticks and repeats without presentation dependence") {
  ScreenPass screen;
  CHECK((screen_wave_rows(screen) == std::array<std::int8_t, 64>{}));
  screen.seconds = 1.0 / 30;
  const auto first = screen_wave_rows(screen);
  CHECK(first[0] == 25);
  CHECK(first[2] == 48);
  CHECK(first[6] == 0);
  screen.seconds = 1.0 / 30 + 1.0 / 144;
  CHECK(screen_wave_rows(screen) == first);
  screen.seconds = 2.0 / 30;
  CHECK(screen_wave_rows(screen) != first);
  screen.seconds = 33.0 / 30;
  CHECK(screen_wave_rows(screen) == first);
}

TEST_CASE("only daytime Shinjuku selects the shipped distortion") {
  const auto stages =
      parse_stages(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/stages.json"));
  for (const auto &[name, stage] : stages) {
    CHECK(stage.lighting.screen.distortion == (name == "shinjuku_day"));
    CHECK(stage.lighting.screen.focus == 80.0F);
  }
}
