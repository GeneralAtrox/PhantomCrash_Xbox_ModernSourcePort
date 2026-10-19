// Parsing of a shop's goods list look (module_shop.json, customize.json).

#include "phantomcrash/shop_list_style.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

int integer_of(const json::JsonValue &value, const char *key) {
  return static_cast<int>(required(value, key).number());
}

template <std::size_t N> std::array<Sprite, N> sprite_array(const json::JsonValue &value) {
  const auto sprites = parse_sprites(value);
  if (sprites.size() != N) {
    throw std::runtime_error("needs " + std::to_string(N) + " sprites");
  }
  std::array<Sprite, N> out;
  std::copy(sprites.begin(), sprites.end(), out.begin());
  return out;
}

std::array<PopWindow, 2> pop_pair(const json::JsonValue &value) {
  const auto &items = value.array();
  if (items.size() != 2) {
    throw std::runtime_error("needs two windows");
  }
  return {parse_pop_window(items[0]), parse_pop_window(items[1])};
}

// The members only the parts shop has: modules' taller windows and labels,
// the price and a paint's or emblem's picture.
void parse_extras(const json::JsonValue &value, ShopGoodsList &list) {
  if (value.find("module_details") != nullptr) {
    list.module_details = member(value, "module_details", pop_pair);
    list.module_rise = number_of(value, "module_rise");
    list.module_stats_top = number_of(value, "module_stats_top");
    list.module_stats_right = static_cast<float>(value.number_or("module_stats_right", 0.0));
    list.module_names = member(value, "module_names", parse_sprite);
    list.laser_names = member(value, "laser_names", parse_sprite);
  }
  if (value.find("price") != nullptr) {
    list.price_label = member(value, "price_label", parse_sprites);
    list.price = member(value, "price", parse_number_style);
  }
  if (value.find("swatch_frame") != nullptr) {
    list.swatch_frame = member(value, "swatch_frame", parse_sprite);
    list.swatch_at = numbers<2>(required(value, "swatch_at"), "swatch_at");
    list.swatch_layer = integer_of(value, "swatch_layer");
  }
  if (const json::JsonValue *badges = value.find("hangar_badges")) {
    list.hangar_badges = parse_hangar_labels(*badges);
  }
  if (value.find("in_use") != nullptr) {
    list.in_use = member(value, "in_use", parse_sprite);
  }
  if (value.find("badge_base") != nullptr) {
    list.badge_base = member(value, "badge_base", parse_sprite);
  }
  if (value.find("row_plate") != nullptr) {
    list.row_plate = member(value, "row_plate", parse_sprite);
  }
  if (const json::JsonValue *marks = value.find("lit_side_marks")) {
    list.lit_side_marks = parse_sprites(*marks);
  }
  if (const json::JsonValue *marks = value.find("side_marks")) {
    list.side_marks = parse_sprites(*marks);
    if (list.side_marks.size() != 2) {
      throw std::runtime_error("side_marks: needs two sprites");
    }
  }
}

} // namespace

PopWindow parse_pop_window(const json::JsonValue &value) {
  PopWindow pop;
  pop.sprite = member(value, "sprite", parse_sprite);
  pop.seconds = required(value, "seconds").number();
  if (pop.seconds <= 0.0) {
    throw std::runtime_error("seconds must be above 0");
  }
  pop.thin = number_of(value, "thin");
  if (value.find("caps") != nullptr) {
    pop.caps = member(value, "caps", sprite_array<2>);
  }
  if (const auto *grow = value.find("grow_caps")) {
    const auto &rows = grow->array();
    if (rows.size() != 2) {
      throw std::runtime_error("grow_caps needs 2 numbers");
    }
    pop.grow_caps =
        std::array{static_cast<float>(rows[0].number()), static_cast<float>(rows[1].number())};
  }
  return pop;
}

ShopGoodsList parse_goods_list(const json::JsonValue &value) {
  ShopGoodsList list;
  list.window = member(value, "window", parse_pop_window);
  list.details = member(value, "details", pop_pair);
  list.shade = member(value, "shade", parse_sprite);
  list.shade_alpha = number_of(value, "shade_alpha");
  list.cursor = member(value, "cursor", parse_sprite);
  list.rows = integer_of(value, "rows");
  list.row_step = number_of(value, "row_step");
  const auto &name = required(value, "name");
  list.name_at = numbers<2>(required(name, "position"), "position");
  list.name_width = static_cast<float>(name.number_or("width", 0));
  list.name_colour = parse_colour(required(name, "colour").string());
  list.name_layer = integer_of(name, "layer");
  list.maker_icons = member(value, "maker_icons", parse_sprites);
  list.scroll = member(value, "scroll", sprite_array<2>);
  list.cancel_hint = member(value, "cancel_hint", parse_sprites);
  list.stats_top = number_of(value, "stats_top");
  parse_extras(value, list);
  if (list.rows <= 0) {
    throw std::runtime_error("list: rows must be above 0");
  }
  return list;
}

float goods_name_width(const ShopGoodsList &list, const float padding) {
  const auto &window = list.window.sprite;
  const float panel_width = window.size ? (*window.size)[0] : window.rect[2] - window.rect[0];
  float right = window.position[0] + panel_width * (window.centered ? .5F : 1.0F);
  const auto reserve = [&](const Sprite &sprite) {
    const float width = sprite.size ? (*sprite.size)[0] : sprite.rect[2] - sprite.rect[0];
    const float left = sprite.position[0] - (sprite.centered ? width * .5F : 0);
    if (width > 0 && left > list.name_at[0]) {
      right = std::min(right, left);
    }
  };
  for (const auto &label : list.hangar_badges) {
    for (const auto &badge : label) {
      reserve(badge);
    }
  }
  for (const auto *badges : {&list.side_marks, &list.lit_side_marks}) {
    for (const auto &badge : *badges) {
      reserve(badge);
    }
  }
  if (list.badge_base) {
    reserve(*list.badge_base);
  }
  reserve(list.in_use);
  const float available = std::max(0.0F, right - list.name_at[0] - padding);
  return list.name_width > 0 ? std::min(list.name_width, available) : available;
}

} // namespace phantomcrash
