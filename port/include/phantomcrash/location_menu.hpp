#pragma once

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_cursor.hpp"
#include "phantomcrash/menu_screen.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace phantomcrash {

// One item on a location's wheel: its wedge, the wedge lit at rest, its
// name beside the wheel, its help line, the screen it opens and, when it
// differs from the menu's, the sound choosing it plays.
struct LocationItem {
  std::string id;
  std::string next;
  HelpLine help;
  Sprite wedge;
  Sprite highlight;
  Sprite label;
  std::string select_sound;
};

// The wheel: the wedge of the item at rest sits at `position` (its
// top-left, pointing to 9 o'clock); the others are turned about `pivot`
// (from that top-left) a step apart. A turn takes turn_seconds.
struct LocationWheel {
  std::array<float, 2> position{};
  std::array<float, 2> pivot{};
  float step_degrees{45.0F};
  int slots{8};
  double turn_seconds{8.0 / 60.0};
};

// A town location's menu: data/ui/club_wired.json (original scene 8) and
// data/ui/garage.json (scene 13) share one layout and differ in their
// pictures, items and help colour.
struct LocationMenuScript : MenuBasics {
  int scene{}; // the original's scene id; it seeds the music meter
  TickerStyle help;
  FramePlacement frame;
  LocationWheel wheel;
  std::vector<LocationItem> items;
  Buttons back_buttons{};    // the cursor steps back (the wheel turns clockwise)
  Buttons forward_buttons{}; // and forward
  std::string cancel_item;   // cancel moves the cursor here
  std::map<std::string, std::string> start_items;         // screen before -> item
  std::map<std::string, std::string> arrive_sounds;       // screen before -> sound
  std::vector<std::pair<Buttons, std::string>> shortcuts; // a press moves the cursor to the item
};

// Throws std::runtime_error naming the document (`name`) and entry at fault.
[[nodiscard]] LocationMenuScript parse_location_menu(const json::JsonValue &document,
                                                     const std::string &name);

// A location's menu (Club Wired 0x1ba45, 0x1bec1; the garage 0x1cbd8,
// 0x1ca21): a wheel of items turned by a horizontal cursor that wraps.
// Confirm fades out to the item's screen; cancel moves the cursor to the
// way out, and a shortcut straight to its item. The cursor starts on the
// item for the screen the menu was opened from.
class LocationMenu final {
public:
  LocationMenu(LocationMenuScript script, const std::string &from);

  void update(double tick_seconds, const InputFrame &input);
  void choose(int item);
  void rotate(int steps);

  [[nodiscard]] bool done() const noexcept {
    return fade_.done();
  }
  [[nodiscard]] const std::string &next() const {
    return fade_.next();
  }
  [[nodiscard]] std::vector<std::string> take_sounds() {
    return std::exchange(sounds_, {});
  }
  [[nodiscard]] const LocationMenuScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] int cursor() const noexcept {
    return cursor_;
  }
  [[nodiscard]] bool turning() const noexcept {
    return shown_ != cursor_;
  }
  // The wedges to draw, back to front: each item and its angle in degrees.
  [[nodiscard]] std::vector<std::pair<int, float>> wedges() const;
  [[nodiscard]] double seconds() const noexcept {
    return seconds_;
  }
  [[nodiscard]] double help_seconds() const noexcept {
    return help_seconds_;
  }
  [[nodiscard]] float black_alpha() const noexcept {
    return fade_.black_alpha();
  }
  [[nodiscard]] bool faded_in() const noexcept {
    return !fade_.fading_in();
  }

private:
  void move_to(int item);
  void sound(const std::string &name);

  LocationMenuScript script_;
  ScreenFade fade_;
  CursorRepeat repeat_;
  int cursor_{};
  int shown_{}; // the item the wheel turns from; the cursor once at rest
  double turn_{};
  double seconds_{};
  double help_seconds_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
