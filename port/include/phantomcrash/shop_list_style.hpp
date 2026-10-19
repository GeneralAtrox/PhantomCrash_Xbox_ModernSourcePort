#pragma once

#include "phantomcrash/json.hpp"
#include "phantomcrash/hangar_labels.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <optional>
#include <cstdint>
#include <vector>

namespace phantomcrash {

// A window that pops open (0x03c6fe with 0xbe91c): it widens while thin,
// then heightens, over `seconds`.
struct PopWindow {
  Sprite sprite; // centred on its place, its `size` when it has one
  double seconds{};
  float thin{0.05F};
  // A window built of a stretched middle (`sprite`) and caps above and
  // below it, opening with it (0x0177b8).
  std::optional<std::array<Sprite, 2>> caps;
  // The art's rows kept whole above and below when the window grows for
  // extra stat lines (the title and divider above, the corners below); the
  // window keeps its size without them.
  std::optional<std::array<float, 2>> grow_caps;
};

// The goods list (0x1f8a3 buy, 0x21cd2 sell, 0x303d7 tune-up) and the
// chosen goods' details in the windows beside it. The module, price and
// swatch members are read only where the data has them.
struct ShopGoodsList {
  PopWindow window;
  std::array<PopWindow, 2> details;        // for parts, paints and emblems
  std::array<PopWindow, 2> module_details; // taller, for modules
  Sprite shade;                            // dimmed over the grid
  float shade_alpha{1.0F};
  Sprite cursor; // at the first row
  int rows{4};
  float row_step{};
  std::array<float, 2> name_at{};
  float name_width{}; // optional column width, bounded by the panel and row badges
  std::uint32_t name_colour{0xffffff};
  int name_layer{};
  std::vector<Sprite> maker_icons; // by maker, at the first row
  std::array<Sprite, 2> scroll;    // more above, more below
  std::vector<Sprite> cancel_hint;
  std::vector<Sprite> price_label; // plain, then lit
  float module_rise{};             // the price and its label sit higher for modules
  NumberStyle price;
  float stats_top{};
  float module_stats_top{};
  float module_stats_right{}; // a module's stats' right edge, when not the stats' own
  PartStatsStyle stats;
  Sprite module_names; // a module's labels
  Sprite laser_names;  // those of a module cutting lasers
  Sprite swatch_frame; // around a paint's or emblem's picture
  std::array<float, 2> swatch_at{};
  int swatch_layer{};
  std::vector<HangarLabel> hangar_badges; // the player's: the hangar a part is fitted in
  // ASSEMBLE's: the plate under each hangar number (alone for a loose part)
  // and the plates under every row, shown or not.
  std::optional<Sprite> badge_base;
  std::optional<Sprite> row_plate;
  Sprite in_use; // the player's: a paint or emblem a hangar wears
  // The player's arm and backpack weapons: which of the pair of slots one
  // is fitted in (0x030228's R and L); none when empty.
  std::vector<Sprite> side_marks;
  std::vector<Sprite> lit_side_marks; // ASSEMBLE's: over them, for the hangar assembled
};

// {"sprite", "seconds", "thin"}. Throws std::runtime_error naming the member at fault.
[[nodiscard]] PopWindow parse_pop_window(const json::JsonValue &value);
// A goods list's look, without its stats style (the caller's). Throws
// std::runtime_error naming the member at fault.
[[nodiscard]] ShopGoodsList parse_goods_list(const json::JsonValue &value);

// Reserve the right-hand row badges as well as the window's edge.
[[nodiscard]] float goods_name_width(const ShopGoodsList &list, float padding);

} // namespace phantomcrash
