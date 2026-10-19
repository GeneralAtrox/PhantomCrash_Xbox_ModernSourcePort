#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/money.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

// Parses a member, naming it in the error.
Sprite glyph(const std::string &image, const json::JsonValue &rect) {
  Sprite sprite;
  sprite.image = image;
  sprite.rect = numbers<4>(rect, "rect");
  return sprite;
}

} // namespace

NumberFont parse_number_font(const json::JsonValue &value) {
  NumberFont font;
  const std::string image = required(value, "image").string();
  const auto &digits = required(value, "digits").array();
  if (digits.size() != font.digits.size()) {
    throw std::runtime_error("digits needs ten rects, 0 to 9");
  }
  for (std::size_t digit = 0; digit < digits.size(); ++digit) {
    font.digits[digit] = glyph(image, digits[digit]);
  }
  font.advance = static_cast<float>(required(value, "advance").number());
  font.comma = glyph(image, required(value, "comma"));
  font.comma_advance = static_cast<float>(required(value, "comma_advance").number());
  if (const auto *point = value.find("point")) {
    font.point = glyph(image, *point);
    font.point_advance = static_cast<float>(value.number_or("point_advance", font.point_advance));
  }
  return font;
}

NumberStyle parse_number_style(const json::JsonValue &value) {
  NumberStyle style;
  style.right = numbers<2>(required(value, "right"), "right");
  style.digits = std::clamp(static_cast<int>(required(value, "digits").number()), 1, 18);
  style.pad = value.boolean_or("pad", false);
  style.commas = value.boolean_or("commas", false);
  style.decimals = std::clamp(static_cast<int>(value.number_or("decimals", 0)), 0, 6);
  if (const auto *colour = value.find("colour")) {
    style.colour = parse_colour(colour->string());
  }
  return style;
}

namespace {

MeterStyle parse_meter_style(const json::JsonValue &value) {
  MeterStyle style;
  style.unlit = member(value, "unlit", parse_sprite);
  style.lit = member(value, "lit", parse_sprite);
  style.bars = std::max(0, static_cast<int>(required(value, "bars").number()));
  style.step = static_cast<float>(required(value, "step").number());
  style.bar_size = numbers<2>(required(value, "bar_size"), "bar_size");
  const auto number = [&](const char *key, const auto fallback) {
    return static_cast<decltype(fallback)>(value.number_or(key, fallback));
  };
  style.level_step = std::max(number("level_step", style.level_step), 0.0F);
  style.fall_per_second = number("fall_per_second", style.fall_per_second);
  if (const auto *music = value.find("music")) {
    try {
      style.music = parse_spectrum_style(*music);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("music: ") + error.what());
    }
  }
  return style;
}

RidingPlateStyle parse_riding_plate(const json::JsonValue &value) {
  RidingPlateStyle style;
  style.plate = member(value, "plate", parse_sprite);
  style.name_offset = numbers<2>(required(value, "name_offset"), "name_offset");
  style.name_colour = parse_colour(value.string_or("name_colour", "#f0f0f0"));
  style.name_layer = static_cast<int>(value.number_or("name_layer", style.name_layer));
  style.max_glyphs = std::max(0, static_cast<int>(value.number_or("max_glyphs", style.max_glyphs)));
  style.slide_from = static_cast<float>(value.number_or("slide_from", style.slide_from));
  style.slide_seconds = std::max(value.number_or("slide_seconds", style.slide_seconds), 0.0);
  return style;
}

} // namespace

float riding_plate_offset(const RidingPlateStyle &style, const double seconds) {
  if (style.slide_seconds <= 0.0) {
    return 0.0F;
  }
  const double left = 1.0 - std::clamp(seconds / style.slide_seconds, 0.0, 1.0);
  return static_cast<float>(left) * style.slide_from;
}

