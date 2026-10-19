#pragma once

#include "phantomcrash/extra_stats_style.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/shop_list_style.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_text.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Whether the parts shop sells the day's goods or buys the player's own;
// or the garage's STOCK MODULE (scene 38), which only shows the player's.
enum class ModuleShopMode { buy, sell, stock };

// A cell of the category grid (0x1f61e): the goods it opens (none for
// EXIT), its icon sliding in from `from_x` and the lit one over it; a
// cell without an icon shows nothing, even chosen.
struct ShopCategory {
  std::string id;
  std::optional<GoodsKind> kind;
  bool shown{true};
  Sprite icon;
  Sprite lit;
  float from_x{};
  Sprite title;      // BODY MODULE and so on, over the list
  Sprite stat_names; // the stats panel's labels
};

// The panel beside the grid (0x1f1f5): pictures taking turns, fading into
// each other, and a strip rolling up through its window.
struct ShopSidePanel {
  std::vector<Sprite> panels; // drawn as they are
  std::vector<Sprite> pictures;
  double picture_seconds{};
  double fade_seconds{};
  Sprite strip;
  float strip_speed{}; // units a second, upwards
  float strip_period{};
  std::array<float, 4> strip_clip{};
};

// Wild Machines' BUY MODULE and SELL MODULE (data/ui/module_shop.json),
// and STOCK MODULE (data/ui/stock_module.json).
struct ModuleShopScript : TownBasics {
  ModuleShopMode mode{ModuleShopMode::buy};
  Buttons left{}, right{}, up{}, down{};
  Buttons sort_name{}, sort_price{};
  std::string jump_sound;
  std::string back_sound;
  std::string trade_sound;
  std::string open_sound;
  std::string sort_sound;

  // The Wild Machines backdrop: the gears turning and the plate sliding in
  // once `plate_wait` is over. STOCK MODULE has neither.
  std::vector<Sprite> gears;
  float gear_spin{}; // degrees a second
  Sprite plate;
  float plate_from{};
  double plate_wait{};
  float plate_speed{}; // units a second

  int columns{4};
  int grid_rows{2};
  std::vector<ShopCategory> categories; // column by column, top first
  double slide_seconds{};               // the icons' slide in
  ShopSidePanel side;                   // no pictures: no panel
  ShopGoodsList list;

  std::array<float, 2> message_text_at{};
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
  std::vector<std::string> category_help; // by category
  // The goods' descriptions, by kind then record; paints and emblems have one.
  std::array<std::vector<std::string>, goods_kind_count> goods_help;
  // The paints' and emblems' pictures, by their entry's index.
  std::vector<Sprite> paint_swatches;
  std::vector<Sprite> emblem_swatches;
  double sell_rate{0.5};
  std::optional<ExtraStatsStyle> extra_stats; // none: no extra lines
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] ModuleShopScript parse_module_shop(const json::JsonValue &document,
                                                 ModuleShopMode mode);

// STOCK MODULE's script (data/ui/stock_module.json, its one mode "stock"):
// without goods descriptions or swatches of its own, it takes the parts
// shop's.
[[nodiscard]] ModuleShopScript parse_stock_module(const json::JsonValue &document,
                                                  const ModuleShopScript &shop);

} // namespace phantomcrash
