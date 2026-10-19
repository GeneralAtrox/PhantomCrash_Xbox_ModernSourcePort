#include "phantomcrash/battle_hud_style.hpp"
#include "phantomcrash/robot_parts.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

const json::JsonValue &member(const json::JsonValue &value, const char *key) {
  const json::JsonValue *found = value.find(key);
  if (found == nullptr) {
    throw std::runtime_error(std::string("battle HUD has no ") + key);
  }
  return *found;
}

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

HudPiece piece(const json::JsonValue &value) {
  HudPiece out;
  out.sprite = parse_sprite(value);
  out.alpha = number(value, "alpha", 1.0F);
  return out;
}

std::array<HudPiece, 3> pieces(const json::JsonValue &value, const char *key) {
  const auto &list = member(value, key).array();
  if (list.size() != 3) {
    throw std::runtime_error(std::string("battle HUD ") + key + " needs 3 pieces");
  }
  return {piece(list[0]), piece(list[1]), piece(list[2])};
}

std::array<ReticleSizes, 3> sizes(const json::JsonValue &value, const char *key) {
  const auto &list = member(value, key).array();
  if (list.size() != 3) {
    throw std::runtime_error(std::string("battle HUD ") + key + " needs 3 shapes");
  }
  std::array<ReticleSizes, 3> out{};
  for (std::size_t shape = 0; shape < out.size(); ++shape) {
    const auto &row = list[shape].array();
    if (row.size() != 4) {
      throw std::runtime_error(std::string("battle HUD ") + key + " rows need 4 numbers");
    }
    for (std::size_t i = 0; i < 4; ++i) {
      out[shape][i] = static_cast<float>(row[i].number());
    }
  }
  return out;
}

std::array<float, 2> pair(const json::JsonValue &value, const char *key,
                          const std::array<float, 2> fallback) {
  const json::JsonValue *found = value.find(key);
  if (found == nullptr || !found->is_array() || found->array().size() != 2) {
    return fallback;
  }
  return {static_cast<float>(found->array()[0].number()),
          static_cast<float>(found->array()[1].number())};
}

int shape(const json::JsonValue &value, const char *key) {
  return std::clamp(static_cast<int>(value.number_or(key, 0.0)), 0, 2);
}

void parse_sight(const json::JsonValue &sight, BattleHudStyle::Sight &out) {
  out.ring = piece(member(sight, "ring"));
  out.outline = piece(member(sight, "outline"));
  out.fill = piece(member(sight, "fill"));
  for (const json::JsonValue &marker : member(sight, "markers").array()) {
    const auto &f = marker.array();
    if (f.size() == 3) {
      out.markers.push_back({static_cast<float>(f[0].number()), static_cast<float>(f[1].number()),
                             static_cast<float>(f[2].number())});
    }
  }
  out.arrow_radius = number(sight, "arrow_radius", out.arrow_radius);
  out.arrow_alpha = number(sight, "arrow_alpha", out.arrow_alpha);
}

void parse_control(const json::JsonValue &control, BattleHudStyle::Control &out) {
  out.inner_shape = shape(control, "inner_shape");
  out.inner_value = number(control, "inner_value", out.inner_value);
  out.outer_shape = shape(control, "outer_shape");
  out.outer_value = number(control, "outer_value", out.outer_value);
  out.track_value = number(control, "track_value", out.track_value);
  out.value_max = number(control, "value_max", out.value_max);
  out.track_base = number(control, "track_base", out.track_base);
  out.track_scale = number(control, "track_scale", out.track_scale);
  out.track_full = number(control, "track_full", out.track_full);
  out.track_range = number(control, "track_range", out.track_range);
  out.lock_frames = number(control, "lock_frames", out.lock_frames);
}

} // namespace

std::uint32_t BattleHudStyle::team_colour(const int team) const {
  if (team < 0 || team >= static_cast<int>(markers.team_colours.size())) {
    return markers.team_colours.empty() ? 0xffffff : markers.team_colours.front();
  }
  return markers.team_colours[static_cast<std::size_t>(team)];
}

std::array<float, 2> BattleHudStyle::safe_scale() const {
  return {safe_area[0] / 640.0F, safe_area[1] / 480.0F};
}

