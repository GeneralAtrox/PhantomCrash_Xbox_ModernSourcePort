#pragma once

#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// Where a menu leaves for, and whether it shows the day and money, as
// entered from one screen: the OPTION and LOAD GAME screens return to the
// garage's system menu when opened from it, and show the town's frame there.
struct MenuEntry {
  std::string exit_to;
  bool hud{};
};

// What the OPTION screen and its sub-screens have in common in their data
// files: music, the 0.5 s black fades (0x9f734 in, 0x9f6e0 out), where EXIT
// returns to, the confirm and cancel buttons, the cursor's repeat timing,
// the move and select sounds, and the fixed background sprites.
struct MenuBasics {
  std::string music;
  double fade_in{};
  double fade_out{};
  std::string exit_to;
  bool hud{};                                    // the day and money
  std::map<std::string, MenuEntry> entered_from; // screen -> its exit and hud
  Buttons confirm_buttons{};
  Buttons cancel_buttons{};
  double repeat_delay{};
  double repeat_interval{};
  std::string move_sound;
  std::string select_sound;
  std::vector<Sprite> layers;
};

// Throws std::runtime_error naming the member at fault.
void parse_menu_basics(const json::JsonValue &document, MenuBasics &basics);

// The exit and hud of a menu entered from `origin`: its entered_from entry,
// or its own.
[[nodiscard]] MenuEntry menu_entry(const MenuBasics &basics, const std::string &origin);

// The per-item help line along the top, the current music's credit in the
// CURRENT MUSIC band (0x2a85a) and where the band's meter sits, if it has one.
struct MenuTickers {
  TickerStyle help;
  TickerStyle now_playing;
  std::string now_playing_format; // "{title}" and "{artist}" are replaced
  std::optional<std::array<float, 2>> meter;
};

[[nodiscard]] MenuTickers parse_menu_tickers(const json::JsonValue &document);

// A help line and the padding after it, when it differs from the style's
// (the original pads each string with its own count of spaces).
struct HelpLine {
  std::string text;
  std::optional<float> trail;
};

[[nodiscard]] HelpLine parse_help(const json::JsonValue &item);
[[nodiscard]] TickerStyle help_style(const TickerStyle &style, const HelpLine &line);

// An array of strings (a message box's lines).
[[nodiscard]] std::vector<std::string> parse_lines(const json::JsonValue &value);

// A screen's black fade in, during which it takes no input, and its fade out
// to the next screen.
class ScreenFade final {
public:
  ScreenFade(double fade_in, double fade_out);

  // Advances the fades; true when the screen takes input this tick. Input
  // waits one tick past the end of the fade in, as the original's does.
  bool update(double tick_seconds);
  // Starts the fade out; the screen is done once it ends.
  void leave(const std::string &target);

  [[nodiscard]] bool fading_in() const noexcept {
    return fading_in_;
  }
  [[nodiscard]] bool leaving() const noexcept {
    return pending_.has_value();
  }
  [[nodiscard]] bool done() const noexcept {
    return next_.has_value();
  }
  [[nodiscard]] const std::string &next() const {
    return *next_;
  }
  [[nodiscard]] float black_alpha() const noexcept;

private:
  double fade_in_;
  double fade_out_;
  double elapsed_{};
  bool fading_in_;
  std::optional<std::string> pending_;
  std::optional<std::string> next_;
};

// How long each item's help line has scrolled. The original keeps a text box
// per item, restarted when the cursor moves onto the item.
class HelpClocks final {
public:
  explicit HelpClocks(std::size_t items = 0) : elapsed_(items, 0.0) {}

  void advance(int item, double tick_seconds) {
    elapsed_.at(static_cast<std::size_t>(item)) += tick_seconds;
  }
  void restart(int item) {
    elapsed_.at(static_cast<std::size_t>(item)) = 0.0;
  }
  [[nodiscard]] double seconds(int item) const {
    return elapsed_.at(static_cast<std::size_t>(item));
  }

private:
  std::vector<double> elapsed_;
};

} // namespace phantomcrash
