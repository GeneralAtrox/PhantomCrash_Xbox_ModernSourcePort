#include "desktop_online_factory.hpp"
#include "phantomcrash/added_chips.hpp"
#include "phantomcrash/stage_fixes.hpp"
#include <stdexcept>

namespace phantomcrash {
namespace {
json::JsonValue converted(const DataRoots &roots, const std::string &name) {
  auto table = read_converted_table(roots, name);
  if (!table) {
    throw std::runtime_error("Online battle requires converted asset: " + name);
  }
  return std::move(*table);
}
} // namespace

OnlineBattleResources::OnlineBattleResources(SDL_Renderer *renderer, DataRoots data)
    : roots(std::move(data)), textures(renderer, roots), world(renderer, roots), robots(roots),
      parts(parse_part_tables(converted(roots, "tables/parts.json"))),
      garage(parse_garage_tables(converted(roots, "tables/garage.json"))),
      contests(parse_contest_tables(converted(roots, "tables/town.json"))),
      stats(load_part_stats(roots)),
      rules(parse_rumbling_rules(roots.document("world/rumbling.json"))),
      training(parse_training(roots.document("world/training.json"))),
      drive(parse_test_drive(roots.document("world/test_drive.json"))),
      rumbling(parse_rumbling_battle(roots.document("world/rumbling.json"))),
      live(parse_rumbling_live(roots.document("world/rumbling.json"))),
      stages(parse_stages(roots.document("world/stages.json"))),
      hud(parse_battle_hud_style(roots.document("ui/battle_hud.json"))),
      font(parse_glyph_font(converted(roots, "tables/font.json"))),
      talk(parse_talk_tables(converted(roots, "tables/talk.json"))),
      talk_tuning(parse_event_tuning(roots.document("story/talk.json"))), assets(robots, world) {
  if (!world.available() || !robots.available()) {
    throw std::runtime_error("Online battle requires Direct3D and converted robot assets");
  }
  apply_talk_edges(roots.document("story/talk.json"), talk);
  live.ai.original = parse_original_ai(roots.document("world/robot_ai.json"));
  const auto objects = converted(roots, "tables/stage_objects.json");
  for (int area = 0; area < roster_areas; ++area) {
    const auto &stage = stages.at(live.stages[area][0]);
    auto &gameplay = gameplay_stages[area];
    if (!stage.enemy_path.empty()) {
      gameplay.path = parse_enemy_path(converted(roots, stage.enemy_path));
    }
    if (!stage.collision.empty()) {
      gameplay.collision = fixed_stage_collision(roots, stage, converted(roots, stage.collision));
    }
    gameplay.objects = parse_stage_objects(objects, stage.scene_stage);
  }
}
} // namespace phantomcrash