BattleHudStyle parse_battle_hud_style(const json::JsonValue &document) {
  BattleHudStyle style;
  style.sense_range = number(document, "sense_range", style.sense_range);
  style.safe_area = pair(document, "safe_area", style.safe_area);
  style.aim_distance = number(document, "aim_distance", style.aim_distance);
  parse_sight(member(document, "sight"), style.sight);
  if (const json::JsonValue *control = document.find("control"); control != nullptr) {
    parse_control(*control, style.control);
  }

  const json::JsonValue &reticle = member(document, "reticle");
  style.reticle.inner = pieces(reticle, "inner");
  style.reticle.outer = pieces(reticle, "outer");
  style.reticle.inner_sizes = sizes(reticle, "inner_sizes");
  style.reticle.outer_sizes = sizes(reticle, "outer_sizes");
  style.reticle.inner_pad = number(reticle, "inner_pad", style.reticle.inner_pad);
  style.reticle.outer_pad = number(reticle, "outer_pad", style.reticle.outer_pad);
  style.reticle.diamond = piece(member(reticle, "diamond"));

  const json::JsonValue &locks = member(document, "locks");
  style.locks.right_shoulder = piece(member(locks, "right_shoulder"));
  style.locks.left_shoulder = piece(member(locks, "left_shoulder"));
  style.locks.melee = piece(member(locks, "melee"));
  style.locks.corner = pair(locks, "corner", style.locks.corner);
  if (const json::JsonValue *colour = locks.find("colour"); colour != nullptr) {
    style.locks.colour = parse_colour(colour->string());
  }

  const json::JsonValue &markers = member(document, "markers");
  style.markers.frame = piece(member(markers, "frame"));
  style.markers.frame_offset = pair(markers, "frame_offset", style.markers.frame_offset);
  style.markers.bar = piece(member(markers, "bar"));
  style.markers.bar_offset = pair(markers, "bar_offset", style.markers.bar_offset);
  if (const json::JsonValue *tag = markers.find("tag"); tag != nullptr) {
    auto &out = style.markers.tag;
    out.offset = pair(*tag, "offset", out.offset);
    out.cell = static_cast<float>(tag->number_or("cell", out.cell));
    out.advance = static_cast<float>(tag->number_or("advance", out.advance));
    out.max_glyphs = static_cast<int>(tag->number_or("max_glyphs", out.max_glyphs));
  }
  if (const json::JsonValue *defeated = markers.find("defeated")) {
    style.markers.defeated = piece(*defeated);
    style.markers.defeated_offset = pair(*defeated, "offset", style.markers.defeated_offset);
  }
  if (const json::JsonValue *leader = markers.find("leader"); leader != nullptr) {
    style.markers.leader = piece(*leader);
    style.markers.leader_offset = pair(*leader, "offset", style.markers.leader_offset);
  }
  if (const json::JsonValue *numbers = markers.find("player_numbers"); numbers != nullptr) {
    style.markers.number_offset = pair(*numbers, "offset", style.markers.number_offset);
    for (const json::JsonValue &number : member(*numbers, "sprites").array()) {
      style.markers.player_numbers.push_back(piece(number));
    }
  }
  if (const json::JsonValue *colours = markers.find("team_colours"); colours != nullptr) {
    for (const json::JsonValue &colour : colours->array()) {
      style.markers.team_colours.push_back(parse_colour(colour.string()));
    }
  }
  return style;
}

float track_speed(const BattleHudStyle::Control &control) {
  const float share = control.track_full > 0.0F
                          ? std::clamp(control.track_value / control.track_full, 0.0F, 1.0F)
                          : 0.0F;
  return control.track_base + control.track_scale * share;
}

std::array<float, 2> reticle_half_size(const ReticleSizes &sizes, const float value,
                                       const float value_max) {
  const float t = value_max > 0.0F ? std::clamp(value / value_max, 0.0F, 1.0F) : 0.0F;
  return {sizes[0] + (sizes[1] - sizes[0]) * t, sizes[2] + (sizes[3] - sizes[2]) * t};
}

SightBoxes sight_boxes(const BattleHudStyle &style, const ChipStats &chip) {
  const auto shape = [](const int value) {
    return static_cast<std::size_t>(std::clamp(value, 0, reticle_shapes - 1));
  };
  const auto &reticle = style.reticle;
  const float most = style.control.value_max;
  return {reticle_half_size(reticle.inner_sizes[shape(chip.inner_shape)], chip.inner_value, most),
          reticle_half_size(reticle.outer_sizes[shape(chip.outer_shape)], chip.outer_value, most)};
}

std::array<float, 2> reticle_corner_inset(const HudPiece &corner, const float pad) {
  const float width = corner.sprite.rect[2] - corner.sprite.rect[0];
  const float height = corner.sprite.rect[3] - corner.sprite.rect[1];
  // (size / 2 + pad) / 640 in NDC is half that in 640x480 units.
  return {(width * 0.5F + pad) * 0.5F, (height * 0.5F + pad) * 0.5F};
}

} // namespace phantomcrash