std::vector<Sprite> number_sprites(const NumberFont &font, const NumberStyle &style,
                                   const std::int64_t value, const int layer) {
  const int count = style.digits + style.decimals;
  std::int64_t limit = 1;
  for (int digit = 0; digit < count; ++digit) {
    limit *= 10;
  }
  // Too many digits shows all nines (0xbcd4b).
  const bool overflow = value >= limit;
  std::int64_t rest = std::max<std::int64_t>(value, 0);
  float pen = style.right[0];
  std::vector<Sprite> sprites;
  const auto put = [&](const Sprite &source, const float advance) {
    pen -= advance;
    Sprite sprite = source;
    sprite.position = {pen, style.right[1]};
    sprite.colour = style.colour;
    sprite.layer = layer;
    sprites.push_back(std::move(sprite));
  };
  for (int index = 0; index < count; ++index) {
    const int digit = overflow ? 9 : static_cast<int>(rest % 10);
    rest /= 10;
    // The whole part's leading zeros go, but not its last digit.
    if (rest == 0 && digit == 0 && index > style.decimals && !style.pad && !overflow) {
      break;
    }
    if (style.decimals > 0 && index == style.decimals && !font.point.image.empty()) {
      put(font.point, font.point_advance);
    }
    const int whole = index - style.decimals;
    if (style.commas && whole > 2 && whole % 3 == 0) {
      put(font.comma, font.comma_advance);
    }
    put(font.digits[static_cast<std::size_t>(digit)], font.advance);
  }
  return sprites;
}

std::vector<Sprite> number_sprites_from(const NumberFont &font, NumberStyle style, const float left,
                                        const std::int64_t value, const int layer, float &width) {
  // Written right to left from 0, then moved so that it starts at `left`.
  style.right[0] = 0.0F;
  std::vector<Sprite> sprites = number_sprites(font, style, value, layer);
  width = 0.0F;
  for (const auto &sprite : sprites) {
    width = std::max(width, -sprite.position[0]);
  }
  for (auto &sprite : sprites) {
    sprite.position[0] += left + width;
  }
  return sprites;
}

std::vector<Sprite> hud_sprites(const HudStyle &style, const QuestSave &quest) {
  std::vector<Sprite> sprites{style.day_label};
  auto day = number_sprites(style.font, style.day, quest.day + 1, style.layer);
  auto money = number_sprites(style.font, style.money, shown_ny(quest.money), style.layer);
  sprites.insert(sprites.end(), day.begin(), day.end());
  sprites.insert(sprites.end(), money.begin(), money.end());
  sprites.push_back(style.money_unit);
  return sprites;
}

std::optional<std::array<float, 4>> hud_save_box(const HudStyle &style) {
  if (!style.save_button) {
    return std::nullopt;
  }
  const HudSaveButton &button = *style.save_button;
  const float left = style.day_label.position[0] + button.offset[0];
  const float top = style.day_label.position[1] + button.offset[1];
  return std::array{left, top, left + button.size[0], top + button.size[1]};
}

std::vector<Sprite> money_sprites(const HudStyle &hud, const NumberStyle &style,
                                  const std::int64_t shown, const int layer) {
  std::vector<Sprite> sprites = number_sprites(hud.font, style, shown, layer);
  Sprite unit = hud.money_unit;
  unit.position = {style.right[0] + 1.0F, style.right[1]};
  unit.layer = layer;
  sprites.push_back(std::move(unit));
  return sprites;
}

HudPlacement parse_hud_placement(const json::JsonValue &value) {
  return {numbers<2>(required(value, "day"), "day"), numbers<2>(required(value, "money"), "money")};
}

HudStyle placed_hud(HudStyle style, const HudPlacement &placement) {
  const float day_dx = placement.day[0] - style.day_label.position[0];
  const float day_dy = placement.day[1] - style.day_label.position[1];
  style.day_label.position = placement.day;
  style.day.right = {style.day.right[0] + day_dx, style.day.right[1] + day_dy};
  const float money_dx = placement.money[0] - style.money.right[0];
  const float money_dy = placement.money[1] - style.money.right[1];
  style.money.right = placement.money;
  style.money_unit.position = {style.money_unit.position[0] + money_dx,
                               style.money_unit.position[1] + money_dy};
  return style;
}

MusicMeter::MusicMeter(MeterStyle style)
    : style_(std::move(style)), levels_(static_cast<std::size_t>(style_.bars), 0.0F) {}

