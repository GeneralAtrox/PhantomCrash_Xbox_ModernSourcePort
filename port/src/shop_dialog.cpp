#include "phantomcrash/shop_dialog.hpp"
#include "phantomcrash/money.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <stdexcept>
#include <string_view>
#include <utility>

namespace phantomcrash {

ShopIcon parse_shop_icon(const json::JsonValue &value) {
  ShopIcon icon;
  icon.id = json::required(value, "id").string();
  icon.category = static_cast<int>(value.number_or("category", -1));
  icon.sprite = json::member(value, "sprite", parse_sprite);
  icon.lit = json::member(value, "lit", parse_sprite);
  return icon;
}

std::vector<ShopIcon> parse_shop_icons(const json::JsonValue &value) {
  std::vector<ShopIcon> icons;
  for (const auto &entry : value.array()) {
    icons.push_back(parse_shop_icon(entry));
  }
  if (icons.empty()) {
    throw std::runtime_error("a row needs icons");
  }
  return icons;
}

TextBlock parse_text_block(const json::JsonValue &value) {
  TextBlock block;
  block.at = json::numbers<2>(json::required(value, "at"), "at");
  block.line_step = static_cast<float>(json::required(value, "line_step").number());
  block.colour = parse_colour(value.string_or("colour", "#ffffff"));
  block.layer = static_cast<int>(json::required(value, "layer").number());
  block.lines = parse_lines(json::required(value, "lines"));
  return block;
}

ShopDialog parse_shop_dialog(const json::JsonValue &value) {
  ShopDialog dialog;
  dialog.window = json::member(value, "window", parse_sprite);
  if (value.find("decorations") != nullptr) {
    dialog.decorations = json::member(value, "decorations", parse_sprites);
  }
  dialog.text_at = json::numbers<2>(json::required(value, "text_at"), "text_at");
  dialog.line_step = static_cast<float>(json::required(value, "line_step").number());
  dialog.text_colour = parse_colour(value.string_or("text_colour", "#ffffff"));
  dialog.text_layer = static_cast<int>(json::required(value, "text_layer").number());
  for (const auto &[name, entry] : json::required(value, "says").object()) {
    ShopLine line;
    line.face = json::member(entry, "face", parse_sprite);
    line.lines = parse_lines(json::required(entry, "lines"));
    dialog.says[name] = std::move(line);
  }
  if (dialog.says.empty()) {
    throw std::runtime_error("the dialog needs something to say");
  }
  return dialog;
}

std::string shop_text(std::string line, const std::int64_t price) {
  static constexpr std::string_view key = "{price}";
  if (const auto at = line.find(key); at != std::string::npos) {
    line.replace(at, key.size(), std::to_string(shown_ny(price)));
  }
  return line;
}

} // namespace phantomcrash
