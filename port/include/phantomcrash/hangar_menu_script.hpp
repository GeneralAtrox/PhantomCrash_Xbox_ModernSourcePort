#pragma once

#include "phantomcrash/garage.hpp"
#include "phantomcrash/hangar_labels.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
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

// The hangars' cards on their wheel (0x01d0df, 0x01d4d1). Offsets are from
// a card's top-left.
struct HangarCards {
  float centre_x{}; // the chosen card's left
  float y{};
  float step{};  // between neighbouring cards
  float reach{}; // a card further than this from 0 wraps by `period`
  float period{};
  double scroll_seconds{};
  Sprite card;
  std::uint32_t colour{0xf0f0f0};
  std::uint32_t riding_colour{0xff9400};
  std::vector<HangarLabel> numbers; // one label per hangar
  Sprite for_sale;
  NumberStyle price; // `right` from the card's top-left
  int price_layer{};
  Sprite empty;
  Sprite ride;
  std::vector<Sprite> emblems; // by the body's maker; none past the end
  std::array<float, 2> name_at{};
  std::uint32_t name_colour{0xf0f0f0};
  int name_layer{};
  int name_glyphs{9};
  Sprite name_line;
  Sprite incomplete;
  Sprite underkill;
  Sprite overweight; // `warning_step` higher when Underkill shows too
  float warning_step{};
  Sprite exit; // the way out's card
};

// The Scoobees standing on the cards (0x01dd99, 0x01d8ba): the hangars
// within `neighbours` of the chosen card, each `spacing` from the next at
// its card's offset from centre_x over `step`. The chosen one spins from
// the preview's yaw (robot_preview.json's hangar); the others face that yaw
// plus yaw_per_step for each step they are off centre.
struct HangarRobots {
  float spacing{};
  float yaw_per_step{};
  int neighbours{};
};

// The numbers' plates along the bottom (0x06c530), sliding in with the line
// and EXIT, the ridden hangar's and the chosen one's drawn again over them.
struct HangarPlates {
  std::vector<HangarLabel> plates;
  std::vector<HangarLabel> marks;
  std::vector<float> from_x;
  std::vector<float> to_x;
  float y{};
  double slide_seconds{};
  std::uint32_t riding_colour{0xff9400};
  std::uint32_t chosen_colour{0xffcd00};
  int chosen_layer{};
  Sprite line; // slides as the last plate does
  Sprite exit;
  Sprite exit_lit;
};

// keep is KEEP SV's SAVE: the hangar's Scoobee is kept for the VS mode.
enum class HangarAction { ride, test_drive, spec, assemble, close, keep };

struct HangarMenuItem {
  HangarAction action{HangarAction::close};
  Sprite sprite;
  std::string help;
};

// A hangar's menu (0x01e4d3, 0x01d1e9).
struct HangarMenuStyle {
  PopWindow window;
  std::vector<HangarMenuItem> items; // RIDE, TEST DRIVE, SPEC, ASSEMBLE, CLOSE; CLOSE last
  std::uint32_t lit_colour{0xffffff};
  int lit_layer{};
  Sprite strike; // over an item that cannot be chosen, at its height
  float strike_alpha{1.0F};
};

// The hangar as another screen than the garage opens it (0x01d4d1,
// 0x01d1e9): its own EXIT card and help, and its own menu's window and items.
// A hangar the origin will not take, by the notice that says so: KEEP SV
// takes only a complete, armed Scoobee (0x01e81b) and sells no hangars.
struct HangarRefusals {
  std::string incomplete; // not owned, or no body, legs or CHIP (0x01e2d1)
  std::string unarmed;    // no weapons (0x01e281)
};

struct HangarOrigin {
  std::optional<Sprite> exit;
  std::optional<HangarRefusals> refuse;
  std::optional<std::string> exit_help;
  std::optional<HangarMenuStyle> menu;
};

// The garage's HANGAR (data/ui/hangar.json).
struct HangarMenuScript : TownBasics {
  Buttons left{}, right{}, up{}, down{}, riding{};
  std::string turn_sound; // with move_sound as the wheel turns (0x01e81b)
  std::string row_sound;
  std::string back_sound;
  std::string exit_sound;
  std::string close_sound;
  std::string no_sound;
  std::string trade_sound;
  std::string board_sound;
  // The notices that play board_sound as they are dismissed (0x01e2d1):
  // the boarding one and KEEP SV's refusal of a Scoobee not boarded.
  std::vector<std::string> board_notices;

  std::vector<Sprite> bands; // each stretched to its own width
  HangarCards cards;
  HangarPlates plates;
  HangarRobots robots;
  HangarMenuStyle menu;
  // The screens TEST DRIVE, SPEC and ASSEMBLE open.
  std::map<HangarAction, std::string> routes;
  std::int64_t hangar_price{};
  float overweight_margin{};

  std::array<float, 2> message_text_at{};
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
  std::string hangar_help;
  std::string exit_help;
  std::array<std::string, 2> buy_help;         // YES, NO
  std::optional<HangarRefusals> refuse;        // none: the garage's own way
  std::map<std::string, HangarOrigin> origins; // by the screen it is opened from
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] HangarMenuScript parse_hangar_menu(const json::JsonValue &document);

// The script as opened from `origin`, with that origin's overrides.
[[nodiscard]] HangarMenuScript hangar_from(const HangarMenuScript &script,
                                           const std::string &origin);

} // namespace phantomcrash
