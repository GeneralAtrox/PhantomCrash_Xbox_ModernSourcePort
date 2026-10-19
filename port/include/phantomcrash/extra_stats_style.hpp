#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_text.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Extra stat lines (part_stats.hpp) below a module's printed ones, each a
// stats step lower, the details windows growing a step for each. Places are
// in source pixels of the labels' art (module_names), from its left and
// from the top of a line.
struct ExtraStatsStyle {
  std::string font; // a data/ui/sprite_text.json display font; the game's font without it
  std::uint32_t colour{0xffffff};
  std::optional<TextShadow> shadow;
  float shadow_blur{};
  float label_left{};
  float unit_left{};
  // Above 0, each unit starts this far right of the values' right edge
  // instead of at unit_left (ASSEMBLE's cells end their values apart).
  float unit_gap{};
  // A lettered value (a weapon's type) fits between here and the values'
  // right edge, against the right.
  float text_left{};
  std::array<float, 2> ink{};       // the lettering's top and bottom in a line
  std::array<float, 2> line_rows{}; // the separator's rows in the labels' art
  // A part's details with extra lines (a leg's) fit above the screen's
  // CURRENT MUSIC plate: the window, its picture and lines move up by
  // part_raise and the lines close up to part_step apart, the labels' art
  // squeezed with them. 0 leaves them as the original's.
  float part_raise{};
  float part_step{};
  // The most lines that fit at part_step; more (a weapon's second page)
  // close up further. 0 sets no limit.
  int part_lines{};
  // Shown beside the details while the goods have a second page (a port
  // addition): left and right, or a click on the details, turn to it.
  std::vector<Sprite> page_arrows;
};

// Read with the parts shop's script (module_shop_script.cpp).
[[nodiscard]] ExtraStatsStyle parse_extra_stats_style(const json::JsonValue &value);

} // namespace phantomcrash
