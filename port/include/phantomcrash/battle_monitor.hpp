#pragma once

#include "phantomcrash/battle.hpp"
#include "phantomcrash/chase_camera.hpp"

#include <cstdint>
#include <random>

namespace phantomcrash {

// Port spectator mode for the arena's TV screens. Times are seconds,
// independent of presentation rate; settings live in world/stages.json.
struct BattleMonitorSettings {
  float switch_seconds{8.0F};
  float aspect{4.0F / 3.0F};
  ChaseCameraSettings camera;
};

[[nodiscard]] BattleMonitorSettings parse_battle_monitor(const json::JsonValue &value);

class BattleMonitor final {
public:
  BattleMonitor(const BattleMonitorSettings &settings, std::uint32_t seed);
  void tick(const Battle &battle, float seconds);
  [[nodiscard]] int subject() const noexcept {
    return subject_;
  }
  [[nodiscard]] const ChaseCamera &camera() const noexcept {
    return camera_;
  }
  [[nodiscard]] float aspect() const noexcept {
    return settings_.aspect;
  }

private:
  BattleMonitorSettings settings_;
  ChaseCamera camera_;
  std::mt19937 random_;
  int subject_{-1};
  float remaining_{};
};

} // namespace phantomcrash
