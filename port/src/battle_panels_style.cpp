#include "phantomcrash/battle_panels_style.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

const json::JsonValue &member(const json::JsonValue &value, const char *key) {
  const json::JsonValue *found = value.find(key);
  if (found == nullptr) {
    throw std::runtime_error(std::string("battle panels have no ") + key);
  }
  return *found;
}

float number(const json::JsonValue &value, const char *key, const float fallback) {
  return static_cast<float>(value.number_or(key, fallback));
}

std::uint32_t colour(const json::JsonValue &value, const char *key, const std::uint32_t fallback) {
  const json::JsonValue *found = value.find(key);
  return found == nullptr ? fallback : parse_colour(found->string());
}

std::array<float, 2> pair(const json::JsonValue &value, const char *key) {
  const auto &list = member(value, key).array();
  if (list.size() != 2) {
    throw std::runtime_error(std::string("battle panels ") + key + " needs 2 numbers");
  }
  return {static_cast<float>(list[0].number()), static_cast<float>(list[1].number())};
}

Sprite sprite(const json::JsonValue &value, const char *key) {
  return parse_sprite(member(value, key));
}

HudAnchor anchor(const json::JsonValue &value) {
  const std::string name = value.string_or("anchor", "centre");
  if (name == "left") {
    return HudAnchor::left;
  }
  if (name == "right") {
    return HudAnchor::right;
  }
  if (name == "centre") {
    return HudAnchor::centre;
  }
  throw std::runtime_error("battle panels: unknown anchor \"" + name + "\"");
}

HudGauge gauge(const json::JsonValue &value) {
  HudGauge out;
  out.anchor = anchor(value);
  out.frame = sprite(value, "frame");
  out.fill = sprite(value, "fill");
  out.width = number(value, "width", out.width);
  out.colour = colour(value, "colour", out.colour);
  out.low_colour = colour(value, "low_colour", out.low_colour);
  out.low = number(value, "low", out.low);
  out.flash_colour = colour(value, "flash_colour", out.flash_colour);
  return out;
}

// An icons key: the HUD's weapon kind as a number ("0" .. "6").
int hud_kind_key(const std::string &key) {
  if (key.empty() ||
      !std::all_of(key.begin(), key.end(), [](const char c) { return c >= '0' && c <= '9'; })) {
    throw std::runtime_error("battle panels: weapon icon key \"" + key + "\" is not a kind");
  }
  return std::stoi(key);
}

WeaponReadout readout(const std::string &name) {
  if (name == "ammo") {
    return WeaponReadout::ammo;
  }
  if (name == "charge") {
    return WeaponReadout::charge;
  }
  if (name == "none") {
    return WeaponReadout::none;
  }
  throw std::runtime_error("battle panels: unknown readout \"" + name + "\"");
}

WeaponPanelSide side(const json::JsonValue &value) {
  WeaponPanelSide out;
  out.anchor = anchor(value);
  out.arm = pair(value, "arm");
  out.shoulder = pair(value, "shoulder");
  out.frame = sprite(value, "frame");
  const std::string image = member(value, "digit_image").string();
  const auto &digits = member(value, "digits").array();
  const auto &positions = member(value, "digit_positions").array();
  if (digits.size() != out.digits.size() || positions.size() != out.digit_positions.size()) {
    throw std::runtime_error("battle panels need 10 digits and 4 digit positions");
  }
  for (std::size_t i = 0; i < digits.size(); ++i) {
    const auto &rect = digits[i].array();
    out.digits[i].image = image;
    for (std::size_t k = 0; k < 4 && k < rect.size(); ++k) {
      out.digits[i].rect[k] = static_cast<float>(rect[k].number());
    }
  }
  for (std::size_t i = 0; i < positions.size(); ++i) {
    const auto &at = positions[i].array();
    out.digit_positions[i] = {static_cast<float>(at.at(0).number()),
                              static_cast<float>(at.at(1).number())};
  }
  out.percent = sprite(value, "percent");
  out.icon = pair(value, "icon");
  if (value.find("bar") != nullptr) {
    out.bar = sprite(value, "bar");
  }
  out.bar_mirrored = value.boolean_or("bar_mirrored", false);
  out.lost = sprite(value, "lost");
  return out;
}

