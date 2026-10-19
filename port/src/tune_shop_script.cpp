// Parsing of data/ui/customize.json, and what a tune-up costs.

#include "phantomcrash/tune_shop_script.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <algorithm>
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

std::vector<std::string> strings(const json::JsonValue &value) {
  std::vector<std::string> out;
  for (const json::JsonValue &item : value.array()) {
    out.push_back(item.string());
  }
  return out;
}

std::array<Sprite, 3> three_sprites(const json::JsonValue &value) {
  const auto sprites = parse_sprites(value);
  if (sprites.size() != 3) {
    throw std::runtime_error("needs 3 sprites");
  }
  return {sprites[0], sprites[1], sprites[2]};
}

TuneItem parse_item(const json::JsonValue &value) {
  TuneItem item;
  item.id = required(value, "id").string();
  if (const json::JsonValue *kind = value.find("kind")) {
    item.kind = goods_kind_named(kind->string());
    if (!item.kind || stock_kind(*item.kind) == std::nullopt) {
      throw std::runtime_error(item.id + ": no part kind " + kind->string());
    }
    item.title = member(value, "title", parse_sprite);
    item.stat_names = member(value, "stat_names", parse_sprite);
  }
  item.icon = member(value, "icon", parse_sprite);
  item.lit = member(value, "lit", parse_sprite);
  item.from_x = number_of(value, "from_x");
  item.help = required(value, "help").string();
  return item;
}

TuneStrip parse_strip(const json::JsonValue &value) {
  TuneStrip strip;
  strip.sprite = member(value, "sprite", parse_sprite);
  strip.speed = number_of(value, "speed");
  strip.period = number_of(value, "period");
  strip.alpha = static_cast<float>(value.number_or("alpha", 1.0));
  if (strip.period <= 0.0F) {
    throw std::runtime_error("strips: period must be above 0");
  }
  return strip;
}

TuneLoad parse_load(const json::JsonValue &value) {
  TuneLoad load;
  load.weight = member(value, "weight", parse_number_style);
  load.capacity = member(value, "capacity", parse_number_style);
  load.labels = member(value, "labels", parse_sprites);
  load.layer = integer_of(value, "layer");
  return load;
}

TuneMeter parse_meter(const json::JsonValue &value) {
  TuneMeter meter;
  meter.window = member(value, "window", parse_pop_window);
  meter.cursor = member(value, "cursor", parse_sprite);
  meter.cancel_hint = member(value, "cancel_hint", parse_sprites);
  if (const auto *hint = value.find("confirm_hint")) {
    meter.confirm_hint = parse_sprites(*hint);
  }
  meter.signs = member(value, "signs", three_sprites);
  meter.old_sign_at = numbers<2>(required(value, "old_sign_at"), "old_sign_at");
  meter.new_sign_at = numbers<2>(required(value, "new_sign_at"), "new_sign_at");
  meter.old_level = member(value, "old_level", parse_number_style);
  meter.new_level = member(value, "new_level", parse_number_style);
  meter.marker = member(value, "marker", parse_sprite);
  meter.marker_step = number_of(value, "marker_step");
  meter.price = member(value, "price", parse_number_style);
  meter.poor_colour = parse_colour(required(value, "poor_colour").string());
  meter.layer = integer_of(value, "layer");
  meter.help = required(value, "help").string();
  return meter;
}

void parse_items(const json::JsonValue &value, TuneShopScript &script) {
  script.slide_seconds = required(value, "slide_seconds").number();
  for (const json::JsonValue &item : required(value, "icons").array()) {
    script.items.push_back(parse_item(item));
  }
  if (script.slide_seconds <= 0.0 || script.items.empty() || script.items.back().kind) {
    throw std::runtime_error("items: needs a slide time and EXIT last");
  }
}

void parse_texts(const json::JsonValue &document, TuneShopScript &script) {
  script.message_text_at = numbers<2>(required(document, "message_text_at"), "message_text_at");
  for (const auto &[id, lines] : required(document, "messages").object()) {
    script.messages[id] = parse_lines(lines);
  }
  script.help_style = member(document, "help", parse_ticker);
  for (const auto &[name, lines] : required(document, "goods_help").object()) {
    const auto kind = goods_kind_named(name);
    if (!kind) {
      throw std::runtime_error("goods_help: no kind " + name);
    }
    script.goods_help[static_cast<std::size_t>(*kind)] = strings(lines);
  }
}

} // namespace

TuneShopScript parse_tune_shop(const json::JsonValue &document) {
  try {
    TuneShopScript script;
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
    script.sort_sound = sounds.string_or("sort", "");
    script.plate = member(document, "plate", parse_sprite);
    for (const json::JsonValue &strip : required(document, "strips").array()) {
      script.strips.push_back(parse_strip(strip));
    }
    parse_items(required(document, "items"), script);
    script.list = member(document, "list", parse_goods_list);
    script.list.stats = member(document, "stats", parse_part_stats_style);
    script.load = member(document, "load", parse_load);
    script.meter = member(document, "meter", parse_meter);
    if (const auto *control = document.find("level_control")) {
      script.level_control = parse_level_control(*control);
    }
    if (const auto *comparison = document.find("comparison")) {
      script.increase_colour = parse_colour(comparison->string_or("increase", "#80dfff"));
      script.decrease_colour = parse_colour(comparison->string_or("decrease", "#ff6666"));
      script.comparison_scale = static_cast<float>(comparison->number_or("scale", 0.7));
      script.comparison_width = static_cast<float>(comparison->number_or("width", 88));
    }
    const auto levels = numbers<2>(required(document, "levels"), "levels");
    script.level_min = static_cast<int>(levels[0]);
    script.level_max = static_cast<int>(levels[1]);
    if (script.level_min > 0 || script.level_max < 0) {
      throw std::runtime_error("levels: must hold level 0");
    }
    parse_texts(document, script);
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("customize.json: ") + error.what());
  }
}

std::int64_t tune_price(const PartTables &parts, const GarageTables &garage, const Goods &goods,
                        const int level) {
  const std::int64_t from = goods_price(parts, garage, goods);
  const std::int64_t to = goods_price(parts, garage, {goods.kind, goods.id, level});
  if (goods.level * level < 0) {
    const std::int64_t zero = goods_price(parts, garage, {goods.kind, goods.id, 0});
    return to + from - 2 * zero;
  }
  return to > from ? to - from : from - to;
}

} // namespace phantomcrash
