#include "phantomcrash/battle_monitor.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

BattleMonitorSettings parse_battle_monitor(const json::JsonValue &value) {
  BattleMonitorSettings settings;
  settings.switch_seconds = static_cast<float>(value.number_or("switch_seconds", 8.0));
  settings.aspect = static_cast<float>(value.number_or("aspect", 4.0 / 3.0));
  if (!std::isfinite(settings.switch_seconds) || settings.switch_seconds <= 0.0F ||
      !std::isfinite(settings.aspect) || settings.aspect <= 0.0F) {
    throw std::runtime_error("Battle monitor interval and aspect must be positive");
  }
  if (const auto *camera = value.find("camera")) {
    settings.camera = parse_chase_camera(*camera);
  }
  return settings;
}

BattleMonitor::BattleMonitor(const BattleMonitorSettings &settings, const std::uint32_t seed)
    : settings_(settings), camera_(settings.camera), random_(seed) {}

void BattleMonitor::tick(const Battle &battle, const float seconds) {
  const auto &robots = battle.robots();
  std::vector<int> eligible;
  for (std::size_t i = 0; i < robots.size(); ++i) {
    const Combatant &robot = robots[i];
    if (robot.alive && robot.hp > 0.0F && robot.mover.mode() != MoveMode::scripted &&
        battle.stage().in_play_area(robot.mover.position())) {
      eligible.push_back(static_cast<int>(i));
    }
  }
  const bool valid = std::ranges::find(eligible, subject_) != eligible.end();
  remaining_ -= std::max(seconds, 0.0F);
  bool snap = false;
  if (!valid || remaining_ <= 0.0F) {
    if (eligible.size() > 1) {
      std::erase(eligible, subject_); // never cut straight back to the same robot
    }
    subject_ =
        eligible.empty()
            ? -1
            : eligible[std::uniform_int_distribution<std::size_t>(0, eligible.size() - 1)(random_)];
    remaining_ = settings_.switch_seconds;
    snap = true;
  }
  if (subject_ < 0) {
    return;
  }
  const Combatant &robot = robots[static_cast<std::size_t>(subject_)];
  CameraSubject subject;
  subject.position = robot.mover.position();
  subject.yaw = robot.mover.aim_heading();
  subject.height = robot.radius;
  subject.torso = subject.position + Vec3{0.0F, robot.radius * 2.0F, 0.0F};
  subject.grounded = robot.mover.grounded();
  camera_.tick(subject, battle.stage(), seconds, snap);
  if (const auto *objects = battle.objects()) {
    for (const auto &object : objects->objects()) {
      if (!object.solid()) {
        continue;
      }
      if (const auto hit = object.collision.segment_hit(camera_.anchor(), camera_.eye())) {
        camera_.block_eye(camera_.anchor(), *hit);
      }
    }
  }
  if (const auto hit = battle.wrecks().segment_hit(camera_.anchor(), camera_.eye())) {
    camera_.block_eye(camera_.anchor(), *hit);
  }
  if (const auto *doors = battle.doors()) {
    if (const auto hit = doors->segment_hit(camera_.anchor(), camera_.eye())) {
      camera_.block_eye(camera_.anchor(), *hit);
    }
  }
}

} // namespace phantomcrash
