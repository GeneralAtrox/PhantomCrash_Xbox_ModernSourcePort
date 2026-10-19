// Reads tables/effects.json (effect_bank.py) into the EffectBank.
#include "phantomcrash/effect_bank.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace phantomcrash {

namespace {

using json::JsonValue;

constexpr std::pair<const char *, std::uint32_t> flag_names[] = {
    {"spawn", 0x2},
    {"replaceable", effect_flags::replaceable},
    {"inherit_velocity", effect_flags::inherit_velocity},
    {"exact_spread", effect_flags::exact_spread},
    {"radial_push", effect_flags::radial_push},
    {"random_direction", effect_flags::random_direction},
    {"spin", effect_flags::spin},
    {"streak", effect_flags::streak},
    {"streak_long", effect_flags::streak_long},
    {"streak_short", effect_flags::streak_short},
    {"random_offset", effect_flags::random_offset},
    {"fragments", effect_flags::fragments},
    {"detached", effect_flags::detached},
    {"random_speed", effect_flags::random_speed},
    {"swirl", effect_flags::swirl},
    {"die_at_target", effect_flags::die_at_target},
    {"debris", effect_flags::debris},
    {"debris_kind", effect_flags::debris_kind},
    {"kept", effect_flags::kept},
    {"streak_line", effect_flags::streak_line},
};

// Shape types that place a particle on a sphere, and the cluster (0x215b18).
constexpr int sphere_variant = 5;
constexpr int filled_sphere_type = 3;
constexpr int cluster_type = 5;

std::uint32_t flag_bits(const JsonValue &value) {
  std::uint32_t bits = 0;
  for (const JsonValue &name : value.array()) {
    const std::string &text = name.string();
    const auto *found = std::find_if(std::begin(flag_names), std::end(flag_names),
                                     [&](const auto &entry) { return text == entry.first; });
    if (found != std::end(flag_names)) {
      bits |= found->second;
    } else if (text.rfind("bit_", 0) == 0) {
      bits |= 1U << std::stoi(text.substr(4));
    } else {
      throw std::runtime_error("unknown effect flag " + text);
    }
  }
  return bits;
}

Vec3 vec3(const JsonValue &value, const char *key, const Vec3 fallback) {
  const JsonValue *found = value.find(key);
  if (found == nullptr) {
    return fallback;
  }
  const auto v = json::numbers<3>(*found, key);
  return {v[0], v[1], v[2]};
}

CurveTime curve_time(const JsonValue &value, const char *key, const CurveTime fallback) {
  const int mode = static_cast<int>(value.number_or(key, static_cast<int>(fallback)));
  return mode >= 0 && mode <= 3 ? static_cast<CurveTime>(mode) : CurveTime::none;
}

EffectCurve curve(const JsonValue *curves, const char *key) {
  EffectCurve out;
  const JsonValue *found = curves != nullptr ? curves->find(key) : nullptr;
  if (found != nullptr) {
    for (const JsonValue &k : found->array()) {
      out.keys.push_back(json::numbers<2>(k, key));
    }
  }
  return out;
}

EffectDefinition definition(const JsonValue &value) {
  EffectDefinition d;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  d.force_scale = number("force_scale", d.force_scale);
  d.force_jitter = number("force_jitter", d.force_jitter);
  d.drag = number("drag", d.drag);
  d.drag_jitter = number("drag_jitter", d.drag_jitter);
  d.additive = value.boolean_or("additive", d.additive);
  d.no_gravity = value.boolean_or("no_gravity", d.no_gravity);
  d.gravity_angles = vec3(value, "gravity_angles", d.gravity_angles);
  d.gravity = number("gravity", d.gravity);
  d.wind_off = value.boolean_or("wind_off", d.wind_off);
  d.wind = number("wind", d.wind);
  d.wind_angles = vec3(value, "wind_angles", d.wind_angles);
  d.pull_off = value.boolean_or("pull_off", d.pull_off);
  d.pull = number("pull", d.pull);
  d.pull_to_target = value.boolean_or("pull_to_target", d.pull_to_target);
  d.pull_range = number("pull_range", d.pull_range);
  d.pull_angles = vec3(value, "pull_angles", d.pull_angles);
  d.target = vec3(value, "target", d.target);
  d.colour_time = curve_time(value, "colour_time", d.colour_time);
  d.alpha_time = curve_time(value, "alpha_time", d.alpha_time);
  d.mass_time = curve_time(value, "mass_time", d.mass_time);
  d.scale_time = curve_time(value, "scale_time", d.scale_time);
  d.min_speed_time = curve_time(value, "min_speed_time", d.min_speed_time);
  d.max_speed_time = curve_time(value, "max_speed_time", d.max_speed_time);
  d.life = number("life", d.life);
  d.life_jitter = number("life_jitter", d.life_jitter);
  d.mass = number("mass", d.mass);
  d.size = number("size", d.size);
  d.size_jitter = number("size_jitter", d.size_jitter);
  d.min_speed = number("min_speed", d.min_speed);
  d.max_speed = number("max_speed", d.max_speed);
  if (const JsonValue *colour = value.find("colour")) {
    d.colour = json::numbers<3>(*colour, "colour");
  }
  d.alpha = number("alpha", d.alpha);
  d.alpha_extra = number("alpha_extra", d.alpha_extra);
  d.colour_jitter = number("colour_jitter", d.colour_jitter);
  const int type = static_cast<int>(value.number_or("shape_type", 0));
  const int variant = static_cast<int>(value.number_or("shape_variant", 0));
  if (type == cluster_type) {
    d.shape = EffectShape::cluster;
  } else if (variant == sphere_variant) {
    d.shape = EffectShape::sphere;
    d.sphere_filled = type == filled_sphere_type;
  }
  d.direction_mode = static_cast<int>(value.number_or("direction_mode", d.direction_mode));
  d.offset = vec3(value, "offset", d.offset);
  d.rotation = vec3(value, "rotation", d.rotation);
  d.scale = vec3(value, "scale", d.scale);
  d.rate = number("rate", d.rate);
  d.rate_jitter = number("rate_jitter", d.rate_jitter);
  d.spread = number("spread", d.spread);
  d.spread_jitter = number("spread_jitter", d.spread_jitter);
  d.speed = number("speed", d.speed);
  d.speed_jitter = number("speed_jitter", d.speed_jitter);
  d.push = number("push", d.push);
  d.cell = static_cast<int>(value.number_or("cell", d.cell));
  d.frames = static_cast<int>(value.number_or("frames", d.frames));
  const JsonValue *curves = value.find("curves");
  d.blue = curve(curves, "blue");
  d.green = curve(curves, "green");
  d.red = curve(curves, "red");
  d.alpha_curve = curve(curves, "alpha");
  d.scale_curve = curve(curves, "scale");
  d.pull_curve = curve(curves, "pull");
  d.wind_curve = curve(curves, "wind");
  d.rate_curve = curve(curves, "rate");
  d.mass_curve = curve(curves, "mass");
  d.speed_curve = curve(curves, "speed");
  d.min_speed_curve = curve(curves, "min_speed");
  d.max_speed_curve = curve(curves, "max_speed");
  return d;
}

std::vector<int> ids(const JsonValue &value, const char *key) {
  std::vector<int> out;
  if (const JsonValue *found = value.find(key)) {
    for (const JsonValue &id : found->array()) {
      out.push_back(static_cast<int>(id.number()));
    }
  }
  return out;
}

} // namespace

