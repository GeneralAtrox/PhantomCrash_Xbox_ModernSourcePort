#pragma once

#include "phantomcrash/extra_stats_style.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_totals.hpp"
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

// What a cell of ASSEMBLE's grid does (0x1dae08's states).
enum class AssembleItem {
  body,
  legs,
  arm,
  back,
  chip,
  module,
  paint,
  sticker,
  name,
  dismantle,
  spec,
  exit,
};

// ARM's and BACK's choice of side (0x017af0): the pair rises `rise` out of
// the cell over the first half of `seconds`, then parts `spread` each way.
// Side 0 is the right-hand weapon, 1 the left.
struct AssembleSides {
  std::array<Sprite, 2> plain; // at the pair's start, before it moves
  std::array<Sprite, 2> lit;   // over the chosen side
  float rise{};
  float spread{};
  double seconds{};
  std::array<std::string, 2> help;
};

// A cell of the grid, its icon sliding in from `from_x`; for a list, its
// title (one a side for ARM and BACK), the details' labels (a module
// cutting lasers has the second) and where its stats and totals go.
struct AssembleCell {
  AssembleItem item{AssembleItem::exit};
  std::optional<GoodsKind> kind;
  Sprite icon;
  float from_x{};
  Sprite lit;
  std::vector<Sprite> titles;
  std::vector<Sprite> labels;
  float stats_right{};
  std::vector<TotalRow> totals;
  float totals_top{};
  float totals_right{};
  std::optional<AssembleSides> sides;
  std::string help;
};

// The hangar's panel (0x01877e): its number, its sockets used and owned,
// its Scoobee's name, what it wears in the three boxes and what keeps it
// from a battle.
struct AssemblePanel {
  std::vector<Sprite> numbers; // by hangar
  std::array<float, 2> sockets_used{};
  std::array<float, 2> sockets_total{};
  std::uint32_t sockets_colour{0xffffff};
  int sockets_layer{};
  std::array<float, 2> name_at{};
  std::uint32_t name_colour{0xffffff};
  int name_layer{};
  int name_glyphs{9};
  std::array<Sprite, 3> no_equip; // CHIP, COLOR and STICKER's empty boxes
  std::array<float, 2> chip_at{};
  std::array<float, 2> paint_at{};
  std::array<float, 2> emblem_at{};
  int equip_layer{};
  Sprite incomplete;
  Sprite underkill;
  Sprite overweight;
  std::array<float, 2> pair_x{}; // Underkill's and Overweight's places when both show
};

// The whole robot's lines in the details (0x019489, 0x0179ed): each a
// `step` below the last, right-aligned, the up or down arrow `arrow_dx`
// beside a value the part changes; O.C's active time `pair_gap` left of
// its charge time.
struct AssembleTotalsStyle {
  float step{};
  int layer{};
  std::uint32_t colour{0xffffff};
  Sprite up;
  Sprite down;
  float arrow_dx{};
  float arrow_dy{};
  float pair_gap{};
  std::array<float, 2> chip_name_at{};
  // A value below 0 shows only this minus sign (sprite 0xe78, 0x019018),
  // `negative_dx`, `negative_dy` from the value's right edge.
  std::optional<Sprite> negative;
  float negative_dx{};
  float negative_dy{};
  // Opt-in (port addition): a value the part raises or lowers, and its
  // arrow, take these colours; the original draws both in `colour`.
  std::optional<std::uint32_t> up_colour;
  std::optional<std::uint32_t> down_colour;
};

// A line's details (0x019d24): the box fading in over `fade_seconds`, the
// EQUIPMENT tag over a part fitted here, then the stats and totals.
struct AssembleDetails {
  Sprite box;
  float box_alpha{1.0F};
  double fade_seconds{};
  Sprite equipped;
  AssembleTotalsStyle totals;
  // 0x03cbc3: the CHIP's face, paint or emblem picture of the part shown.
  std::array<float, 2> picture_at{};
  int picture_layer{};
  // A part's second page (a port addition): its extra stat lines
  // (part_stats.hpp "extras" and "details") lettered over the labels' art
  // below its title, in place of its lines and the totals; left and right
  // turn to it. Places are in source pixels of the cell's labels' art.
  std::optional<ExtraStatsStyle> extra_stats;
  float extra_title_rows{}; // the art kept above the lines: its title
  float extra_rows_top{};   // the first line's top
};

// The hangar's ASSEMBLE (data/ui/assemble.json).
struct AssembleScript : TownBasics {
  Buttons left{}, right{}, up{}, down{};
  Buttons sort_name{}, sort_price{};
  // The hide button shows or hides the equipment the Scoobee cannot fit; LB and RB
  // turn the list's page, LT and RT go to its top and bottom line.
  Buttons hide_incompatible{};
  Buttons page_back{}, page_next{}, page_top{}, page_bottom{};
  std::string row_sound;
  std::string back_sound;
  std::string exit_sound;
  std::string list_row_sound;
  std::string sort_sound;
  std::string fit_sound;
  std::string question_sound;
  std::string dismantle_sound;
  std::string no_sound;
  std::string hide_sound;

  int columns{6};
  int grid_rows{2};
  double slide_seconds{};
  std::vector<AssembleCell> cells; // column by column, top first
  Sprite disabled;                 // over a cell that cannot be chosen, at its place
  AssemblePanel panel;
  ShopGoodsList list;
  std::uint32_t unusable_colour{0x7f7f7f};
  std::uint32_t fitted_colour{0xffd300};
  std::uint32_t badge_colour{0x808080};
  AssembleDetails details;

  std::map<AssembleItem, std::string> routes; // NAME's and SPEC's screens
  // Only parts of the robot's maker fit (0x0186e9); false mixes makers.
  bool same_maker{true};
  std::vector<int> unpaintable_makers; // robots that cannot be painted
  float overweight_margin{};

  std::array<float, 2> message_text_at{};
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
  std::string list_help;
  std::string details_help;
  std::string hide_on_help;  // shown when the equipment is hidden
  std::string hide_off_help; // shown when X shows all of it
  // The paints' and emblems' pictures, by their entry's index: the parts
  // shop's (module_shop.json), handed over by the caller.
  std::vector<Sprite> paint_swatches;
  std::vector<Sprite> emblem_swatches;

  [[nodiscard]] const AssembleCell *cell_for(AssembleItem item) const;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] AssembleScript parse_assemble(const json::JsonValue &document);

} // namespace phantomcrash
