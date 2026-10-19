#include "phantomcrash/stage_fixes.hpp"

#include <exception>
#include <iostream>

namespace phantomcrash {

StageCollision with_collision_fixes(const StageCollision &base, const json::JsonValue &fixes) {
  const StageCollision extra = parse_stage_collision(fixes);
  std::vector<CollisionTriangle> walls = base.walls();
  std::vector<CollisionTriangle> floor = base.floor();
  walls.insert(walls.end(), extra.walls().begin(), extra.walls().end());
  floor.insert(floor.end(), extra.floor().begin(), extra.floor().end());
  return {std::move(walls), std::move(floor)};
}

StageCollision fixed_stage_collision(const DataRoots &roots, const StageDefinition &stage,
                                     const json::JsonValue &collision) {
  StageCollision base = parse_stage_collision(collision);
  if (stage.collision_fixes.empty()) {
    return base;
  }
  try {
    return with_collision_fixes(base, roots.document(stage.collision_fixes));
  } catch (const std::exception &error) {
    std::clog << stage.collision_fixes << ": " << error.what() << '\n';
  }
  return base;
}

} // namespace phantomcrash