float EffectCurve::at(const float time) const {
  if (keys.empty()) {
    return 0.0F;
  }
  if (time <= keys.front()[0]) {
    return keys.front()[1];
  }
  for (std::size_t i = 1; i < keys.size(); ++i) {
    const auto &a = keys[i - 1];
    const auto &b = keys[i];
    if (time < b[0]) {
      return b[0] > a[0] ? a[1] + (b[1] - a[1]) * ((time - a[0]) / (b[0] - a[0])) : a[1];
    }
  }
  return keys.back()[1];
}

EffectBank parse_effect_bank(const JsonValue &value) {
  EffectBank bank;
  bank.frame_rate = std::max(static_cast<float>(value.number_or("frame_rate", 30.0)), 1.0F);
  if (const auto *motion = value.find("particle_motion")) {
    auto &out = bank.particle_motion;
    constexpr int max_fragments = 0x6e; // original live-particle cap
    out.fragment_copies =
        std::clamp(static_cast<int>(motion->number_or("fragment_copies", out.fragment_copies)), 0,
                   max_fragments);
    out.fragment_step = static_cast<float>(motion->number_or("fragment_step", out.fragment_step));
    out.fragment_jitter =
        static_cast<float>(motion->number_or("fragment_jitter", out.fragment_jitter));
    out.debris_restitution = std::clamp(
        static_cast<float>(motion->number_or("debris_restitution", out.debris_restitution)), 0.0F,
        1.0F);
    out.debris_friction = std::clamp(
        static_cast<float>(motion->number_or("debris_friction", out.debris_friction)), 0.0F, 1.0F);
    out.debris_rest_speed = std::max(
        0.0F, static_cast<float>(motion->number_or("debris_rest_speed", out.debris_rest_speed)));
    out.debris_clearance = std::max(
        0.0F, static_cast<float>(motion->number_or("debris_clearance", out.debris_clearance)));
    out.debris_stage_contacts =
        motion->boolean_or("debris_stage_contacts", out.debris_stage_contacts);
    out.start_at_offset = motion->boolean_or("start_at_offset", out.start_at_offset);
  }
  if (const JsonValue *atlas = value.find("atlas")) {
    bank.atlas.texture = atlas->string_or("texture", "");
    if (const JsonValue *size = atlas->find("cell_size")) {
      bank.atlas.cell_size = json::numbers<2>(*size, "atlas cell_size");
    }
    if (const JsonValue *cells = atlas->find("cells")) {
      for (const JsonValue &cell : cells->array()) {
        bank.atlas.cells.push_back(json::numbers<2>(cell, "atlas cell"));
      }
    }
  }
  for (const JsonValue &group : json::required(value, "groups").array()) {
    bank.groups.push_back({ids(group, "scripts"), ids(group, "flashes")});
  }
  for (const JsonValue &script : json::required(value, "scripts").array()) {
    EffectScript out;
    for (const JsonValue &emitter : json::required(script, "emitters").array()) {
      out.emitters.push_back({flag_bits(json::required(emitter, "flags")),
                              static_cast<int>(json::required(emitter, "definition").number())});
    }
    bank.scripts.push_back(std::move(out));
  }
  for (const JsonValue &entry : json::required(value, "definitions").array()) {
    bank.definitions.push_back(definition(entry));
  }
  for (const EffectScript &script : bank.scripts) {
    for (const EffectEmitterSpec &emitter : script.emitters) {
      if (emitter.definition < 0 ||
          emitter.definition >= static_cast<int>(bank.definitions.size())) {
        throw std::runtime_error("effect script names a missing definition");
      }
    }
  }
  for (const EffectGroup &group : bank.groups) {
    for (const int script : group.scripts) {
      if (script < 0 || script >= static_cast<int>(bank.scripts.size())) {
        throw std::runtime_error("effect group names a missing script");
      }
    }
  }
  parse_effect_pictures(value, bank);
  return bank;
}

} // namespace phantomcrash
