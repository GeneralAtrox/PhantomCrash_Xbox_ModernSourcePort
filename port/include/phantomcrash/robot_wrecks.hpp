#pragma once

#include "phantomcrash/robot.hpp"
#include "phantomcrash/stage_collision.hpp"
#include <functional>
#include <memory>
#include <ranges>

namespace phantomcrash {

struct RobotWreckSettings {
  float burn_seconds{6.45F};
  int reward_ny{10000};
  int break_effect{15};
  std::string break_sound{"sound/sfx/battle/16.wav"};
};

[[nodiscard]] RobotWreckSettings parse_robot_wreck_settings(const json::JsonValue &value);

// Independent of the living roster: replacing a pilot never removes their wreck.
struct RobotWreck {
  RobotAssembly assembly;
  Pose pose;
  std::array<Mat4, 2> shoulders;
  std::array<bool, 4> lost{};
  Mat4 root;
  Vec3 centre;
  float radius{};
  float burn_left{};
  bool removed{};
  std::optional<StageCollision> collision;
  bool collision_prepared{};

  [[nodiscard]] bool solid() const {
    return !removed && burn_left <= 0 && collision.has_value();
  }
};

class RobotWrecks final {
  friend class StateInspector;

public:
  using CollisionBuilder = std::function<StageCollision(const RobotWreck &)>;
  // Online assets outlive the battle. The builder only reads those assets and
  // the captured wreck pose; it must not depend on a view or presentation clock.
  void set_collision_builder(CollisionBuilder builder) {
    collision_builder_ = std::move(builder);
  }
  [[nodiscard]] bool simulation_collision() const {
    return static_cast<bool>(collision_builder_);
  }
  // Before the next gameplay tick, matching co-op's previous view-update phase.
  void prepare_collision();
  void add(RobotWreck wreck);
  void tick(float seconds);
  // Installed from the same posed geometry used to draw this wreck.
  void set_collision(std::size_t index, StageCollision collision);
  [[nodiscard]] bool destroy(std::size_t index);
  // Each wreck in order, as const RobotWreck &.
  [[nodiscard]] auto items() const {
    return items_ | std::views::transform(
                        [](const std::shared_ptr<RobotWreck> &wreck) -> const RobotWreck & {
                          return *wreck;
                        });
  }
  [[nodiscard]] float ground_height(Vec3 at, float step, float radius) const;
  unsigned push_sphere(Vec3 &at, float radius) const;
  [[nodiscard]] std::optional<Vec3> segment_hit(Vec3 from, Vec3 to) const;

private:
  CollisionBuilder collision_builder_;
  // A wreck about to change, copied first if a checkpoint still shares it.
  RobotWreck &edit(std::size_t index);

  // Tombstones retain target indices held by in-flight projectiles. Battle
  // checkpoints (online rollback copies several a tick) share the wrecks.
  std::vector<std::shared_ptr<RobotWreck>> items_;
};
} // namespace phantomcrash
