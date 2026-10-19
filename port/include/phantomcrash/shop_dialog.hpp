#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// One icon of a row a shop's cursor moves along: its sprite and its lit
// sprite, what it does ("spot", "catalogue", "exit"; "buy", "preview",
// "cancel") and, for a catalogue, which one.
struct ShopIcon {
  std::string id;
  int category{-1};
  Sprite sprite;
  Sprite lit;
};

// [{"id", "category"?, "sprite", "lit"}, ...]; throws when empty.
[[nodiscard]] ShopIcon parse_shop_icon(const json::JsonValue &value);
[[nodiscard]] std::vector<ShopIcon> parse_shop_icons(const json::JsonValue &value);

// Lines of text one under the other (a dealer's corporation profile).
struct TextBlock {
  std::array<float, 2> at{}; // the first line's top-left
  float line_step{};
  std::uint32_t colour{0xffffff};
  int layer{};
  std::vector<std::string> lines;
};

[[nodiscard]] TextBlock parse_text_block(const json::JsonValue &value);

// One thing a shop's clerk says: the face shown beside it and its lines.
struct ShopLine {
  Sprite face;
  std::vector<std::string> lines;
};

// A shop's dialog window (Sonic Amp's 0x2a714): the window, the sprites
// over it (the clerk's name plate), the clerk's face for what is said and
// up to three lines of text, one under the other. "{price}" in a line is
// the shop's price.
struct ShopDialog {
  Sprite window;
  std::vector<Sprite> decorations;
  std::array<float, 2> text_at{}; // the first line's top-left
  float line_step{};
  std::uint32_t text_colour{0xffffff};
  int text_layer{};
  std::map<std::string, ShopLine> says; // by what it is said for
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] ShopDialog parse_shop_dialog(const json::JsonValue &value);

// A line with "{price}" replaced by `price`.
[[nodiscard]] std::string shop_text(std::string line, std::int64_t price);

} // namespace phantomcrash
