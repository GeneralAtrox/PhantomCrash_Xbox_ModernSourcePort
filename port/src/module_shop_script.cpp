// Parsing of data/ui/module_shop.json and data/ui/stock_module.json.

#include "phantomcrash/module_shop_script.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <stdexcept>

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

double seconds_of(const json::JsonValue &value, const char *key) {
  const double seconds = required(value, key).number();
  if (seconds <= 0.0) {
    throw std::runtime_error(std::string(key) + " must be above 0");
  }
  return seconds;
}

std::vector<std::string> strings(const json::JsonValue &value) {
  std::vector<std::string> out;
  for (const json::JsonValue &item : value.array()) {
    out.push_back(item.string());
  }
  return out;
}

ShopCategory parse_category(const json::JsonValue &value) {
  ShopCategory category;
  category.id = required(value, "id").string();
  if (const json::JsonValue *kind = value.find("kind")) {
    category.kind = goods_kind_named(kind->string());
    if (!category.kind) {
      throw std::runtime_error(category.id + ": no kind " + kind->string());
    }
    category.title = member(value, "title", parse_sprite);
    category.stat_names = member(value, "stat_names", parse_sprite);
  }
  category.shown = value.find("icon") != nullptr;
  if (category.shown) {
    category.icon = member(value, "icon", parse_sprite);
    category.lit = member(value, "lit", parse_sprite);
  }
  category.from_x = number_of(value, "from_x");
  return category;
}

void parse_grid(const json::JsonValue &grid, ModuleShopScript &script) {
  script.columns = integer_of(grid, "columns");
  script.grid_rows = integer_of(grid, "rows");
  script.slide_seconds = seconds_of(grid, "slide_seconds");
  for (const json::JsonValue &cell : required(grid, "cells").array()) {
    script.categories.push_back(parse_category(cell));
  }
  if (script.columns <= 0 || script.grid_rows <= 0 ||
      static_cast<int>(script.categories.size()) != script.columns * script.grid_rows) {
    throw std::runtime_error("grid: needs columns x rows cells");
  }
}

ShopSidePanel parse_side(const json::JsonValue &value) {
  ShopSidePanel side;
  side.panels = member(value, "panels", parse_sprites);
  side.pictures = member(value, "pictures", parse_sprites);
  side.picture_seconds = seconds_of(value, "picture_seconds");
  side.fade_seconds = seconds_of(value, "fade_seconds");
  side.strip = member(value, "strip", parse_sprite);
  side.strip_speed = number_of(value, "strip_speed");
  side.strip_period = number_of(value, "strip_period");
  side.strip_clip = numbers<4>(required(value, "strip_clip"), "strip_clip");
  if (side.pictures.empty() || side.strip_period <= 0.0F) {
    throw std::runtime_error("side: needs pictures and a strip period");
  }
  return side;
}

void parse_mode(const json::JsonValue &mode, ModuleShopScript &script) {
  script.exit_to = mode.string_or("exit_to", script.exit_to);
  script.layers = member(mode, "layers", parse_sprites);
  if (mode.find("side") != nullptr) {
    script.side = member(mode, "side", parse_side);
  }
  script.list = member(mode, "list", parse_goods_list);
  script.category_help = member(mode, "category_help", strings);
  if (const json::JsonValue *messages = mode.find("messages")) {
    for (const auto &[id, lines] : messages->object()) {
      script.messages[id] = parse_lines(lines);
    }
  }
  script.sell_rate = mode.number_or("sell_rate", script.sell_rate);
}

void parse_backdrop(const json::JsonValue &document, ModuleShopScript &script) {
  if (document.find("plate") == nullptr) {
    return;
  }
  script.gears = member(document, "gears", parse_sprites);
  script.gear_spin = number_of(document, "gear_spin");
  const auto &plate = required(document, "plate");
  script.plate = member(plate, "sprite", parse_sprite);
  script.plate_from = number_of(plate, "from");
  script.plate_wait = required(plate, "wait").number();
  script.plate_speed = number_of(plate, "speed");
}

