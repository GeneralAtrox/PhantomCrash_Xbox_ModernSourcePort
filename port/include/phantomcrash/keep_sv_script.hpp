#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/message_box.hpp"
#include "phantomcrash/saved_time.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/town_screen.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace phantomcrash {

// How KEEP SV opens (0x042b3a, 0x0426a4): the frame grows, the title and
// arrows fade in, then the slots grow one after another.
struct KeepSvIntro {
  Sprite frame;
  double frame_seconds{};
  Sprite title;
  double title_from{};
  double title_seconds{};
  double slots_from{};
  double slot_stagger{};
  double slot_grow_seconds{};
};

// The row of cards (0x0426a4): seven slots, the first and last the
// neighbours a scroll brings in.
struct KeepSvSlots {
  std::array<float, 2> first{};
  float step{};
  std::array<float, 4> clip{};
  int visible{5};
  double scroll_seconds{};
  std::vector<ChoiceSprites> maker_cards; // by the body's maker
  ChoiceSprites damaged;
  ChoiceSprites empty;
  Sprite cursor; // over the lit card
};

struct KeepSvArrows {
  Sprite left, right;           // always
  Sprite more_left, more_right; // while the list scrolls further that way
};

// The chosen Scoobee's name and when it was kept.
struct KeepSvDetails {
  std::array<float, 2> name_at{};
  std::uint32_t colour{0xffffff};
  int layer{};
  std::array<float, 2> date{};
  std::array<float, 2> time{};
};

enum class KeepSvOption { spec, remove, make, cancel };

struct KeepSvPopupOption {
  KeepSvOption option{};
  ChoiceSprites sprites;
};

// data/ui/keep_sv.json (original scene 43).
struct KeepSvScript : TownBasics {
  std::string spec_route;
  std::string make_route;
  Buttons left{}, right{}, up{}, down{};
  std::string column_sound;
  std::string no_sound;
  std::string to_exit_sound;
  std::string option_sound;
  std::string to_cancel_sound;

  KeepSvIntro intro;
  KeepSvSlots slots;
  KeepSvArrows arrows;
  ChoiceSprites save;
  ChoiceSprites exit;
  KeepSvDetails details;
  SavedTimeStyle saved_time;
  // By the message that asks: "choose", "damaged" and "save".
  std::map<std::string, std::vector<KeepSvPopupOption>> popups;

  std::array<float, 2> message_text_at{};
  std::map<std::string, std::vector<std::string>> messages;
  TickerStyle help_style;
  std::string help_text;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] KeepSvScript parse_keep_sv(const json::JsonValue &document);

} // namespace phantomcrash
