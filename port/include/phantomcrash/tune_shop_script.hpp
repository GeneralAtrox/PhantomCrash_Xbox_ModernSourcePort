#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/level_control.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/shop_list_style.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// An icon of CUSTOMIZE's row (0x0324bd): the parts it lists (none for
// EXIT), its icon sliding in from `from_x`, the list's title and the stats
// panel's labels for them, and its help line.
struct TuneItem {
  std::string id;
  std::optional<GoodsKind> kind;
  Sprite icon; // at its place once slid in
  Sprite lit;
  float from_x{};
  Sprite title;
  Sprite stat_names;
  std::string help;
};

// A drawing scrolling down behind the screen (0x02efe6), a second copy a
// `period` above it.
struct TuneStrip {
  Sprite sprite;
  float speed{}; // units a second
  float period{};
  float alpha{1.0F};
};

// The level meter (0x02f3fc): the old level and the new, each with its
// word (LIGHT, NORMAL or HEAVY), the marker at the new level and the price.
struct TuneMeter {
  PopWindow window;
  Sprite cursor; // the list's cursor, dark, while the meter is up
  std::vector<Sprite> confirm_hint;
  std::vector<Sprite> cancel_hint;
  std::array<Sprite, 3> signs; // LIGHT (below 0), NORMAL (0), HEAVY (above 0)
  std::array<float, 2> old_sign_at{};
  std::array<float, 2> new_sign_at{};
  NumberStyle old_level; // the level's size, without its sign
  NumberStyle new_level;
  Sprite marker; // at level 0; `marker_step` along a level
  float marker_step{1.0F};
  NumberStyle price;
  std::uint32_t poor_colour{0xff0000};
  int layer{};
  std::string help;
};

// The robot a fitted part is in, with the part at the level chosen
// (0x0303d7): its weight and its legs' capacity, in tonnes.
struct TuneLoad {
  NumberStyle weight;
  NumberStyle capacity;
  std::vector<Sprite> labels;
  int layer{};
};

// PlusTech's CUSTOMIZE (data/ui/customize.json): the tune-up of the
// player's bodies, legs and weapons.
struct TuneShopScript : TownBasics {
  Buttons left{}, right{}, up{}, down{};
  Buttons sort_name{}, sort_price{};
  std::string jump_sound;
  std::string back_sound;
  std::string trade_sound;
  std::string sort_sound;

  Sprite plate;
  std::vector<TuneStrip> strips;
  std::vector<TuneItem> items; // left to right, EXIT last
  double slide_seconds{};
  ShopGoodsList list;
  TuneLoad load;
  TuneMeter meter;
  LevelControlStyle level_control;
  std::uint32_t increase_colour{0x80dfff}, decrease_colour{0xff6666};
  float comparison_scale{0.7F};
  float comparison_width{88.0F};
  int level_min{-99};
  int level_max{99};

  std::array<float, 2> message_text_at{};
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
  // The parts' descriptions, by kind then record.
  std::array<std::vector<std::string>, goods_kind_count> goods_help;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] TuneShopScript parse_tune_shop(const json::JsonValue &document);

// What a tune-up costs (0x02f37e): the difference between the part's
// prices at the two levels, or, from one side of level 0 to the other, the
// way to level 0 and on from it.
[[nodiscard]] std::int64_t tune_price(const PartTables &parts, const GarageTables &garage,
                                      const Goods &goods, int level);

} // namespace phantomcrash