void parse_help(const json::JsonValue &document, ModuleShopScript &script) {
  script.help_style = member(document, "help", parse_ticker);
  if (const json::JsonValue *help = document.find("goods_help")) {
    for (const auto &[name, lines] : help->object()) {
      const auto kind = goods_kind_named(name);
      if (!kind) {
        throw std::runtime_error("goods_help: no kind " + name);
      }
      script.goods_help[static_cast<std::size_t>(*kind)] = strings(lines);
    }
  }
  if (const json::JsonValue *swatches = document.find("swatches")) {
    script.paint_swatches = member(*swatches, "paint", parse_sprites);
    script.emblem_swatches = member(*swatches, "emblem", parse_sprites);
  }
}

} // namespace

ModuleShopScript parse_module_shop(const json::JsonValue &document, const ModuleShopMode mode) {
  const char *mode_name = mode == ModuleShopMode::buy    ? "buy"
                          : mode == ModuleShopMode::sell ? "sell"
                                                         : "stock";
  const char *file = mode == ModuleShopMode::stock ? "stock_module.json: " : "module_shop.json: ";
  try {
    ModuleShopScript script;
    script.mode = mode;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    script.sort_name = buttons_from_json(required(buttons, "sort_name"));
    script.sort_price = buttons_from_json(required(buttons, "sort_price"));
    const auto &sounds = required(document, "sounds");
    script.jump_sound = sounds.string_or("jump", "");
    script.back_sound = sounds.string_or("back", "");
    script.trade_sound = sounds.string_or("trade", "");
    script.open_sound = sounds.string_or("open", "");
    script.sort_sound = sounds.string_or("sort", "");
    parse_backdrop(document, script);
    parse_grid(required(document, "grid"), script);
    const PartStatsStyle stats = member(document, "stats", parse_part_stats_style);
    script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
    parse_help(document, script);
    try {
      parse_mode(required(required(document, "modes"), mode_name), script);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string("modes.") + mode_name + ": " + error.what());
    }
    script.list.stats = stats;
    if (const auto *extras = document.find("extra_stats")) {
      script.extra_stats = parse_extra_stats_style(*extras);
    }
    if (script.category_help.size() != script.categories.size()) {
      throw std::runtime_error("category_help: needs one line a category");
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string(file) + error.what());
  }
}

ExtraStatsStyle parse_extra_stats_style(const json::JsonValue &value) {
  ExtraStatsStyle style;
  style.font = value.string_or("font", "");
  if (const auto *colour = value.find("colour")) {
    style.colour = parse_colour(colour->string());
  }
  if (const auto *shadow = value.find("shadow")) {
    style.shadow = parse_text_shadow(*shadow);
    style.shadow_blur = static_cast<float>(shadow->number_or("blur", 0.0));
  }
  style.label_left = number_of(value, "label_left");
  style.unit_left = number_of(value, "unit_left");
  style.unit_gap = static_cast<float>(value.number_or("unit_gap", 0.0));
  style.text_left = static_cast<float>(value.number_or("text_left", style.label_left));
  style.ink = numbers<2>(required(value, "ink"), "ink");
  style.line_rows = numbers<2>(required(value, "line_rows"), "line_rows");
  style.part_raise = static_cast<float>(value.number_or("part_raise", 0.0));
  style.part_step = static_cast<float>(value.number_or("part_step", 0.0));
  style.part_lines = static_cast<int>(value.number_or("part_lines", 0.0));
  if (value.find("page_arrows") != nullptr) {
    style.page_arrows = member(value, "page_arrows", parse_sprites);
  }
  return style;
}

ModuleShopScript parse_stock_module(const json::JsonValue &document, const ModuleShopScript &shop) {
  ModuleShopScript script = parse_module_shop(document, ModuleShopMode::stock);
  for (std::size_t kind = 0; kind < script.goods_help.size(); ++kind) {
    if (script.goods_help[kind].empty()) {
      script.goods_help[kind] = shop.goods_help[kind];
    }
  }
  if (!script.extra_stats) {
    script.extra_stats = shop.extra_stats;
  }
  if (script.paint_swatches.empty() && script.emblem_swatches.empty()) {
    script.paint_swatches = shop.paint_swatches;
    script.emblem_swatches = shop.emblem_swatches;
  }
  return script;
}

} // namespace phantomcrash
