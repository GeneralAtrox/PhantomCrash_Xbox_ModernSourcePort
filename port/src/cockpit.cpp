// The first-person cockpit (0x04c459, drawn in view layer 0xb from the
// HUD pass 0x04eaa3 while the camera is in first person).
#include "phantomcrash/cockpit.hpp"

#include "phantomcrash/battle_hud_state.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/skeleton.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
namespace {

using json::JsonValue;

std::uint32_t colour(const JsonValue &value, const char *key, const std::uint32_t fallback) {
  const JsonValue *found = value.find(key);
  return found == nullptr ? fallback : parse_colour(found->string());
}

CockpitGauge parse_gauge(const std::string &name) {
  if (name == "none") {
    return CockpitGauge::none;
  }
  if (name == "climb") {
    return CockpitGauge::climb;
  }
  if (name == "speed") {
    return CockpitGauge::speed;
  }
  if (name == "armour") {
    return CockpitGauge::armour;
  }
  if (name == "camouflage") {
    return CockpitGauge::camouflage;
  }
  throw std::runtime_error("unknown cockpit gauge \"" + name + "\"");
}

// Moves `value` toward `target` by at most `step` (0x04c459's 0.05 a frame).
float approach(const float value, const float target, const float step) {
  const float diff = target - value;
  if (diff > step) {
    return value + step;
  }
  if (diff < -step) {
    return value - step;
  }
  return target;
}

} // namespace

CockpitStyle parse_cockpit_style(const JsonValue &document) {
  CockpitStyle s;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(document.number_or(key, fallback));
  };
  try {
    s.model = document.string_or("model", s.model);
    s.texture_size = number("texture_size", s.texture_size);
    s.distance = number("distance", s.distance);
    s.height = number("height", s.height);
    s.colour = colour(document, "colour", s.colour);
    s.ease_per_second = number("ease_per_second", s.ease_per_second);
    s.climb_fall_ticks = number("climb_fall_ticks", s.climb_fall_ticks);
    s.climb_rise_ticks = number("climb_rise_ticks", s.climb_rise_ticks);
    s.tick_rate = number("tick_rate", s.tick_rate);
    s.armour_colour = colour(document, "armour_colour", s.armour_colour);
    s.armour_low_colour = colour(document, "armour_low_colour", s.armour_low_colour);
    s.armour_flash_colour = colour(document, "armour_flash_colour", s.armour_flash_colour);
    s.armour_low = number("armour_low", s.armour_low);
    s.camouflage_colour = colour(document, "camouflage_colour", s.camouflage_colour);
    s.camouflage_low_colour = colour(document, "camouflage_low_colour", s.camouflage_low_colour);
    s.camouflage_low = number("camouflage_low", s.camouflage_low);
    s.flash_frames = number("flash_frames", s.flash_frames);
    s.visor_mesh = static_cast<int>(document.number_or("visor_mesh", s.visor_mesh));
    s.visor_colour = colour(document, "visor_colour", s.visor_colour);
    s.visor_distance = number("visor_distance", s.visor_distance);
    s.visor_width = number("visor_width", s.visor_width);
    s.visor_fade_ticks = number("visor_fade_ticks", s.visor_fade_ticks);
    for (const auto &[name, group] : json::required(document, "groups").object()) {
      s.groups[name] = {static_cast<float>(group.number_or("slide", 0.0)),
                        static_cast<float>(group.number_or("bob", 0.0))};
    }
    for (const auto &[name, cells] : json::required(document, "cells").object()) {
      CockpitCells out;
      for (const JsonValue &offset : json::required(cells, "offsets").array()) {
        out.offsets.push_back(json::numbers<2>(offset, "cell offset"));
      }
      out.per_second = static_cast<float>(cells.number_or("per_second", 0.0));
      if (out.offsets.empty()) {
        throw std::runtime_error("cells \"" + name + "\" have no offsets");
      }
      s.cells[name] = std::move(out);
    }
    for (const JsonValue &value : json::required(document, "pieces").array()) {
      CockpitPiece piece;
      piece.mesh = static_cast<int>(json::required(value, "mesh").number());
      piece.group = json::required(value, "group").string();
      piece.scale = parse_gauge(value.string_or("scale", "none"));
      piece.cells = value.string_or("cells", "");
      piece.cell_source = parse_gauge(value.string_or("cell_source", "none"));
      if (!s.groups.contains(piece.group)) {
        throw std::runtime_error("unknown cockpit group \"" + piece.group + "\"");
      }
      if (!piece.cells.empty() && !s.cells.contains(piece.cells)) {
        throw std::runtime_error("unknown cockpit cells \"" + piece.cells + "\"");
      }
      s.pieces.push_back(std::move(piece));
    }
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("cockpit: ") + error.what());
  }
  return s;
}

