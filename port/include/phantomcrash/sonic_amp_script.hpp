#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/shop_dialog.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

// The chart on the television (0x29de7, 0x2b1ec): each of the ten tracks
// in turn from No. 10 to No. 1, for `seconds` each, its jacket fading in
// and out, a segment of the ring lit per rank shown so far, and the rank
// and title running along the left with the artist below.
struct SonicAmpChart {
  double seconds{};
  double fade_in{};
  double fade_out{};
  Sprite ring;
  std::vector<Sprite> static_frames; // alternate, each shown half a period
  double static_period{};
  Sprite tint;
  float tint_alpha{1.0F};
  Sprite scan; // rolls scan_distance down over scan_seconds, then again
  float scan_distance{};
  double scan_seconds{};
  float scan_alpha{1.0F};
  std::array<float, 2> jacket_centre{};
  float jacket_scale{1.0F};
  int jacket_layer{};
  std::vector<Sprite> segments; // No. 1 to No. 10
  std::uint32_t segment_colour{0xffffff};
  Sprite segment_lit; // the colour and animation of the rank shown
  TickerStyle rank_style;
  std::string rank_format; // "{rank}" (two digits) and "{title}"
  TickerStyle artist_style;
};

// The track grids (0x2ad11, 0x2a9f0): SPOT, the chart's ten tracks after
// its badge, and the catalogues of every track, a page at a time; each
// slot has a shadow, the jacket and SOLD OUT! once bought.
struct SonicAmpGrid {
  std::array<float, 2> step{};
  Sprite shadow;
  int jacket_layer{};
  Sprite cursor; // at a slot's top-left plus cursor_offset
  std::array<float, 2> cursor_offset{};
  Sprite sold_out;
  Sprite band; // stretched behind the grid, fading in over band_seconds
  double band_seconds{};
  std::array<float, 2> spot_origin{};
  int spot_columns{};
  Sprite spot_badge;
  std::array<float, 2> catalogue_origin{};
  int catalogue_columns{};
  int catalogue_rows{};
  std::vector<int> category_pages; // pages of each catalogue, in order
  int catalogue_tracks{};          // the credits sold, from the first
  std::vector<int> catalogue_ids;  // optional explicit credits, including mod songs
  float short_page_offset{};       // a page with fewer rows sits this lower
  Sprite page_box;
  Sprite page_slash;
  NumberStyle page_number; // `right` places the page, total_at the count
  std::array<float, 2> total_at{};
  std::vector<Sprite> page_arrows;
  std::vector<Sprite> hints;
};

// data/ui/sonic_amp.json (original scene 20).
struct SonicAmpScript : TownBasics {
  Buttons left{};
  Buttons right{};
  Buttons up{};
  Buttons down{};
  Buttons page_back{};
  Buttons page_forward{};
  std::string jump_sound;
  std::string cancel_sound;
  std::string buy_sound;
  std::string leave_sound;
  std::string open_sound; // as the shop slides in (0x2b4aa)
  std::int64_t price{};

  TickerStyle help_style;
  HelpLine help;
  std::string track_format; // "{title}", "{artist}"
  float track_trail{};

  std::vector<ShopIcon> items;
  std::vector<Sprite> arrows; // bouncing over the catalogues
  float arrow_drop{};
  double arrow_seconds{};
  Sprite watch; // "Be sure to also watch these!", pulsing
  SonicAmpChart chart;
  SonicAmpGrid grid;

  std::vector<ShopIcon> actions; // BUY, PREVIEW, CANCEL
  int first_action{};
  ShopDialog dialog;
  double message_seconds{};

  std::vector<Sprite> preview_layers; // over the screen while previewing
  std::array<float, 2> preview_jacket{};
  int preview_layer{};
  std::array<float, 2> preview_name{};
  std::array<float, 2> preview_link{};
  std::vector<Sprite> preview_hints;
  double preview_min_seconds{};
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] SonicAmpScript parse_sonic_amp(const json::JsonValue &document);

} // namespace phantomcrash
