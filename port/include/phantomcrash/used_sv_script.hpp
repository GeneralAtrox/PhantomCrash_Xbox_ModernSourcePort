#pragma once

#include "phantomcrash/glyph_font.hpp"
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

// data/ui/used_sv.json: Wild Machines' used SVs (original scene 15, state
// 3: setup 0x27607, input 0x27910, draw 0x230f4 and 0x2022b).
struct UsedSvScript : TownBasics {
  SvPurchaseScript purchase; // the trade window, the hangars and the notices

  // The opening: the strip fades in, then the plate slides in (with
  // open_sound) and the SVs take input; their icons slide in meanwhile.
  double strip_seconds{};
  double plate_seconds{};
  float plate_from{};
  std::string open_sound;
  Sprite strip;
  Sprite plate;
  std::vector<Sprite> gears; // turning together
  float gear_spin{};         // degrees a second

  // Six SV slots, then EXIT's; with fewer SVs the last slots are used.
  std::vector<std::array<float, 2>> slots;
  std::vector<float> slot_from; // each slot's x as it starts sliding in
  float slot_speed{};           // units a second
  int icon_layer{};
  int lit_layer{};
  ShopIcon exit;
  std::map<int, ShopIcon> legs_icons; // by the SV's legs record
  ShopIcon default_icon;

  // Over an SV: the panel grows open, then its maker's badge, its name and
  // its price show.
  Sprite info_panel;
  PopupGrowth info_growth;
  std::vector<Sprite> badges; // by the body's maker
  std::array<float, 2> name_at{};
  std::uint32_t name_colour{0xffffff};
  int name_layer{};
  Sprite price_label;
  NumberStyle price;

  // SVs of a maker from `gated_maker` on show once contest `unlock_contest`
  // is won (0x05f750, 0x096f64).
  int gated_maker{3};
  int unlock_contest{49};

  TickerStyle help_style;
  std::map<std::string, std::string> help; // "sv" and "exit"
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] UsedSvScript parse_used_sv(const json::JsonValue &document);

} // namespace phantomcrash
