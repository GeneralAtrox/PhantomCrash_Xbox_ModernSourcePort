// Reads the flashes, shot looks, impacts and bullet holes of
// tables/effects.json (effect_models.py) into the EffectBank.
#include "phantomcrash/effect_bank.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace phantomcrash {

namespace {

using json::JsonValue;

std::optional<EffectPicture> picture(const JsonValue *value) {
  if (value == nullptr || !value->is_object()) {
    return std::nullopt;
  }
  EffectPicture out;
  out.model = value->string_or("model", "");
  out.mesh = static_cast<int>(value->number_or("mesh", -1.0));
  if (out.model.empty() || out.mesh < 0) {
    return std::nullopt;
  }
  return out;
}

FlashUpdate flash_update(const std::string &name) {
  constexpr std::pair<std::string_view, FlashUpdate> names[] = {
      {"fade", FlashUpdate::fade},
      {"rise", FlashUpdate::rise},
      {"pulse", FlashUpdate::pulse},
      {"still", FlashUpdate::still},
  };
  for (const auto &[text, update] : names) {
    if (name == text) {
      return update;
    }
  }
  throw std::runtime_error("unknown flash update " + name);
}

EffectFlash flash(const JsonValue &value) {
  EffectFlash out;
  for (const JsonValue &frame : json::required(value, "frames").array()) {
    out.frames.push_back(frame.is_string() ? frame.string() : std::string{});
  }
  out.step = std::max(static_cast<int>(value.number_or("step", 1.0)), 1);
  out.billboard = value.boolean_or("billboard", true);
  out.additive = value.boolean_or("additive", true);
  out.update = flash_update(value.string_or("update", "still"));
  return out;
}

ShotLook shot(const JsonValue &value) {
  ShotLook out;
  out.billboard = value.boolean_or("billboard", false);
  out.model = picture(value.find("model"));
  if (const JsonValue *sprite = value.find("sprite"); sprite != nullptr && sprite->is_string()) {
    out.sprite = sprite->string();
  }
  out.scale = static_cast<float>(value.number_or("scale", 1.0));
  out.trail = value.boolean_or("trail", false);
  if (const JsonValue *exhaust = value.find("exhaust")) {
    out.exhaust_offset = static_cast<float>(exhaust->number_or("offset", 0.0));
    if (const JsonValue *scale = exhaust->find("scale")) {
      const auto v = json::numbers<3>(*scale, "exhaust scale");
      out.exhaust_scale = {v[0], v[1], v[2]};
    }
  }
  return out;
}

} // namespace

void parse_effect_pictures(const JsonValue &value, EffectBank &bank) {
  if (const auto *fence = value.find("fence")) {
    bank.fence = parse_fence_look(*fence);
  }
  if (const JsonValue *flashes = value.find("flashes")) {
    for (const JsonValue &entry : flashes->array()) {
      bank.flashes.push_back(flash(entry));
    }
  }
  if (const JsonValue *shots = value.find("projectiles")) {
    for (const JsonValue &entry : shots->array()) {
      bank.shots.push_back(shot(entry));
    }
  }
  bank.exhaust = picture(value.find("exhaust"));
  if (const JsonValue *beam = value.find("beam")) {
    bank.beam.model = picture(beam);
    bank.beam.width = static_cast<float>(beam->number_or("width", bank.beam.width));
    bank.beam.fade_frames =
        static_cast<float>(beam->number_or("fade_frames", bank.beam.fade_frames));
  }
  if (const JsonValue *impacts = value.find("impacts")) {
    for (const JsonValue &entry : impacts->array()) {
      bank.impacts.push_back({static_cast<int>(entry.number_or("group", -1.0)),
                              static_cast<float>(entry.number_or("flash_scale", 1.0)),
                              entry.boolean_or("decal", false)});
    }
  }
  if (const JsonValue *wrecks = value.find("wreck_groups")) {
    for (const JsonValue &group : wrecks->array()) {
      bank.wreck_groups.push_back(static_cast<int>(group.number()));
    }
  }
  if (const JsonValue *decals = value.find("decals")) {
    if (const JsonValue *textures = decals->find("textures")) {
      for (const JsonValue &texture : textures->array()) {
        bank.decals.textures.push_back(texture.is_string() ? texture.string() : std::string{});
      }
    }
    bank.decals.scale = static_cast<float>(decals->number_or("scale", bank.decals.scale));
    bank.decals.count = static_cast<int>(decals->number_or("count", bank.decals.count));
    bank.decals.depth_offset =
        static_cast<float>(decals->number_or("depth_offset", bank.decals.depth_offset));
    bank.decals.scorch_scale =
        static_cast<float>(decals->number_or("scorch_scale", bank.decals.scorch_scale));
    if (const JsonValue *tint = decals->find("scorch_tint")) {
      bank.decals.scorch_tint = json::numbers<3>(*tint, "scorch_tint");
    }
    bank.decals.fit_reach =
        static_cast<float>(decals->number_or("fit_reach", bank.decals.fit_reach));
  }
  if (const auto *trail = value.find("missile_trail")) {
    auto &look = bank.missile_trail;
    look.texture = trail->string_or("texture", "");
    look.points = std::clamp(static_cast<int>(trail->number_or("points", look.points)), 2, 128);
    look.capacity = std::max(0, static_cast<int>(trail->number_or("capacity", look.capacity)));
    look.sample_hz =
        std::max(1.0F, static_cast<float>(trail->number_or("sample_hz", look.sample_hz)));
    look.fade_seconds =
        std::max(0.01F, static_cast<float>(trail->number_or("fade_seconds", look.fade_seconds)));
    look.head_width = static_cast<float>(trail->number_or("head_width", look.head_width));
    look.tail_width = static_cast<float>(trail->number_or("tail_width", look.tail_width));
  }
  // A bank without flashes (an older conversion) leaves the groups' lists unused.
  if (bank.flashes.empty()) {
    return;
  }
  for (const EffectGroup &group : bank.groups) {
    for (const int id : group.flashes) {
      if (id < 0 || id >= static_cast<int>(bank.flashes.size())) {
        throw std::runtime_error("effect group names a missing flash");
      }
    }
  }
}

} // namespace phantomcrash
