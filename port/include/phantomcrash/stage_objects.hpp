#pragma once

#include "phantomcrash/stage_collision.hpp"
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// A separately placed city prop. Mesh indices address the stage's effect model.
struct StageObject {
  int kind{-1}; // its type (object +0x40), which picks its handler
  int mesh{-1};
  int wreck_mesh{-1};
  int break_effect{-1};
  int wreck_break_effect{-1};
  std::string break_sound;
  bool wreck_solid{true};
  float wreck_hit_delay{};
  Vec3 position;
  Mat4 world;
  StageCollision collision;
  std::optional<StageCollision> wreck_collision;
  Vec3 centre;
  float radius{};
  bool destroyed{};
  bool removed{};
  float cooldown{};

  [[nodiscard]] int visible_mesh() const {
    return removed ? -1 : destroyed ? wreck_mesh : mesh;
  }
  [[nodiscard]] bool solid() const {
    return !removed && (!destroyed || wreck_solid);
  }
};

struct ObjectBreak {
  int effect{-1};
  Vec3 at;
  int kind{-1};
};

class StageObjects {
  friend class StateInspector;
public:
  explicit StageObjects(std::vector<StageObject> objects = {}) : objects_(std::move(objects)) {}
  void tick(float seconds);
  unsigned push_sphere(Vec3 &centre, float radius) const;
  [[nodiscard]] float ground_height(Vec3 at, float step, float radius = 0.0F) const;
  // A shot triggers the original type handler once. Type 1 wrecks can break again
  // after the burning interval; type 0 wrecks remain permanently.
  [[nodiscard]] std::optional<ObjectBreak> hit(std::size_t index);
  [[nodiscard]] const std::vector<StageObject> &objects() const {
    return objects_;
  }
  [[nodiscard]] std::vector<std::pair<std::string, Vec3>> take_sounds() {
    return std::exchange(sounds_, {});
  }

private:
  std::vector<StageObject> objects_;
  std::vector<std::pair<std::string, Vec3>> sounds_;
};

[[nodiscard]] StageObjects parse_stage_objects(const json::JsonValue &document, int stage);

} // namespace phantomcrash
