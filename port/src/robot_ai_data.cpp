#include "phantomcrash/robot_ai.hpp"
#include "phantomcrash/json_fields.hpp"
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {
template <typename T, std::size_t N> std::array<T, N> values(const json::JsonValue &value) {
  const auto &list = value.array();
  if (list.size() != N) {
    throw std::runtime_error("original AI: wrong table row length");
  }
  std::array<T, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    out[i] = static_cast<T>(list[i].number());
  }
  return out;
}
} // namespace

float OriginalAiData::value(const std::string_view name) const {
  const auto found = parameters.find(name);
  if (found == parameters.end()) {
    throw std::runtime_error("original AI: missing parameter " + std::string(name));
  }
  return found->second;
}

std::shared_ptr<const OriginalAiData> parse_original_ai(const json::JsonValue &document) {
  auto data = std::make_shared<OriginalAiData>();
  for (const auto &row : json::required(document, "profiles").array()) {
    data->profiles.push_back(values<int, 25>(row));
  }
  for (const auto &row : json::required(document, "decisions").array()) {
    data->decisions.push_back(values<int, 5>(row));
  }
  data->state_order = values<int, 5>(json::required(document, "state_order"));
  data->ranker_weapon_masks = values<int, 5>(json::required(document, "ranker_weapon_masks"));
  for (const auto &v : json::required(document, "search_weights").array()) {
    data->search_weights.push_back(static_cast<int>(v.number()));
  }
  for (const auto &v : json::required(document, "ranker_entrants").array()) {
    data->ranker_entrants.push_back(static_cast<int>(v.number()));
  }
  const auto &levels = json::required(document, "levels").array();
  if (levels.size() != data->levels.size() || data->profiles.empty() || data->decisions.empty()) {
    throw std::runtime_error("original AI: incomplete tables");
  }
  for (std::size_t i = 0; i < levels.size(); ++i) {
    const auto &v = levels[i];
    auto &out = data->levels[i];
    out.durations = values<float, 7>(json::required(v, "durations"));
    out.movement_chances = values<int, 3>(json::required(v, "movement_chances"));
    const auto number = [&](const char *key) {
      return static_cast<float>(json::required(v, key).number());
    };
    out.memory_seconds = number("memory_seconds");
    out.control_scale = number("control_scale");
    out.sight_yaw = number("sight_yaw");
    out.sight_pitch = number("sight_pitch");
    out.max_weapon_requests = static_cast<int>(number("max_weapon_requests"));
    out.fire_probability = number("fire_probability");
    out.fire_interval = number("fire_interval");
    out.fire_delay = number("fire_delay");
    out.camouflage_interval = number("camouflage_interval");
    out.camouflage_probability = number("camouflage_probability");
    out.camouflage_duration = number("camouflage_duration");
  }
  for (const auto &[key, v] : json::required(document, "parameters").object()) {
    const float number = static_cast<float>(v.number());
    if (!std::isfinite(number)) {
      throw std::runtime_error("original AI: non-finite parameter");
    }
    data->parameters.emplace(key, number);
  }
  if (data->value("tick_hz") <= 0 || data->value("probability_scale") <= 0) {
    throw std::runtime_error("original AI: invalid tick/probability scale");
  }
  for (const auto &profile : data->profiles) {
    for (int row : profile) {
      if (row < 0 || row >= static_cast<int>(data->decisions.size())) {
        throw std::runtime_error("original AI: invalid profile row");
      }
    }
  }
  for (int mode : data->state_order) {
    if (mode < 0 || mode > 6) {
      throw std::runtime_error("original AI: invalid state");
    }
  }
  return data;
}
} // namespace phantomcrash