void MusicMeter::update(const double tick_seconds, const std::vector<float> &heard) {
  const auto fall = static_cast<float>(style_.fall_per_second * tick_seconds);
  for (std::size_t index = 0; index < levels_.size(); ++index) {
    const float now = index < heard.size() ? std::clamp(heard[index], 0.0F, 1.0F) : 0.0F;
    levels_[index] = std::max(levels_[index] - fall, now);
  }
}

std::vector<Sprite> meter_sprites(const MeterStyle &style, const std::array<float, 2> origin,
                                  const std::vector<float> &levels) {
  std::vector<Sprite> sprites;
  const auto [width, height] = style.bar_size;
  const auto bar = [&](const Sprite &source, const float x, const float top, const float bottom) {
    if (bottom <= top) {
      return;
    }
    Sprite sprite = source;
    sprite.position = {x, top};
    sprite.size = std::array<float, 2>{width, bottom - top};
    sprites.push_back(std::move(sprite));
  };
  for (std::size_t index = 0; index < levels.size(); ++index) {
    float lit = std::clamp(levels[index], 0.0F, 1.0F) * height;
    if (style.level_step > 0.0F) {
      lit = std::round(lit / style.level_step) * style.level_step;
    }
    const float x = origin[0] + static_cast<float>(index) * style.step;
    const float split = origin[1] + height - lit;
    bar(style.unlit, x, origin[1], split);
    bar(style.lit, x, split, origin[1] + height);
  }
  return sprites;
}

namespace {

HudSaveButton parse_save_button(const json::JsonValue &value) {
  HudSaveButton button;
  button.offset = numbers<2>(required(value, "offset"), "offset");
  button.size = numbers<2>(required(value, "size"), "size");
  if (button.size[0] <= 0 || button.size[1] <= 0) {
    throw std::runtime_error("save_button: size must be positive");
  }
  button.normal = parse_shape_outlines(required(value, "normal"));
  button.lit = parse_shape_outlines(required(value, "lit"));
  return button;
}

} // namespace

FrameStyle parse_frame_style(const json::JsonValue &document) {
  try {
    FrameStyle style;
    const auto &hud = required(document, "hud");
    style.hud.font = member(hud, "font", parse_number_font);
    style.hud.layer = static_cast<int>(hud.number_or("layer", style.hud.layer));
    style.hud.day_label = member(hud, "day_label", parse_sprite);
    style.hud.day = member(hud, "day", parse_number_style);
    style.hud.money = member(hud, "money", parse_number_style);
    style.hud.money_unit = member(hud, "money_unit", parse_sprite);
    if (hud.find("save_button") != nullptr) {
      style.hud.save_button = member(hud, "save_button", parse_save_button);
    }
    style.meter = member(document, "meter", parse_meter_style);
    style.riding_plate = member(document, "riding_plate", parse_riding_plate);
    style.sort_hints = member(document, "sort_hints", parse_sort_hints);
    return style;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("frame.json: ") + error.what());
  }
}

FramePlacement parse_frame_placement(const json::JsonValue &value) {
  FramePlacement placement;
  placement.hud = value.boolean_or("hud", true);
  if (const auto *hud_at = value.find("hud_at")) {
    placement.hud_at = parse_hud_placement(*hud_at);
  }
  if (const auto *offset = value.find("save_offset")) {
    placement.save_offset = numbers<2>(*offset, "save_offset");
  }
  placement.riding_plate = value.boolean_or("riding_plate", false);
  if (const auto *y = value.find("riding_plate_y")) {
    placement.riding_plate_y = static_cast<float>(y->number());
  }
  if (const auto *plate = value.find("riding_plate_style")) {
    placement.riding_plate_style = parse_riding_plate(*plate);
  }
  if (const auto *meter = value.find("meter")) {
    placement.meter = numbers<2>(*meter, "meter");
  }
  const auto &now_playing = required(value, "now_playing");
  try {
    placement.now_playing = parse_ticker(now_playing);
    placement.now_playing_format = required(now_playing, "format").string();
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("now_playing: ") + error.what());
  }
  return placement;
}

} // namespace phantomcrash
