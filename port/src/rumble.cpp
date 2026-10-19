// Controller vibration: the damage pattern and the effect player.

#include "phantomcrash/rumble.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace phantomcrash {
namespace {

// The motors' full speed (XInput's 16-bit speeds).
constexpr float full_speed = 65535.0F;

float positive(const json::JsonValue &value, const char *key) {
  const float number = static_cast<float>(json::required(value, key).number());
  if (number <= 0.0F) {
    throw std::runtime_error(std::string(key) + " must be above zero");
  }
  return number;
}

RumbleStep parse_step(const json::JsonValue &value, const float frame_rate) {
  RumbleStep step;
  step.seconds = positive(value, "frames") / frame_rate;
  step.low = std::clamp(static_cast<float>(value.number_or("low", 0.0)) / full_speed, 0.0F, 1.0F);
  step.high = std::clamp(static_cast<float>(value.number_or("high", 0.0)) / full_speed, 0.0F, 1.0F);
  return step;
}

} // namespace

RumbleTuning parse_rumble(const json::JsonValue &document) {
  try {
    const auto &damage = json::required(document, "damage");
    const float frame_rate = positive(document, "frame_rate");
    RumbleTuning tuning;
    for (const auto &step : json::required(damage, "steps").array()) {
      tuning.damage_steps.push_back(parse_step(step, frame_rate));
    }
    tuning.damage_floor = static_cast<float>(damage.number_or("floor", 0.0));
    tuning.damage_ceiling = positive(damage, "ceiling");
    tuning.full_damage = positive(damage, "full");
    tuning.short_below = static_cast<float>(damage.number_or("short_below", 0.0));
    tuning.short_scale = static_cast<float>(damage.number_or("short_scale", 1.0));
    if (tuning.damage_floor > tuning.damage_ceiling) {
      throw std::runtime_error("floor is above ceiling");
    }
    return tuning;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("rumble.json: ") + error.what());
  }
}

std::vector<RumbleStep> damage_rumble(const RumbleTuning &tuning, const float damage) {
  if (damage <= 0.0F) {
    return {};
  }
  const float clamped = std::clamp(damage, tuning.damage_floor, tuning.damage_ceiling);
  const float length = clamped < tuning.short_below ? tuning.short_scale : 1.0F;
  const float strength = clamped / tuning.full_damage;
  std::vector<RumbleStep> steps;
  for (const RumbleStep &full : tuning.damage_steps) {
    steps.push_back({full.seconds * length, std::min(full.low * strength, 1.0F),
                     std::min(full.high * strength, 1.0F)});
  }
  return steps;
}

void RumblePlayer::start(const int port, std::vector<RumbleStep> steps) {
  if (port < 0 || port >= controller_ports) {
    return;
  }
  Track &track = tracks_[static_cast<std::size_t>(port)];
  track.steps = std::move(steps);
  track.step = 0;
  track.left = track.steps.empty() ? 0.0F : track.steps.front().seconds;
}

void RumblePlayer::update(float seconds) {
  for (Track &track : tracks_) {
    float time = seconds;
    while (track.step < track.steps.size() && time > 0.0F) {
      const float used = std::min(time, track.left);
      track.left -= used;
      time -= used;
      if (track.left <= 0.0F && ++track.step < track.steps.size()) {
        track.left = track.steps[track.step].seconds;
      }
    }
  }
}

void RumblePlayer::stop() {
  tracks_ = {};
}

RumbleMotors RumblePlayer::motors(const int port) const {
  if (port < 0 || port >= controller_ports) {
    return {};
  }
  const Track &track = tracks_[static_cast<std::size_t>(port)];
  if (track.step >= track.steps.size()) {
    return {};
  }
  const RumbleStep &step = track.steps[track.step];
  return {step.low, step.high};
}

std::array<RumbleMotors, controller_ports> RumblePlayer::all_motors() const {
  std::array<RumbleMotors, controller_ports> all{};
  for (int port = 0; port < controller_ports; ++port) {
    all[static_cast<std::size_t>(port)] = motors(port);
  }
  return all;
}

} // namespace phantomcrash
