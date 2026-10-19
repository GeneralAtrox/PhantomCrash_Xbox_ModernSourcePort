#include "phantomcrash/sort_hints.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace phantomcrash {

std::vector<Sprite> sort_hint_sprites(const SortHintStyle &style, const int key, const bool fresh) {
  std::vector<Sprite> sprites;
  for (int index = 0; index < static_cast<int>(style.keys.size()); ++index) {
    const SortHintKey &hint = style.keys[static_cast<std::size_t>(index)];
    const bool chosen = index == key;
    // Only the chosen key's arrow turns; the other keeps pointing up.
    Sprite arrow = chosen && !fresh ? style.down : style.up;
    arrow.position = hint.arrow_at;
    for (Sprite sprite : {hint.button, hint.label, arrow}) {
      sprite.colour = chosen ? style.lit : style.dim;
      sprites.push_back(std::move(sprite));
    }
  }
  return sprites;
}

SortHintStyle parse_sort_hints(const json::JsonValue &value) {
  SortHintStyle style;
  const auto &keys = json::required(value, "keys").array();
  if (keys.size() != style.keys.size()) {
    throw std::runtime_error("keys: needs two, by name and by price");
  }
  for (std::size_t index = 0; index < keys.size(); ++index) {
    SortHintKey &hint = style.keys[index];
    hint.button = json::member(keys[index], "button", parse_sprite);
    hint.label = json::member(keys[index], "label", parse_sprite);
    hint.arrow_at = json::numbers<2>(json::required(keys[index], "arrow_at"), "arrow_at");
  }
  style.up = json::member(value, "up", parse_sprite);
  style.down = json::member(value, "down", parse_sprite);
  style.lit = parse_colour(json::required(value, "lit").string());
  style.dim = parse_colour(json::required(value, "dim").string());
  return style;
}

} // namespace phantomcrash
