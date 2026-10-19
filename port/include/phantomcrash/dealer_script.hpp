#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/shop_dialog.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sv_purchase.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// What one dealer shows (0x1fd708 to 0x1fddb8, by the dealer index
// 0x0393de sets): its panorama, bands, logo and maker badge, the profile's
// panel and text, the SV name plates by picture and the menu icons by the
// SV's legs record.
struct DealerLook {
  int index{};                  // which of a calendar day's dealer lineups is this dealer's
  std::vector<Sprite> panorama; // scrolled left, one after the other, wrapping
  Sprite top;
  Sprite bottom;
  Sprite logo;
  Sprite badge; // beside the SV chosen (0x1fddb8)
  Sprite profile_panel;
  TextBlock profile;
  std::vector<Sprite> pictures;       // by a lineup's picture
  std::map<int, ShopIcon> legs_icons; // by the SV's legs record
  ShopIcon default_icon;              // for legs the dealer has no icon for
};

// The logo (0x39e46, 0x393ee): large and centred while the profile or EXIT
// is chosen, small at the top left over an SV, moving between the two with
// a trail of copies behind it.
struct DealerLogo {
  std::array<float, 2> small_at{};
  std::array<float, 2> large_at{};
  float large_scale{1.0F};
  double move_seconds{};
  int trail{};            // copies, the newest first
  float trail_fade{1.0F}; // each copy's opacity is the one before's times this
  float opening_alpha{0.5F};
};

// data/ui/dealer.json (original scene 34): the three SV dealers.
struct DealerScript : TownBasics {
  Buttons left{};
  Buttons right{};
  std::string jump_sound;
  std::string cancel_sound;
  std::string leave_sound;

  double band_seconds{};    // the band slides in, then
  double menu_seconds{};    // the menu's icons slide in
  double picture_seconds{}; // an SV's name plate fades over the last
  float scroll_speed{};     // the panorama, units a second
  std::uint32_t panorama_colour{0xffffff};
  Sprite band;
  float band_from{};
  Sprite line;
  Sprite welcome;  // as the menu opens
  Sprite farewell; // as it closes
  DealerLogo logo;

  // profile, the three SVs and EXIT; the SVs' sprites come from the look.
  std::vector<ShopIcon> items;
  std::vector<float> item_from; // each icon's x as it starts sliding in
  float item_slide{};           // how far the icons slide as the menu opens
  std::array<float, 2> picture_at{};
  Sprite price_label;
  NumberStyle price;
  float price_markup{1.0F}; // an SV costs its parts' total times this (0x039ca8)
  PopupGrowth profile_growth;

  TickerStyle help_style;
  float help_trail{};
  std::map<std::string, std::string> help; // by item id

  SvPurchaseScript purchase; // the trade window, the hangars and the notices

  std::map<std::string, DealerLook> dealers; // by screen name
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] DealerScript parse_dealer(const json::JsonValue &document);

} // namespace phantomcrash
