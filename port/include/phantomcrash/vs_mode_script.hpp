#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// A player's panel and what it shows (0x03ffc9), all four players' records
// at the player's own places.
struct VsPlayerLayout {
  Sprite panel;
  Sprite label;
  Sprite enter_now;
  Sprite entered;
  Sprite entered_box;
  Sprite box;
  Sprite hint;
  Sprite sv_name;
  Sprite win_lose;
  Sprite strip;
  Sprite arrow_left;
  Sprite arrow_right;
  Sprite your_sv;
  Sprite no_controller;
  Sprite no_entry;
  NumberStyle wins;
  NumberStyle losses;
  std::array<float, 2> cards_at{};
  std::array<float, 2> clip{}; // the strip's left and right edges
  std::array<float, 2> name_at{};
  std::array<float, 2> icon_at{};
  std::array<float, 2> chosen_name_at{};
  ScaleAnchor grow_from{ScaleAnchor::centre};
  bool hint_from_bottom{};
};

// One of the preset Scoobees every player may choose.
struct VsPreset {
  int robot{}; // tables/garage.json robots
  ChoiceSprites card;
  Sprite icon;
};

struct VsCards {
  int visible{3};
  float step{};
  double scroll_seconds{};
  float push{};
  std::vector<ChoiceSprites> makers; // by the body's maker
  ChoiceSprites empty;               // a damaged kept Scoobee
  std::vector<Sprite> icons;         // by the body's maker
  std::vector<VsPreset> presets;
};

struct VsPopup {
  Sprite window;
  Sprite entry;
  Sprite question;
  Sprite select;
  ChoiceSprites yes;
  ChoiceSprites no;
};

struct VsIntro {
  double grow_seconds{};
  double shrink_from{};
  double end{};
  double stagger{};
  double settle_seconds{};
};

struct VsTimings {
  double question_grow{};
  double leave_seconds{}; // the panels' contents close before the next phase
  double fade_seconds{};  // the black between the Scoobee and area selects
  double exit_seconds{};  // the panels close before leaving
  double panel_grow{};
  double label_seconds{};
  double open_seconds{};
  std::uint32_t joined_colour{0xffffff};
};

enum class VsAreaKind { area, random, exit };

struct VsAreaItem {
  std::string id;
  VsAreaKind kind{};
  Sprite picture;
  Sprite night_picture;
  Sprite label;
  Sprite title;
  ChoiceSprites item;
  float from_x{};
  std::array<int, 2> stages{}; // day, night
  std::string text;            // optional mod arena label instead of atlas artwork
};

struct VsCrowd {
  int players{3};
  int first{};
  std::vector<float> xs;
  std::vector<float> from_xs;
  std::vector<int> stages;
};

struct VsTimeZone {
  Sprite window;
  ChoiceSprites day;
  ChoiceSprites night;
  std::uint32_t dim_colour{0x606060};
  double grow_seconds{};
};

struct VsAreaSelect {
  std::vector<Sprite> layers;
  Sprite picture_frame;
  Sprite bar;
  float bar_offset{};
  double slide_seconds{};
  double fade_seconds{};
  std::vector<VsAreaItem> items;
  VsCrowd crowd;
  double random_every{};
  std::vector<Sprite> random_pictures; // by stage
  VsTimeZone time_zone;
};

// data/ui/vs_mode.json (original scene 44).
struct VsModeScript : TownBasics {
  std::string battle_route;
  Buttons left{}, right{};
  std::string no_sound;
  // A pick of a Scoobee that cannot fight: the original plays its confirm
  // sound and stays on the strip (0x0418c6).
  std::string unusable_sound;
  std::string choose_sound;
  std::string to_no_sound;
  std::string slide_sound;
  std::vector<Sprite> backdrop;
  Sprite entry_hint;
  Sprite select_hint;
  VsIntro intro;
  VsPopup popup;
  VsTimings timings;
  std::array<VsPlayerLayout, controller_ports> players;
  VsCards cards;
  std::uint32_t text_colour{0xffffff};
  int name_layer{};
  int chosen_layer{};
  NumberFont record_font;
  int record_layer{};
  VsAreaSelect area;
  std::array<float, 2> message_text_at{};
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
  std::map<std::string, std::string> helps; // "entry", "select", "area"
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] VsModeScript parse_vs_mode(const json::JsonValue &document);

} // namespace phantomcrash
