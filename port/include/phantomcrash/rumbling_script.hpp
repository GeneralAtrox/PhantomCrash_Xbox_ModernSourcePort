#pragma once

#include "phantomcrash/contest_info.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/rumbling_map.hpp"
#include "phantomcrash/shop_list_style.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

using CanvasPoint = std::array<float, 2>;

// One of the area select's items (0x1d7160 and 0x1d70e0): an area of the
// town, or the way out.
struct RumblingArea {
  std::string id;
  std::string help;
  bool exit{};
  Sprite picture; // the venue by day, and by night
  Sprite night_picture;
  Sprite label; // the area's name strip
  Sprite title; // its logo; the sub-menu shows it at the menu's title_at
  Sprite item;  // along the bottom, sliding in from item_from_x
  Sprite item_lit;
  float item_from_x{};
  std::vector<Sprite> map_frames;  // the area map drawing itself in (0x014af3)
  std::vector<CanvasPoint> gates;  // the gates' marks on the map (0x1d7010)
  std::vector<CanvasPoint> marks;  // the H marks (0x1d7070)
  RumblingMapProjection robot_map; // where its robots are on the map
};

// The area select (0x0163e7, drawn by 0x015fce and 0x015c44).
struct RumblingAreaSelect {
  double slide_seconds{0.4};
  Sprite bar;
  float bar_from_x{};
  double bar_slide_seconds{};
  Sprite picture_frame;
  double fade_seconds{}; // the pictures cross-fade as the cursor moves
  Sprite closed;         // over a venue with no contest, with the noise
  Sprite noise;
  float noise_alpha{0.5F};
  Sprite info_panel;
  float info_right{}; // the contest's and class's names end here
  float contest_y{};
  float class_y{};
  std::array<float, 2> fee_at{}; // the fee's right edge (the HUD's money writer)
  int fee_digits{9};
  std::uint32_t text_colour{0xffffff};
  int text_layer{};
  Sprite clear;
  std::vector<int> no_clear_mark;
};

enum class RumblingAction { entry, rental, select_sv, character_info, contest_info, back };

struct RumblingMenuItem {
  RumblingAction action{RumblingAction::back};
  std::string help;
  Sprite normal;
  Sprite lit;
};

// The sub-menu (0x01671e, drawn by 0x014edc and 0x014af3).
struct RumblingMenu {
  double turn_seconds{}; // between the area select and the sub-menu
  float info_slide{};    // the info panel slides this far right as it turns
  CanvasPoint title_at{};
  std::vector<RumblingMenuItem> items;
  std::uint32_t disabled_colour{0x404040};
  float item_slide{}; // the items come in from this far left
  Sprite map;
  double map_frame_seconds{};
  std::uint32_t map_fade_colour{0xffffff};
  Sprite mark;
};

// Choosing the gate to come in by (0x0169e5, drawn by 0x0150ea).
struct RumblingGates {
  Sprite window;
  std::vector<Sprite> hint;
  std::vector<Sprite> labels;
  std::map<int, float> label_x; // the first label's x, by the gates there are
  float label_step{};
  std::uint32_t chosen_colour{0xffffff};
  int chosen_layer{};
  Sprite marker;
  Sprite marker_lit;
  Sprite ring;
  float ring_degrees_per_second{};
  std::string help;
};

// CONTEST INFO (0x015bcb, drawn by 0x013d07).
struct RumblingContestInfo {
  PopWindow window;
  ContestInfoStyle style;
  std::vector<Sprite> close_hint;
  std::string help;
};

// WIRE HEADS DATA (0x015aa2, drawn by 0x013f5a): a window with the
// opponent's pilot, robot, partner, team and bounty in NY, the pilot's and
// partner's faces, and the way back.
struct RumblingCharacterInfo {
  PopWindow window;
  std::array<float, 2> pilot{}, robot{}, partner{}, team{}, bounty{};
  int bounty_digits{9};
  Sprite bounty_unit;
  std::string no_team; // for a pilot with no team
  std::uint32_t text_colour{0xffffff};
  int text_layer{};
  std::array<float, 2> pilot_face{}, partner_face{};
  int face_layer{};
  std::vector<Sprite> close_hint;
  // A port addition: the live RUMBLING opponent's armour as it stands, or
  // `down_label` while it is shot down; not drawn without "armour".
  bool show_armour{};
  std::array<float, 2> armour{};
  std::string armour_label;
  std::string down_label;
};

// RUMBLING (scene 3): what data/ui/rumbling.json holds.
struct RumblingScript : TownBasics {
  Buttons left{}, right{}, up{}, down{};
  std::string row_sound, to_exit_sound, no_sound, close_sound, browse_sound;
  std::string hangar_route{"hangar"};
  std::string battle_route{"rumbling_battle"};
  std::vector<RumblingArea> areas;
  RumblingAreaSelect area_select;
  RumblingMenu menu;
  RumblingGates gates;
  RumblingRobotMarkers robot_markers; // none when absent
  RumblingCharacterInfo character_info;
  RumblingContestInfo contest_info;
  std::array<float, 2> message_text_at{};
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] RumblingScript parse_rumbling(const json::JsonValue &document);

} // namespace phantomcrash
