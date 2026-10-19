#pragma once

#include "phantomcrash/battle_hud_state.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_text.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Which edge of the 640x480 screen a HUD element keeps to when the window
// is wider (docs/WIDESCREEN.md).
enum class HudAnchor { left, centre, right };

// A gauge: its frame, and a fill stretched to `width` times the amount.
struct HudGauge {
  HudAnchor anchor{HudAnchor::centre};
  Sprite frame;
  Sprite fill;
  float width{128.0F};
  std::uint32_t colour{0xffffff};
  std::uint32_t low_colour{0xffffff}; // below `low`
  float low{0.25F};
  std::uint32_t flash_colour{0xffffff}; // flashed toward while it changes
};

// One side's weapon panels (the records at 0x204340 for the left and
// 0x2046b0 for the right), positions from the panel's top-left.
struct WeaponPanelSide {
  HudAnchor anchor{HudAnchor::left};
  std::array<float, 2> arm{};      // the arm weapon's panel
  std::array<float, 2> shoulder{}; // the shoulder weapon's panel
  Sprite frame;
  std::array<Sprite, 10> digits;
  // The ammunition's four digits; a charge uses the first three.
  std::array<std::array<float, 2>, 4> digit_positions{};
  Sprite percent; // placed by its position
  std::array<float, 2> icon{};
  // Placed by its position and stretched by the amount; split screen has
  // none (0x4ad3b draws it with one screen only).
  std::optional<Sprite> bar;
  bool bar_mirrored{};
  Sprite lost; // WEAPON LOST, placed by its position
};

// What a weapon panel shows beside the icon.
enum class WeaponReadout {
  ammo,   // four digits and the bar
  charge, // three digits, the percent sign and the bar
  none,
};

struct WeaponIcon {
  Sprite left;
  Sprite right;
  std::optional<Sprite> homing_left; // in place of the others for a homing weapon
  std::optional<Sprite> homing_right;
  WeaponReadout readout{WeaponReadout::ammo};
};

struct WeaponPanelStyle {
  std::uint32_t ready_colour{0x32c88c};
  std::uint32_t refill_colour{0x8080e1}; // flashed toward while the ammunition refills
  std::uint32_t bar_colour{0x32ff8c};
  std::uint32_t bar_low_colour{0xc04040};
  std::uint32_t bar_charging_colour{0xffc823}; // a charge flashes toward it until full
  float bar_low{0.25F};
  float bar_width{72.0F};
  WeaponPanelSide left;
  WeaponPanelSide right;
  std::map<int, WeaponIcon> icons; // by the HUD's weapon kind (hud_kind)
};

// A label with a number written after it in the HUD's number font.
struct HudCounter {
  HudAnchor anchor{HudAnchor::left};
  Sprite label;
  NumberStyle number;
};

// Replaces lettering in an atlas sprite (sprite_text.hpp).
using BattlePanelTextStyle = SpriteTextStyle;

struct BattlePanelsStyle {
  std::vector<BattlePanelTextStyle> text_replacements;
  bool widescreen{true}; // anchored elements keep to the window's edges
  // The screen's canvas: 640x480 alone, 640x240 for each of two screens and
  // 320x240 for each of three or four.
  std::array<float, 2> canvas{640.0F, 480.0F};
  // Split screen leaves out the DAY, TIME, MONEY and SHOOTDOWN labels and
  // the message log (0x4ad3b and 0x4aaed with more than one screen).
  bool labels{true};
  bool messages_shown{true};
  float flash_frames{10.0F};
  float money_count_seconds{0.75F};
  float text_gap{6.0F};
  HudGauge durability;
  HudGauge optic;
  std::uint32_t optic_active_colour{0x64ff6e}; // recovery flash; legacy configuration name
  WeaponPanelStyle weapons;
  NumberFont font;
  Sprite colon;
  float colon_advance{5.0F};
  HudCounter day;
  HudCounter time;
  HudCounter money;
  HudCounter shootdown;
  HudAnchor messages_anchor{HudAnchor::left};
  std::array<float, 2> messages_position{48.0F, 388.0F};
  HudMessageLog::Style messages;
  std::array<Sprite, 3> message_sprites; // DAMAGED, HIT, BINGO!
  HudAnchor warning_anchor{HudAnchor::right};
  Sprite warning_stripes;
  Sprite warning_half;    // durability below `warning_half_below`
  Sprite warning_quarter; // and below `warning_quarter_below`
  float warning_half_below{0.5F};
  float warning_quarter_below{0.25F};
  HudAnchor alert_anchor{HudAnchor::centre};
  Sprite missile_alert;
  float alert_wave_frames{60.0F};
};

// The kinds of the message log's entries, as the original numbers them.
enum HudMessage { hud_message_damaged = 0, hud_message_hit = 1, hud_message_bingo = 2 };

// Throws std::runtime_error naming what is missing or malformed.
[[nodiscard]] BattlePanelsStyle parse_battle_panels_style(const json::JsonValue &document);

// Canvas x of `x` (a 640x480 position) kept to its anchor's edge of a
// window whose canvas spans `left`..`right`, on a screen `width` units wide.
[[nodiscard]] float anchor_x(HudAnchor anchor, float x, float left, float right,
                             float width = 640.0F);

} // namespace phantomcrash