void Cockpit::tick(const CockpitInput &input, const float seconds) {
  const float step = style_.ease_per_second * seconds;
  // The climb gauge reads the vertical speed against gravity's pull over a
  // few ticks, falling or rising.
  const float ticks =
      input.vertical_speed < 0.0F ? style_.climb_fall_ticks : style_.climb_rise_ticks;
  const float full = input.gravity * ticks / std::max(style_.tick_rate, 1.0F);
  const float climb = full > 0.0F ? std::fabs(input.vertical_speed) / full : 0.0F;
  climb_ = std::clamp(approach(climb_, climb, step), 0.0F, 1.0F);
  speed_ = std::clamp(approach(speed_, input.speed_ratio, step), 0.0F, 1.0F);
  const float fade = style_.visor_fade_ticks > 0.0F ? style_.tick_rate / style_.visor_fade_ticks
                                                    : 1.0F / std::max(seconds, 1e-6F);
  visor_ =
      std::clamp(approach(visor_, input.camouflaged ? 1.0F : 0.0F, fade * seconds), 0.0F, 1.0F);
  animation_ = std::fmod(animation_ + seconds, 3600.0F);
}

float Cockpit::value(const CockpitGauge gauge, const CockpitInput &input) const {
  switch (gauge) {
  case CockpitGauge::climb:
    return climb_;
  case CockpitGauge::speed:
    return speed_;
  case CockpitGauge::armour:
    return std::clamp(input.armour, 0.0F, 1.0F);
  case CockpitGauge::camouflage:
    return std::clamp(input.camouflage, 0.0F, 1.0F);
  case CockpitGauge::none:
    break;
  }
  return 1.0F;
}

std::uint32_t Cockpit::colour(const CockpitGauge gauge, const CockpitInput &input,
                              const double clock) const {
  if (gauge == CockpitGauge::armour) {
    const std::uint32_t base =
        value(gauge, input) >= style_.armour_low ? style_.armour_colour : style_.armour_low_colour;
    return input.filling ? hud_flash(base, style_.armour_flash_colour, clock, style_.flash_frames)
                         : base;
  }
  if (gauge == CockpitGauge::camouflage) {
    if (input.camouflaged) {
      return hud_flash(style_.colour, style_.camouflage_colour, clock, style_.flash_frames);
    }
    return value(gauge, input) >= style_.camouflage_low ? style_.camouflage_colour
                                                        : style_.camouflage_low_colour;
  }
  return style_.colour;
}

std::vector<CockpitDraw> Cockpit::draws(const CockpitInput &input, const double clock) const {
  std::vector<CockpitDraw> out;
  if (visor_ > 0.0F) {
    CockpitDraw visor;
    visor.mesh = style_.visor_mesh;
    visor.place = scaling({style_.visor_width, 1.0F, 1.0F}) *
                  translation({0.0F, 0.0F, style_.visor_distance});
    visor.colour = style_.visor_colour;
    visor.alpha = visor_;
    visor.additive = true;
    out.push_back(visor);
  }
  // Each group turns about its own origin by the aim's lead on the camera,
  // then stands ahead of the eye, slid by the change of view.
  const Mat4 sway = rotation_y(input.sway);
  for (const CockpitPiece &piece : style_.pieces) {
    const CockpitGroup &group = style_.groups.at(piece.group);
    CockpitDraw draw;
    draw.mesh = piece.mesh;
    draw.place = sway *
                 translation({0.0F, style_.height + group.bob * input.bob, style_.distance}) *
                 translation({0.0F, group.slide * input.slide, 0.0F});
    draw.colour = colour(piece.scale, input, clock);
    if (piece.scale != CockpitGauge::none) {
      draw.scale_x = value(piece.scale, input);
    }
    if (!piece.cells.empty()) {
      const CockpitCells &cells = style_.cells.at(piece.cells);
      const int count = static_cast<int>(cells.offsets.size());
      int cell = 0;
      if (cells.per_second > 0.0F) {
        cell = static_cast<int>(animation_ * cells.per_second) % count;
      } else {
        cell = std::min(static_cast<int>(value(piece.cell_source, input) * count), count - 1);
      }
      const auto &offset = cells.offsets[static_cast<std::size_t>(cell)];
      draw.uv_offset = {offset[0] / style_.texture_size, offset[1] / style_.texture_size};
    }
    out.push_back(draw);
  }
  return out;
}

} // namespace phantomcrash