WeaponPanelStyle weapons(const json::JsonValue &value) {
  WeaponPanelStyle out;
  out.ready_colour = colour(value, "ready_colour", out.ready_colour);
  out.refill_colour = colour(value, "refill_colour", out.refill_colour);
  out.bar_colour = colour(value, "bar_colour", out.bar_colour);
  out.bar_low_colour = colour(value, "bar_low_colour", out.bar_low_colour);
  out.bar_charging_colour = colour(value, "bar_charging_colour", out.bar_charging_colour);
  out.bar_low = number(value, "bar_low", out.bar_low);
  out.bar_width = number(value, "bar_width", out.bar_width);
  out.left = side(member(value, "left"));
  out.right = side(member(value, "right"));
  for (const auto &[kind, icon] : member(value, "icons").object()) {
    WeaponIcon &entry = out.icons[hud_kind_key(kind)];
    entry.left = sprite(icon, "left");
    entry.right = sprite(icon, "right");
    if (const json::JsonValue *homing = icon.find("homing")) {
      entry.homing_left = sprite(*homing, "left");
      entry.homing_right = sprite(*homing, "right");
    }
    entry.readout = readout(icon.string_or("readout", "ammo"));
  }
  return out;
}

HudCounter counter(const json::JsonValue &value) {
  return {anchor(value), sprite(value, "label"), parse_number_style(member(value, "number"))};
}

void parse_labels(const json::JsonValue &value, BattlePanelsStyle &out) {
  out.font = parse_number_font(member(value, "font"));
  out.colon = sprite(value, "colon");
  out.colon_advance = number(value, "colon_advance", out.colon_advance);
  out.day = counter(member(value, "day"));
  out.time = counter(member(value, "time"));
  out.money = counter(member(value, "money"));
  out.shootdown = counter(member(value, "shootdown"));
}

void parse_messages(const json::JsonValue &value, BattlePanelsStyle &out) {
  out.messages_anchor = anchor(value);
  out.messages_position = pair(value, "position");
  HudMessageLog::Style &log = out.messages;
  log.slots = static_cast<int>(value.number_or("slots", log.slots));
  log.life_frames = number(value, "life_frames", log.life_frames);
  log.fade_frames = number(value, "fade_frames", log.fade_frames);
  log.step = number(value, "step", log.step);
  log.enter = number(value, "enter", log.enter);
  log.leave = number(value, "leave", log.leave);
  log.push_scroll = number(value, "push_scroll", log.push_scroll);
  log.scroll_decay = number(value, "scroll_decay", log.scroll_decay);
  log.min_scroll = number(value, "min_scroll", log.min_scroll);
  out.message_sprites = {sprite(value, "damaged"), sprite(value, "hit"), sprite(value, "bingo")};
}

} // namespace

BattlePanelsStyle parse_battle_panels_style(const json::JsonValue &document) {
  BattlePanelsStyle out;
  if (const auto *replacements = document.find("text_replacements")) {
    for (const auto &entry : replacements->array()) {
      out.text_replacements.push_back(parse_sprite_text_style(entry));
    }
  }
  out.widescreen = document.string_or("layout", "widescreen") != "4:3";
  out.flash_frames = number(document, "flash_frames", out.flash_frames);
  out.money_count_seconds = number(document, "money_count_seconds", out.money_count_seconds);
  out.text_gap = std::max(0.0F, number(document, "text_gap", out.text_gap));
  out.durability = gauge(member(document, "durability"));
  const json::JsonValue &optic = member(document, "optic");
  out.optic = gauge(optic);
  out.optic_active_colour = colour(optic, "active_colour", out.optic_active_colour);
  if (document.find("canvas") != nullptr) {
    out.canvas = pair(document, "canvas");
  }
  out.weapons = weapons(member(document, "weapons"));
  if (const json::JsonValue *labels = document.find("labels")) {
    parse_labels(*labels, out);
  } else {
    out.labels = false;
  }
  if (const json::JsonValue *messages = document.find("messages")) {
    parse_messages(*messages, out);
  } else {
    out.messages_shown = false;
  }

  const json::JsonValue &warning = member(document, "warning");
  out.warning_anchor = anchor(warning);
  out.warning_stripes = sprite(warning, "stripes");
  out.warning_half = sprite(warning, "half");
  out.warning_quarter = sprite(warning, "quarter");
  out.warning_half_below = number(warning, "half_below", out.warning_half_below);
  out.warning_quarter_below = number(warning, "quarter_below", out.warning_quarter_below);

  const json::JsonValue &alert = member(document, "missile_alert");
  out.alert_anchor = anchor(alert);
  out.missile_alert = sprite(alert, "sprite");
  out.alert_wave_frames = number(alert, "wave_frames", out.alert_wave_frames);
  return out;
}

float anchor_x(const HudAnchor anchor, const float x, const float left, const float right,
               const float width) {
  switch (anchor) {
  case HudAnchor::left:
    return x + left;
  case HudAnchor::right:
    return x + right - width;
  case HudAnchor::centre:
    break;
  }
  return x;
}

} // namespace phantomcrash
