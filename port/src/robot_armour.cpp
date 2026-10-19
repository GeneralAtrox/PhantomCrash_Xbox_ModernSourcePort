#include "robot_armour.hpp"
#include "phantomcrash/json_fields.hpp"

namespace phantomcrash {
namespace {

// Fields older extractions lack read as zero unless `optional` is false.
LevelCurve curve(const json::JsonValue &value, const char *key, const bool optional = true) {
  const auto *entry = value.find(key);
  if (!entry && optional) {
    return {};
  }
  return parse_level_curve(entry ? *entry : json::required(value, key));
}

} // namespace

LevelCurve parse_level_curve(const json::JsonValue &entry) {
  LevelCurve out;
  out.base = static_cast<float>(json::required(entry, "base").number());
  out.mid = static_cast<float>(json::required(entry, "mid").number());
  out.top = static_cast<float>(json::required(entry, "top").number());
  out.knee = static_cast<int>(json::required(entry, "knee").number());
  out.low_scale = static_cast<int>(json::required(entry, "low_scale").number());
  return out;
}

ArmourRecord parse_armour_record(const json::JsonValue &value) {
  ArmourRecord out;
  out.index = static_cast<int>(json::required(value, "index").number());
  out.name = value.string_or("name", "");
  out.maker = static_cast<int>(value.number_or("class", value.number_or("maker", 0)));
  out.armour = curve(value, "armour", false);
  const auto &floor = json::required(value, "armour_floor");
  out.player_floor = static_cast<float>(json::required(floor, "player").number());
  out.computer_floor = static_cast<float>(json::required(floor, "computer").number());
  out.knockback_resistance = curve(value, "knockback_resistance");
  out.weight = curve(value, "weight");
  out.price = curve(value, "price");
  out.model = static_cast<int>(value.number_or("model", -1));
  out.capacity = curve(value, "capacity");
  out.sockets = curve(value, "sockets");
  out.speed = curve(value, "speed");
  out.jump = curve(value, "jump");
  out.turn = curve(value, "turn");
  out.dodge = curve(value, "dodge");
  out.air_dodge = curve(value, "air_dodge");
  out.stop_keep = static_cast<float>(value.number_or("stop_keep", out.stop_keep));
  out.wheels = parse_wheeled_tuning(value.find("movement"));
  return out;
}

} // namespace phantomcrash
