#include "phantomcrash/robot_ai.hpp"
#include <algorithm>

namespace phantomcrash {

void RobotAiState::reset() {
  const int saved_ability = ability, saved_profile = profile, saved_entrant = entrant;
  *this = {};
  ability = saved_ability;
  profile = saved_profile;
  entrant = saved_entrant;
}

float ai_random(std::minstd_rand &random) {
  return std::uniform_real_distribution<float>(0.0F, 1.0F)(random);
}

bool ai_chance(std::minstd_rand &random, const float threshold, const float scale) {
  // 0x05be18: compare a truncated random draw, inclusively, with the threshold.
  return threshold >= scale ||
         (threshold > 0 && static_cast<int>(ai_random(random) * scale) <= threshold);
}

void ai_enter(RobotAiState &s, const AiMode mode, const OriginalAiData &data,
              std::minstd_rand &random) {
  const auto &level = data.levels[static_cast<std::size_t>(std::clamp(s.ability, 0, 99))];
  s.mode = mode;
  s.entered = true;
  s.state_left = level.durations[static_cast<std::size_t>(mode)];
  s.guard = data.value("transition_guard");
  s.action_left = data.value("movement_interval");
  s.submode = static_cast<int>(s.ticks % 5);
  // The original's fourth engagement subchoice requires a melee weapon;
  // the battle adapter makes that decision after inspecting the loadout.
  s.search = -1;
  // 0x05bdce releases every trigger; requests, holds and delays carry on.
  s.fire.fill(false);
  s.control = {};
  s.route_rejoin = true;
  s.dodge_side = (random() & 1U) != 0 ? 1 : -1;
  if (mode == AiMode::idle) {
    s.state_left = s.guard = data.value("idle_seconds");
  }
  ++s.transitions;
}

void ai_decide(RobotAiState &s, const OriginalAiData &data, const float hp, const float max_hp,
               const bool armed, std::minstd_rand &random) {
  if (s.guard > 0) {
    return;
  }
  // 0x05c680: later situations intentionally replace earlier ones.
  int situation = s.state_left <= 0 ? 0 : -1;
  if (s.target >= 0) {
    situation = 1;
  }
  if (hp != s.previous_hp) {
    situation = 2;
  }
  if (!armed) {
    situation = 4;
  }
  if (situation < 0) {
    return;
  }
  const int band = std::clamp(static_cast<int>(hp * 5 / std::max(max_hp, 1.0F)), 0, 4);
  const auto &profile = data.profiles.at(static_cast<std::size_t>(s.profile));
  const auto &row = data.decisions.at(static_cast<std::size_t>(profile[situation * 5 + band]));
  auto next = s.mode;
  for (std::size_t i = 0; i < row.size(); ++i) {
    if (ai_chance(random, static_cast<float>(row[i]), data.value("probability_scale"))) {
      next = static_cast<AiMode>(data.state_order[i]);
      break;
    }
  }
  if (next == AiMode::idle &&
      ai_chance(random, data.value("idle_route_chance"), data.value("probability_scale"))) {
    next = AiMode::route;
  }
  ai_enter(s, next, data, random);
}

} // namespace phantomcrash
