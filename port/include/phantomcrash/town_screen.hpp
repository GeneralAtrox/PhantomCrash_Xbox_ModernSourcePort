#pragma once

// What the town's own screens share (the schedule, the records, the shops,
// the garage's rooms): the menu basics, the original's scene number, the
// music they play depending on the screen they were opened from, where the
// town frame sits, a queue of the sounds to play, and the INFORMATION style
// popups that grow out of the item they describe.

#include "phantomcrash/json.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/menu_screen.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

struct TownBasics : MenuBasics {
  int scene{}; // the original's scene number (see data/ui/scenes.json)
  // The music when opened from a screen, overriding `music`; "keep" keeps
  // the music playing (the radio, opened from Club Wired).
  std::map<std::string, std::string> music_from;
  FramePlacement frame;
};

// Reads the menu basics, "scene", "music_from" and "frame". Throws
// std::runtime_error naming the member at fault.
void parse_town_basics(const json::JsonValue &document, TownBasics &basics);

// The music a screen plays when opened from `from`.
[[nodiscard]] std::string town_music(const TownBasics &basics, const std::string &from);

// The sounds a screen asks for, taken by the host each tick.
class SoundQueue final {
public:
  // Empty names (a sound the data leaves out) are ignored.
  void play(const std::string &sound);
  [[nodiscard]] std::vector<std::string> take();

private:
  std::vector<std::string> sounds_;
};

// How a popup grows open: from `from_scale` of its size, centred on the item
// it describes, to its full size over `seconds` (the schedule's 0.2 s).
struct PopupGrowth {
  double seconds{0.2};
  float from_scale{0.3F};
};

// Reads "grow_seconds" and "grow_from_scale".
[[nodiscard]] PopupGrowth parse_popup_growth(const json::JsonValue &value);

class GrowingPopup final {
public:
  explicit GrowingPopup(PopupGrowth growth = {}) : growth_(growth) {}

  void open() {
    open_ = true;
    left_ = growth_.seconds;
  }
  void close() {
    open_ = false;
  }
  void update(const double tick_seconds) {
    if (open_) {
      left_ -= tick_seconds;
    }
  }
  [[nodiscard]] bool is_open() const noexcept {
    return open_;
  }
  // How far it still has to grow: 1 as it opens, 0 once grown (and shut).
  [[nodiscard]] float growing() const noexcept;
  // Open and at full size; its contents show from then on.
  [[nodiscard]] bool grown() const noexcept {
    return open_ && left_ <= 0.0;
  }
  [[nodiscard]] const PopupGrowth &growth() const noexcept {
    return growth_;
  }

private:
  PopupGrowth growth_;
  bool open_{};
  double left_{};
};

// How the original pops a window open (0x0be87c, 0x0be91c): over the first
// half of `t` (0 to 1) it widens to `widest` while `thin` high, then it
// settles to its width while it heightens to full. The x and y scales.
[[nodiscard]] std::array<float, 2> pop_open(float t, float widest, float thin);

// How far a panel opened `seconds` ago still has to grow (1 to 0).
[[nodiscard]] float growing_after(const PopupGrowth &growth, double seconds);

// `panel` as it is drawn while it still has `growing` to go, out of `from`
// (a canvas point; its own centre when none).
[[nodiscard]] Sprite grown_panel(const Sprite &panel, float growing, float from_scale,
                                 const std::optional<std::array<float, 2>> &from = std::nullopt);
[[nodiscard]] Sprite grown_panel(const Sprite &panel, const GrowingPopup &popup,
                                 const std::array<float, 2> &from);

} // namespace phantomcrash
